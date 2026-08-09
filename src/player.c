#include "player.h"

#include "raymath.h"
#include "chunks.h"
#include "world.h"
#include "audio.h"
#include "interaction.h"
#include "particles.h"
#include "ship.h"
#include "space.h"

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

static bool IsStairsBlock(BlockType type)
{
    return type == BLOCK_STONE_STAIRS || type == BLOCK_WOOD_STAIRS;
}

static float StairsCollisionTop(int x, int y, int z, float playerZ)
{
    (void)x;
    float zFrac = playerZ - (float)z;
    int step = (int)floorf(zFrac * 3.0f);
    if (step < 0) step = 0;
    if (step > 2) step = 2;
    return (float)y + (float)(step + 1) / 3.0f;
}

static float PlayerGroundCeiling(Vector3 position)
{
    int minX = (int)floorf(position.x - PLAYER_RADIUS);
    int maxX = (int)floorf(position.x + PLAYER_RADIUS);
    int minY = (int)floorf(position.y);
    int maxY = (int)floorf(position.y + PLAYER_HEIGHT);
    int minZ = (int)floorf(position.z - PLAYER_RADIUS);
    int maxZ = (int)floorf(position.z + PLAYER_RADIUS);

    float minTop = INFINITY;
    for (int x = minX; x <= maxX; x++) {
        for (int y = minY; y <= maxY; y++) {
            for (int z = minZ; z <= maxZ; z++) {
                BlockType type = GetBlockAt(x, y, z);
                float top;
                if (IsStairsBlock(type)) {
                    top = StairsCollisionTop(x, y, z, position.z);
                } else {
                    float height = BlockCollisionHeightAt(x, y, z);
                    if (height <= 0.0f) continue;
                    top = (float)y + height;
                }
                if (top > position.y && top < minTop) minTop = top;
            }
        }
    }
    return minTop;
}

static bool TryStepUp(Player *player, Vector3 next)
{
    float ceiling = PlayerGroundCeiling(next);
    if (ceiling == INFINITY) return false;

    float rise = ceiling - player->position.y;
    if (rise <= 0.0f || rise > 0.5f) return false;

    Vector3 raised = next;
    raised.y += rise;
    if (PlayerOverlapsWorld(raised)) return false;

    player->position.y += rise;
    player->position.x = next.x;
    player->position.z = next.z;
    return true;
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
                if (y >= SPACE_LAYER_Y && y < SPACE_LAYER_TOP &&
                    !SpaceBlockReadyAt(x, y, z)) {
                    // Space is streamed asynchronously. A ship may continue
                    // through an ungenerated chunk; treating it as a wall
                    // makes cruise flight stop at the streaming frontier.
                    if (ShipIsDriving()) continue;
                    return true;
                }
                BlockType type = GetBlockAt(x, y, z);
                float top;
                if (IsStairsBlock(type)) {
                    top = StairsCollisionTop(x, y, z, position.z);
                } else {
                    float height = BlockCollisionHeightAt(x, y, z);
                    if (height <= 0.0f) continue;
                    top = (float)y + height;
                }
                if (position.y < top && position.y + PLAYER_HEIGHT > (float)y) return true;
            }
        }
    }
    return false;
}

void MovePlayer(Player *player, Vector3 delta)
{
    bool canStepUp = player->onGround && !player->floating &&
                     (HomeWorldSurfaceIsActive() || PlanetWorldIsActive());

    Vector3 next = player->position;
    next.x += delta.x;
    if (!PlayerOverlapsWorld(next)) {
        player->position.x = next.x;
    } else {
        player->velocity.x = 0.0f;
        if (canStepUp) TryStepUp(player, next);
    }

    next = player->position;
    next.z += delta.z;
    if (!PlayerOverlapsWorld(next)) {
        player->position.z = next.z;
    } else {
        player->velocity.z = 0.0f;
        if (canStepUp) TryStepUp(player, next);
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
    if (ShipIsDriving() && ShipIsWarping()) targetFov += 24.0f;
    else if (ShipIsDriving() && ShipIsCruising()) targetFov += 12.0f;
    float smoothing = Clamp(dt * CAMERA_FOV_SMOOTHING, 0.0f, 1.0f);

    camera->target = Vector3Add(eye, look);
    if (thirdPerson) {
        float distance = 3.5f;
        float occlusion = RaycastCameraOcclusion(eye, Vector3Negate(look), distance);
        if (occlusion > 0.3f) distance = occlusion - 0.25f;
        else if (occlusion >= 0.0f) distance = 0.3f;
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

    bool inSpace = !HomeWorldSurfaceIsActive() && !PlanetWorldIsActive();
    // Scale the takeoff impulse with gravity so high-g worlds remain
    // traversable instead of trapping the player below a one-block ledge.
    float gravityScale = Clamp(PlanetWorldGravityScale(), 0.45f, 1.75f);
    float jumpSpeed = JUMP_SPEED * sqrtf(gravityScale);
    if (inSpace) {
        Vector3 gravityDir = Vector3Zero();
        float surfaceDist = 0.0f;
        float gravityScale = 1.0f;
        if (PlanetSurfaceAt(player->position, &gravityDir, &surfaceDist, &gravityScale)) {
            float strength = 10.0f * gravityScale *
                             (1.0f - Clamp(surfaceDist / 24.0f, 0.0f, 1.0f));
            if (surfaceDist < 3.0f) strength *= 0.25f;
            player->velocity = Vector3Add(player->velocity, Vector3Scale(gravityDir, strength * dt));
        }

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
            player->velocity.y = jumpSpeed * 0.75f;
            player->onGround = false;
        }
        player->velocity.y -= GRAVITY * gravityScale * 0.3f * dt;
        if (IsKeyDown(KEY_SPACE)) player->velocity.y += 11.0f * dt;
        if (player->velocity.y < -6.0f) player->velocity.y = -6.0f;
    } else {
        if (IsKeyPressed(KEY_SPACE) && player->onGround) {
            player->velocity.y = jumpSpeed;
            player->onGround = false;
        }

        player->velocity.y -= GRAVITY * gravityScale * dt;
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
