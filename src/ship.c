#include "ship.h"

#include "raymath.h"
#include "chunks.h"
#include "world.h"
#include "player.h"
#include "particles.h"
#include "space.h"

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

static Model shipModel = { 0 };

void ShipLoadModel(void)
{
    if (shipModel.meshCount > 0) return;

    Mesh mesh = { 0 };
    mesh.vertexCount = 8 * 6 * 6;
    mesh.triangleCount = 8 * 6 * 2;
    mesh.vertices = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = malloc((size_t)mesh.vertexCount * 2 * sizeof(float));
    mesh.normals = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.colors = malloc((size_t)mesh.vertexCount * 4 * sizeof(unsigned char));
    if (!mesh.vertices || !mesh.texcoords || !mesh.normals || !mesh.colors) {
        free(mesh.vertices);
        free(mesh.texcoords);
        free(mesh.normals);
        free(mesh.colors);
        return;
    }

    int vertexIndex = 0;
    int parts[8][3] = {
        { 0, 0, -1 }, { 0, 0, 0 }, { 0, 0, 1 },
        { 0, 1, 0 },
        { -1, 0, 0 }, { 1, 0, 0 },
        { 0, 0, -2 },
        { 0, -1, -1 }
    };
    for (int i = 0; i < 8; i++) {
        for (int face = 0; face < 6; face++) {
            AddBlockFace(&mesh, &vertexIndex, parts[i][0], parts[i][1], parts[i][2],
                         face, BLOCK_SPACESHIP, WHITE, 0.0f);
        }
    }

    UploadMesh(&mesh, false);
    shipModel = LoadModelFromMesh(mesh);
    SetMaterialTexture(&shipModel.materials[0], MATERIAL_MAP_DIFFUSE, blockAtlas);
}

void ShipCleanup(void)
{
    if (shipModel.meshCount > 0) UnloadModel(shipModel);
    shipModel = (Model){ 0 };
}

void ShipDraw(const Player *player)
{
    if (shipModel.meshCount == 0) return;
    shipModel.transform = MatrixRotateXYZ((Vector3){ player->pitch, player->yaw, 0.0f });
    Vector3 pos = Vector3Add(player->position, (Vector3){ 0.0f, 0.4f, 0.0f });
    DrawModel(shipModel, pos, 1.0f, WHITE);
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

    Vector3 planetDir = Vector3Zero();
    float surfaceDist = 0.0f;
    bool nearPlanet = PlanetSurfaceAt(player->position, &planetDir, &surfaceDist);
    Vector3 vertical = (Vector3){ 0.0f, 1.0f, 0.0f };
    if (nearPlanet) vertical = planetDir;

    if (IsKeyDown(KEY_SPACE)) accel = Vector3Add(accel, vertical);
    if (IsKeyDown(KEY_LEFT_CONTROL)) accel = Vector3Subtract(accel, vertical);
    if (Vector3LengthSqr(accel) > 0.0f) accel = Vector3Normalize(accel);

    player->velocity = Vector3Add(player->velocity, Vector3Scale(accel, SHIP_THRUST * dt));
    float damping = (cruising ? 0.04f : SHIP_DAMPING) * dt;
    player->velocity = Vector3Scale(player->velocity, 1.0f - damping);

    if (nearPlanet) {
        float gravity = 6.0f * (1.0f - Clamp(surfaceDist / 25.0f, 0.0f, 1.0f));
        player->velocity = Vector3Add(player->velocity, Vector3Scale(planetDir, gravity * dt));
        if (surfaceDist < 8.0f) {
            float toward = Vector3DotProduct(player->velocity, planetDir);
            if (toward > 0.5f) {
                player->velocity = Vector3Subtract(player->velocity, Vector3Scale(planetDir, toward * 5.0f * dt));
            }
        }
    }

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

    if (nearPlanet && IsKeyDown(KEY_LEFT_CONTROL)) {
        Vector3 exhaustPos = Vector3Add(player->position, Vector3Scale(Vector3Negate(vertical), 0.9f));
        ParticlesEmitOne(exhaustPos, Vector3Scale(Vector3Negate(vertical), 2.2f),
                         (Color){ 190, 220, 255, 210 },
                         (Vector3){ 0.14f, 0.14f, 0.14f },
                         0.4f, 0.0f);
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

static bool FindShipLandingSpot(int *outX, int *outY, int *outZ, const Player *player)
{
    int x = (int)floorf(player->position.x);
    int z = (int)floorf(player->position.z);
    int y = (int)floorf(player->position.y);

    for (int sy = y; sy >= y - 24 && sy > 0; sy--) {
        if (sy >= SPACE_LAYER_TOP) continue;
        if (GetBlockAt(x, sy, z) != BLOCK_AIR) continue;
        if (GetBlockAt(x, sy - 1, z) == BLOCK_AIR) continue;
        *outX = x;
        *outY = sy;
        *outZ = z;
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
    if (!FindShipLandingSpot(&sx, &sy, &sz, player)) {
        if (!FindShipPlacement(&sx, &sy, &sz, player)) {
            SetImportMessage("No room to place the ship - it floats away.");
            driving = false;
            cruising = false;
            return;
        }
    }
    SetBlock(sx, sy, sz, BLOCK_SPACESHIP);
    driving = false;
    cruising = false;
}
