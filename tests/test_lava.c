#include "world/lava_runtime.h"
#include "world/volcano_runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool closed, unknown, slope;
static unsigned solidBlocks;
static LavaBoundary Boundary(int x, int y, int z, void *user)
{
    (void)user;
    if (unknown) return LAVA_UNKNOWN;
    if (y < (slope && x > 0 ? 0 : 1)) return LAVA_BLOCKED;
    if (closed && (x != 0 || z != 0)) return LAVA_BLOCKED;
    return LAVA_OPEN;
}
static bool Solidify(int x, int y, int z, void *user)
{
    (void)x; (void)y; (void)z; (void)user;
    solidBlocks++; return true;
}
static LavaContext Context(void)
{
    return (LavaContext){ .valid = true, .bodyId = 7u, .boundary = Boundary, .solidify = Solidify };
}
static void Reset(void)
{
    LavaRuntimeReset(); closed = unknown = slope = false; solidBlocks = 0u;
}
static void Balance(void)
{
    LavaStats s = LavaRuntimeStats();
    assert(s.supplied == s.volume + s.solidified + s.removed);
    assert(fabs(s.suppliedEnergy - s.cooledEnergy - s.exportedEnergy - s.energy) <
           fmax(0.01, s.suppliedEnergy * 1e-9));
    assert(s.hotCells <= LAVA_MAX_HOT_CELLS && s.records <= LAVA_MAX_RECORDS);
    LavaCell c;
    for (unsigned i = 0u; LavaRuntimeAt(i, &c); i++) assert(LavaThermalValid(c.thermal));
}
static void TestThermal(void)
{
    LavaThermalState full = { 1024u, 1024u * LavaSpecificEnthalpy(1450.0) };
    assert(LavaTemperature(full) == 1450.0 && LavaLiquidVolume(full) == 1024u);
    LavaThermalState half = { 1024u, 1024u * (LAVA_FREEZING_K - LAVA_REFERENCE_K + LAVA_LATENT_K / 2) };
    assert(LavaTemperature(half) == LAVA_FREEZING_K && LavaLiquidVolume(half) == 512u);
    assert(LavaFlowBudget(half) < LavaFlowBudget(full));
    double energy = full.energy;
    assert(LavaCool(&full, 293.0, 0.1) > 0.0 && full.energy < energy);
    for (unsigned i = 0u; i < 10000u; i++) LavaCool(&full, 293.0, 0.1);
    assert(LavaTemperature(full) == 293.0 && LavaLiquidVolume(full) == 0u);
}
static void TestFlowAndCooling(void)
{
    Reset(); slope = true;
    LavaContext c = Context();
    assert(LavaRuntimeDeposit(&c, 0, 2, 0, 1024u, 1450.0) == 1024u);
    LavaRuntimeStep(&c);
    LavaCell below;
    assert(LavaRuntimeCellAt(7u, 0, 1, 0, &below) && below.thermal.volume > 0u);
    for (unsigned i = 0u; i < 100u; i++) { LavaRuntimeStep(&c); Balance(); }
    bool downslope = false;
    LavaCell cell;
    for (unsigned i = 0u; LavaRuntimeAt(i, &cell); i++) downslope |= cell.y == 0 && cell.x > 0;
    assert(downslope);
    for (unsigned i = 0u; i < 500u; i++) LavaRuntimeStep(&c);
    assert(LavaRuntimeStats().hotCells == 0u);
    assert(LavaRuntimeStats().liquidVolume == 0u);
    assert(LavaRuntimeStats().solidVolume > 0u); /* partials are never promoted to full blocks */
    Balance();
    Reset(); closed = true;
    c = Context();
    assert(LavaRuntimeDeposit(&c, 0, 1, 0, 1024u, 1450.0) == 1024u);
    for (unsigned i = 0u; i < 500u; i++) LavaRuntimeStep(&c);
    assert(solidBlocks == 1u && LavaRuntimeStats().solidified == 1024u);
    assert(LavaRuntimeStats().records == 0u);
    Balance();
}
static void TestUnknownAndMixing(void)
{
    Reset(); closed = true;
    LavaContext c = Context();
    assert(LavaRuntimeDeposit(&c, 0, 1, 0, 256u, 1450.0) == 256u);
    assert(LavaRuntimeDeposit(&c, 0, 1, 0, 768u, 1250.0) == 768u);
    LavaCell before, after;
    assert(LavaRuntimeCellAt(7u, 0, 1, 0, &before));
    assert(LavaTemperature(before.thermal) == 1300.0);
    unknown = true;
    for (unsigned i = 0u; i < 100u; i++) LavaRuntimeStep(&c);
    assert(LavaRuntimeDeposit(&c, 0, 2, 0, 10u, 1450.0) == 0u);
    assert(LavaRuntimeCellAt(7u, 0, 1, 0, &after));
    assert(before.thermal.energy == after.thermal.energy && before.thermal.volume == after.thermal.volume);
    unknown = false; c.bodyId = 8u;
    LavaRuntimeStep(&c);
    assert(LavaRuntimeCellAt(7u, 0, 1, 0, &after) && before.thermal.energy == after.thermal.energy);
    Balance();
}
static void TestCapacity(void)
{
    Reset(); LavaContext c = Context();
    for (unsigned i = 0u; i < LAVA_MAX_HOT_CELLS; i++) {
        assert(LavaRuntimeDeposit(&c, (int)i, 1, 0, 16u, 1450.0) == 16u);
    }
    assert(LavaRuntimeDeposit(&c, -100, 1, 0, 16u, 1450.0) == 0u);
    uint64_t before = LavaRuntimeStats().supplied;
    LavaRuntimeStep(&c);
    assert(LavaRuntimeStats().supplied == before);
    Balance();
}
static void Tick(void *user) { LavaRuntimeStep(user); }
static void TestOrderAndPersistence(void)
{
    LavaCell baseline[1024]; unsigned baselineCount = 0u;
    LavaContext c = Context();
    for (unsigned run = 0u; run < 2u; run++) {
        Reset();
        int first = run ? 1 : -1;
        assert(LavaRuntimeDeposit(&c, first, 1, 0, 1024u, first < 0 ? 1450.0 : 1250.0) == 1024u);
        assert(LavaRuntimeDeposit(&c, -first, 1, 0, 1024u, first < 0 ? 1250.0 : 1450.0) == 1024u);
        for (unsigned i = 0u; i < 20u; i++) LavaRuntimeStep(&c);
        Balance();
        unsigned n = LavaRuntimeStats().records;
        assert(n < 1024u);
        if (!run) {
            baselineCount = n;
            for (unsigned i = 0u; i < n; i++) assert(LavaRuntimeAt(i, &baseline[i]));
        } else {
            assert(n == baselineCount);
            for (unsigned i = 0u; i < n; i++) {
                LavaCell actual; assert(LavaRuntimeAt(i, &actual));
                assert(actual.x == baseline[i].x && actual.y == baseline[i].y && actual.z == baseline[i].z);
                assert(actual.thermal.volume == baseline[i].thermal.volume);
                assert(actual.thermal.energy == baseline[i].thermal.energy);
            }
        }
    }
    LavaRuntimeAdvance(0.025, true, Tick, &c);
    FILE *file = tmpfile(); assert(file && LavaRuntimeSave(file));
    long length = ftell(file); assert(length > 96);
    unsigned char bytes[65536]; assert(length < (long)sizeof(bytes));
    rewind(file); assert(fread(bytes, 1u, (size_t)length, file) == (size_t)length);
    rewind(file);
    for (unsigned i = 0u; i < 10u; i++) LavaRuntimeAdvance(0.5, true, Tick, &c);
    LavaStats expected = LavaRuntimeStats();
    assert(LavaRuntimeLoad(file)); fclose(file);
    for (unsigned i = 0u; i < 300u; i++) LavaRuntimeAdvance(1.0 / 60.0, true, Tick, &c);
    LavaStats actual = LavaRuntimeStats();
    assert(actual.volume == expected.volume && actual.liquidVolume == expected.liquidVolume);
    assert(actual.energy == expected.energy && actual.ticks == expected.ticks);
    Balance();
    for (unsigned cut = 0u; cut < (unsigned)length; cut += 17u) {
        file = tmpfile(); assert(file && fwrite(bytes, 1u, cut, file) == cut);
        rewind(file); assert(!LavaRuntimeLoad(file)); fclose(file);
        assert(LavaRuntimeStats().energy == expected.energy);
    }
    bytes[16] ^= 1u; /* corrupt supplied volume: conservation validation */
    file = tmpfile(); assert(file && fwrite(bytes, 1u, (size_t)length, file) == (size_t)length);
    rewind(file); assert(!LavaRuntimeLoad(file)); fclose(file);
    assert(LavaRuntimeStats().energy == expected.energy);
}
static bool Ready(const VolcanoDefinition *d, void *user) { (void)d; (void)user; return true; }
static unsigned Emit(const VolcanoDefinition *d, unsigned units, void *user)
{
    return LavaRuntimeDeposit(user, d->mapX, d->address.radial, d->mapZ, units, 1450.0);
}
static bool Hot(const VolcanoDefinition *d, void *user)
{
    (void)user; return LavaRuntimeHasHotNear(d->address.bodyId, d->mapX, d->address.radial, d->mapZ, 200.0f);
}
static void TestVolcanoInventory(void)
{
    Reset(); VolcanoRuntimeReset(); closed = true;
    LavaContext lava = Context();
    VolcanoDefinition d = VolcanoDefine(7u, 123u, 0, 1, 0, 1.0f, true);
    assert(VolcanoRuntimeForce(d, 1.0f, 10u));
    VolcanoContext volcano = { .valid = true, .bodyId = 7u, .seed = 123u,
        .observerMap = { 0, 1, 0 }, .ready = Ready, .emitLava = Emit, .hasHotLava = Hot, .userData = &lava };
    for (unsigned i = 0u; i < 400u; i++) {
        VolcanoRuntimeUpdate(0.5, &volcano);
        for (unsigned step = 0u; step < 5u; step++) LavaRuntimeStep(&lava);
        VolcanoRecord r; assert(VolcanoRuntimeAt(0u, &r));
        assert(r.state.reservoir * 1024.0 + (double)LavaRuntimeStats().supplied == 256.0 * 1024.0);
        Balance();
    }
    assert(LavaRuntimeStats().supplied > 0u);
}
int main(void)
{
    TestThermal(); TestFlowAndCooling(); TestUnknownAndMixing(); TestCapacity();
    TestOrderAndPersistence(); TestVolcanoInventory();
    puts("lava conservation, thermal, persistence and coupling tests passed");
}
