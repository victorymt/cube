#include "world/volcano_history_cache.h"
#include "world/terrain_cube.h"
#include "world/terrain_generation.h"
#include "raymath.h"
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static PlanetProfile Profile(uint32_t seed)
{
    PlanetProfile p = PlanetProfileGenerateLegacy(seed, SOLAR_STYLE_DESERT, 6000);
    p.volcanicActivity = 1; p.oceanCoverage = 0;
    return p;
}
static VolcanoTerrainDefinition Find(uint32_t seed, const PlanetProfile *p, int region)
{
    for (SurfaceFace face = 0; face < SURFACE_FACE_COUNT; face++) {
        if (region == 1 && face != SURFACE_FACE_POS_Y) continue;
        for (int v = 320; v < SURFACE_FACE_BLOCKS; v += 512) for (int u = 320; u < SURFACE_FACE_BLOCKS; u += 512) {
            if (region == 1 && (abs(u - 2048) > 400 || abs(v - 2048) > 400)) continue;
            if (region == 2 && u < 3584 && v < 3584) continue;
            SurfaceAddress center = {0,face,u,v,0};
            VolcanoTerrainDefinition d;
            if (VolcanoTerrainDefine(seed, p->volcanicActivity, center, &d) &&
                VolcanoHistoricalTerrainEligible(seed, p, center)) return d;
        }
    }
    return (VolcanoTerrainDefinition){0};
}
typedef struct Worker {
    uint32_t seed;
    PlanetProfile p;
    Vector3 direction;
    VolcanoHistoricalTerrain result;
} Worker;
static void *Query(void *value)
{
    Worker *w = value;
    w->result = VolcanoHistoricalTerrainAt(w->seed, &w->p, w->direction);
    return NULL;
}
static void TestConcurrentBuildAndIdentity(void)
{
    VolcanoHistoryCacheReset();
    PlanetProfile p = Profile(1234);
    VolcanoTerrainDefinition d = Find(1234, &p, 0); assert(d.radius > 0);
    pthread_t threads[8]; Worker workers[8];
    for (unsigned i = 0; i < 8; i++) {
        workers[i] = (Worker){.seed=1234, .p=p, .direction=SurfaceAddressDirection(d.center)};
        assert(pthread_create(&threads[i], NULL, Query, &workers[i]) == 0);
    }
    for (unsigned i = 0; i < 8; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
        VolcanoHistoricalTerrain h = workers[i].result;
        assert(h.domain && !h.boundaryRejected && h.depth > 0);
        assert(h.depth == workers[0].result.depth && h.rock == workers[0].result.rock);
        assert(fabs(h.model.ledger.retainedVolume - h.representedVolume - h.quantizationLoss) < 1e-7);
        assert(h.quantizationLoss >= 0 && h.quantizationLoss < h.model.side * h.model.side * 16);
    }
    VolcanoHistoryCacheStats s = VolcanoHistoryCacheInspect();
    assert(s.builds == 1 && s.hits == 7 && s.residents == 1);
    printf("concurrent seed=1234 face=%d u=%d v=%d depth=%u build_cpu=%.3f resident=%zu peak=%zu\n",
        d.center.face, d.center.u, d.center.v, workers[0].result.depth, s.buildSeconds, s.residentBytes, s.peakBytes);
    /* Profile fields are part of identity even when this particular sample
       does not visibly change; aliases of body/radial do not change geology. */
    PlanetProfile changed = p; changed.prevailingWindAngle += .7f;
    (void)VolcanoHistoricalTerrainAt(1234, &changed, workers[0].direction);
    assert(VolcanoHistoryCacheInspect().builds == 2);
    d.center.bodyId = 99; d.center.radial = 123;
    assert(VolcanoHistoricalTerrainEligible(1234, &p, d.center));
    VolcanoHistoryCacheReset();
    VolcanoHistoricalTerrain rebuilt = VolcanoHistoricalTerrainAt(1234, &p, workers[0].direction);
    assert(rebuilt.depth == workers[0].result.depth && rebuilt.representedVolume == workers[0].result.representedVolume);
}
static void TestCubeColumnsAndBoundaries(void)
{
    for (unsigned region = 0; region < 3; region++) {
        uint32_t seed = 1234 + region;
        PlanetProfile p;
        VolcanoTerrainDefinition d = {0};
        for (unsigned tries = 0; tries < 20 && d.radius == 0; tries++, seed++) {
            p = Profile(seed); d = Find(seed, &p, region);
            if (d.radius > 0) break;
        }
        assert(d.radius > 0);
        WorldSurfaceSnapshot snapshot = {.context = {.active=true, .kind=WORLD_SURFACE_PLANET,
            .bodyId=77, .seed=seed, .topology={SURFACE_TOPOLOGY_CUBE_GRID,1}}, .planetProfile=p};
        assert(TerrainGenerationSetVersion(10));
        TerrainCubeContext *initial = TerrainCubeContextCreate(&snapshot, TERRAIN_VARIED, 1);
        assert(TerrainGenerationSetVersion(11));
        TerrainCubeContext *final = TerrainCubeContextCreate(&snapshot, TERRAIN_VARIED, 2);
        assert(initial && final);
        unsigned deposited = 0, otherFaces = 0, otherFaceDeposits = 0;
        int reach = region == 2 ? 280 : 140;
        for (int v = -reach; v <= reach; v += 7) for (int u = -reach; u <= reach; u += 7) {
            SurfaceAddress address = SurfaceAddressOffset(d.center, u, v, 0); address.bodyId = 77;
            TerrainCubeColumn a, b;
            assert(TerrainCubeSampleColumn(initial, address, &a));
            assert(TerrainCubeSampleColumn(final, address, &b));
            assert(b.terrain.elevation == a.terrain.elevation + b.terrain.historicalDepth);
            assert(b.terrain.elevation < SURFACE_GENERATION_MAX_Y_EXCLUSIVE - 1);
            if (address.face != d.center.face) otherFaces++;
            if (!b.terrain.historicalDepth) continue;
            deposited++;
            if (address.face != d.center.face) otherFaceDeposits++;
            int height = (int)lroundf(b.terrain.elevation);
            BlockType rock;
            for (unsigned layer = 0; layer < b.terrain.historicalDepth; layer++) {
                assert(TerrainCubeBlock(final, &b, height - (int)layer, &rock));
                assert(rock == b.terrain.historicalRock);
            }
            BlockType before, after;
            assert(TerrainCubeBlock(initial, &a, (int)a.terrain.elevation, &before));
            assert(TerrainCubeBlock(final, &b, (int)a.terrain.elevation, &after));
            assert(before == after); /* deposits do not rewrite the buried substrate */
        }
        assert(deposited > 0);
        if (region == 2) assert(otherFaces > 0);
        /* Compare actual generated sections across a chunk boundary, including
           material and air, against the same immutable column queries. */
        d.center.bodyId = 77;
        TerrainCubeColumn center; assert(TerrainCubeSampleColumn(final, d.center, &center));
        unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
        int section = (int)center.terrain.elevation / SURFACE_SECTION_HEIGHT;
        clock_t begin = clock();
        for (int neighbor = 0; neighbor < 2; neighbor++) {
            SurfaceCubeChunkKey key = {77, d.center.face, d.center.u/16 + neighbor, d.center.v/16};
            assert(TerrainCubeGenerateSection(final, key, section, blocks));
            for (int x = 0; x < CHUNK_SIZE; x++) for (int z = 0; z < CHUNK_SIZE; z++) {
                TerrainCubeColumn c;
                assert(TerrainCubeSampleColumn(final, (SurfaceAddress){77,key.face,key.u*16+x,key.v*16+z,0}, &c));
                for (int y = 0; y < SURFACE_SECTION_HEIGHT; y++) {
                    BlockType expected; assert(TerrainCubeBlock(final, &c, section*16+y, &expected));
                    assert(blocks[x][y][z] == expected);
                }
            }
        }
        printf("region=%u seed=%u face=%d u=%d v=%d deposited=%u cross_face_queries=%u cross_face_deposits=%u two_sections_seconds=%.4f\n",
            region, seed, d.center.face, d.center.u, d.center.v, deposited, otherFaces, otherFaceDeposits,
            (double)(clock()-begin)/CLOCKS_PER_SEC);
        SurfaceCubeChunkKey key = {77, d.center.face, d.center.u/16, d.center.v/16};
        unsigned short cold[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
        VolcanoHistoryCacheReset();
        begin = clock(); assert(TerrainCubeGenerateSection(final, key, section, cold));
        double coldSeconds = (double)(clock()-begin)/CLOCKS_PER_SEC;
        begin = clock(); assert(TerrainCubeGenerateSection(final, key, section, blocks));
        double warmSeconds = (double)(clock()-begin)/CLOCKS_PER_SEC;
        assert(memcmp(cold, blocks, sizeof(cold)) == 0);
        begin = clock(); assert(TerrainCubeGenerateSection(initial, key, section, blocks));
        printf("section_benchmark region=%u cold=%.6f warm=%.6f baseline=%.6f\n", region, coldSeconds, warmSeconds,
            (double)(clock()-begin)/CLOCKS_PER_SEC);
        TerrainCubeContextRelease(initial); TerrainCubeContextRelease(final);
    }
}
static void TestCapacityAndSelection(void)
{
    VolcanoHistoryCacheReset();
    PlanetProfile p = Profile(1234);
    unsigned built = 0;
    for (SurfaceFace face = 0; face < SURFACE_FACE_COUNT && built < 10; face++)
        for (int v = 320; v < 4096 && built < 10; v += 512) for (int u = 320; u < 4096 && built < 10; u += 512) {
            SurfaceAddress center = {0,face,u,v,0};
            if (!VolcanoHistoricalTerrainEligible(1234, &p, center)) continue;
            VolcanoHistoricalTerrain h = VolcanoHistoricalTerrainAt(1234, &p, SurfaceAddressDirection(center));
            assert(h.domain); built++;
        }
    VolcanoHistoryCacheStats s = VolcanoHistoryCacheInspect();
    assert(built == 10 && s.residents == VOLCANO_HISTORY_CACHE_CAPACITY && s.evictions == 2);
    assert(s.residentBytes < 16u * 1024 * 1024 && s.peakBytes < 20u * 1024 * 1024);
    printf("cache builds=%llu evictions=%llu rejected=%llu resident=%zu peak=%zu\n",
        (unsigned long long)s.builds, (unsigned long long)s.evictions,
        (unsigned long long)s.boundaryRejected, s.residentBytes, s.peakBytes);
    p.style = SOLAR_STYLE_TEMPERATE; p.oceanCoverage = .6;
    assert(!VolcanoHistoricalTerrainAt(1234, &p, (Vector3){1,0,0}).domain);
    VolcanoHistoryCacheReset();
    assert(VolcanoHistoryCacheInspect().residentBytes == 0);
}
static void TestDepositAcrossFace(void)
{
    /* Find an actual solid deposit crossing a face, not merely a query on an
       empty neighboring face. Stable search also prints the H3 visual fixture. */
    bool found = false;
    for (uint32_t seed = 1234; seed < 1240 && !found; seed++) {
        PlanetProfile p = Profile(seed);
        for (SurfaceFace face = 0; face < SURFACE_FACE_COUNT && !found; face++)
            for (int v = 320; v < 4096 && !found; v += 512) for (int u = 320; u < 4096 && !found; u += 512) {
                if (u < 3584 && v < 3584) continue;
                SurfaceAddress center = {0,face,u,v,0};
                VolcanoTerrainDefinition d;
                if (!VolcanoTerrainDefine(seed, 1, center, &d) || !VolcanoHistoricalTerrainEligible(seed, &p, center)) continue;
                for (int offset = -224; offset <= 224 && !found; offset += 8) {
                    SurfaceAddress across = u >= 3584 ? SurfaceAddressOffset(center, 4096-u+2, offset, 0)
                        : SurfaceAddressOffset(center, offset, 4096-v+2, 0);
                    assert(across.face != face);
                    Vector3 direction = SurfaceAddressDirection(across);
                    VolcanoHistoricalTerrain h = VolcanoHistoricalTerrainAt(seed, &p, direction);
                    if (!h.domain || !h.depth || h.definition.seed != d.seed) continue;
                    float longitude, latitude; SurfaceAddressLatLon(across, &longitude, &latitude);
                    Vector3 alias = {cosf(latitude)*cosf(longitude), sinf(latitude), cosf(latitude)*sinf(longitude)};
                    VolcanoHistoricalTerrain same = VolcanoHistoricalTerrainAt(seed, &p, alias);
                    assert(same.depth == h.depth && same.rock == h.rock && same.definition.seed == h.definition.seed);
                    assert(!h.boundaryRejected && h.model.ledger.boundaryVolume <= 1e-6);
                    WorldSurfaceSnapshot snapshot = {.context = {.active=true, .kind=WORLD_SURFACE_PLANET,
                        .bodyId=77, .seed=seed, .topology={SURFACE_TOPOLOGY_CUBE_GRID,1}}, .planetProfile=p};
                    TerrainCubeContext *context = TerrainCubeContextCreate(&snapshot, TERRAIN_VARIED, 9);
                    assert(context); across.bodyId = 77;
                    TerrainCubeColumn column; assert(TerrainCubeSampleColumn(context, across, &column));
                    assert(column.terrain.historicalDepth == h.depth);
                    int height = (int)lroundf(column.terrain.elevation);
                    unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
                    SurfaceCubeChunkKey key = {77, across.face, across.u/16, across.v/16};
                    assert(TerrainCubeGenerateSection(context, key, height/16, blocks));
                    assert(blocks[across.u%16][height%16][across.v%16] == column.terrain.historicalRock);
                    TerrainCubeContextRelease(context);
                    printf("cross_face_fixture seed=%u center=%d,%d,%d deposit=%d,%d,%d depth=%u\n",
                        seed, face, u, v, across.face, across.u, across.v, h.depth);
                    found = true;
                }
            }
    }
    assert(found);
    VolcanoHistoryCacheReset();
}
int main(void)
{
    TestConcurrentBuildAndIdentity(); TestCubeColumnsAndBoundaries(); TestCapacityAndSelection(); TestDepositAcrossFace();
    puts("volcano history cache: publication, identity, rebuild, material, sections, bounds passed");
}
