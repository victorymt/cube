#include "world/volcano_deposition_model.h"
#include "world/lava_model.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static VolcanoDepositionConfig Config(unsigned w, unsigned h)
{
    return (VolcanoDepositionConfig){w, h, 10000, 1, 100, 293, 1, 0};
}
static VolcanoDepositionCell Read(VolcanoDepositionModel *m, unsigned x, unsigned y)
{
    VolcanoDepositionCell c;
    assert(VolcanoDepositionRead(m, x, y, &c));
    assert(isfinite(c.energy) && isfinite(c.volume));
    assert(c.energy >= 0 && c.volume >= 0 && c.liquidVolume >= 0);
    assert(c.liquidVolume <= c.volume && c.solidDepth >= 0);
    return c;
}
static void Conserved(VolcanoDepositionModel *m)
{
    VolcanoDepositionLedger l = VolcanoDepositionInspect(m);
    assert(fabs(l.volumeError) <= 1e-10 * fmax(1, l.suppliedVolume));
    assert(fabs(l.energyError) <= 1e-10 * fmax(1, l.suppliedEnergy));
}
static void Steps(VolcanoDepositionModel *m, unsigned n)
{
    for (unsigned i = 0; i < n; i++) { assert(VolcanoDepositionStep(m)); Conserved(m); }
}
static void TestSlopeAndBasin(void)
{
    double slope[] = {6, 4, 2, 0};
    VolcanoDepositionModel *m = VolcanoDepositionCreate(Config(4, 1), slope);
    assert(m && VolcanoDepositionSupply(m, 0, 0, 1, 1450) == 1);
    Steps(m, 300);
    assert(Read(m, 3, 0).volume > .99);
    VolcanoDepositionDestroy(m);

    /* A one-block saddle separates a closed basin from the lower outlet. */
    double basin[] = {10, 0, 1, -1};
    m = VolcanoDepositionCreate(Config(4, 1), basin);
    assert(m && VolcanoDepositionSupply(m, 1, 0, .5, 1450) == .5);
    Steps(m, 100);
    assert(Read(m, 1, 0).volume == .5);
    assert(Read(m, 2, 0).volume == 0 && Read(m, 3, 0).volume == 0);
    assert(VolcanoDepositionSupply(m, 1, 0, 4, 1450) == 4);
    Steps(m, 1000);
    assert(Read(m, 3, 0).volume > 2);
    assert(Read(m, 0, 0).volume == 0);
    Conserved(m);
    VolcanoDepositionDestroy(m);
}
static void TestCoolingAndArea(void)
{
    double ground[] = {2, 0};
    VolcanoDepositionConfig c = Config(2, 1); c.cooling = 10000;
    VolcanoDepositionModel *m = VolcanoDepositionCreate(c, ground);
    assert(m && VolcanoDepositionSupply(m, 0, 0, .25, 1450) == .25);
    Steps(m, 10);
    VolcanoDepositionCell a = Read(m, 0, 0), b = Read(m, 1, 0);
    assert(a.liquidVolume == 0 && b.liquidVolume == 0);
    assert(fabs(a.solidDepth + b.solidDepth - .25) < 1e-12);
    Steps(m, 100);
    assert(Read(m, 0, 0).volume == a.volume && Read(m, 1, 0).volume == b.volume);
    Conserved(m);
    VolcanoDepositionDestroy(m);

    /* Equal thickness and exposed area/volume ratio cool identically at
       two resolutions; halving thickness doubles specific cooling. */
    double flat[] = {0};
    c = Config(1, 1); c.cooling = 10;
    m = VolcanoDepositionCreate(c, flat);
    c.cellSize = 4;
    VolcanoDepositionModel *large = VolcanoDepositionCreate(c, flat);
    c.cellSize = 1;
    VolcanoDepositionModel *thin = VolcanoDepositionCreate(c, flat);
    assert(m && large && thin);
    assert(VolcanoDepositionSupply(m, 0, 0, 2, 1450) == 2);
    assert(VolcanoDepositionSupply(large, 0, 0, 32, 1450) == 32);
    assert(VolcanoDepositionSupply(thin, 0, 0, 1, 1450) == 1);
    Steps(m, 100); Steps(large, 100); Steps(thin, 100);
    a = Read(m, 0, 0); b = Read(large, 0, 0);
    assert(a.temperatureK == b.temperatureK);
    assert(fabs((1450 - Read(thin, 0, 0).temperatureK) - 2 * (1450 - a.temperatureK)) < 1e-10);
    VolcanoDepositionDestroy(m); VolcanoDepositionDestroy(large); VolcanoDepositionDestroy(thin);
}
static void TestLatentTransport(void)
{
    double ground[] = {5, 0};
    VolcanoDepositionModel *m = VolcanoDepositionCreate(Config(2, 1), ground);
    /* Equal hot/cold masses at the phase boundary create half-liquid lava. */
    assert(VolcanoDepositionSupply(m, 0, 0, 1, 1100) == 1);
    assert(VolcanoDepositionSupply(m, 0, 0, 1, 1100.001) == 1);
    double liquid = Read(m, 0, 0).liquidVolume;
    assert(liquid > 1 && liquid < 1.001);
    Steps(m, 100);
    assert(fabs(Read(m, 1, 0).volume - liquid) < 1e-10);
    assert(Read(m, 0, 0).liquidVolume < 1e-10);
    Conserved(m);
    VolcanoDepositionDestroy(m);
}
static void TestCapacityAndLimit(void)
{
    double ground[] = {10, 10, 10, 10, 0, 10, 10, 10, 10};
    VolcanoDepositionConfig c = Config(3, 3); c.maxDepth = 1; c.maxSteps = 20;
    VolcanoDepositionModel *m = VolcanoDepositionCreate(c, ground);
    assert(m);
    for (unsigned y = 0; y < 3; y++) for (unsigned x = 0; x < 3; x++) {
        if (x == 1 && y == 1) continue;
        assert(VolcanoDepositionSupply(m, x, y, 2, 1450) == 1);
        assert(VolcanoDepositionSupply(m, x, y, 1, 1450) == 0);
    }
    Steps(m, 20);
    for (unsigned y = 0; y < 3; y++) for (unsigned x = 0; x < 3; x++)
        assert(Read(m, x, y).volume <= 1 + 1e-12);
    assert(Read(m, 1, 1).volume > .99);
    VolcanoDepositionLedger before = VolcanoDepositionInspect(m);
    assert(!VolcanoDepositionStep(m));
    assert(VolcanoDepositionSupply(m, 1, 1, 1, 1450) == 0);
    VolcanoDepositionLedger after = VolcanoDepositionInspect(m);
    assert(before.retainedVolume == after.retainedVolume && before.retainedEnergy == after.retainedEnergy);
    assert(after.retainedVolume == 8 && after.liquidVolume > 0 && after.boundaryVolume > 0);
    VolcanoDepositionDestroy(m);
}
static void TestDeterminism(void)
{
    enum { W = 13, H = 11 };
    double ground[W * H];
    for (unsigned i = 0; i < W * H; i++) ground[i] = sin(i * .37) * 2 + (i / W) * .1;
    VolcanoDepositionConfig c = Config(W, H); c.cellSize = 4; c.cooling = 7; c.maxDepth = 3;
    VolcanoDepositionModel *a = VolcanoDepositionCreate(c, ground), *b = VolcanoDepositionCreate(c, ground);
    assert(a && b);
    for (unsigned tick = 0; tick < 2000; tick++) {
        if (tick % 31 == 0) {
            unsigned x = (tick * 17) % W, y = (tick * 13) % H;
            assert(VolcanoDepositionSupply(a, x, y, 10, 1450) == VolcanoDepositionSupply(b, x, y, 10, 1450));
        }
        assert(VolcanoDepositionStep(a)); assert(VolcanoDepositionStepReverse(b));
        for (unsigned y = 0; y < H; y++) for (unsigned x = 0; x < W; x++) {
            VolcanoDepositionCell ca = Read(a, x, y), cb = Read(b, x, y);
            assert(ca.volume == cb.volume && ca.energy == cb.energy);
            assert(ca.volume <= c.maxDepth * c.cellSize * c.cellSize + 1e-10);
        }
        Conserved(a); Conserved(b);
    }
    VolcanoDepositionDestroy(a); VolcanoDepositionDestroy(b);
}
static void TestInvalid(void)
{
    double ground[] = {0}; VolcanoDepositionConfig c = Config(1, 1);
    assert(!VolcanoDepositionCreate(c, NULL));
    c.width = 129; assert(!VolcanoDepositionCreate(c, ground)); c.width = 1;
    c.cellSize = NAN; assert(!VolcanoDepositionCreate(c, ground)); c.cellSize = 1;
    ground[0] = INFINITY; assert(!VolcanoDepositionCreate(c, ground)); ground[0] = 0;
    VolcanoDepositionModel *m = VolcanoDepositionCreate(c, ground); assert(m);
    assert(VolcanoDepositionSupply(m, 0, 0, NAN, 1450) == 0);
    assert(VolcanoDepositionSupply(m, 0, 0, 1, INFINITY) == 0);
    assert(VolcanoDepositionSupply(m, 1, 0, 1, 1450) == 0);
    assert(VolcanoDepositionSupply(m, 0, 0, -1, 1450) == 0);
    assert(VolcanoDepositionSupply(m, 0, 0, 1, 200) == 0);
    assert(!VolcanoDepositionRead(m, 0, 0, NULL));
    assert(VolcanoDepositionInspect(m).suppliedVolume == 0);
    assert(!VolcanoDepositionStep(NULL));
    VolcanoDepositionDestroy(m); VolcanoDepositionDestroy(NULL);
}
static void TestSymmetryAndMaximumGrid(void)
{
    double ground[9] = {0};
    VolcanoDepositionModel *m = VolcanoDepositionCreate(Config(3, 3), ground);
    assert(m && VolcanoDepositionSupply(m, 1, 1, 8, 1450) == 8);
    Steps(m, 1);
    double side = Read(m, 0, 1).volume;
    assert(side > 0 && side == Read(m, 2, 1).volume);
    assert(side == Read(m, 1, 0).volume && side == Read(m, 1, 2).volume);
    Steps(m, 1000);
    for (unsigned y = 0; y < 3; y++) for (unsigned x = 0; x < 3; x++)
        assert(fabs(Read(m, x, y).volume - 8.0 / 9) < 1e-10);
    VolcanoDepositionDestroy(m);

    static double large[VOLCANO_DEPOSITION_MAX_SIDE * VOLCANO_DEPOSITION_MAX_SIDE];
    VolcanoDepositionConfig c = Config(VOLCANO_DEPOSITION_MAX_SIDE, VOLCANO_DEPOSITION_MAX_SIDE);
    m = VolcanoDepositionCreate(c, large);
    assert(m && VolcanoDepositionSupply(m, c.width - 1, c.height - 1, 1, 1450) == 1);
    Steps(m, 10);
    assert(VolcanoDepositionInspect(m).boundaryVolume > 0);
    VolcanoDepositionDestroy(m);
}
int main(void)
{
    TestSlopeAndBasin(); TestCoolingAndArea(); TestLatentTransport();
    TestCapacityAndLimit(); TestDeterminism(); TestInvalid(); TestSymmetryAndMaximumGrid();
    puts("volcano deposition: flow, overflow, phase, conservation, bounds and determinism passed");
}
