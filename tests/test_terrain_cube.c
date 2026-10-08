#include "world/terrain_cube.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static WorldSurfaceSnapshot Snapshot(bool home, uint32_t seed)
{
    return (WorldSurfaceSnapshot){
        .context = {.active = true, .kind = home ? WORLD_SURFACE_HOME : WORLD_SURFACE_PLANET,
            .topology = {SURFACE_TOPOLOGY_CUBE_GRID, 1}, .bodyId = home ? 0 : 99, .seed = seed},
        .planetProfile = {.seed = seed, .style = SOLAR_STYLE_DESERT,
            .hasSolidSurface = true, .physicalRadiusKm = 5000, .surfaceGravity = 7,
            .rotationRate = 180, .equilibriumTempK = 288, .radiativeTempK = 255,
            .surfacePressureAtm = 1, .atmosphereDensity = 1, .receivedIrradiance = 1,
            .terrainRoughness = .8f, .yearLength = 365, .ageGyr = 4}
    };
}

static uint32_t Fingerprint(const TerrainCubeContext *context)
{
    uint32_t hash = 2166136261u;
    const int positions[] = {0, 2048, 4095};
    uint32_t body = TerrainCubeContextSnapshot(context)->context.bodyId;
    for (int face = 0; face < 6; face++) for (int u = 0; u < 3; u++) for (int v = 0; v < 3; v++) {
        TerrainCubeColumn column;
        assert(TerrainCubeSampleColumn(context,
            (SurfaceAddress){body, (SurfaceFace)face, positions[u], positions[v], 0}, &column));
        assert(column.terrain.elevation > TERRAIN_CUBE_MIN_RADIAL);
        assert(column.terrain.elevation < 256);
        for (int y = TERRAIN_CUBE_MIN_RADIAL; y < 256; y += 17) {
            BlockType block;
            assert(TerrainCubeBlock(context, &column, y, &block));
            if (y == TERRAIN_CUBE_MIN_RADIAL) assert(block == BLOCK_BEDROCK);
            hash = (hash ^ (unsigned)block)*16777619u;
        }
    }
    return hash;
}

typedef struct Worker { TerrainCubeContext *context; uint32_t fingerprint; } Worker;
static void *Generate(void *data)
{
    Worker *worker = data;
    worker->fingerprint = Fingerprint(worker->context);
    TerrainCubeContextRelease(worker->context);
    return NULL;
}

static void TestImmutableContext(void)
{
    WorldSurfaceSnapshot snapshot = Snapshot(true, 1234);
    TerrainCubeContext *home = TerrainCubeContextCreate(&snapshot, TERRAIN_VARIED, 123);
    assert(home);
    uint32_t expected = Fingerprint(home);
    Worker workers[4]; pthread_t threads[4];
    for (int i = 0; i < 4; i++) {
        workers[i] = (Worker){TerrainCubeContextRetain(home), 0};
        assert(workers[i].context);
        assert(pthread_create(&threads[i], NULL, Generate, &workers[i]) == 0);
    }
    // Mutating the caller's snapshot and releasing the owning world must not
    // change or destroy worker inputs, including the immutable drainage atlas.
    snapshot.context.bodyId = 77;
    snapshot.context.seed = 987;
    TerrainCubeContextRelease(home);
    for (int i = 0; i < 4; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
        assert(workers[i].fingerprint == expected);
    }
    WorldSurfaceSnapshot dry = Snapshot(false, 1234);
    TerrainCubeContext *planet = TerrainCubeContextCreate(&dry, TERRAIN_VARIED, 124);
    assert(planet);
    uint32_t dryHash = Fingerprint(planet);
    assert(dryHash != expected);
    assert(expected == 872907943u);
    assert(dryHash == 1510604102u);
    printf("v9 home=%u desert=%u\n", expected, dryHash);
    TerrainCubeContextRelease(planet);
}

static void TestSectionsAndInvalid(void)
{
    WorldSurfaceSnapshot snapshot = Snapshot(true, 77);
    TerrainCubeContext *context = TerrainCubeContextCreate(&snapshot, TERRAIN_FLAT, 100);
    assert(context && TerrainCubeContextGeneration(context) == 100);
    unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    for (int face = 0; face < 6; face++) {
        assert(TerrainCubeGenerateSection(context, (SurfaceCubeChunkKey){0, (SurfaceFace)face, 0, 255}, 0, blocks));
        for (int u = 0; u < 16; u++) for (int y = 0; y < 16; y++) for (int v = 0; v < 16; v++)
            assert(blocks[u][y][v] == (y < 8 ? BLOCK_DIRT : y == 8 ? BLOCK_GRASS : BLOCK_AIR));
    }
    assert(!TerrainCubeGenerateSection(context, (SurfaceCubeChunkKey){1, SURFACE_FACE_POS_X, 0, 0}, 0, blocks));
    assert(!TerrainCubeGenerateSection(context, (SurfaceCubeChunkKey){0, SURFACE_FACE_POS_X, 0, 0}, INT_MAX, blocks));
    TerrainCubeColumn column;
    assert(TerrainCubeSampleColumn(context, (SurfaceAddress){0, SURFACE_FACE_POS_Y, 2048, 2048, 0}, &column));
    BlockType block = BLOCK_GLASS;
    assert(!TerrainCubeBlock(context, &column, INT_MIN, &block));
    assert(block == BLOCK_GLASS);
    column.generation++;
    assert(!TerrainCubeBlock(context, &column, 0, &block));
    TerrainCubeContextRelease(context);
    snapshot.context.topology.kind = SURFACE_TOPOLOGY_LEGACY_MAP;
    assert(!TerrainCubeContextCreate(&snapshot, TERRAIN_VARIED, 100));
    snapshot.context.topology.kind = SURFACE_TOPOLOGY_CUBE_GRID;
    snapshot.context.topology.version++;
    assert(!TerrainCubeContextCreate(&snapshot, TERRAIN_VARIED, 100));
}

int main(void)
{
    TestSectionsAndInvalid(); TestImmutableContext();
    puts("cube terrain tests passed");
    return 0;
}
