#include "gameplay/player_state.h"

#include "gameplay/player_actor.h"

#include "raymath.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define PLAYER_HASH_OFFSET UINT64_C(1469598103934665603)
#define PLAYER_HASH_PRIME UINT64_C(1099511628211)

static uint64_t PlayerHashU64(uint64_t hash, uint64_t value)
{
    for (unsigned index = 0; index < 8u; index++) {
        hash = (hash ^ (value & UINT64_C(0xff))) * PLAYER_HASH_PRIME;
        value >>= 8;
    }
    return hash;
}

static uint64_t PlayerHashFloat(uint64_t hash, float value)
{
    uint32_t bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    return PlayerHashU64(hash, bits);
}

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

uint64_t PlayerStateHash(const Player *player)
{
    uint64_t hash = PLAYER_HASH_OFFSET;
    if (!player) return hash;
    hash = PlayerHashFloat(hash, player->position.x);
    hash = PlayerHashFloat(hash, player->position.y);
    hash = PlayerHashFloat(hash, player->position.z);
    hash = PlayerHashFloat(hash, player->velocity.x);
    hash = PlayerHashFloat(hash, player->velocity.y);
    hash = PlayerHashFloat(hash, player->velocity.z);
    hash = PlayerHashFloat(hash, player->yaw);
    hash = PlayerHashFloat(hash, player->pitch);
    hash = PlayerHashU64(hash, player->onGround);
    hash = PlayerHashU64(hash, player->floating);
    hash = PlayerHashU64(hash, player->wasInWater);
    return PlayerHashFloat(hash, player->stepTimer);
}
