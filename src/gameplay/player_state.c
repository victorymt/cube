#include "gameplay/player_state.h"

#include "gameplay/player_actor.h"

#include "raymath.h"

#include <math.h>

static bool PlayerStateVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

void PlayerHaltMotion(Player *player)
{
    if (player) player->velocity = Vector3Zero();
}

void PlayerSetLook(Player *player, float yaw, float pitch)
{
    if (!player || !isfinite(yaw) || !isfinite(pitch)) return;
    player->yaw = yaw;
    player->pitch = Clamp(pitch, -1.45f, 1.45f);
}

void PlayerSetFloating(Player *player, bool floating)
{
    if (!player) return;
    player->floating = floating;
    if (floating) {
        player->velocity.y = 0.0f;
        player->onGround = false;
    }
}

bool PlayerPlaceAt(Player *player, Vector3 position, Vector3 velocity,
                   float yaw, float pitch, bool onGround, bool floating)
{
    if (!player || !PlayerStateVectorIsFinite(position) ||
        !PlayerStateVectorIsFinite(velocity) || !isfinite(yaw) ||
        !isfinite(pitch)) {
        return false;
    }
    player->position = position;
    player->velocity = velocity;
    player->yaw = yaw;
    player->pitch = Clamp(pitch, -1.45f, 1.45f);
    player->onGround = onGround;
    player->floating = floating;
    return true;
}

void PlayerApplyMotionResult(Player *player, const ActorMotionResult *result)
{
    if (player && result) PlayerApplyActorBody(player, &result->body);
}

void PlayerRestoreState(Player *player, const Player *restored)
{
    if (player && restored) *player = *restored;
}
