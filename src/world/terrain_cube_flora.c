#include "world/terrain_cube_flora.h"

#include "world/chunks_internal.h"
#include "world/flora_taxa.h"
#include "world/home_tree_shape.h"
#include "world/surface_cube.h"
#include "world/surface_pose.h"

#include <math.h>
#include <string.h>

enum {
    CUBE_TREE_MARGIN_CHUNKS = 2,
    CUBE_TREE_MIN_SPACING = 3,
    CUBE_TREE_MAX_CROWN_RADIUS = 4
};

static uint32_t MixHash(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

static uint32_t CubeTreeHash(SurfaceAddress root, uint32_t salt,
                             uint32_t seed)
{
    return MixHash(SurfaceCubeCellHash(root) ^ MixHash(seed + salt));
}

static unsigned HomeTreeDensityDivisor(Biome biome)
{
    switch (biome) {
    case BIOME_FOREST: return 47u;
    case BIOME_PLAINS: return 131u;
    case BIOME_MOUNTAIN: return 149u;
    case BIOME_SNOW: return 67u;
    case BIOME_SWAMP: return 211u;
    default: return 0u;
    }
}

static int HomeTreeCrownRadius(FloraTaxonId taxonId)
{
    const FloraTaxon *taxon = FloraTaxonAt(taxonId);
    return taxon && FloraTaxonIsTree(taxonId)
        ? (int)lroundf(taxon->crownRadius) : CUBE_TREE_MIN_SPACING;
}

static bool PotentialRoot(SurfaceAddress root, uint32_t seed)
{
    uint32_t hash = CubeTreeHash(root, 1303u, seed);
    return hash % 47u == 0u || hash % 67u == 0u || hash % 131u == 0u ||
        hash % 149u == 0u || hash % 211u == 0u;
}

static bool CubeTreeCandidateAt(const TerrainCubeContext *context,
                                SurfaceAddress root, uint32_t seed,
                                FloraTaxonId *outTaxon)
{
    if (TerrainCubeContextMode(context) == TERRAIN_FLAT ||
        !SurfaceAddressIsValid(root)) return false;
    TerrainCubeColumn column;
    if (!TerrainCubeSampleColumn(context, root, &column)) return false;
    const SurfaceTerrainSample *surface = &column.terrain;
    if (surface->landform.hasWater ||
        surface->landform.kind == LANDFORM_DRY_CHANNEL) return false;
    unsigned divisor = HomeTreeDensityDivisor(surface->biome);
    if (divisor == 0u) return false;
    uint32_t hash = CubeTreeHash(root, 1303u, seed);
    if (hash % divisor != 0u) return false;
    int height = (int)lroundf(surface->elevation);
    if (height <= (int)lroundf(surface->seaLevel) ||
        height > SURFACE_MAX_Y_EXCLUSIVE - 18) return false;

    BlockType substrate = BLOCK_AIR;
    if (!TerrainCubeBlock(context, &column, height, &substrate)) return false;
    float temperatureK = 288.0f;
    float moisture = 0.55f;
    float usableLight = 0.76f;
    switch (surface->biome) {
    case BIOME_FOREST:
        temperatureK = 285.0f; moisture = 0.78f; usableLight = 0.54f; break;
    case BIOME_DESERT:
        temperatureK = 306.0f; moisture = 0.10f; usableLight = 0.94f; break;
    case BIOME_SNOW:
        temperatureK = 263.0f; moisture = 0.48f; usableLight = 0.62f; break;
    case BIOME_MOUNTAIN:
        temperatureK = 275.0f; moisture = 0.42f; usableLight = 0.82f; break;
    case BIOME_SWAMP:
        temperatureK = 286.0f; moisture = 0.94f; usableLight = 0.64f; break;
    default: break;
    }
    float elevation = fmaxf(surface->elevation - surface->seaLevel, 0.0f);
    moisture = fmaxf(moisture, surface->landform.wetness);
    temperatureK -= elevation * 0.0065f;
    FloraHabitat habitat = {
        .temperatureK = temperatureK,
        .moisture = moisture,
        .usableLight = usableLight,
        .elevation = elevation,
        .slope = Clamp(surface->slope / 8.0f, 0.0f, 1.0f),
        .biome = surface->biome,
        .substrate = substrate,
        .burnSeverity = 0.0f,
        .burnRecovery = 1.0f
    };
    FloraTaxonId taxon = FloraSelectTaxon(&habitat, hash, true);
    if (taxon == FLORA_TAXON_COUNT) return false;
    if (outTaxon) *outTaxon = taxon;
    return true;
}

static int CompareAddress(SurfaceAddress a, SurfaceAddress b)
{
    if (a.face != b.face) return a.face < b.face ? -1 : 1;
    if (a.u != b.u) return a.u < b.u ? -1 : 1;
    if (a.v != b.v) return a.v < b.v ? -1 : 1;
    return 0;
}

static bool CubeShouldPlaceTree(const TerrainCubeContext *context,
                                SurfaceAddress root, uint32_t seed,
                                FloraTaxonId *outTaxon)
{
    FloraTaxonId taxon = FLORA_TAXON_COUNT;
    if (!CubeTreeCandidateAt(context, root, seed, &taxon)) return false;
    if (outTaxon) *outTaxon = taxon;
    uint32_t priority = CubeTreeHash(root, 1307u, seed);
    int crownRadius = HomeTreeCrownRadius(taxon);
    if (crownRadius < CUBE_TREE_MIN_SPACING)
        crownRadius = CUBE_TREE_MIN_SPACING;
    for (int du = -CUBE_TREE_MAX_CROWN_RADIUS;
         du <= CUBE_TREE_MAX_CROWN_RADIUS; du++) {
        for (int dv = -CUBE_TREE_MAX_CROWN_RADIUS;
             dv <= CUBE_TREE_MAX_CROWN_RADIUS; dv++) {
            if (du == 0 && dv == 0) continue;
            SurfaceAddress neighbor;
            if (!SurfaceCellOffset(root, du, dv, root.radial, &neighbor))
                continue;
            FloraTaxonId neighborTaxon = FLORA_TAXON_COUNT;
            if (SurfaceAddressEqual(root, neighbor) ||
                !CubeTreeCandidateAt(context, neighbor, seed,
                                     &neighborTaxon)) continue;
            int neighborRadius = HomeTreeCrownRadius(neighborTaxon);
            int required = crownRadius > neighborRadius
                ? crownRadius : neighborRadius;
            if (required < CUBE_TREE_MIN_SPACING)
                required = CUBE_TREE_MIN_SPACING;
            int distance = abs(du) > abs(dv) ? abs(du) : abs(dv);
            if (distance > required) continue;
            uint32_t neighborPriority = CubeTreeHash(neighbor, 1307u, seed);
            if (neighborPriority < priority ||
                (neighborPriority == priority &&
                 CompareAddress(neighbor, root) < 0)) return false;
        }
    }
    return true;
}

bool TerrainCubeHomeTreeAt(const TerrainCubeContext *context,
                           SurfaceAddress root, TerrainCubeHomeTree *outTree)
{
    if (!outTree || !context || !SurfaceAddressIsValid(root)) return false;
    root.radial = 0;
    const WorldSurfaceSnapshot *snapshot = TerrainCubeContextSnapshot(context);
    if (!snapshot || snapshot->context.kind != WORLD_SURFACE_HOME ||
        snapshot->context.topology.kind != SURFACE_TOPOLOGY_CUBE_GRID)
        return false;
    uint32_t seed = snapshot->context.seed;
    FloraTaxonId taxon = FLORA_TAXON_COUNT;
    if (!PotentialRoot(root, seed) ||
        !CubeShouldPlaceTree(context, root, seed, &taxon)) return false;
    TerrainCubeColumn column;
    if (!TerrainCubeSampleColumn(context, root, &column)) return false;
    int base = (int)lroundf(column.terrain.elevation) + 1;
    root.radial = base;
    const FloraTaxon *taxonInfo = FloraTaxonAt(taxon);
    if (!taxonInfo) return false;
    uint32_t shapeHash = CubeTreeHash(root, 1319u, seed);
    HomeTreeShapeSpec spec = {
        .rootX = root.u,
        .baseY = base,
        .rootZ = root.v,
        .taxonId = taxon,
        .shapeHash = shapeHash,
        .primaryBlock = taxonInfo->primaryBlock,
        .accentBlock = taxonInfo->accentBlock
    };
    HomeTreeShapeBounds bounds;
    if (!HomeTreeShapeBoundsAt(&spec, &bounds)) return false;
    *outTree = (TerrainCubeHomeTree){
        .root = root,
        .baseY = base,
        .taxonId = taxon,
        .shapeHash = shapeHash,
        .bounds = bounds
    };
    return true;
}

typedef struct TreeEmitContext {
    Chunk *chunk;
    SurfaceCubeChunkKey key;
    int sectionY;
    SurfaceAddress root;
    bool emitted;
} TreeEmitContext;

static bool EmitCubeTreeBlock(void *opaque, int shapeU, int y, int shapeV,
                              BlockType type, bool replace)
{
    TreeEmitContext *context = opaque;
    int du = shapeU - context->root.u;
    int dv = shapeV - context->root.v;
    SurfaceAddress cell;
    if (!SurfaceCellOffset(context->root, du, dv, y, &cell)) return false;
    SurfaceCubeChunkKey key;
    if (!SurfaceCubeChunkAt(cell, &key) ||
        !SurfaceCubeChunkKeyEqual(key, context->key)) return true;
    int section = SurfaceSectionYFromBlockY(cell.radial);
    if (context->sectionY != INT_MIN && section != context->sectionY)
        return true;
    ChunkSection *target = ChunkGetSection(context->chunk, section, true);
    if (!target) return false;
    int lx = cell.u % CHUNK_SIZE;
    int lz = cell.v % CHUNK_SIZE;
    int ly = SurfaceSectionLocalYFromBlockY(cell.radial);
    if (replace || target->blocks[lx][ly][lz] == BLOCK_AIR) {
        target->blocks[lx][ly][lz] = (unsigned short)type;
        context->emitted = true;
    }
    return true;
}

static bool SameTree(const FloraStructureInstance *structure,
                     SurfaceAddress root, FloraStructureKind kind)
{
    return structure->kind == kind && structure->cubeNative &&
        SurfaceAddressEqual(structure->rootAddress, root);
}

static void RegisterCubeTree(Chunk *chunk, SurfaceAddress root,
                             FloraTaxonId taxon, uint32_t shapeHash,
                             const HomeTreeShapeBounds *bounds, int base)
{
    if (!chunk || !bounds) return;
    for (int i = 0; i < chunk->floraStructureCount; i++)
        if (SameTree(&chunk->floraStructures[i], root,
                     FLORA_STRUCTURE_HOME_TREE)) return;
    if (chunk->floraStructureCount >= MAX_CHUNK_FLORA_STRUCTURES) return;
    const FloraTaxon *taxonInfo = FloraTaxonAt(taxon);
    if (!taxonInfo) return;
    chunk->floraStructures[chunk->floraStructureCount++] =
        (FloraStructureInstance){
            .kind = FLORA_STRUCTURE_HOME_TREE,
            .taxonId = taxon,
            .shapeHash = shapeHash,
            .rootX = root.u,
            .groundY = base - 1,
            .rootZ = root.v,
            .minY = bounds->minY,
            .maxY = bounds->maxY,
            .primaryBlock = taxonInfo->primaryBlock,
            .accentBlock = taxonInfo->accentBlock,
            .windResponse = taxonInfo->windResponse,
            .cubeNative = true,
            .rootAddress = root,
            .minU = bounds->minX - root.u,
            .maxU = bounds->maxX - root.u,
            .minV = bounds->minZ - root.v,
            .maxV = bounds->maxZ - root.v
        };
}

bool TerrainCubeGenerateHomeTrees(const TerrainCubeContext *context,
                                  Chunk *staging, int sectionY)
{
    if (!context || !staging || !staging->cubeGrid ||
        TerrainCubeContextMode(context) == TERRAIN_FLAT) return true;
    const WorldSurfaceSnapshot *snapshot = TerrainCubeContextSnapshot(context);
    if (!snapshot || snapshot->context.kind != WORLD_SURFACE_HOME ||
        snapshot->context.topology.kind != SURFACE_TOPOLOGY_CUBE_GRID)
        return true;
    SurfaceCubeCandidate neighbors[SURFACE_CUBE_NEIGHBORHOOD_CAPACITY];
    unsigned neighborCount = 0;
    if (!SurfaceCubeNeighborhood(staging->cubeKey, CUBE_TREE_MARGIN_CHUNKS,
            neighbors, SURFACE_CUBE_NEIGHBORHOOD_CAPACITY,
            &neighborCount)) return false;
    for (unsigned n = 0; n < neighborCount; n++) {
        SurfaceCubeChunkKey rootChunk = neighbors[n].key;
        for (int lv = 0; lv < CHUNK_SIZE; lv++) {
            for (int lu = 0; lu < CHUNK_SIZE; lu++) {
                SurfaceAddress root = {rootChunk.bodyId, rootChunk.face,
                    rootChunk.u * CHUNK_SIZE + lu,
                    rootChunk.v * CHUNK_SIZE + lv, 0};
                TerrainCubeHomeTree tree;
                if (!TerrainCubeHomeTreeAt(context, root, &tree)) continue;
                root = tree.root;
                const FloraTaxon *taxonInfo = FloraTaxonAt(tree.taxonId);
                if (!taxonInfo) continue;
                HomeTreeShapeSpec spec = {
                    .rootX = root.u,
                    .baseY = tree.baseY,
                    .rootZ = root.v,
                    .taxonId = tree.taxonId,
                    .shapeHash = tree.shapeHash,
                    .primaryBlock = taxonInfo->primaryBlock,
                    .accentBlock = taxonInfo->accentBlock
                };
                TreeEmitContext emit = {
                    .chunk = staging, .key = staging->cubeKey,
                    .sectionY = sectionY, .root = root
                };
                if (!HomeTreeShapeEmit(&spec, EmitCubeTreeBlock, &emit))
                    return false;
                if (emit.emitted)
                    RegisterCubeTree(staging, root, tree.taxonId,
                                     tree.shapeHash, &tree.bounds,
                                     tree.baseY);
            }
        }
    }
    return true;
}
