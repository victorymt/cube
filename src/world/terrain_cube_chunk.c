#include "world/terrain_cube.h"
#include "world/chunks.h"
#include "world/cga_voxel.h"
#include "world/surface_topology.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define CGA_CUBE_PLAN_MARGIN 72

typedef struct CgaCubeSampleContext {
    const TerrainCubeContext *terrain;
    uint32_t bodyId;
} CgaCubeSampleContext;

typedef struct CgaCubeWriteContext {
    Chunk *chunk;
    SurfaceCubeChunkKey key;
} CgaCubeWriteContext;

static bool SampleCubeTerrainForCga(void *userData, int x, int z,
                                    TerrainMode mode,
                                    CgaTerrainSample *outSample)
{
    (void)mode;
    CgaCubeSampleContext *context = userData;
    if (!context || !context->terrain || !outSample) return false;
    SurfaceAddress address = SurfaceAddressFromMapCoordinates(
        context->bodyId, (float)x + 0.5f, (float)z + 0.5f, 0);
    TerrainCubeColumn column;
    if (!TerrainCubeSampleColumn(context->terrain, address, &column)) {
        return false;
    }
    *outSample = (CgaTerrainSample){
        .height = (int)lroundf(column.terrain.elevation),
        .biome = column.terrain.biome
    };
    return true;
}

static bool CubeChunkMapBounds(SurfaceCubeChunkKey key,
                               int *outMinX, int *outMinZ,
                               int *outMaxX, int *outMaxZ)
{
    if (!SurfaceCubeChunkKeyIsValid(key) || !outMinX || !outMinZ ||
        !outMaxX || !outMaxZ) return false;
    SurfaceAddress origin = {key.bodyId, key.face,
        key.u * CHUNK_SIZE, key.v * CHUNK_SIZE, 0};
    SurfaceMapCell originCell;
    if (!SurfaceAddressCanonicalMapCell(origin, &originCell)) return false;
    int minX = originCell.x;
    int maxX = originCell.x;
    int minZ = originCell.z;
    int maxZ = originCell.z;
    for (int u = 0; u < CHUNK_SIZE; u++) {
        for (int v = 0; v < CHUNK_SIZE; v++) {
            SurfaceAddress address = {key.bodyId, key.face,
                key.u * CHUNK_SIZE + u, key.v * CHUNK_SIZE + v, 0};
            SurfaceMapCell cell;
            if (!SurfaceAddressCanonicalMapCell(address, &cell)) return false;
            int x = cell.x;
            while (x - originCell.x > SURFACE_EQUATOR_BLOCKS / 2) {
                x -= SURFACE_EQUATOR_BLOCKS;
            }
            while (x - originCell.x < -SURFACE_EQUATOR_BLOCKS / 2) {
                x += SURFACE_EQUATOR_BLOCKS;
            }
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (cell.z < minZ) minZ = cell.z;
            if (cell.z > maxZ) maxZ = cell.z;
        }
    }
    *outMinX = minX - CGA_CUBE_PLAN_MARGIN;
    *outMinZ = minZ - CGA_CUBE_PLAN_MARGIN;
    *outMaxX = maxX + CGA_CUBE_PLAN_MARGIN + 1;
    *outMaxZ = maxZ + CGA_CUBE_PLAN_MARGIN + 1;
    return true;
}

static bool WriteCubeStructureCell(void *userData, SurfaceAddress address,
                                   BlockType block)
{
    CgaCubeWriteContext *context = userData;
    SurfaceCubeChunkKey cellKey;
    if (!context || !context->chunk ||
        !SurfaceCubeChunkAt(address, &cellKey) ||
        !SurfaceCubeChunkKeyEqual(cellKey, context->key)) return true;
    int localX = address.u - context->key.u * CHUNK_SIZE;
    int localZ = address.v - context->key.v * CHUNK_SIZE;
    return ChunkSetLocalBlock(context->chunk, localX, address.radial,
                              localZ, block);
}

static bool GenerateCubeStructures(const TerrainCubeContext *context,
                                   Chunk *staging, SurfaceCubeChunkKey key,
                                   int sectionY)
{
    if (!context || !staging || TerrainCubeContextMode(context) == TERRAIN_FLAT) {
        return true;
    }
    int minX, minZ, maxX, maxZ;
    if (!CubeChunkMapBounds(key, &minX, &minZ, &maxX, &maxZ)) return false;
    CgaWorkspace workspace;
    CgaWorkspaceReset(&workspace);
    CgaCubeSampleContext sampleContext = {
        .terrain = context,
        .bodyId = key.bodyId
    };
    const WorldSurfaceSnapshot *snapshot = TerrainCubeContextSnapshot(context);
    if (!snapshot || !CgaPlanStructureFeaturesForBounds(
            &workspace, minX, minZ, maxX, maxZ,
            snapshot->context.seed, TerrainCubeContextMode(context),
            NULL, SampleCubeTerrainForCga, &sampleContext)) return false;
    for (size_t i = 0; i < workspace.featureCount; i++) {
        if (!CgaExpandFeature(&workspace, workspace.features[i])) return false;
    }
    int minY = sectionY == INT_MIN
        ? TERRAIN_CUBE_MIN_RADIAL : sectionY * SURFACE_SECTION_HEIGHT;
    int maxY = sectionY == INT_MIN
        ? SURFACE_GENERATION_MAX_Y_EXCLUSIVE
        : minY + SURFACE_SECTION_HEIGHT;
    CgaCubeWriteContext writeContext = { .chunk = staging, .key = key };
    return CgaVisitSurfaceCells(&workspace, key.bodyId,
                                minX, minZ, maxX, maxZ,
                                minY, maxY,
                                WriteCubeStructureCell, &writeContext);
}

static bool GenerateCubeJob(const TerrainCubeContext *context,
                            SurfaceCubeChunkKey key, Chunk *staging,
                            ChunkGenScope scope, int sectionY)
{
    if (scope == CHUNK_GEN_SCOPE_COLUMN)
        return TerrainCubeGenerateChunk(context, key, staging);
    return scope == CHUNK_GEN_SCOPE_SECTION &&
        TerrainCubeGenerateChunkSection(context, staging, sectionY);
}

bool TerrainCubePrepareJob(TerrainCubeContext *context,
                           SurfaceCubeChunkKey key, ChunkGenJob *job)
{
    const WorldSurfaceSnapshot *snapshot = TerrainCubeContextSnapshot(context);
    if (!job || job->inUse || job->running || job->done || job->queued ||
        job->completionQueued || job->columnResult || job->cubeContext || !snapshot ||
        !SurfaceCubeChunkKeyIsValid(key) || key.bodyId != snapshot->context.bodyId ||
        (snapshot->context.kind == WORLD_SURFACE_HOME && key.bodyId != 0) ||
        (snapshot->context.kind == WORLD_SURFACE_PLANET && key.bodyId == 0) ||
        (job->scope != CHUNK_GEN_SCOPE_COLUMN && job->scope != CHUNK_GEN_SCOPE_SECTION) ||
        (job->scope == CHUNK_GEN_SCOPE_SECTION &&
         (job->sectionY < TERRAIN_CUBE_MIN_RADIAL / SURFACE_SECTION_HEIGHT ||
          job->sectionY >= SURFACE_MAX_Y_EXCLUSIVE / SURFACE_SECTION_HEIGHT))) return false;
    TerrainCubeContext *retained = TerrainCubeContextRetain(context);
    if (!retained) return false;
    job->cubeContext = retained;
    job->generateCube = GenerateCubeJob;
    job->releaseCube = TerrainCubeContextRelease;
    job->cubeGrid = true;
    job->spherical = true;
    job->cubeKey = key;
    job->worldGeneration = TerrainCubeContextGeneration(context);
    job->surfaceAddress = (SurfaceAddress){key.bodyId, key.face,
        key.u * CHUNK_SIZE, key.v * CHUNK_SIZE, 0};
    job->cx = key.u;
    job->cz = key.v;
    return true;
}

static bool GenerateCubeSectionBase(const TerrainCubeContext *context,
                                    Chunk *staging, int sectionY)
{
    if (!staging || staging->loaded || !staging->cubeGrid || !staging->spherical ||
        ChunkTerrainSectionIsResolved(staging, sectionY) ||
        ChunkGetSectionConst(staging, sectionY)) return false;
    unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    if (!TerrainCubeGenerateSection(context, staging->cubeKey, sectionY, blocks)) return false;
    bool any = false;
    for (int u = 0; u < CHUNK_SIZE && !any; u++)
        for (int y = 0; y < SURFACE_SECTION_HEIGHT && !any; y++)
            for (int v = 0; v < CHUNK_SIZE; v++)
                if (blocks[u][y][v] != BLOCK_AIR) { any = true; break; }
    if (any) {
        ChunkSection *section = ChunkGetSection(staging, sectionY, true);
        if (!section) return false;
        memcpy(section->blocks, blocks, sizeof(blocks));
        section->dirty = true;
        section->dirtyStamp = 1;
    }
    return ChunkMarkTerrainSectionResolved(staging, sectionY);
}

bool TerrainCubeGenerateChunkSection(const TerrainCubeContext *context,
                                     Chunk *staging, int sectionY)
{
    if (!GenerateCubeSectionBase(context, staging, sectionY)) return false;
    return GenerateCubeStructures(context, staging, staging->cubeKey, sectionY);
}

bool TerrainCubeGenerateChunk(const TerrainCubeContext *context,
                              SurfaceCubeChunkKey key, Chunk *staging)
{
    const WorldSurfaceSnapshot *snapshot = TerrainCubeContextSnapshot(context);
    if (!snapshot || !staging || staging->loaded || staging->sectionCount ||
        staging->resolvedTerrainSectionCount || !SurfaceCubeChunkKeyIsValid(key) ||
        key.bodyId != snapshot->context.bodyId) return false;
    staging->spherical = true;
    staging->cubeGrid = true;
    staging->cubeKey = key;
    staging->cubeWorldGeneration = TerrainCubeContextGeneration(context);
    staging->surfaceAddress = (SurfaceAddress){key.bodyId, key.face,
        key.u*CHUNK_SIZE, key.v*CHUNK_SIZE, 0};
    enum {FIRST = TERRAIN_CUBE_MIN_RADIAL / SURFACE_SECTION_HEIGHT,
          LAST = SURFACE_GENERATION_MAX_Y_EXCLUSIVE / SURFACE_SECTION_HEIGHT};
    bool required[LAST - FIRST] = {false};
    for (int u = 0; u < CHUNK_SIZE; u++) for (int v = 0; v < CHUNK_SIZE; v++) {
        TerrainCubeColumn column;
        SurfaceAddress cell = {key.bodyId, key.face, key.u*CHUNK_SIZE + u, key.v*CHUNK_SIZE + v, 0};
        if (!TerrainCubeSampleColumn(context, cell, &column)) return false;
        float surfaces[] = {column.terrain.elevation, column.terrain.seaLevel,
            column.terrain.landform.hasWater ? column.terrain.landform.waterLevel : column.terrain.elevation};
        for (unsigned i = 0; i < sizeof(surfaces)/sizeof(surfaces[0]); i++) {
            if (i == 1 && surfaces[i] < 0) continue;
            int center = (int)floorf(surfaces[i]/SURFACE_SECTION_HEIGHT);
            for (int sectionY = center - 1; sectionY <= center + 1; sectionY++)
                if (sectionY >= FIRST && sectionY < LAST) required[sectionY - FIRST] = true;
        }
    }
    for (int sectionY = FIRST; sectionY < LAST; sectionY++)
        if (required[sectionY - FIRST] &&
            !GenerateCubeSectionBase(context, staging, sectionY))
            return false;
    return GenerateCubeStructures(context, staging, key, INT_MIN);
}
