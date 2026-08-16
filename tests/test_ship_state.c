#include "ship.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEST_WORLD_SIZE 16
#define TEST_WORLD_OFFSET 8

static BlockType testWorld[TEST_WORLD_SIZE][WORLD_HEIGHT][TEST_WORLD_SIZE];
static int undoBeginCalls;
static int undoEndCalls;

static void ResetTestWorld(void)
{
    memset(testWorld, 0, sizeof(testWorld));
    undoBeginCalls = 0;
    undoEndCalls = 0;
}

bool PlanetWorldIsActive(void) { return false; }
bool HomeWorldSurfaceIsActive(void) { return true; }
WorldBlockRegion WorldBlockRegionAt(int y)
{
    if (y >= SPACE_LAYER_Y && y < SPACE_LAYER_TOP) return WORLD_BLOCK_REGION_SPACE;
    if (y >= NETHER_LAYER_Y && y < NETHER_LAYER_TOP) return WORLD_BLOCK_REGION_NETHER;
    if (y >= 0 && y < WORLD_HEIGHT) return WORLD_BLOCK_REGION_SURFACE;
    return WORLD_BLOCK_REGION_NONE;
}
uint32_t WorldCurrentSurfaceId(void) { return 0u; }
int SpaceOriginX(void) { return 0; }
int SpaceOriginZ(void) { return 0; }
const char *PlanetWorldName(void) { return "Test Planet"; }

BlockType GetBlockAt(int x, int y, int z)
{
    x += TEST_WORLD_OFFSET;
    z += TEST_WORLD_OFFSET;
    if (x < 0 || x >= TEST_WORLD_SIZE || y < 0 || y >= WORLD_HEIGHT ||
        z < 0 || z >= TEST_WORLD_SIZE) return BLOCK_AIR;
    return testWorld[x][y][z];
}

bool SetBlock(int x, int y, int z, BlockType type)
{
    x += TEST_WORLD_OFFSET;
    z += TEST_WORLD_OFFSET;
    if (x < 0 || x >= TEST_WORLD_SIZE || y < 0 || y >= WORLD_HEIGHT ||
        z < 0 || z >= TEST_WORLD_SIZE) return false;
    testWorld[x][y][z] = type;
    return true;
}

bool SetBlockNoUndo(int x, int y, int z, BlockType type)
{
    return SetBlock(x, y, z, type);
}

void WorldBeginUndoGroup(void) { undoBeginCalls++; }
void WorldEndUndoGroup(void) { undoEndCalls++; }
bool SpaceBlockReadyAt(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
    return true;
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
    ResetTestWorld();
    SetBlock(1, 2, 3, BLOCK_SPACESHIP);
    ShipReset();
    assert(ShipTryEnter(1, 2, 3, &player));
    ShipToggleCruise();
    assert(ShipIsDriving());
    assert(ShipIsCruising());
    assert(ShipGetDriveMode() == SHIP_DRIVE_MANUAL_CRUISE);
    assert(strcmp(ShipDriveModeName(), "MANUAL CRUISE") == 0);
    assert(ShipWarpVisualIntensity() == 0.0f);
    assert(!ShipLocatorHasTarget());
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
    assert(ShipGetDriveMode() == SHIP_DRIVE_MANEUVER);
    assert(ShipRelativeSpeed() == 0.0f);
    assert(ShipTargetSpeed() == 0.0f);
    assert(ShipWarpVisualIntensity() == 0.0f);
    assert(!ShipFlightAssistEnabled());
    assert(!ShipHasGravityPrimary());
    assert(!ShipHasWarpTarget());
}

static void TestParkedShipLayoutAndInteraction(void)
{
    ResetTestWorld();
    Player distantPlayer = { .position = { 7.0f, 20.0f, 7.0f } };
    for (int direction = SHIP_DIRECTION_NORTH;
         direction <= SHIP_DIRECTION_WEST; direction++) {
        assert(ShipCanPlaceParked(0, 4, 0, (ShipDirection)direction,
                                  &distantPlayer));
        assert(ShipPlaceParked(0, 4, 0, (ShipDirection)direction, true));
        assert(GetBlockAt(0, 4, 0) ==
               ShipCoreBlockForDirection((ShipDirection)direction));
        assert(undoBeginCalls == direction * 2 + 1);
        assert(undoEndCalls == direction * 2 + 1);

        int occupied = 0;
        for (int y = 4; y < 4 + SHIP_FOOTPRINT_HEIGHT; y++) {
            for (int x = -1; x <= 2; x++) {
                for (int z = -1; z <= 2; z++) {
                    ParkedShip resolved = { 0 };
                    assert(ShipResolveParkedAt(x, y, z, &resolved));
                    assert(resolved.coreX == 0 && resolved.coreY == 4 &&
                           resolved.coreZ == 0 && !resolved.legacy);
                    assert(resolved.direction == (ShipDirection)direction);
                    if (GetBlockAt(x, y, z) == BLOCK_SPACESHIP_OCCUPIED) {
                        occupied++;
                    }
                }
            }
        }
        assert(occupied == SHIP_FOOTPRINT_SIZE * SHIP_FOOTPRINT_SIZE *
                           SHIP_FOOTPRINT_HEIGHT - 1);
        assert(ShipRemoveParkedAt(2, 5, 2, true));
        for (int y = 4; y < 4 + SHIP_FOOTPRINT_HEIGHT; y++) {
            for (int x = -1; x <= 2; x++) {
                for (int z = -1; z <= 2; z++) {
                    assert(GetBlockAt(x, y, z) == BLOCK_AIR);
                }
            }
        }
    }

    SetBlock(2, 5, 2, BLOCK_STONE);
    assert(!ShipCanPlaceParked(0, 4, 0, SHIP_DIRECTION_NORTH, NULL));
    assert(!ShipPlaceParked(0, 4, 0, SHIP_DIRECTION_NORTH, false));
    assert(GetBlockAt(2, 5, 2) == BLOCK_STONE);
    SetBlock(2, 5, 2, BLOCK_AIR);

    Player overlappingPlayer = { .position = { 0.5f, 4.0f, 0.5f } };
    assert(!ShipCanPlaceParked(0, 4, 0, SHIP_DIRECTION_EAST,
                               &overlappingPlayer));
    assert(ShipPlaceParked(0, 4, 0, SHIP_DIRECTION_EAST, false));
    Player pilot = { 0 };
    assert(ShipTryEnter(2, 5, 2, &pilot));
    assert(ShipIsDriving());
    assert(fabsf(pilot.position.x - 1.0f) < 0.001f);
    assert(fabsf(pilot.position.z - 1.0f) < 0.001f);
    assert(fabsf(pilot.yaw - PI * 0.5f) < 0.001f);
    for (int y = 4; y < 4 + SHIP_FOOTPRINT_HEIGHT; y++) {
        for (int x = -1; x <= 2; x++) {
            for (int z = -1; z <= 2; z++) {
                assert(GetBlockAt(x, y, z) == BLOCK_AIR);
            }
        }
    }
    ShipReset();

    SetBlock(4, 4, 4, BLOCK_SPACESHIP);
    ParkedShip legacy = { 0 };
    assert(ShipResolveParkedAt(4, 4, 4, &legacy));
    assert(legacy.legacy && legacy.coreX == 4 && legacy.coreZ == 4);
    assert(ShipRemoveParkedAt(4, 4, 4, false));
    assert(GetBlockAt(4, 4, 4) == BLOCK_AIR);
}

static void TestShipDirectionQuantization(void)
{
    assert(ShipDirectionFromYaw(0.0f) == SHIP_DIRECTION_NORTH);
    assert(ShipDirectionFromYaw(PI * 0.5f) == SHIP_DIRECTION_EAST);
    assert(ShipDirectionFromYaw(PI) == SHIP_DIRECTION_SOUTH);
    assert(ShipDirectionFromYaw(-PI * 0.5f) == SHIP_DIRECTION_WEST);
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
    TestParkedShipLayoutAndInteraction();
    TestShipDirectionQuantization();
    TestStateFileValidation();
    puts("ship state tests passed");
    return 0;
}
