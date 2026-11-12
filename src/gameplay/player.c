#include "gameplay/player.h"

#include "core/game_effects.h"
#include "gameplay/interaction.h"
#include "gameplay/player_camera.h"
#include "gameplay/player_collision_internal.h"
#include "gameplay/player_motion.h"
#include "gameplay/player_sensors.h"
#include "gameplay/player_world.h"
#include "gameplay/ship.h"
#include "world/block_edits.h"
#include "world/block_types.h"
#include "world/chunks.h"
#include "world/world_environment.h"

#include "raymath.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define WATER_WALK_SPEED 3.0f
#define WATER_SPRINT_SPEED 4.2f
#define WATER_VERTICAL_SPEED 3.0f
#define WATER_MAX_VERTICAL_SPEED 3.5f
#define WATER_HORIZONTAL_RESPONSE 6.0f
#define WATER_VERTICAL_RESPONSE 5.0f
#define CAMERA_COLLISION_MARGIN 0.025f
#define CAMERA_COLLISION_STEPS 24

void PlayerResetRuntimeState(Player *player)
{
    if (!player) return;
    player->wasInWater = false;
    player->stepTimer = 0.0f;
}

float CameraHeightFactor(float eyeHeight)
{
    float heightFactor = Clamp((eyeHeight - CAMERA_FOV_MIN_HEIGHT) /
                               (CAMERA_FOV_MAX_HEIGHT - CAMERA_FOV_MIN_HEIGHT),
                               0.0f, 1.0f);
    // Smoothstep keeps camera changes stable near the height thresholds.
    return heightFactor * heightFactor * (3.0f - 2.0f * heightFactor);
}

float CameraFovForHeight(float eyeHeight)
{
    return Lerp(CAMERA_MIN_FOV, CAMERA_MAX_FOV, CameraHeightFactor(eyeHeight));
}

int EffectiveRenderDistanceForHeight(float eyeHeight)
{
    int extraDistance = (int)floorf(CameraHeightFactor(eyeHeight) *
                                    (float)CAMERA_MAX_EXTRA_DISTANCE_CHUNKS);
    int effectiveDistance = ChunksRenderDistance() + extraDistance;
    if (effectiveDistance > MAX_RENDER_DISTANCE_CHUNKS) effectiveDistance = MAX_RENDER_DISTANCE_CHUNKS;
    return effectiveDistance;
}

bool PlayerCameraPositionInsideSolid(Vector3 position)
{
    if (!PlayerInternalVectorIsFinite(position) ||
        position.x < (float)INT_MIN || position.x > (float)INT_MAX ||
        position.y < (float)INT_MIN || position.y > (float)INT_MAX ||
        position.z < (float)INT_MIN || position.z > (float)INT_MAX) {
        return true;
    }

    int x = (int)floorf(position.x);
    int y = (int)floorf(position.y);
    int z = (int)floorf(position.z);
    BlockType type = GetBlockAt(x, y, z);
    float top;
    if (PlayerInternalIsStairs(type)) {
        top = PlayerInternalStairsTop(x, y, z, position.z);
    } else {
        float height = BlockCollisionHeightAt(x, y, z);
        if (height <= 0.0f) return false;
        top = (float)y + height;
    }
    return position.y > (float)y + CAMERA_COLLISION_MARGIN &&
           position.y < top - CAMERA_COLLISION_MARGIN;
}

Vector3 PlayerResolveCameraPosition(Vector3 pivot, Vector3 desired)
{
    if (!PlayerInternalVectorIsFinite(desired)) return pivot;
    if (!PlayerCameraPositionInsideSolid(desired)) return desired;

    if (PlayerInternalVectorIsFinite(pivot)) {
        Vector3 towardPivot = Vector3Subtract(pivot, desired);
        for (int step = 1; step <= CAMERA_COLLISION_STEPS; step++) {
            float amount = (float)step / (float)CAMERA_COLLISION_STEPS;
            Vector3 candidate = Vector3Add(
                desired, Vector3Scale(towardPivot, amount));
            if (!PlayerCameraPositionInsideSolid(candidate)) return candidate;
        }
    }

    int x = (int)floorf(desired.x);
    int y = (int)floorf(desired.y);
    int z = (int)floorf(desired.z);
    BlockType type = GetBlockAt(x, y, z);
    float top = PlayerInternalIsStairs(type)
        ? PlayerInternalStairsTop(x, y, z, desired.z)
        : (float)y + BlockCollisionHeightAt(x, y, z);
    Vector3 candidates[6] = {
        { (float)x - CAMERA_COLLISION_MARGIN, desired.y, desired.z },
        { (float)x + 1.0f + CAMERA_COLLISION_MARGIN, desired.y, desired.z },
        { desired.x, (float)y - CAMERA_COLLISION_MARGIN, desired.z },
        { desired.x, top + CAMERA_COLLISION_MARGIN, desired.z },
        { desired.x, desired.y, (float)z - CAMERA_COLLISION_MARGIN },
        { desired.x, desired.y, (float)z + 1.0f + CAMERA_COLLISION_MARGIN }
    };
    float bestDistance = INFINITY;
    Vector3 best = desired;
    for (int index = 0; index < 6; index++) {
        if (PlayerCameraPositionInsideSolid(candidates[index])) continue;
        float distance = Vector3DistanceSqr(desired, candidates[index]);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = candidates[index];
        }
    }
    return best;
}

void UpdatePlayerCamera(Camera3D *camera, const Player *player, float dt, bool thirdPerson)
{
    float eyeHeight = WorldIsSpaceActive() ? 0.0f : EYE_HEIGHT;
    Vector3 eye = Vector3Add(player->position,
                             (Vector3){ 0.0f, eyeHeight, 0.0f });
    Vector3 look = ForwardFromAngles(player->yaw, player->pitch);
    float targetFov = CameraFovForHeight(eye.y);
    if (ShipIsDriving() && ShipIsHighSpeedTransit()) targetFov += 24.0f;
    else if (ShipIsDriving() && ShipIsCruising()) targetFov += 12.0f;
    float smoothing = Clamp(dt * CAMERA_FOV_SMOOTHING, 0.0f, 1.0f);

    Vector3 pivot = eye;
    if (thirdPerson && ShipIsDriving()) {
        pivot.y = player->position.y + 0.62f;
    }
    camera->target = Vector3Add(pivot, look);
    if (thirdPerson) {
        float distance = ShipIsDriving() ? 5.6f : 3.5f;
        float occlusion = RaycastCameraOcclusion(pivot, Vector3Negate(look), distance);
        if (occlusion > 0.3f) distance = occlusion - 0.25f;
        else if (occlusion >= 0.0f) distance = 0.3f;
        Vector3 desired = Vector3Add(
            pivot, Vector3Scale(Vector3Negate(look), distance));
        camera->position = PlayerResolveCameraPosition(pivot, desired);
    } else {
        camera->position = PlayerResolveCameraPosition(eye, eye);
    }
    camera->fovy = Lerp(camera->fovy, targetFov, smoothing);
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

    PlayerBodyState body = {
        .position = player->position,
        .velocity = player->velocity,
        .onGround = player->onGround,
        .floating = player->floating
    };
    PlayerMoveRequest move = {
        .delta = Vector3Scale(player->velocity, dt),
        .canStepUp = body.onGround && !body.floating
    };
    (void)world->move(world->user, &body, &move);
    player->position = body.position;
    player->velocity = body.velocity;
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
