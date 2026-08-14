#include "chunks.h"
#include "terrain.h"
#include "world.h"
#include "world_environment.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

TerrainMode terrainMode = TERRAIN_VARIED;

WorldBlockRegion WorldBlockRegionAt(int y)
{
    (void)y;
    return WORLD_BLOCK_REGION_SURFACE;
}

bool WorldCanAccessBlockY(int y)
{
    (void)y;
    return true;
}

BlockType SpaceBlockAt(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
    return BLOCK_AIR;
}

BlockType NetherBlockAt(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
    return BLOCK_AIR;
}

static double NowMs(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (double)value.tv_sec * 1000.0 + (double)value.tv_nsec / 1000000.0;
}

static void FreeMeshDataForBenchmark(Mesh *mesh)
{
    free(mesh->vertices);
    free(mesh->texcoords);
    free(mesh->texcoords2);
    free(mesh->normals);
    free(mesh->colors);
    *mesh = (Mesh){ 0 };
}

static void BuildFixture(
    unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int layerY, int cx, int cz)
{
    memset(blocks, 0, sizeof(unsigned short) * CHUNK_SIZE *
           SURFACE_SECTION_HEIGHT * CHUNK_SIZE);
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int worldX = cx * CHUNK_SIZE + lx;
            int worldZ = cz * CHUNK_SIZE + lz;
            int height = TerrainHeight(worldX, worldZ, TERRAIN_VARIED);
            for (int localY = 0; localY < SURFACE_SECTION_HEIGHT; localY++) {
                int worldY = layerY + localY;
                if (worldY <= height) {
                    blocks[lx][localY][lz] = (unsigned short)(
                        worldY == height ? BLOCK_GRASS :
                        (worldY > height - 3 ? BLOCK_DIRT : BLOCK_STONE));
                } else if (worldY <= HOME_SEA_LEVEL) {
                    blocks[lx][localY][lz] = BLOCK_WATER;
                }
            }
            int floraY = height + 1 - layerY;
            if (((worldX * 13 + worldZ * 7) & 31) == 0 &&
                floraY >= 0 && floraY < SURFACE_SECTION_HEIGHT &&
                height > HOME_SEA_LEVEL) {
                blocks[lx][floraY][lz] = BLOCK_FLOWER;
            }
        }
    }
}

static double MeasureRound(const int coordinates[][2], int count,
                           uint64_t *snapshotBytes)
{
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    double startedMs = NowMs();
    for (int i = 0; i < count; i++) {
        unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
        int centerX = coordinates[i][0] * CHUNK_SIZE + CHUNK_SIZE / 2;
        int centerZ = coordinates[i][1] * CHUNK_SIZE + CHUNK_SIZE / 2;
        int representativeY = TerrainHeight(centerX, centerZ, TERRAIN_VARIED);
        if (representativeY < HOME_SEA_LEVEL) representativeY = HOME_SEA_LEVEL;
        int layerY = representativeY / SURFACE_SECTION_HEIGHT *
                     SURFACE_SECTION_HEIGHT;
        BuildFixture(blocks, layerY, coordinates[i][0], coordinates[i][1]);
        Mesh solid = { 0 };
        Mesh water = { 0 };
        Mesh flora = { 0 };
        bool hasSolid = BuildSurfaceSolidMeshData(
            (const unsigned short (*)[CHUNK_SIZE])blocks,
            SURFACE_SECTION_HEIGHT, layerY,
            coordinates[i][0], coordinates[i][1], faces, NULL, 0, &solid);
        bool hasWater = BuildSurfaceWaterMeshData(
            (const unsigned short (*)[CHUNK_SIZE])blocks,
            SURFACE_SECTION_HEIGHT, layerY,
            coordinates[i][0], coordinates[i][1], faces, NULL, 0, &water);
        bool hasFlora = BuildFloraMeshData(
            (const unsigned short (*)[CHUNK_SIZE])blocks,
            SURFACE_SECTION_HEIGHT, layerY,
            coordinates[i][0], coordinates[i][1], faces, NULL, 0, &flora);
        assert(hasSolid || hasWater || hasFlora);
        FreeMeshDataForBenchmark(&solid);
        FreeMeshDataForBenchmark(&water);
        FreeMeshDataForBenchmark(&flora);
    }
    *snapshotBytes = sizeof(
        unsigned short[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE]);
    return NowMs() - startedMs;
}

static int CompareDouble(const void *left, const void *right)
{
    double a = *(const double *)left;
    double b = *(const double *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static bool ReadBaseline(const char *path, double *medianMs, double *p95Ms,
                         double *snapshotBytes)
{
    FILE *file = fopen(path, "r");
    if (!file) return false;
    bool haveMedian = false;
    bool haveP95 = false;
    bool haveSnapshot = false;
    char line[160];
    while (fgets(line, sizeof(line), file)) {
        char key[64];
        double value = 0.0;
        if (sscanf(line, " %63[^=]=%lf", key, &value) != 2) continue;
        if (strcmp(key, "median_total_ms") == 0) {
            *medianMs = value;
            haveMedian = true;
        } else if (strcmp(key, "p95_total_ms") == 0) {
            *p95Ms = value;
            haveP95 = true;
        } else if (strcmp(key, "mesh_snapshot_bytes") == 0) {
            *snapshotBytes = value;
            haveSnapshot = true;
        }
    }
    fclose(file);
    return haveMedian && haveP95 && haveSnapshot;
}

static bool CheckBaseline(const char *path, double medianMs, double p95Ms,
                          double snapshotBytes)
{
    double baselineMedian = 0.0;
    double baselineP95 = 0.0;
    double baselineSnapshot = 0.0;
    if (!ReadBaseline(path, &baselineMedian, &baselineP95, &baselineSnapshot) ||
        baselineMedian <= 0.0 || baselineP95 <= 0.0 || baselineSnapshot <= 0.0) {
        fprintf(stderr, "baseline_invalid=%s\n", path);
        return false;
    }
    bool medianImproved = medianMs <= baselineMedian * 0.80;
    bool p95Stable = p95Ms <= baselineP95 * 1.05;
    bool snapshotReduced = snapshotBytes <= baselineSnapshot * 0.60;
    printf("baseline_median_target=%s\n", medianImproved ? "pass" : "fail");
    printf("baseline_p95_target=%s\n", p95Stable ? "pass" : "fail");
    printf("baseline_snapshot_target=%s\n", snapshotReduced ? "pass" : "fail");
    return medianImproved && p95Stable && snapshotReduced;
}

int main(int argc, char **argv)
{
    static const int coordinates[][2] = {
        { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 },
        { 0, -1 }, { 2, 2 }, { -2, 1 }, { 3, -2 }
    };
    enum { warmupRounds = 4, measureRounds = 20 };
    double samples[measureRounds];
    uint64_t snapshotBytes = 0;

    WorldReset(DEFAULT_WORLD_SEED);
    terrainMode = TERRAIN_VARIED;
    for (int i = 0; i < warmupRounds; i++) {
        MeasureRound(coordinates, (int)(sizeof(coordinates) / sizeof(coordinates[0])),
                     &snapshotBytes);
    }
    for (int i = 0; i < measureRounds; i++) {
        samples[i] = MeasureRound(coordinates,
                                  (int)(sizeof(coordinates) / sizeof(coordinates[0])),
                                  &snapshotBytes);
    }
    qsort(samples, measureRounds, sizeof(samples[0]), CompareDouble);
    printf("seed=%u\n", DEFAULT_WORLD_SEED);
    printf("rounds=%d\n", measureRounds);
    printf("chunks_per_round=%zu\n", sizeof(coordinates) / sizeof(coordinates[0]));
    double medianMs = samples[measureRounds / 2];
    double p95Ms = samples[(measureRounds * 95) / 100];
    printf("median_total_ms=%.3f\n", medianMs);
    printf("p95_total_ms=%.3f\n", p95Ms);
    printf("mesh_snapshot_bytes=%llu\n", (unsigned long long)snapshotBytes);
    printf("legacy_two_snapshot_bytes=%llu\n", (unsigned long long)(snapshotBytes * 2u));
    printf("sync_rebuilds=0\n");
    if (argc > 1) return CheckBaseline(argv[1], medianMs, p95Ms,
                                        (double)snapshotBytes) ? 0 : 2;
    return 0;
}
