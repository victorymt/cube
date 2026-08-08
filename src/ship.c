#include "ship.h"

#include "raymath.h"
#include "world.h"
#include "player.h"
#include "particles.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#define SHIP_THRUST 30.0f
#define SHIP_DAMPING 0.5f
#define SHIP_CRUISE_MULTIPLIER 25.0f

static bool driving = false;
static bool cruising = false;

bool ShipTryEnter(int x, int y, int z, Player *player)
{
    if (driving) return false;
    if (GetBlockAt(x, y, z) != BLOCK_SPACESHIP) return false;

    SetBlock(x, y, z, BLOCK_AIR);
    player->position = (Vector3){ (float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f };
    player->velocity = Vector3Zero();
    player->floating = true;
    driving = true;
    cruising = false;
    SetImportMessage("Ship: W/S thrust, A/D strafe, Space/Ctrl up/down, E exit.");
    return true;
}

bool ShipIsDriving(void)
{
    return driving;
}

bool ShipIsCruising(void)
{
    return cruising;
}

void ShipToggleCruise(void)
{
    cruising = !cruising;
    SetImportMessage(cruising ? "Cruise mode: 25x speed (X to toggle)." : "Cruise mode off.");
}

void ShipUpdate(Player *player, float dt)
{
    if (IsKeyPressed(KEY_X)) ShipToggleCruise();

    Vector2 mouseDelta = GetMouseDelta();
    player->yaw -= mouseDelta.x * MOUSE_SENSITIVITY;
    player->pitch -= mouseDelta.y * MOUSE_SENSITIVITY;
    player->pitch = Clamp(player->pitch, -1.45f, 1.45f);

    Vector3 forward = ForwardFromAngles(player->yaw, player->pitch);
    Vector3 right = RightFromYaw(player->yaw);
    Vector3 accel = Vector3Zero();
    if (IsKeyDown(KEY_W)) accel = Vector3Add(accel, forward);
    if (IsKeyDown(KEY_S)) accel = Vector3Subtract(accel, forward);
    if (IsKeyDown(KEY_D)) accel = Vector3Add(accel, right);
    if (IsKeyDown(KEY_A)) accel = Vector3Subtract(accel, right);
    if (IsKeyDown(KEY_SPACE)) accel.y += 1.0f;
    if (IsKeyDown(KEY_LEFT_CONTROL)) accel.y -= 1.0f;
    if (Vector3LengthSqr(accel) > 0.0f) accel = Vector3Normalize(accel);

    player->velocity = Vector3Add(player->velocity, Vector3Scale(accel, SHIP_THRUST * dt));
    float damping = (cruising ? 0.04f : SHIP_DAMPING) * dt;
    player->velocity = Vector3Scale(player->velocity, 1.0f - damping);

    float maxSpeed = cruising ? SHIP_MAX_SPEED * SHIP_CRUISE_MULTIPLIER : SHIP_MAX_SPEED;
    float speed = Vector3Length(player->velocity);
    if (speed > maxSpeed) {
        player->velocity = Vector3Scale(player->velocity, maxSpeed / speed);
    }

    Vector3 delta = Vector3Scale(player->velocity, dt);
    if (cruising) {
        float total = Vector3Length(delta);
        if (total > 0.001f) {
            Vector3 dir = Vector3Scale(delta, 1.0f / total);
            float remaining = total;
            while (remaining > 0.0f) {
                float stepLen = fminf(remaining, 1.0f);
                Vector3 before = player->position;
                MovePlayer(player, Vector3Scale(dir, stepLen));
                if (Vector3Distance(before, player->position) < stepLen * 0.9f) {
                    player->velocity = Vector3Zero();
                    break;
                }
                remaining -= stepLen;
            }
        }
    } else {
        MovePlayer(player, delta);
    }

    int exhaustCount = cruising ? 3 : 1;
    if (IsKeyDown(KEY_W)) {
        for (int k = 0; k < exhaustCount; k++) {
            Vector3 tail = Vector3Subtract(player->position, Vector3Scale(forward, 0.9f));
            ParticlesEmitOne(tail, Vector3Negate(Vector3Scale(forward, 2.5f)),
                             (Color){ 255, 170, 60, 230 },
                             (Vector3){ 0.16f, 0.16f, 0.16f },
                             0.45f, 0.0f);
        }
        if (cruising) {
            Vector3 tail = Vector3Subtract(player->position, Vector3Scale(forward, 1.4f));
            ParticlesEmitOne(tail, Vector3Negate(Vector3Scale(forward, 8.0f)),
                             (Color){ 255, 220, 130, 200 },
                             (Vector3){ 0.22f, 0.22f, 0.22f },
                             0.6f, 0.0f);
        }
    }
}

static bool FindShipPlacement(int *outX, int *outY, int *outZ, const Player *player)
{
    Vector3 forward = FlatForward(player->yaw);
    int px = (int)floorf(player->position.x + forward.x);
    int py = (int)floorf(player->position.y);
    int pz = (int)floorf(player->position.z + forward.z);
    int pAbove = py + 1;
    int pBelow = py - 1;

    int candidates[4][3] = {
        { px, py, pz },
        { px, pAbove, pz },
        { px, pBelow, pz },
        { (int)floorf(player->position.x), pAbove, (int)floorf(player->position.z) }
    };

    for (int i = 0; i < 4; i++) {
        int y = candidates[i][1];
        if (y < 0 || y >= SPACE_LAYER_TOP) continue;
        if (GetBlockAt(candidates[i][0], y, candidates[i][2]) != BLOCK_AIR) continue;
        *outX = candidates[i][0];
        *outY = y;
        *outZ = candidates[i][2];
        return true;
    }
    return false;
}

void ShipExit(Player *player)
{
    if (!driving) return;

    int sx = 0;
    int sy = 0;
    int sz = 0;
    if (FindShipPlacement(&sx, &sy, &sz, player)) {
        SetBlock(sx, sy, sz, BLOCK_SPACESHIP);
    } else {
        SetImportMessage("No room to place the ship - it floats away.");
        driving = false;
        return;
    }

    driving = false;
    cruising = false;
}
