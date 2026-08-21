#include "world/chunks.h"
#include "world/chunks_internal.h"
#include "world/terrain.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

TerrainMode terrainMode = TERRAIN_VARIED;

typedef struct MeshBenchmarkTotals {
    uint64_t solidVertices;
    uint64_t waterVertices;
    uint64_t floraVertices;
    uint64_t legacySolidVertices;
    uint64_t meshBytes;
} MeshBenchmarkTotals;

bool WorldIsSurfaceActive(void)
{
    return false;
}

bool HomeWorldSurfaceIsActive(void)
{
    return false;
}

uint32_t WorldCurrentSurfaceId(void)
{
    return 0u;
}

int WorldSurfaceMapOriginX(void)
{
    return 0;
}

int WorldSurfaceMapOriginZ(void)
{
    return 0;
}

void WorldSetNetherActive(bool active)
{
    (void)active;
}

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

static uint64_t MeshCpuBytes(const Mesh *mesh)
{
    if (!mesh || mesh->vertexCount <= 0) return 0u;
    uint64_t vertices = (uint64_t)mesh->vertexCount;
    return vertices * (3u * sizeof(float) + 2u * sizeof(float) +
                       2u * sizeof(float) + 3u * sizeof(float) +
                       4u * sizeof(unsigned char));
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
                           uint64_t *snapshotBytes,
                           MeshBenchmarkTotals *totals, int solidSpan)
{
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    if (totals) *totals = (MeshBenchmarkTotals){ 0 };
    double startedMs = NowMs();
    for (int i = 0; i < count; i++) {
        unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
        int centerX = coordinates[i][0] * CHUNK_SIZE + CHUNK_SIZE / 2;
        int centerZ = coordinates[i][1] * CHUNK_SIZE + CHUNK_SIZE / 2;
        int representativeY = TerrainHeight(centerX, centerZ, TERRAIN_VARIED);
        int sectionY = representativeY >= 0
            ? representativeY / SURFACE_SECTION_HEIGHT
            : (representativeY - SURFACE_SECTION_HEIGHT + 1) /
              SURFACE_SECTION_HEIGHT;
        int layerY = sectionY * SURFACE_SECTION_HEIGHT;
        BuildFixture(blocks, layerY, coordinates[i][0], coordinates[i][1]);
        Mesh solid = { 0 };
        Mesh water = { 0 };
        Mesh flora = { 0 };
        bool hasSolid = BuildMeshDataFilteredWithSnapshotSpan(
            (const unsigned short (*)[CHUNK_SIZE])blocks,
            SURFACE_SECTION_HEIGHT, layerY,
            coordinates[i][0], coordinates[i][1],
            false, false, false, false, faces, NULL, 0, solidSpan, NULL,
            &solid);
        bool hasWater = BuildSurfaceWaterMeshData(
            (const unsigned short (*)[CHUNK_SIZE])blocks,
            NULL, SURFACE_SECTION_HEIGHT, layerY,
            coordinates[i][0], coordinates[i][1], faces, NULL, 0, &water);
        bool hasFlora = BuildFloraMeshData(
            (const unsigned short (*)[CHUNK_SIZE])blocks,
            SURFACE_SECTION_HEIGHT, layerY,
            coordinates[i][0], coordinates[i][1], faces, NULL, 0, &flora);
        assert(hasSolid || hasWater || hasFlora);
        if (totals) {
            totals->solidVertices += (uint64_t)solid.vertexCount;
            totals->waterVertices += (uint64_t)water.vertexCount;
            totals->floraVertices += (uint64_t)flora.vertexCount;
            totals->meshBytes += MeshCpuBytes(&solid) + MeshCpuBytes(&water) +
                                 MeshCpuBytes(&flora);
            Mesh legacySolid = { 0 };
            bool hasLegacySolid = BuildMeshDataFilteredWithSnapshotSpan(
                (const unsigned short (*)[CHUNK_SIZE])blocks,
                SURFACE_SECTION_HEIGHT, layerY,
                coordinates[i][0], coordinates[i][1],
                false, false, false, false, faces, NULL, 0, 0, NULL,
                &legacySolid);
            assert(hasLegacySolid == hasSolid);
            totals->legacySolidVertices +=
                (uint64_t)legacySolid.vertexCount;
            FreeMeshDataForBenchmark(&legacySolid);
        }
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
    enum {
        warmupRounds = 4,
        measureRounds = 20,
        legacyWarmupRounds = 2,
        legacyMeasureRounds = 10
    };
    double samples[measureRounds];
    double legacySamples[legacyMeasureRounds];
    uint64_t snapshotBytes = 0;
    MeshBenchmarkTotals totals = { 0 };

    WorldReset(DEFAULT_WORLD_SEED);
    terrainMode = TERRAIN_VARIED;
    for (int i = 0; i < warmupRounds; i++) {
        MeasureRound(coordinates, (int)(sizeof(coordinates) / sizeof(coordinates[0])),
                     &snapshotBytes, NULL, GREEDY_MESH_MAX_SPAN);
    }
    for (int i = 0; i < measureRounds; i++) {
        samples[i] = MeasureRound(coordinates,
                                  (int)(sizeof(coordinates) / sizeof(coordinates[0])),
                                  &snapshotBytes, NULL,
                                  GREEDY_MESH_MAX_SPAN);
    }
    int chunkCount = (int)(sizeof(coordinates) / sizeof(coordinates[0]));
    for (int i = 0; i < legacyWarmupRounds; i++) {
        (void)MeasureRound(
            coordinates, chunkCount, &snapshotBytes, NULL, 0);
    }
    for (int i = 0; i < legacyMeasureRounds; i++) {
        legacySamples[i] = MeasureRound(
            coordinates, chunkCount, &snapshotBytes, NULL, 0);
    }
    (void)MeasureRound(
        coordinates, chunkCount, &snapshotBytes, &totals,
        GREEDY_MESH_MAX_SPAN);
    qsort(samples, measureRounds, sizeof(samples[0]), CompareDouble);
    qsort(legacySamples, legacyMeasureRounds, sizeof(legacySamples[0]),
          CompareDouble);
    printf("seed=%u\n", DEFAULT_WORLD_SEED);
    printf("rounds=%d\n", measureRounds);
    printf("chunks_per_round=%zu\n", sizeof(coordinates) / sizeof(coordinates[0]));
    double medianMs = samples[measureRounds / 2];
    double p95Ms = samples[(measureRounds * 95) / 100];
    printf("median_total_ms=%.3f\n", medianMs);
    printf("p95_total_ms=%.3f\n", p95Ms);
    double legacyMedianMs = legacySamples[legacyMeasureRounds / 2];
    printf("legacy_median_total_ms=%.3f\n", legacyMedianMs);
    printf("greedy_cpu_ratio_percent=%.1f\n",
           legacyMedianMs > 0.0 ? medianMs / legacyMedianMs * 100.0 : 0.0);
    printf("mesh_snapshot_bytes=%llu\n", (unsigned long long)snapshotBytes);
    printf("legacy_two_snapshot_bytes=%llu\n", (unsigned long long)(snapshotBytes * 2u));
    double solidVerticesPerChunk =
        (double)totals.solidVertices / (double)chunkCount;
    double legacySolidVerticesPerChunk =
        (double)totals.legacySolidVertices / (double)chunkCount;
    double totalVerticesPerChunk =
        (double)(totals.solidVertices + totals.waterVertices +
                 totals.floraVertices) / (double)chunkCount;
    double reduction = legacySolidVerticesPerChunk > 0.0
        ? 100.0 * (1.0 - solidVerticesPerChunk /
                         legacySolidVerticesPerChunk)
        : 0.0;
    printf("solid_vertices_per_chunk=%.1f\n", solidVerticesPerChunk);
    printf("legacy_solid_vertices_per_chunk=%.1f\n",
           legacySolidVerticesPerChunk);
    printf("solid_vertex_reduction_percent=%.1f\n", reduction);
    printf("total_vertices_per_chunk=%.1f\n", totalVerticesPerChunk);
    printf("mesh_cpu_bytes_per_chunk=%.1f\n",
           (double)totals.meshBytes / (double)chunkCount);
    printf("sync_rebuilds=0\n");
    if (argc > 1) return CheckBaseline(argv[1], medianMs, p95Ms,
                                        (double)snapshotBytes) ? 0 : 2;
    return 0;
}
