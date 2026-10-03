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
    assert(fabs(s.suppliedEnergy - s.cooledEnergy - s.exportedEnergy - s.waterEnergy - s.energy) <
           fmax(0.01, s.suppliedEnergy * 1e-9));
    assert(s.waterEnergy == s.evaporatedWater * LAVA_WATER_UNIT_ENERGY);
    assert(s.reservoirWater <= s.evaporatedWater);
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
    return LavaRuntimeHasActiveHotNear(user, d->mapX, d->address.radial, d->mapZ, 200.0f);
}
static void TestCoolingIgnoresParkedLava(void)
{
    Reset(); VolcanoRuntimeReset(); closed = true;
    LavaContext lava = Context();
    assert(LavaRuntimeDeposit(&lava, 0, 1, 0, 128u, 1450.0) == 128u);
    VolcanoDefinition d = VolcanoDefine(7u, 123u, 161, 1, 0, 1.0f, true);
    assert(VolcanoRuntimeForce(d, 1.0f, 10u) && VolcanoRuntimeStop(d.id));
    VolcanoContext volcano = { .valid = true, .bodyId = 7u, .seed = 123u,
        .observerMap = { 161, 1, 0 }, .ready = Ready, .hasHotLava = Hot, .userData = &lava };
    for (unsigned i = 0u; i < 120u; i++) VolcanoRuntimeUpdate(0.5, &volcano);
    VolcanoRecord r;
    assert(VolcanoRuntimeAt(0u, &r) && r.state.phase == VOLCANO_COOLING);
    double parkedEnergy = LavaRuntimeStats().energy;
    unknown = true;
    for (unsigned i = 0u; i < 120u; i++) {
        VolcanoRuntimeUpdate(0.5, &volcano);
        LavaRuntimeStep(&lava);
    }
    assert(VolcanoRuntimeAt(0u, &r) && r.state.phase == VOLCANO_RECOVERY);
    assert(LavaRuntimeStats().energy == parkedEnergy && LavaRuntimeStats().hotCells == 1u);
    unknown = false;
    assert(Hot(&d, &lava));
    lava.bodyId = 8u;
    assert(!Hot(&d, &lava));
    lava.bodyId = 7u;
    for (unsigned i = 0u; i < 600u; i++) LavaRuntimeStep(&lava);
    assert(!Hot(&d, &lava));
    Balance();
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

static unsigned waterVolume;
static int waterX, waterY, waterZ;
static bool waterUnknown, waterFail, waterReservoir;
static bool WaterAt(int x, int y, int z, unsigned *volume, void *user)
{
    (void)user;
    if (waterUnknown) return false;
    *volume = x == waterX && y == waterY && z == waterZ ? waterVolume : 0u;
    return true;
}
static bool Evaporate(int x, int y, int z, unsigned expected, unsigned units,
                      bool *reservoir, void *user)
{
    (void)user;
    assert(x == waterX && y == waterY && z == waterZ);
    if (waterFail || expected != waterVolume || units > waterVolume) return false;
    *reservoir = waterReservoir;
    if (!waterReservoir) waterVolume -= units;
    return true;
}
static LavaContext WaterContext(unsigned lava, unsigned water)
{
    Reset(); closed = true;
    waterX = 1; waterY = 1; waterZ = 0; waterVolume = water;
    waterUnknown = waterFail = waterReservoir = false;
    LavaContext ctx = Context(); ctx.water = WaterAt; ctx.evaporate = Evaporate;
    assert(LavaRuntimeDeposit(&ctx, 0, 1, 0, lava, 1450.0) == lava);
    return ctx;
}
static void TestWaterContact(void)
{
    LavaContext ctx = WaterContext(1024u, 4u);
    for (unsigned i = 0; i < 300u; i++) { LavaRuntimeStep(&ctx); Balance(); }
    assert(waterVolume == 0u && LavaRuntimeStats().evaporatedWater == 4u);

    ctx = WaterContext(128u, 255u);
    for (unsigned i = 0; i < 300u; i++) { LavaRuntimeStep(&ctx); Balance(); }
    assert(waterVolume > 0u && waterVolume < 255u);
    assert(waterVolume + LavaRuntimeStats().evaporatedWater == 255u);
    assert(LavaRuntimeStats().solidVolume == 128u && !solidBlocks);

    ctx = WaterContext(1024u, 255u);
    for (unsigned i = 0; i < 500u; i++) LavaRuntimeStep(&ctx);
    assert(solidBlocks == 1u && LavaRuntimeStats().solidified == 1024u);
    Balance();

    ctx = WaterContext(128u, 255u);
    waterX = 0; waterY = 2;
    for (unsigned i = 0; i < 30u; i++) LavaRuntimeStep(&ctx);
    assert(LavaRuntimeStats().evaporatedWater == 0u); /* upper water floats above partial lava */
    ctx = WaterContext(1024u, 255u); waterX = 0; waterY = 2;
    LavaRuntimeStep(&ctx);
    assert(LavaRuntimeStats().evaporatedWater > 0u);
    ctx = WaterContext(128u, 254u); waterX = 0; waterY = 0;
    LavaRuntimeStep(&ctx);
    assert(LavaRuntimeStats().evaporatedWater == 0u); /* lower water has an air gap */
    waterVolume = 255u;
    LavaRuntimeStep(&ctx);
    assert(LavaRuntimeStats().evaporatedWater > 0u);
    Balance();

    ctx = WaterContext(1024u, 255u); waterFail = true;
    LavaThermalState ambientOnly = { 1024u, 1024u * LavaSpecificEnthalpy(1450.0) };
    LavaCool(&ambientOnly, 293.0, LAVA_STEP_SECONDS);
    LavaRuntimeStep(&ctx);
    assert(LavaRuntimeStats().energy == ambientOnly.energy);
    assert(waterVolume == 255u && LavaRuntimeStats().waterEnergy == 0.0);
    ctx = WaterContext(1024u, 255u); waterReservoir = true;
    LavaRuntimeStep(&ctx);
    assert(waterVolume == 255u && LavaRuntimeStats().evaporatedWater > 0u);
    assert(LavaRuntimeStats().reservoirWater == LavaRuntimeStats().evaporatedWater);
    Balance();
}
static void TestWaterReplay(void)
{
    LavaContext ctx = WaterContext(128u, 255u);
    LavaRuntimeStep(&ctx);
    LavaCell before, after;
    assert(LavaRuntimeAt(0u, &before) && before.contactCredit[1] > 0.0);
    assert(LavaRuntimeStats().evaporatedWater == 0u);
    FILE *file = tmpfile(); assert(file && LavaRuntimeSave(file));
    unsigned savedWater = waterVolume;
    unknown = true;
    for (unsigned i = 0; i < 20u; i++) LavaRuntimeStep(&ctx);
    assert(LavaRuntimeAt(0u, &after));
    assert(after.thermal.energy == before.thermal.energy);
    assert(after.contactCredit[1] == before.contactCredit[1] && waterVolume == savedWater);
    unknown = false;
    LavaRuntimeAdvance(0.5, false, Tick, &ctx);
    assert(LavaRuntimeAt(0u, &after) && after.thermal.energy == before.thermal.energy);
    for (unsigned i = 0; i < 4u; i++) LavaRuntimeAdvance(0.5, true, Tick, &ctx);
    LavaStats expected = LavaRuntimeStats(); unsigned expectedWater = waterVolume;
    rewind(file); assert(LavaRuntimeLoad(file)); waterVolume = savedWater;
    assert(LavaRuntimeAt(0u, &after) && after.steam == 0.0);
    for (unsigned i = 0; i < 120u; i++) LavaRuntimeAdvance(1.0 / 60.0, true, Tick, &ctx);
    assert(LavaRuntimeStats().energy == expected.energy && waterVolume == expectedWater);
    assert(LavaRuntimeStats().evaporatedWater == expected.evaporatedWater);
    Balance();
    /* Reject corrupt face credit without changing live state. */
    assert(fseek(file, 176L, SEEK_SET) == 0);
    unsigned char bad[8] = { 0, 0, 0, 0, 0, 0, 0xf0, 0x7f }; /* +inf */
    assert(fwrite(bad, 1u, 8u, file) == 8u);
    rewind(file); assert(!LavaRuntimeLoad(file));
    assert(LavaRuntimeStats().energy == expected.energy);
    fclose(file);
}
static void TestLegacyLavaPayload(void)
{
    Reset(); closed = true; LavaContext ctx = Context();
    assert(LavaRuntimeDeposit(&ctx, 0, 1, 0, 128u, 1450.0) == 128u);
    FILE *current = tmpfile(), *legacy = tmpfile();
    assert(current && legacy && LavaRuntimeSave(current));
    unsigned char bytes[224]; rewind(current);
    assert(fread(bytes, 1u, sizeof(bytes), current) == sizeof(bytes));
    bytes[7] = '1';
    assert(fwrite(bytes, 1u, 96u, legacy) == 96u);
    assert(fwrite(bytes + 120u, 1u, 56u, legacy) == 56u);
    assert(fputs("TAIL", legacy) >= 0);
    rewind(legacy); LavaRuntimeReset(); assert(LavaRuntimeLoad(legacy));
    assert(LavaRuntimeStats().volume == 128u && LavaRuntimeStats().waterEnergy == 0.0);
    char tail[4]; assert(fread(tail, 1u, 4u, legacy) == 4u && !memcmp(tail, "TAIL", 4u));
    fclose(current); fclose(legacy); Balance();
}
static LavaBoundary TwoBasins(int x, int y, int z, void *user)
{
    (void)user;
    return (x == -1 || x == 1) && y == 1 && z == 0 ? LAVA_OPEN : LAVA_BLOCKED;
}
static void TestSharedWater(void)
{
    double expectedEnergy = 0.0;
    for (unsigned run = 0u; run < 2u; run++) {
        LavaContext ctx = WaterContext(128u, 5u);
        LavaRuntimeReset(); ctx.boundary = TwoBasins;
        waterX = 0;
        int first = run ? -1 : 1;
        assert(LavaRuntimeDeposit(&ctx, first, 1, 0, 1024u, 1450.0) == 1024u);
        assert(LavaRuntimeDeposit(&ctx, -first, 1, 0, 1024u, 1450.0) == 1024u);
        for (unsigned i = 0u; i < 250u; i++) LavaRuntimeStep(&ctx);
        assert(waterVolume == 0u && LavaRuntimeStats().evaporatedWater == 5u);
        if (!run) expectedEnergy = LavaRuntimeStats().energy;
        else assert(LavaRuntimeStats().energy == expectedEnergy);
        Balance();
    }
}
int main(void)
{
    TestThermal(); TestFlowAndCooling(); TestUnknownAndMixing(); TestCapacity();
    TestOrderAndPersistence(); TestVolcanoInventory(); TestCoolingIgnoresParkedLava();
    TestWaterContact(); TestWaterReplay(); TestLegacyLavaPayload();
    TestSharedWater();
    puts("lava conservation, thermal, persistence and coupling tests passed");
}
