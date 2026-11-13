#include "gameplay/player.h"

#include "core/game_effects.h"
#include "gameplay/player_motion.h"
#include "gameplay/player_world.h"

#include "raymath.h"

#include <math.h>
#include <stdlib.h>

#define WATER_WALK_SPEED 3.0f
#define WATER_SPRINT_SPEED 4.2f
#define WATER_VERTICAL_SPEED 3.0f
#define WATER_MAX_VERTICAL_SPEED 3.5f
#define WATER_HORIZONTAL_RESPONSE 6.0f
#define WATER_VERTICAL_RESPONSE 5.0f

void PlayerResetRuntimeState(Player *player)
{
    if (!player) return;
    player->wasInWater = false;
    player->stepTimer = 0.0f;
}

static float PlayerApproach(float current, float target, float dt,
                            float response)
{
    if (!isfinite(current)) current = 0.0f;
    float amount = 1.0f - expf(-fmaxf(dt, 0.0f) * response);
    return current + (target - current) * amount;
}

void UpdatePlayerWithWorld(Player *player, float dt, const PlayerInput *input,
                           const PlayerWorldOps *world)
{
    if (!player || !input || !world || !world->move ||
        !world->sampleWater || !world->sampleGravity ||
        !isfinite(dt) || dt <= 0.0f) return;
    if (input->toggleFloating) {
        player->floating = !player->floating;
        player->velocity.y = 0.0f;
        player->onGround = false;
    }

    Vector2 mouseDelta = input->lookDelta;
    player->yaw -= mouseDelta.x * MOUSE_SENSITIVITY;
    player->pitch -= mouseDelta.y * MOUSE_SENSITIVITY;
    player->pitch = Clamp(player->pitch, -1.45f, 1.45f);

    Vector3 forward = FlatForward(player->yaw);
    Vector3 right = RightFromYaw(player->yaw);
    float forwardInput = Clamp(input->forward, -1.0f, 1.0f);
    float strafeInput = Clamp(input->strafe, -1.0f, 1.0f);
    Vector3 wish = Vector3Add(Vector3Scale(forward, forwardInput),
                              Vector3Scale(right, strafeInput));
    if (Vector3LengthSqr(wish) > 0.0f) wish = Vector3Normalize(wish);

    PlayerWaterState water = world->sampleWater(world->user, player->position);
    bool swimming = water.bodySubmerged || water.eyesSubmerged;
    float speed = input->sprint ? SPRINT_SPEED : WALK_SPEED;
    if (swimming) speed = input->sprint ? WATER_SPRINT_SPEED : WATER_WALK_SPEED;
    float targetVelocityX = wish.x * speed;
    float targetVelocityZ = wish.z * speed;
    if (swimming) {
        targetVelocityX += water.flowVelocity.x;
        targetVelocityZ += water.flowVelocity.z;
        player->velocity.x = PlayerApproach(player->velocity.x,
                                            targetVelocityX, dt,
                                            WATER_HORIZONTAL_RESPONSE);
        player->velocity.z = PlayerApproach(player->velocity.z,
                                            targetVelocityZ, dt,
                                            WATER_HORIZONTAL_RESPONSE);
    } else {
        player->velocity.x = targetVelocityX;
        player->velocity.z = targetVelocityZ;
    }

    PlayerGravitySample gravity = { .gravityScale = 1.0f };
    (void)world->sampleGravity(world->user, player->position, &gravity);
    bool inSpace = gravity.inSpace;
    float gravityScale = Clamp(gravity.gravityScale, 0.45f, 1.75f);
    float jumpSpeed = JUMP_SPEED * sqrtf(gravityScale);
    if (inSpace) {
        if (gravity.hasPlanetGravity) {
            float strength = 10.0f * gravity.surfaceGravityScale *
                (1.0f - Clamp(gravity.surfaceDistance / 24.0f, 0.0f, 1.0f));
            if (gravity.surfaceDistance < 3.0f) strength *= 0.25f;
            player->velocity = Vector3Add(
                player->velocity, Vector3Scale(gravity.direction, strength * dt));
        }
        player->velocity.y -= player->velocity.y * 2.0f * dt;
        player->velocity.y += Clamp(input->vertical, -1.0f, 1.0f) *
                              12.0f * dt;
        if (player->velocity.y > 10.0f) player->velocity.y = 10.0f;
        if (player->velocity.y < -10.0f) player->velocity.y = -10.0f;
    } else if (player->floating) {
        player->velocity.y = Clamp(input->vertical, -1.0f, 1.0f) *
                             FLOAT_VERTICAL_SPEED;
    } else if (swimming) {
        float verticalInput = Clamp(input->vertical, -1.0f, 1.0f);
        float targetVertical = verticalInput * WATER_VERTICAL_SPEED;
        if (fabsf(verticalInput) < 0.001f) {
            targetVertical = water.eyesSubmerged ? 0.35f : -0.60f;
        }
        targetVertical += water.flowVelocity.y;
        player->velocity.y = PlayerApproach(player->velocity.y,
                                            targetVertical, dt,
                                            WATER_VERTICAL_RESPONSE);
        player->velocity.y = Clamp(player->velocity.y,
                                   -WATER_MAX_VERTICAL_SPEED,
                                   WATER_MAX_VERTICAL_SPEED);
        if (verticalInput > 0.0f) player->onGround = false;
    } else {
        if (input->jumpPressed && input->vertical > 0.0f && player->onGround) {
            player->velocity.y = jumpSpeed;
            player->onGround = false;
        }
        player->velocity.y -= GRAVITY * gravityScale * dt;
        if (player->velocity.y < -35.0f) player->velocity.y = -35.0f;
    }

    ActorBody body = {
        .position = player->position,
        .velocity = player->velocity,
        .yaw = player->yaw,
        .pitch = player->pitch,
        .onGround = player->onGround,
        .floating = player->floating
    };
    ActorMotionRequest move = {
        .delta = Vector3Scale(player->velocity, dt),
        .canStepUp = body.onGround && !body.floating
    };
    (void)world->move(world->user, &body, &move);
    player->position = body.position;
    player->velocity = body.velocity;
    player->yaw = body.yaw;
    player->pitch = body.pitch;
    player->onGround = body.onGround;
    player->floating = body.floating;

    float horizontalSpeed = sqrtf(player->velocity.x * player->velocity.x +
                                  player->velocity.z * player->velocity.z);
    PlayerWaterState movedWater = world->sampleWater(world->user, player->position);
    bool feetInWater = movedWater.feetSubmerged;
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
    if (player->stepTimer <= 0.0f && horizontalSpeed > 0.5f) {
        if (!player->floating && !inSpace) {
            if (feetInWater) {
                GameEffectsPlayAudio(GAME_AUDIO_WATER_STEP);
                player->stepTimer = 0.35f;
            } else if (player->onGround) {
                GameEffectsPlayAudio(GAME_AUDIO_STEP);
                player->stepTimer = 0.38f;
            }
        }
    }
}
