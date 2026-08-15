#include "player.h"

#include "fluid.h"
#include "ship.h"
#include "world.h"
#include "world_environment.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool wallEnabled = false;
static bool ceilingEnabled = false;
static bool stairsEnabled = false;
static bool waterEnabled = false;
static bool groundEnabled = false;
static bool proceduralWaterEnabled = false;
static float proceduralWaterSurface = 0.0f;
static int waterMinY = 1;
static int waterMaxY = 5;

BlockType GetBlockAt(int x, int y, int z)
{
    if (waterEnabled && x >= -16 && x <= 16 && z >= -16 && z <= 16 &&
        y >= waterMinY && y <= waterMaxY) {
        return BLOCK_WATER;
    }
    if (groundEnabled && y == 0) return BLOCK_STONE;
    if (wallEnabled && x == 2 && y >= 0 && y < 4 && z == 0) {
        return BLOCK_STONE;
    }
    if (ceilingEnabled && x == 0 && y == 3 && z == 0) {
        return BLOCK_STONE;
    }
    if (stairsEnabled && x == 2 && y == 1 && z == 0) {
        return BLOCK_STONE_STAIRS;
    }
    return BLOCK_AIR;
}

bool IsLiquidBlock(BlockType type)
{
    return type == BLOCK_WATER || type == BLOCK_LAVA;
}

bool IsWaterBlock(BlockType type)
{
    return type == BLOCK_WATER;
}

bool WorldIsSpaceActive(void) { return false; }
float WorldGravityScale(void) { return 1.0f; }
bool PlanetSurfaceAt(Vector3 position, Vector3 *gravityDirection,
                     float *surfaceDistance, float *gravityScale)
{
    (void)position;
    (void)gravityDirection;
    (void)surfaceDistance;
    (void)gravityScale;
    return false;
}
void AudioPlaySplash(void) { }
void AudioPlayStep(void) { }
void AudioPlayWaterStep(void) { }
void ParticlesEmitOne(Vector3 position, Vector3 velocity, Color color,
                      Vector3 size, float life, float gravity)
{
    (void)position;
    (void)velocity;
    (void)color;
    (void)size;
    (void)life;
    (void)gravity;
}

float BlockCollisionHeightAt(int x, int y, int z)
{
    BlockType block = GetBlockAt(x, y, z);
    return block == BLOCK_AIR || block == BLOCK_WATER ? 0.0f : 1.0f;
}

WorldBlockRegion WorldBlockRegionAt(int y)
{
    (void)y;
    return WORLD_BLOCK_REGION_SURFACE;
}

bool WorldIsSurfaceActive(void)
{
    return true;
}

bool WorldProceduralWaterSurfaceAt(int x, int z, float *outSurfaceY)
{
    (void)x;
    (void)z;
    if (!proceduralWaterEnabled || !outSurfaceY) return false;
    *outSurfaceY = proceduralWaterSurface;
    return true;
}

uint8_t FluidGetVolumeAt(int x, int y, int z)
{
    return GetBlockAt(x, y, z) == BLOCK_WATER ? FLUID_CAPACITY : 0u;
}

FluidSample FluidSampleAt(Vector3 position)
{
    int x = (int)floorf(position.x);
    int y = (int)floorf(position.y);
    int z = (int)floorf(position.z);
    uint8_t volume = FluidGetVolumeAt(x, y, z);
    return (FluidSample){
        .volume = volume,
        .surfaceY = (float)y + (float)volume / (float)FLUID_CAPACITY
    };
}

bool SpaceBlockReadyAt(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
    return true;
}

bool ShipIsDriving(void)
{
    return false;
}

static void ResetCollisionWorld(void)
{
    wallEnabled = false;
    ceilingEnabled = false;
    stairsEnabled = false;
    waterEnabled = false;
    groundEnabled = false;
    proceduralWaterEnabled = false;
    proceduralWaterSurface = 0.0f;
    waterMinY = 1;
    waterMaxY = 5;
}

static void TestHighSpeedHorizontalMovementStopsAtWall(void)
{
    ResetCollisionWorld();
    wallEnabled = true;
    Player player = {
        .position = { 0.5f, 1.0f, 0.5f },
        .velocity = { 8.0f, 0.0f, 0.0f },
        .onGround = false,
        .floating = true
    };

    MovePlayer(&player, (Vector3){ 4.0f, 0.0f, 0.0f });

    assert(isfinite(player.position.x));
    assert(player.position.x + PLAYER_RADIUS <= 2.0001f);
    assert(player.position.x > 1.0f);
    assert(player.velocity.x == 0.0f);
}

static void TestHighSpeedVerticalMovementStopsAtCeiling(void)
{
    ResetCollisionWorld();
    ceilingEnabled = true;
    Player player = {
        .position = { 0.5f, 0.0f, 0.5f },
        .velocity = { 0.0f, 8.0f, 0.0f },
        .onGround = true,
        .floating = true
    };

    MovePlayer(&player, (Vector3){ 0.0f, 5.0f, 0.0f });

    assert(player.position.y + PLAYER_HEIGHT <= 3.0001f);
    assert(player.velocity.y == 0.0f);
}

static void TestHugeMovementIsBounded(void)
{
    ResetCollisionWorld();
    Player player = {
        .position = { 0.5f, 10.0f, 0.5f },
        .velocity = { 0.0f, 0.0f, 0.0f },
        .onGround = false,
        .floating = true
    };

    MovePlayer(&player, (Vector3){ 1000000.0f, 0.0f, 0.0f });

    assert(isfinite(player.position.x));
    assert(player.position.x <= 16.5001f);
}

static void TestSubstepsPreserveStepUp(void)
{
    ResetCollisionWorld();
    stairsEnabled = true;
    Player player = {
        .position = { 0.5f, 1.0f, 0.1f },
        .velocity = { 5.0f, 0.0f, 0.0f },
        .onGround = true,
        .floating = false
    };

    MovePlayer(&player, (Vector3){ 2.5f, 0.0f, 0.0f });

    assert(player.position.x > 2.0f);
    assert(player.position.y > 1.0f);
    assert(player.position.y <= 1.34f);
}

static void TestNonFiniteMovementIsIgnored(void)
{
    ResetCollisionWorld();
    Player player = {
        .position = { 0.5f, 2.0f, 0.5f },
        .velocity = { 1.0f, 2.0f, 3.0f },
        .onGround = true,
        .floating = false
    };
    Player before = player;

    MovePlayer(&player, (Vector3){ NAN, 0.0f, 0.0f });
    assert(memcmp(&player, &before, sizeof(player)) == 0);
    player.position.x = INFINITY;
    before = player;
    MovePlayer(&player, (Vector3){ 1.0f, 0.0f, 0.0f });
    assert(memcmp(&player, &before, sizeof(player)) == 0);
}

static void TestInvalidCollisionPositionsAreBlocked(void)
{
    ResetCollisionWorld();
    assert(PlayerOverlapsWorld((Vector3){ NAN, 2.0f, 0.0f }));
    assert(PlayerOverlapsWorld((Vector3){ 0x1p31f, 2.0f, 0.0f }));
    assert(PlayerOverlapsWorld((Vector3){ 0.0f, 0x1p31f, 0.0f }));
    assert(!PlayerOverlapsWorld(
        (Vector3){ 0.0f, (float)SURFACE_MIN_Y, 0.0f }));
    assert(PlayerOverlapsWorld(
        (Vector3){ 0.0f, (float)SURFACE_MIN_Y - 0.25f, 0.0f }));
    assert(PlayerOverlapsWorld((Vector3){
        0.0f, (float)SURFACE_MAX_Y_EXCLUSIVE - 1.0f, 0.0f
    }));
}

static void TestCameraIsPushedOutOfSolidBlocks(void)
{
    ResetCollisionWorld();
    wallEnabled = true;
    Vector3 inside = { 2.5f, 1.5f, 0.5f };
    Vector3 pivot = { 1.5f, 1.5f, 0.5f };
    assert(PlayerCameraPositionInsideSolid(inside));

    Vector3 resolved = PlayerResolveCameraPosition(pivot, inside);
    assert(!PlayerCameraPositionInsideSolid(resolved));
    assert(resolved.x < 2.0f);

    resolved = PlayerResolveCameraPosition(inside, inside);
    assert(!PlayerCameraPositionInsideSolid(resolved));

    waterEnabled = true;
    assert(!PlayerCameraPositionInsideSolid((Vector3){ 0.5f, 2.5f, 0.5f }));
}

static void TestRuntimeStateIsPerPlayer(void)
{
    Player first = { 0 };
    Player second = { 0 };

    first.wasInWater = true;
    first.stepTimer = 1.25f;
    assert(!second.wasInWater);
    assert(second.stepTimer == 0.0f);

    second.wasInWater = true;
    second.stepTimer = 2.5f;
    assert(first.wasInWater);
    assert(first.stepTimer == 1.25f);
}

static void TestRuntimeStateReset(void)
{
    Player player = {
        .wasInWater = true,
        .stepTimer = NAN
    };

    PlayerResetRuntimeState(&player);
    assert(!player.wasInWater);
    assert(player.stepTimer == 0.0f);
    PlayerResetRuntimeState(NULL);
}

static void TestWaterStateUsesActualColumnSurface(void)
{
    ResetCollisionWorld();
    waterEnabled = true;
    PlayerWaterState water = PlayerWaterStateAt(
        (Vector3){ 0.5f, 1.01f, 0.5f });
    assert(water.feetSubmerged);
    assert(water.bodySubmerged);
    assert(water.eyesSubmerged);
    assert(fabsf(water.surfaceY - 6.0f) < 0.0001f);
    assert(fabsf(water.eyeDepth - (6.0f - 1.01f - EYE_HEIGHT)) < 0.0001f);

    proceduralWaterEnabled = true;
    proceduralWaterSurface = 81.0f;
    water = PlayerWaterStateAt((Vector3){ 0.5f, 1.01f, 0.5f });
    assert(fabsf(water.surfaceY - 6.0f) < 0.0001f);

    waterMinY = -2000;
    waterMaxY = 80;
    water = PlayerWaterStateAt((Vector3){ 0.5f, -1000.0f, 0.5f });
    assert(fabsf(water.surfaceY - 81.0f) < 0.0001f);
    assert(fabsf(water.eyeDepth - (81.0f + 1000.0f - EYE_HEIGHT)) <
           0.0001f);
    proceduralWaterEnabled = false;
    waterMinY = 1;
    waterMaxY = 5;

    water = PlayerWaterStateAt((Vector3){ 0.5f, 5.20f, 0.5f });
    assert(water.feetSubmerged);
    assert(!water.bodySubmerged);
    assert(!water.eyesSubmerged);
    assert(water.eyeDepth == 0.0f);
}

static void TestSwimmingSpeedAndAscent(void)
{
    ResetCollisionWorld();
    waterEnabled = true;
    groundEnabled = true;
    Player player = {
        .position = { 0.5f, 1.01f, 0.5f },
        .yaw = 0.0f,
        .onGround = true
    };
    PlayerInput forward = { .forward = 1.0f };
    for (int frame = 0; frame < 120; frame++) {
        UpdatePlayerWithInput(&player, 1.0f / 60.0f, &forward);
    }
    float distance = player.position.z - 0.5f;
    assert(distance > 5.0f && distance < 6.1f);

    player = (Player){
        .position = { 0.5f, 1.01f, 0.5f },
        .yaw = 0.0f,
        .onGround = true
    };
    PlayerInput ascend = { .vertical = 1.0f, .jumpPressed = true };
    for (int frame = 0; frame < 120; frame++) {
        ascend.jumpPressed = frame == 0;
        UpdatePlayerWithInput(&player, 1.0f / 60.0f, &ascend);
    }
    assert(player.position.y > 3.01f);
}

int main(void)
{
    TestHighSpeedHorizontalMovementStopsAtWall();
    TestHighSpeedVerticalMovementStopsAtCeiling();
    TestHugeMovementIsBounded();
    TestSubstepsPreserveStepUp();
    TestNonFiniteMovementIsIgnored();
    TestInvalidCollisionPositionsAreBlocked();
    TestCameraIsPushedOutOfSolidBlocks();
    TestRuntimeStateIsPerPlayer();
    TestRuntimeStateReset();
    TestWaterStateUsesActualColumnSurface();
    TestSwimmingSpeedAndAscent();
    puts("player collision tests passed");
    return 0;
}
