#include "ship.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static BlockType testShipBlock = BLOCK_SPACESHIP;

BlockType GetBlockAt(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
    return testShipBlock;
}

void SetBlock(int x, int y, int z, BlockType type)
{
    (void)x;
    (void)y;
    (void)z;
    testShipBlock = type;
}

void SetImportMessage(const char *message)
{
    (void)message;
}

static FILE *FuelStateFile(float value)
{
    FILE *file = tmpfile();
    assert(file);
    assert(fwrite(&value, sizeof(value), 1, file) == 1);
    rewind(file);
    return file;
}

static void TestFuelConsumptionContract(void)
{
    ShipReset();
    assert(ShipGetFuel() == SHIP_MAX_FUEL);
    assert(ShipConsumeFuel(25.0f));
    assert(ShipGetFuel() == 75.0f);

    assert(!ShipConsumeFuel(NAN));
    assert(!ShipConsumeFuel(INFINITY));
    assert(!ShipConsumeFuel(-1.0f));
    assert(ShipGetFuel() == 75.0f);
    assert(ShipConsumeFuel(0.0f));
    assert(ShipGetFuel() == 75.0f);

    assert(!ShipConsumeFuel(80.0f));
    assert(ShipGetFuel() == 0.0f);
    assert(!ShipConsumeFuel(0.01f));
    assert(ShipRefuel());
    assert(ShipGetFuel() == SHIP_MAX_FUEL);
}

static void TestLoadIsAtomicAndResetsRuntimeState(void)
{
    Player player = { 0 };
    ShipReset();
    assert(ShipTryEnter(1, 2, 3, &player));
    ShipToggleCruise();
    assert(ShipIsDriving());
    assert(ShipIsCruising());
    assert(ShipConsumeFuel(12.0f));
    float beforeInvalidLoad = ShipGetFuel();

    FILE *invalid = FuelStateFile(NAN);
    assert(!ShipLoadState(invalid));
    fclose(invalid);
    assert(ShipGetFuel() == beforeInvalidLoad);
    assert(ShipIsDriving());
    assert(ShipIsCruising());

    FILE *valid = FuelStateFile(31.5f);
    assert(ShipLoadState(valid));
    fclose(valid);
    assert(ShipGetFuel() == 31.5f);
    assert(!ShipIsDriving());
    assert(!ShipIsCruising());
    assert(!ShipIsWarping());
    assert(!ShipFlightAssistEnabled());
    assert(!ShipHasGravityPrimary());
    assert(!ShipHasWarpTarget());
}

static void TestStateFileValidation(void)
{
    ShipReset();
    assert(!ShipSaveState(NULL));

    FILE *out = tmpfile();
    assert(out);
    assert(ShipSaveState(out));
    assert(ftell(out) == (long)sizeof(float));
    fclose(out);

    const float invalidValues[] = { -1.0f, SHIP_MAX_FUEL + 1.0f, INFINITY };
    for (unsigned index = 0; index < sizeof(invalidValues) / sizeof(invalidValues[0]);
         index++) {
        float before = ShipGetFuel();
        FILE *file = FuelStateFile(invalidValues[index]);
        assert(!ShipLoadState(file));
        fclose(file);
        assert(ShipGetFuel() == before);
    }
    assert(!ShipLoadState(NULL));
}

int main(void)
{
    TestFuelConsumptionContract();
    TestLoadIsAtomicAndResetsRuntimeState();
    TestStateFileValidation();
    puts("ship state tests passed");
    return 0;
}
