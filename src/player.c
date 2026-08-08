#include "player.h"

#include "raymath.h"
#include "chunks.h"
#include "world.h"
#include "audio.h"
#include "interaction.h"
#include "particles.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include "chunks.h"
#include "world.h"
#include "audio.h"
#include "interaction.h"
#include "particles.h"
bool IsSolidBlockAt(int x, int y, int z)
{
    BlockType type = GetBlockAt(x, y, z);
    return type != BLOCK_AIR && type != BLOCK_WATER;
}

bool PlayerOverlapsWorld(Vector3 position)
{
    int minX = (int)floorf(position.x - PLAYER_RADIUS);
    int maxX = (int)floorf(position.x + PLAYER_RADIUS);
    int minY = (int)floorf(position.y);
    int maxY = (int)floorf(position.y + PLAYER_HEIGHT);
    int minZ = (int)floorf(position.z - PLAYER_RADIUS);
    int maxZ = (int)floorf(position.z + PLAYER_RADIUS);

    if (position.y < (float)NETHER_LAYER_Y) return true;

    for (int x = minX; x <= maxX; x++) {
        for (int y = minY; y <= maxY; y++) {
            for (int z = minZ; z <= maxZ; z++) {
                float height = BlockCollisionHeightAt(x, y, z);
                if (height <= 0.0f) continue;
                float cellBottom = (float)y;
                float cellTop = cellBottom + height;
                if (position.y < cellTop && position.y + PLAYER_HEIGHT > cellBottom) return true;
            }
        }
    }
    return false;
}

void MovePlayer(Player *player, Vector3 delta)
{
    Vector3 next = player->position;

    next.x += delta.x;
    if (!PlayerOverlapsWorld(next)) {
        player->position.x = next.x;
    } else {
        player->velocity.x = 0.0f;
    }

    next = player->position;
    next.z += delta.z;
    if (!PlayerOverlapsWorld(next)) {
        player->position.z = next.z;
    } else {
        player->velocity.z = 0.0f;
    }

    next = player->position;
    next.y += delta.y;
    player->onGround = false;
    if (!PlayerOverlapsWorld(next)) {
        player->position.y = next.y;
    } else {
        if (delta.y < 0.0f) player->onGround = true;
        player->velocity.y = 0.0f;
    }
}

Vector3 ForwardFromAngles(float yaw, float pitch)
{
    return Vector3Normalize((Vector3){
        sinf(yaw) * cosf(pitch),
        sinf(pitch),
        cosf(yaw) * cosf(pitch)
    });
}

Vector3 FlatForward(float yaw)
{
    return Vector3Normalize((Vector3){ sinf(yaw), 0.0f, cosf(yaw) });
}

Vector3 RightFromYaw(float yaw)
{
    return Vector3Normalize((Vector3){ cosf(yaw), 0.0f, -sinf(yaw) });
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
    int effectiveDistance = renderDistanceChunks + extraDistance;
    if (effectiveDistance > MAX_RENDER_DISTANCE_CHUNKS) effectiveDistance = MAX_RENDER_DISTANCE_CHUNKS;
    return effectiveDistance;
}

void UpdatePlayerCamera(Camera3D *camera, const Player *player, float dt, bool thirdPerson)
{
    Vector3 eye = Vector3Add(player->position, (Vector3){ 0.0f, EYE_HEIGHT, 0.0f });
    Vector3 look = ForwardFromAngles(player->yaw, player->pitch);
    float targetFov = CameraFovForHeight(eye.y);
    float smoothing = Clamp(dt * CAMERA_FOV_SMOOTHING, 0.0f, 1.0f);

    camera->target = Vector3Add(eye, look);
    if (thirdPerson) {
        float distance = 3.5f;
        HitResult hit = RaycastBlocks(eye, Vector3Negate(look), distance);
        if (hit.hit) {
            Vector3 toHit = {
                (float)hit.x + 0.5f - eye.x,
                (float)hit.y + 0.5f - eye.y,
                (float)hit.z + 0.5f - eye.z
            };
            float hitDistance = sqrtf(toHit.x * toHit.x + toHit.y * toHit.y + toHit.z * toHit.z) - 0.25f;
            if (hitDistance > 0.3f) distance = hitDistance;
        }
        camera->position = Vector3Add(eye, Vector3Scale(Vector3Negate(look), distance));
    } else {
        camera->position = eye;
    }
    camera->fovy = Lerp(camera->fovy, targetFov, smoothing);
}

void UpdatePlayer(Player *player, float dt)
{
    if (IsKeyPressed(KEY_F)) {
        player->floating = !player->floating;
        player->velocity.y = 0.0f;
        player->onGround = false;
    }

    Vector2 mouseDelta = GetMouseDelta();
    player->yaw -= mouseDelta.x * MOUSE_SENSITIVITY;
    player->pitch -= mouseDelta.y * MOUSE_SENSITIVITY;
    player->pitch = Clamp(player->pitch, -1.45f, 1.45f);

    Vector3 wish = Vector3Zero();
    Vector3 forward = FlatForward(player->yaw);
    Vector3 right = RightFromYaw(player->yaw);

    if (IsKeyDown(KEY_W)) wish = Vector3Add(wish, forward);
    if (IsKeyDown(KEY_S)) wish = Vector3Subtract(wish, forward);
    if (IsKeyDown(KEY_D)) wish = Vector3Add(wish, right);
    if (IsKeyDown(KEY_A)) wish = Vector3Subtract(wish, right);

    if (Vector3LengthSqr(wish) > 0.0f) wish = Vector3Normalize(wish);
    float speed = IsKeyDown(KEY_LEFT_SHIFT) ? SPRINT_SPEED : WALK_SPEED;
    player->velocity.x = wish.x * speed;
    player->velocity.z = wish.z * speed;

    int feetX = (int)floorf(player->position.x);
    int feetY = (int)floorf(player->position.y + 0.3f);
    int feetZ = (int)floorf(player->position.z);
    bool inWater = IsLiquidBlock(GetBlockAt(feetX, feetY, feetZ));

    bool inSpace = player->position.y >= (float)SPACE_LAYER_Y;
    if (inSpace) {
        player->velocity.y -= player->velocity.y * 2.0f * dt;
        if (IsKeyDown(KEY_SPACE)) player->velocity.y += 12.0f * dt;
        if (IsKeyDown(KEY_LEFT_CONTROL)) player->velocity.y -= 12.0f * dt;
        if (player->velocity.y > 10.0f) player->velocity.y = 10.0f;
        if (player->velocity.y < -10.0f) player->velocity.y = -10.0f;
    } else if (player->floating) {
        player->velocity.y = 0.0f;
        if (IsKeyDown(KEY_SPACE)) player->velocity.y += FLOAT_VERTICAL_SPEED;
        if (IsKeyDown(KEY_LEFT_CONTROL)) player->velocity.y -= FLOAT_VERTICAL_SPEED;
    } else if (inWater) {
        if (IsKeyPressed(KEY_SPACE) && player->onGround) {
            player->velocity.y = JUMP_SPEED * 0.75f;
            player->onGround = false;
        }
        player->velocity.y -= GRAVITY * 0.3f * dt;
        if (IsKeyDown(KEY_SPACE)) player->velocity.y += 11.0f * dt;
        if (player->velocity.y < -6.0f) player->velocity.y = -6.0f;
    } else {
        if (IsKeyPressed(KEY_SPACE) && player->onGround) {
            player->velocity.y = JUMP_SPEED;
            player->onGround = false;
        }

        player->velocity.y -= GRAVITY * dt;
        if (player->velocity.y < -35.0f) player->velocity.y = -35.0f;
    }

    MovePlayer(player, Vector3Scale(player->velocity, dt));

    float horizontalSpeed = sqrtf(player->velocity.x * player->velocity.x + player->velocity.z * player->velocity.z);

    bool feetInWater = IsWaterBlock(GetBlockAt(feetX, feetY, feetZ));
    static bool wasInWater = false;
    if (feetInWater && !wasInWater) AudioPlaySplash();
    wasInWater = feetInWater;

    if (feetInWater && horizontalSpeed > 0.5f) {
        Vector3 bubblePos = {
            player->position.x + ((float)rand() / (float)RAND_MAX - 0.5f) * 0.5f,
            player->position.y + 0.5f,
            player->position.z + ((float)rand() / (float)RAND_MAX - 0.5f) * 0.5f
        };
        ParticlesEmitOne(bubblePos, (Vector3){ 0.0f, 1.1f, 0.0f },
                         (Color){ 235, 244, 250, 110 },
                         (Vector3){ 0.05f, 0.05f, 0.05f },
                         1.2f, 0.0f);
    }

    static float stepTimer = 0.0f;
    stepTimer -= dt;
    if (stepTimer <= 0.0f && horizontalSpeed > 0.5f) {
        if (!player->floating && !inSpace) {
            if (feetInWater) {
                AudioPlayWaterStep();
                stepTimer = 0.35f;
            } else if (player->onGround) {
                AudioPlayStep();
                stepTimer = 0.38f;
            }
        }
    }
}

