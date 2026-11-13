/* Explicit player transition helpers must validate inputs and mutate only the
 * fields owned by each transition. */
#include "gameplay/player_state.h"

#include "raymath.h"

#include <assert.h>
#include <math.h>

static void TestHaltAndLook(void)
{
    Player player = {
        .position = { 1.0f, 2.0f, 3.0f },
        .velocity = { 4.0f, 5.0f, 6.0f },
        .yaw = 0.25f,
        .pitch = 0.1f
    };
    PlayerHaltMotion(&player);
    assert(Vector3LengthSqr(player.velocity) == 0.0f);
    assert(player.position.x == 1.0f && player.yaw == 0.25f);

    PlayerSetLook(&player, 2.0f, 9.0f);
    assert(player.yaw == 2.0f && player.pitch == 1.45f);
    PlayerSetLook(&player, NAN, 0.0f);
    assert(player.yaw == 2.0f && player.pitch == 1.45f);
}

static void TestFloatingAndPlacement(void)
{
    Player player = {
        .velocity = { 2.0f, -3.0f, 4.0f },
        .onGround = true,
        .floating = false
    };
    PlayerSetFloating(&player, true);
    assert(player.floating && !player.onGround && player.velocity.y == 0.0f);

    assert(PlayerPlaceAt(&player, (Vector3){ 8.0f, 9.0f, 10.0f },
                         (Vector3){ 1.0f, 0.0f, -1.0f }, 0.5f, -0.25f,
                         false, false));
    assert(Vector3Distance(player.position, (Vector3){ 8, 9, 10 }) == 0.0f);
    assert(Vector3Distance(player.velocity, (Vector3){ 1, 0, -1 }) == 0.0f);
    assert(player.yaw == 0.5f && player.pitch == -0.25f);
    assert(!player.onGround && !player.floating);

    Player before = player;
    assert(!PlayerPlaceAt(&player, (Vector3){ NAN, 0, 0 }, Vector3Zero(),
                          0.0f, 0.0f, false, false));
    assert(Vector3Distance(player.position, before.position) == 0.0f);
}

static void TestMotionAndRestoreCommit(void)
{
    Player player = { 0 };
    const ActorMotionResult result = {
        .body = {
            .position = { 3.0f, 4.0f, 5.0f },
            .velocity = { 0.5f, 0.0f, 0.0f },
            .yaw = 0.25f,
            .pitch = -0.5f,
            .onGround = true,
            .floating = false
        },
        .moved = true
    };
    PlayerApplyMotionResult(&player, &result);
    assert(Vector3Distance(player.position, result.body.position) == 0.0f);
    assert(player.yaw == result.body.yaw && player.onGround);

    Player restored = { .position = { 7, 8, 9 }, .wasInWater = true };
    PlayerRestoreState(&player, &restored);
    assert(Vector3Distance(player.position, restored.position) == 0.0f);
    assert(player.wasInWater);
}

int main(void)
{
    TestHaltAndLook();
    TestFloatingAndPlacement();
    TestMotionAndRestoreCommit();
    return 0;
}
