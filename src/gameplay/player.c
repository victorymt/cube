#include "gameplay/player.h"

#include "core/game_effects.h"
#include "gameplay/player_actor.h"
#include "gameplay/player_motion.h"

#include <math.h>
#include <stdlib.h>

void PlayerResetRuntimeState(Player *player)
{
    if (!player) return;
    player->wasInWater = false;
    player->stepTimer = 0.0f;
}

void UpdatePlayerWithWorld(Player *player, float dt, double simulationTime,
                           const PlayerInput *input, const PlayerWorldOps *world)
{
    if (!player || !input || !world || !isfinite(dt) || dt <= 0.0f) return;
    const PlayerMotionIntent intent = {
        .forward = input->forward,
        .strafe = input->strafe,
        .vertical = input->vertical,
        .sprint = input->sprint,
        .jump = input->jumpPressed && input->vertical > 0.0f,
        .toggleFloating = input->toggleFloating,
        .lookDelta = input->lookDelta
    };
    ActorBody body = PlayerActorBody(player);
    PlayerMotionResult result;
    if (!PlayerMotionStep(
            &body, &intent, PlayerMotionProfileDefault(), world,
            simulationTime, dt, &result)) {
        return;
    }
    PlayerApplyActorBody(player, &result.body);

    float horizontalSpeed = sqrtf(player->velocity.x * player->velocity.x +
                                  player->velocity.z * player->velocity.z);
    bool feetInWater = result.feetInWater;
    if (feetInWater && !player->wasInWater) {
        GameEffectsPlayAudio(GAME_AUDIO_SPLASH);
    }
    player->wasInWater = feetInWater;

    if (feetInWater && horizontalSpeed > 0.5f) {
        Vector3 bubblePos = {
            player->position.x + ((float)rand() / (float)RAND_MAX - 0.5f) * 0.5f,
            player->position.y + 0.5f,
            player->position.z + ((float)rand() / (float)RAND_MAX - 0.5f) * 0.5f
        };
        GameEffectsEmitParticleOne(
            bubblePos, (Vector3){ 0.0f, 1.1f, 0.0f },
            (Color){ 235, 244, 250, 110 },
            (Vector3){ 0.05f, 0.05f, 0.05f }, 1.2f, 0.0f);
    }

    if (!isfinite(player->stepTimer)) player->stepTimer = 0.0f;
    player->stepTimer -= dt;
    if (!isfinite(player->stepTimer)) player->stepTimer = 0.0f;
    if (player->stepTimer <= 0.0f && horizontalSpeed > 0.5f &&
        result.mode != PLAYER_MOTION_SPACE &&
        result.mode != PLAYER_MOTION_FLOATING) {
        if (feetInWater) {
            GameEffectsPlayAudio(GAME_AUDIO_WATER_STEP);
            player->stepTimer = 0.35f;
        } else if (player->onGround) {
            GameEffectsPlayAudio(GAME_AUDIO_STEP);
            player->stepTimer = 0.38f;
        }
    }
}
