#include "player.h"

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

BlockType GetBlockAt(int x, int y, int z)
{
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

float BlockCollisionHeightAt(int x, int y, int z)
{
    return GetBlockAt(x, y, z) == BLOCK_AIR ? 0.0f : 1.0f;
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
}

int main(void)
{
    TestHighSpeedHorizontalMovementStopsAtWall();
    TestHighSpeedVerticalMovementStopsAtCeiling();
    TestHugeMovementIsBounded();
    TestSubstepsPreserveStepUp();
    TestNonFiniteMovementIsIgnored();
    TestInvalidCollisionPositionsAreBlocked();
    puts("player collision tests passed");
    return 0;
}
