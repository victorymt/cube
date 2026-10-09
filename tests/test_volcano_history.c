#include "world/volcano_history.h"
#include "raymath.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

static VolcanoTerrainDefinition Definition(uint32_t seed, SurfaceFace face)
{
    for (int v = 320; v < SURFACE_FACE_BLOCKS; v += 512)
        for (int u = 320; u < SURFACE_FACE_BLOCKS; u += 512) {
            VolcanoTerrainDefinition d;
            if (VolcanoTerrainDefine(seed, 1, (SurfaceAddress){1, face, u, v, 0}, &d)) return d;
        }
    assert(false); return (VolcanoTerrainDefinition){0};
}
static void Conserved(VolcanoHistoryStats s)
{
    assert(fabs(s.ledger.volumeError) < 1e-9 * fmax(1, s.ledger.suppliedVolume));
    assert(fabs(s.ledger.energyError) < 1e-9 * fmax(1, s.ledger.suppliedEnergy));
    assert(fabs(s.requestedVolume - s.remainingVolume - s.ledger.suppliedVolume) < 1e-8);
    assert(s.ledger.liquidVolume == 0 && s.terminalCooling > 0);
}
static void TestMountains(void)
{
    for (unsigned seed = 1234; seed < 1237; seed++) {
        VolcanoTerrainDefinition d = Definition(seed, seed == 1235 ? SURFACE_FACE_POS_Y : SURFACE_FACE_POS_X);
        VolcanoHistoryConfig c = VolcanoHistoryDefaults();
        clock_t begin = clock();
        VolcanoHistory *h = VolcanoHistoryBuild(&d, c, NULL, NULL); assert(h);
        VolcanoHistoryStats s = VolcanoHistoryInspect(h); Conserved(s);
        Vector3 center = SurfaceAddressDirection(d.center);
        for (unsigned axis = 0; axis < 4; axis++) {
            unsigned x = s.side/2, y = s.side/2;
            if (axis == 0) x -= 2;
            if (axis == 1) x += 2;
            if (axis == 2) y -= 2;
            if (axis == 3) y += 2;
            Vector3 dir = VolcanoHistoryDirection(h, x, y);
            double distance = Vector3Distance(dir, center) * SURFACE_RADIUS_BLOCKS;
            assert(fabs(distance - 2 * c.cellSize) < .01);
        }
        printf("seed=%u cell=%u side=%u radius=%.2f flow=%.2f flank=%.3f foot=%.3f max=%.3f boundary=%.6g remaining=%.3f seconds=%.3f peak=%zu\n",
            seed, c.cellSize, s.side, d.radius, s.flowRadius, s.flankVolume, s.footVolume,
            s.maxDepth, s.ledger.boundaryVolume, s.remainingVolume,
            (double)(clock()-begin)/CLOCKS_PER_SEC, s.peakBytes);
        fflush(stdout);
        assert(s.flankVolume > 100 && s.flowRadius > d.craterRadius * 1.5);
        assert(s.footVolume > 1 && s.ledger.boundaryVolume < 1e-6);
        assert(s.maxDepth <= c.events * c.maxDepth + 1e-9);
        VolcanoHistoryEvent e;
        assert(VolcanoHistoryEventAt(h, 0, &e) && e.sourceX == s.side/2 && e.sourceY == s.side/2);
        assert(e.ledger.retainedVolume > 0 && e.ledger.liquidVolume == 0);
        unsigned repeated = 0;
        double sum = 0;
        for (unsigned y = 0; y < s.side; y++) for (unsigned x = 0; x < s.side; x++) {
            VolcanoHistoryCell a; assert(VolcanoHistoryRead(h, c.events-1, x, y, &a));
            assert(a.depth >= 0 && isfinite(a.ground)); sum += a.depth * c.cellSize * c.cellSize;
            if ((a.flowMask & (a.flowMask - 1)) != 0) repeated++;
        }
        assert(repeated > 10 && fabs(sum - s.ledger.retainedVolume) < 1e-7);
        /* Independent rebuilds must match every output cell, not just totals. */
        if (seed == 1234) {
            VolcanoHistory *b = VolcanoHistoryBuild(&d, c, NULL, NULL); assert(b);
            for (unsigned y = 0; y < s.side; y++) for (unsigned x = 0; x < s.side; x++) {
                VolcanoHistoryCell a, other;
                assert(VolcanoHistoryRead(h, c.events-1, x, y, &a));
                assert(VolcanoHistoryRead(b, c.events-1, x, y, &other));
                assert(a.ground == other.ground && a.depth == other.depth && a.flowMask == other.flowMask && a.rock == other.rock);
            }
            VolcanoHistoryDestroy(b);
        }
        VolcanoHistoryDestroy(h);
    }
}
static double InvalidGround(Vector3 direction, void *user)
{
    (void)direction; (void)user; return NAN;
}
static void TestBounds(void)
{
    VolcanoTerrainDefinition d = Definition(1234, SURFACE_FACE_POS_X);
    VolcanoHistoryConfig c = VolcanoHistoryDefaults();
    assert(!VolcanoHistoryBuild(NULL, c, NULL, NULL));
    c.cellSize = 3; assert(!VolcanoHistoryBuild(&d, c, NULL, NULL));
    c = VolcanoHistoryDefaults(); c.events = VOLCANO_HISTORY_MAX_EVENTS+1;
    assert(!VolcanoHistoryBuild(&d, c, NULL, NULL));
    c = VolcanoHistoryDefaults(); c.volumeScale = INFINITY;
    assert(!VolcanoHistoryBuild(&d, c, NULL, NULL));
    c = VolcanoHistoryDefaults(); assert(!VolcanoHistoryBuild(&d, c, InvalidGround, NULL));
    c.maxDepth = .001; c.supplySteps = 1; c.settleSteps = 0;
    VolcanoHistory *h = VolcanoHistoryBuild(&d, c, NULL, NULL); assert(h);
    VolcanoHistoryStats s = VolcanoHistoryInspect(h); Conserved(s);
    assert(s.remainingVolume > s.requestedVolume * .99 && s.maxDepth <= c.events * c.maxDepth);
    assert(!VolcanoHistoryRead(h, c.events, 0, 0, &(VolcanoHistoryCell){0}));
    VolcanoHistoryDestroy(h); VolcanoHistoryDestroy(NULL);
}
static void CompareResolutions(void)
{
    VolcanoTerrainDefinition d = Definition(1234, SURFACE_FACE_POS_X);
    for (unsigned cell = 2; cell <= 8; cell *= 2) {
        VolcanoHistoryConfig c = VolcanoHistoryDefaults(); c.cellSize = cell;
        clock_t start = clock();
        VolcanoHistory *h = VolcanoHistoryBuild(&d, c, NULL, NULL); assert(h);
        VolcanoHistoryStats s = VolcanoHistoryInspect(h); Conserved(s);
        printf("resolution=%u side=%u flank=%.3f foot=%.3f radius=%.3f boundary=%.6g seconds=%.3f peak=%zu\n",
            cell, s.side, s.flankVolume, s.footVolume, s.flowRadius, s.ledger.boundaryVolume,
            (double)(clock()-start)/CLOCKS_PER_SEC, s.peakBytes);
        fflush(stdout);
        /* Resolution study records morphology differences instead of assuming
           this game-scale transport law is resolution invariant. */
        assert(s.flankVolume > 100 && s.ledger.boundaryVolume < 1e-6);
        VolcanoHistoryDestroy(h);
    }
}
int main(void)
{
    TestMountains(); TestBounds(); CompareResolutions();
    puts("volcano history: deterministic eruptions, cooled deposits, foot, ledgers and bounds passed");
}
