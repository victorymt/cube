#include "world/ash_runtime.h"
#include "world/volcano_runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool parked;
static double rain;
static AshBoundary Boundary(int x, int y, int z, void *user)
{
    (void)x; (void)z; (void)user;
    if (parked) return ASH_UNKNOWN;
    return y <= 0 ? ASH_BLOCKED : ASH_OPEN;
}
static double Wet(int x, int y, int z, void *user)
{
    (void)x; (void)y; (void)z; (void)user; return rain;
}
static AshContext Context(void)
{
    return (AshContext){ .valid = true, .bodyId = 7u, .boundary = Boundary, .wetness = Wet };
}
static void Balance(void)
{
    AshStats s = AshRuntimeStats(); assert(s.supplied == s.volume + s.washed + s.collected);
    assert(s.cells <= ASH_MAX_CELLS);
}
static void TestLayersAndParking(void)
{
    AshRuntimeReset(); rain = 0; parked = false;
    AshContext ctx = Context(); AshCell cell;
    assert(AshRuntimeDeposit(&ctx, 0, 3, 0, 128) == 128);
    AshRuntimeStep(&ctx);
    assert(AshRuntimeCellAt(7, 0, 2, 0, &cell) && cell.volume == 128);
    assert(!AshRuntimeCellAt(7, 0, 1, 0, &cell));
    AshRuntimeStep(&ctx);
    assert(AshRuntimeCellAt(7, 0, 1, 0, &cell) && cell.volume == 128);
    assert(AshRuntimeDeposit(&ctx, SURFACE_EQUATOR_BLOCKS, 1, 0, 1024) == 896);
    assert(AshRuntimeStats().cells == 1);
    assert(AshRuntimeDeposit(&ctx, 0, 0, 0, 100) == 0);
    assert(AshRuntimeDeposit(&ctx, 0, 2, 0, 100) == 100);
    AshRuntimeStep(&ctx);
    assert(AshRuntimeCellAt(7, 0, 2, 0, &cell) && cell.volume == 100);
    parked = true; rain = 1;
    for (unsigned i = 0; i < 100; i++) AshRuntimeStep(&ctx);
    assert(AshRuntimeStats().volume == 1124 && !AshRuntimeStats().washed);
    assert(AshRuntimeDeposit(&ctx, 1, 1, 0, 1) == 0);
    parked = false; ctx.bodyId = 8;
    for (unsigned i = 0; i < 100; i++) AshRuntimeStep(&ctx);
    assert(!AshRuntimeStats().washed);
    ctx.bodyId = 7; AshRuntimeStep(&ctx);
    assert(AshRuntimeAt(0, &cell) && cell.washCredit > 0);
    Balance();
}
static void TestPersistence(void)
{
    AshContext ctx = Context();
    FILE *f = tmpfile(); assert(f && AshRuntimeSave(f));
    long size = ftell(f); unsigned char bytes[1024]; assert(size < 1024);
    rewind(f); assert(fread(bytes, 1, (size_t)size, f) == (size_t)size);
    for (unsigned i = 0; i < 113; i++) AshRuntimeStep(&ctx);
    AshStats expected = AshRuntimeStats(); AshCell expectedCell;
    assert(AshRuntimeAt(0, &expectedCell));
    rewind(f); assert(AshRuntimeLoad(f)); fclose(f);
    for (unsigned i = 0; i < 113; i++) AshRuntimeStep(&ctx);
    AshCell actual; assert(AshRuntimeAt(0, &actual));
    assert(actual.volume == expectedCell.volume && actual.washCredit == expectedCell.washCredit);
    assert(AshRuntimeStats().washed == expected.washed);
    for (long cut = 0; cut < size; cut++) {
        f = tmpfile(); assert(f && fwrite(bytes, 1, (size_t)cut, f) == (size_t)cut);
        rewind(f); assert(!AshRuntimeLoad(f)); fclose(f);
        assert(AshRuntimeStats().volume == expected.volume);
    }
    bytes[16] ^= 1u;
    f = tmpfile(); assert(f && fwrite(bytes, 1, (size_t)size, f) == (size_t)size);
    rewind(f); assert(!AshRuntimeLoad(f)); fclose(f);
    for (unsigned i = 0; i < 6000; i++) AshRuntimeStep(&ctx);
    assert(!AshRuntimeStats().volume && !AshRuntimeStats().cells);
    Balance();
}
static void TestCapacity(void)
{
    AshRuntimeReset(); rain = 0;
    AshContext ctx = Context();
    for (unsigned i = 0; i < ASH_MAX_CELLS; i++) assert(AshRuntimeDeposit(&ctx, (int)i, 1, 0, 1) == 1);
    assert(!AshRuntimeDeposit(&ctx, -1, 1, 0, 1));
    assert(AshRuntimeStats().rejected == 1);
    Balance();
}
static bool Ready(const VolcanoDefinition *d, void *user) { (void)d; (void)user; return true; }
static unsigned emitted;
static unsigned Emit(const VolcanoDefinition *d, unsigned units, uint64_t tick, void *user)
{
    (void)tick;
    unsigned accepted = AshRuntimeDeposit(user, d->mapX, 1, d->mapZ, units);
    emitted += accepted; return accepted;
}
static void TestInventory(void)
{
    AshRuntimeReset(); VolcanoRuntimeReset(); emitted = 0;
    AshContext ash = Context();
    VolcanoDefinition d = VolcanoDefine(7, 123, 0, 1, 0, 1, true);
    assert(VolcanoRuntimeForce(d, 1, 100));
    VolcanoContext ctx = { .valid = true, .bodyId = 7, .seed = 123, .observerMap = { 0, 1, 0 },
        .ready = Ready, .emitAsh = Emit, .userData = &ash };
    for (unsigned i = 0; i < 200; i++) {
        VolcanoRuntimeUpdate(0.5, &ctx);
        VolcanoRecord r; assert(VolcanoRuntimeAt(0, &r));
        assert(r.state.reservoir * 1024 + emitted == 256 * 1024);
    }
    assert(emitted == 1024); Balance();
}
static void TestCollection(void)
{
    AshRuntimeReset(); rain = 1; parked = false;
    AshContext ctx = Context(); AshCell cell;
    assert(AshRuntimeDeposit(&ctx, 0, 1, 0, 512) == 512);
    for (unsigned i = 0; i < 10; i++) AshRuntimeStep(&ctx);
    assert(!AshRuntimeCollect(7, 0, 1, 0, 512)); /* stale volume after rain */
    assert(!AshRuntimeCollect(8, 0, 1, 0, 510));
    assert(AshRuntimeCollect(7, SURFACE_EQUATOR_BLOCKS, 1, 0, 510));
    assert(!AshRuntimeCellAt(7, 0, 1, 0, &cell) && !AshRuntimeCollect(7, 0, 1, 0, 510));
    assert(AshRuntimeStats().collected == 510 && AshRuntimeStats().washed == 2); Balance();
    FILE *file = tmpfile(); assert(file && AshRuntimeSave(file));
    rewind(file); AshRuntimeReset(); assert(AshRuntimeLoad(file));
    assert(AshRuntimeStats().collected == 510); Balance();
    assert(fseek(file, 48, SEEK_SET) == 0); /* bad collection ledger */
    const unsigned char bad[8] = { 255, 255, 255, 255, 255, 255, 255, 255 };
    assert(fwrite(bad, 1, 8, file) == 8); rewind(file); assert(!AshRuntimeLoad(file));
    assert(AshRuntimeStats().collected == 510); fclose(file);
}

static AshBoundary OpenAddressBoundary(SurfaceAddress address, void *user)
{
    (void)user;
    return address.radial >= 1 ? ASH_OPEN : ASH_BLOCKED;
}

static void TestCrossFaceAddressIdentity(void)
{
    AshRuntimeReset();
    AshContext ctx = { .valid = true, .bodyId = 7u,
        .addressBoundary = OpenAddressBoundary };
    SurfaceAddress first = { 7u, SURFACE_FACE_POS_X, 100, 100, 1 };
    SurfaceAddress second = { 7u, SURFACE_FACE_POS_Y, 100, 100, 1 };
    assert(AshRuntimeDepositAtAddress(&ctx, first, 100u) == 100u);
    assert(AshRuntimeDepositAtAddress(&ctx, second, 200u) == 200u);
    AshCell cell;
    assert(AshRuntimeCellAtAddress(first, &cell) && cell.volume == 100u);
    assert(AshRuntimeCellAtAddress(second, &cell) && cell.volume == 200u);
    assert(AshRuntimeCollectAtAddress(first, 100u));
    assert(!AshRuntimeCellAtAddress(first, &cell));
    assert(AshRuntimeCellAtAddress(second, &cell) && cell.volume == 200u);
    FILE *file = tmpfile();
    assert(file && AshRuntimeSave(file));
    rewind(file); AshRuntimeReset(); assert(AshRuntimeLoad(file));
    assert(AshRuntimeCellAtAddress(second, &cell));
    assert(cell.address.face == second.face && cell.volume == 200u);
    fclose(file); Balance();
}
int main(void)
{
    TestLayersAndParking(); TestPersistence(); TestCapacity(); TestInventory();
    TestCollection(); TestCrossFaceAddressIdentity();
    puts("ash layer, erosion, inventory, parking and persistence tests passed");
}
