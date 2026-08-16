#include "gameplay/ship_visual_internal.h"

#include "gameplay/ship_exhaust.h"
#include "gameplay/ship_ground_effects.h"
#include "raymath.h"
#include "world/block_atlas.h"
#include "world/chunks.h"
#include "presentation/particles.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define SHIP_VISUAL_MAX_FACES 128

typedef struct ShipVisualEffectsState {
    ShipExhaustProfile profile;
    float intensity;
    float phase;
    float exhaustCarry;
    unsigned nozzleCursor;
    uint32_t randomState;
    bool debugExhaustOverride;
    float debugExhaustDemand;
} ShipVisualEffectsState;

static ShipVisualEffectsState shipVisualEffects = { 0 };
static Model shipModel = { 0 };

static float ShipVisualRandom(void)
{
    uint32_t value = shipVisualEffects.randomState;
    if (value == 0u) value = 0x9e3779b9u;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    shipVisualEffects.randomState = value;
    return (float)(value >> 8) * (1.0f / 16777216.0f);
}

static Matrix ShipVisualRotation(const Player *player)
{
    return MatrixRotateXYZ((Vector3){ player->pitch, player->yaw, 0.0f });
}

static Vector3 ShipVisualOrigin(const Player *player)
{
    return Vector3Add(player->position, (Vector3){ 0.0f, 0.30f, 0.0f });
}

static Vector3 ShipVisualWorldPoint(const Player *player, Vector3 local)
{
    return Vector3Add(ShipVisualOrigin(player),
                      Vector3Transform(local, ShipVisualRotation(player)));
}

static Vector3 ShipVisualWorldDirection(const Player *player, Vector3 local)
{
    return Vector3Normalize(Vector3Transform(
        local, ShipVisualRotation(player)));
}

static Vector3 ShipVisualNozzle(const Player *player, int index)
{
    float x = index == 0 ? -0.65f : 0.65f;
    return ShipVisualWorldPoint(player, (Vector3){ x, -0.03f, -1.73f });
}

static Vector3 ShipVisualPlumeDirection(const Player *player, int index)
{
    Vector3 backward = ShipVisualWorldDirection(
        player, (Vector3){ 0.0f, 0.0f, -1.0f });
    Vector3 right = ShipVisualWorldDirection(
        player, (Vector3){ 1.0f, 0.0f, 0.0f });
    float outward = index == 0 ? -0.30f : 0.30f;
    return Vector3Normalize(Vector3Add(
        backward, Vector3Scale(right, outward)));
}

void ShipResetVisualEffects(void)
{
    shipVisualEffects = (ShipVisualEffectsState){ 0 };
    shipVisualEffects.randomState = 0x6d2b79f5u;
    shipVisualEffects.profile = ShipExhaustProfileFor(
        SHIP_DRIVE_MANEUVER, 1.0f, 0.0f);
    ShipGroundEffectsReset();
}

float ShipVisualExhaustIntensity(void)
{
    return shipVisualEffects.intensity;
}

void ShipVisualSetDebugExhaust(const Player *player, float demand,
                               float atmosphereDensity)
{
    if (!player) return;
    shipVisualEffects.debugExhaustOverride = demand > 0.0f;
    shipVisualEffects.debugExhaustDemand = demand;
    shipVisualEffects.profile = ShipExhaustProfileFor(
        SHIP_DRIVE_MANEUVER, demand, atmosphereDensity);
    shipVisualEffects.intensity = demand;
    shipVisualEffects.exhaustCarry = 0.0f;
}

void ShipVisualUpdateMainExhaust(const Player *player, float dt,
                                  ShipDriveMode mode, float demand,
                                  float atmosphereDensity)
{
    if (!player || !isfinite(dt) || dt <= 0.0f) return;
    if (shipVisualEffects.debugExhaustOverride) {
        mode = SHIP_DRIVE_MANEUVER;
        demand = shipVisualEffects.debugExhaustDemand;
    }
    ShipExhaustProfile next = ShipExhaustProfileFor(
        mode, demand, atmosphereDensity);
    if (next.intensity > 0.0f) shipVisualEffects.profile = next;

    float target = next.intensity;
    float response = target > shipVisualEffects.intensity ? 10.0f : 14.0f;
    float change = Clamp(target - shipVisualEffects.intensity,
                         -response * dt, response * dt);
    shipVisualEffects.intensity = Clamp(
        shipVisualEffects.intensity + change, 0.0f, 1.0f);
    shipVisualEffects.phase += dt;
    if (shipVisualEffects.phase > 1000.0f) {
        shipVisualEffects.phase = fmodf(shipVisualEffects.phase, 1000.0f);
    }
    if (shipVisualEffects.intensity <= 0.01f) return;

    int count = ShipExhaustEmissionCount(
        shipVisualEffects.profile.particleRate * shipVisualEffects.intensity,
        dt, &shipVisualEffects.exhaustCarry, 4);
    Vector3 localRight = ShipVisualWorldDirection(
        player, (Vector3){ 1.0f, 0.0f, 0.0f });
    Vector3 localUp = ShipVisualWorldDirection(
        player, (Vector3){ 0.0f, 1.0f, 0.0f });
    for (int i = 0; i < count; i++) {
        int nozzleIndex = (int)(shipVisualEffects.nozzleCursor++ & 1u);
        float lateral = (ShipVisualRandom() - 0.5f) * 0.12f;
        float vertical = (ShipVisualRandom() - 0.5f) * 0.12f;
        Vector3 plumeDirection = ShipVisualPlumeDirection(
            player, nozzleIndex);
        Vector3 jitter = Vector3Add(
            Vector3Scale(localRight, lateral),
            Vector3Scale(localUp, vertical));
        Vector3 position = Vector3Add(
            ShipVisualNozzle(player, nozzleIndex),
            Vector3Add(Vector3Scale(plumeDirection, 0.08f), jitter));
        Vector3 velocity = Vector3Add(
            player->velocity,
            Vector3Add(Vector3Scale(
                           plumeDirection,
                           2.4f + shipVisualEffects.intensity * 3.2f),
                       Vector3Scale(jitter, 1.8f)));
        float size = 0.035f + ShipVisualRandom() * 0.025f;
        Color start = shipVisualEffects.profile.outerColor;
        start.a = (unsigned char)Clamp(
            90.0f + shipVisualEffects.intensity * 70.0f, 0.0f, 255.0f);
        Color end = start;
        end.a = 0;
        ParticleStyle style = {
            .startSize = { size, size, size },
            .endSize = { size * 0.28f, size * 0.28f, size * 0.28f },
            .startColor = start,
            .endColor = end,
            .gravity = 0.0f
        };
        ParticlesEmitStyled(position, velocity, &style,
                            0.16f + ShipVisualRandom() * 0.10f);
    }
}

static Color ShipVisualShade(Color color, Vector3 normal)
{
    float shade = 0.80f;
    if (normal.y > 0.5f) shade = 1.06f;
    else if (normal.y < -0.5f) shade = 0.58f;
    else if (normal.z > 0.5f) shade = 0.94f;
    else if (normal.z < -0.5f) shade = 0.70f;
    else shade = normal.x > 0.0f ? 0.86f : 0.76f;
    return (Color){
        (unsigned char)Clamp((float)color.r * shade, 0.0f, 255.0f),
        (unsigned char)Clamp((float)color.g * shade, 0.0f, 255.0f),
        (unsigned char)Clamp((float)color.b * shade, 0.0f, 255.0f),
        color.a
    };
}

static bool ShipVisualAddQuad(Mesh *mesh, int *vertexIndex,
                              Vector3 a, Vector3 b, Vector3 c, Vector3 d,
                              BlockType textureBlock, Color color)
{
    if (!mesh || !vertexIndex || *vertexIndex < 0 ||
        *vertexIndex + 6 > mesh->vertexCount) return false;

    Vector3 normal = Vector3Normalize(Vector3CrossProduct(
        Vector3Subtract(b, a), Vector3Subtract(c, a)));
    if (Vector3LengthSqr(normal) < 0.5f) return false;

    Vector3 corners[6] = { a, b, c, a, c, d };
    Vector2 uvs[6] = { 0 };
    AtlasUVs(TextureForBlockFace(textureBlock, 2), uvs);
    Color shaded = ShipVisualShade(color, normal);
    for (int i = 0; i < 6; i++) {
        int index = (*vertexIndex)++;
        mesh->vertices[index * 3 + 0] = corners[i].x;
        mesh->vertices[index * 3 + 1] = corners[i].y;
        mesh->vertices[index * 3 + 2] = corners[i].z;
        mesh->texcoords[index * 2 + 0] = uvs[i].x;
        mesh->texcoords[index * 2 + 1] = uvs[i].y;
        mesh->normals[index * 3 + 0] = normal.x;
        mesh->normals[index * 3 + 1] = normal.y;
        mesh->normals[index * 3 + 2] = normal.z;
        mesh->colors[index * 4 + 0] = shaded.r;
        mesh->colors[index * 4 + 1] = shaded.g;
        mesh->colors[index * 4 + 2] = shaded.b;
        mesh->colors[index * 4 + 3] = shaded.a;
    }
    return true;
}

static bool ShipVisualAddBox(Mesh *mesh, int *vertexIndex,
                             Vector3 min, Vector3 max,
                             BlockType textureBlock, Color color)
{
    return ShipVisualAddQuad(
               mesh, vertexIndex,
               (Vector3){ max.x, min.y, max.z }, (Vector3){ max.x, min.y, min.z },
               (Vector3){ max.x, max.y, min.z }, (Vector3){ max.x, max.y, max.z },
               textureBlock, color) &&
           ShipVisualAddQuad(
               mesh, vertexIndex,
               (Vector3){ min.x, min.y, min.z }, (Vector3){ min.x, min.y, max.z },
               (Vector3){ min.x, max.y, max.z }, (Vector3){ min.x, max.y, min.z },
               textureBlock, color) &&
           ShipVisualAddQuad(
               mesh, vertexIndex,
               (Vector3){ min.x, max.y, max.z }, (Vector3){ max.x, max.y, max.z },
               (Vector3){ max.x, max.y, min.z }, (Vector3){ min.x, max.y, min.z },
               textureBlock, color) &&
           ShipVisualAddQuad(
               mesh, vertexIndex,
               (Vector3){ min.x, min.y, min.z }, (Vector3){ max.x, min.y, min.z },
               (Vector3){ max.x, min.y, max.z }, (Vector3){ min.x, min.y, max.z },
               textureBlock, color) &&
           ShipVisualAddQuad(
               mesh, vertexIndex,
               (Vector3){ min.x, min.y, max.z }, (Vector3){ max.x, min.y, max.z },
               (Vector3){ max.x, max.y, max.z }, (Vector3){ min.x, max.y, max.z },
               textureBlock, color) &&
           ShipVisualAddQuad(
               mesh, vertexIndex,
               (Vector3){ max.x, min.y, min.z }, (Vector3){ min.x, min.y, min.z },
               (Vector3){ min.x, max.y, min.z }, (Vector3){ max.x, max.y, min.z },
               textureBlock, color);
}

static bool ShipVisualAddTaperedSection(
    Mesh *mesh, int *vertexIndex,
    float rearZ, float rearY, float rearHalfWidth, float rearHalfHeight,
    float frontZ, float frontY, float frontHalfWidth, float frontHalfHeight,
    BlockType textureBlock, Color color)
{
    Vector3 rear[4] = {
        { -rearHalfWidth, rearY - rearHalfHeight, rearZ },
        { rearHalfWidth, rearY - rearHalfHeight, rearZ },
        { rearHalfWidth, rearY + rearHalfHeight, rearZ },
        { -rearHalfWidth, rearY + rearHalfHeight, rearZ }
    };
    Vector3 front[4] = {
        { -frontHalfWidth, frontY - frontHalfHeight, frontZ },
        { frontHalfWidth, frontY - frontHalfHeight, frontZ },
        { frontHalfWidth, frontY + frontHalfHeight, frontZ },
        { -frontHalfWidth, frontY + frontHalfHeight, frontZ }
    };
    return ShipVisualAddQuad(mesh, vertexIndex, front[0], front[1], front[2], front[3],
                             textureBlock, color) &&
           ShipVisualAddQuad(mesh, vertexIndex, rear[1], rear[0], rear[3], rear[2],
                             textureBlock, color) &&
           ShipVisualAddQuad(mesh, vertexIndex, front[1], rear[1], rear[2], front[2],
                             textureBlock, color) &&
           ShipVisualAddQuad(mesh, vertexIndex, rear[0], front[0], front[3], rear[3],
                             textureBlock, color) &&
           ShipVisualAddQuad(mesh, vertexIndex, front[3], front[2], rear[2], rear[3],
                             textureBlock, color) &&
           ShipVisualAddQuad(mesh, vertexIndex, rear[0], rear[1], front[1], front[0],
                             textureBlock, color);
}

// Points must wind clockwise when viewed from above so the top faces upward.
static bool ShipVisualAddWing(Mesh *mesh, int *vertexIndex,
                              const Vector2 points[4], float bottomY, float topY,
                              BlockType textureBlock, Color color)
{
    Vector3 bottom[4];
    Vector3 top[4];
    for (int i = 0; i < 4; i++) {
        bottom[i] = (Vector3){ points[i].x, bottomY, points[i].y };
        top[i] = (Vector3){ points[i].x, topY, points[i].y };
    }
    if (!ShipVisualAddQuad(mesh, vertexIndex, top[0], top[1], top[2], top[3],
                           textureBlock, color) ||
        !ShipVisualAddQuad(mesh, vertexIndex, bottom[3], bottom[2], bottom[1], bottom[0],
                           textureBlock, color)) return false;
    for (int i = 0; i < 4; i++) {
        int next = (i + 1) % 4;
        if (!ShipVisualAddQuad(mesh, vertexIndex,
                               bottom[i], bottom[next], top[next], top[i],
                               textureBlock, color)) return false;
    }
    return true;
}

static bool ShipVisualAllocateMesh(Mesh *mesh)
{
    if (!mesh) return false;
    mesh->vertexCount = SHIP_VISUAL_MAX_FACES * 6;
    mesh->triangleCount = SHIP_VISUAL_MAX_FACES * 2;
    mesh->vertices = malloc((size_t)mesh->vertexCount * 3 * sizeof(float));
    mesh->texcoords = malloc((size_t)mesh->vertexCount * 2 * sizeof(float));
    mesh->normals = malloc((size_t)mesh->vertexCount * 3 * sizeof(float));
    mesh->colors = malloc((size_t)mesh->vertexCount * 4 * sizeof(unsigned char));
    if (mesh->vertices && mesh->texcoords && mesh->normals && mesh->colors) return true;
    free(mesh->vertices);
    free(mesh->texcoords);
    free(mesh->normals);
    free(mesh->colors);
    *mesh = (Mesh){ 0 };
    return false;
}

void ShipLoadModel(void)
{
    if (shipModel.meshCount > 0) return;

    Mesh mesh = { 0 };
    if (!ShipVisualAllocateMesh(&mesh)) return;

    int vertexIndex = 0;
    const Vector2 leftWing[4] = {
        { -0.38f, -1.08f }, { -1.72f, -0.76f },
        { -1.88f, -0.12f }, { -0.38f, 0.66f }
    };
    const Vector2 rightWing[4] = {
        { 0.38f, 0.66f }, { 1.88f, -0.12f },
        { 1.72f, -0.76f }, { 0.38f, -1.08f }
    };
    const Color hull = { 226, 232, 238, 255 };
    const Color hullDark = { 132, 145, 158, 255 };
    const Color canopy = { 132, 194, 232, 255 };

    bool ok =
        ShipVisualAddTaperedSection(&mesh, &vertexIndex,
                                    -1.34f, 0.0f, 0.54f, 0.32f,
                                    0.76f, 0.02f, 0.46f, 0.29f,
                                    BLOCK_SPACESHIP, hull) &&
        ShipVisualAddTaperedSection(&mesh, &vertexIndex,
                                    0.76f, 0.02f, 0.46f, 0.29f,
                                    2.18f, -0.04f, 0.07f, 0.07f,
                                    BLOCK_WHITE, hull) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ -0.31f, -0.43f, -1.20f },
                         (Vector3){ 0.31f, -0.26f, 0.92f },
                         BLOCK_GRAY, hullDark) &&
        ShipVisualAddTaperedSection(&mesh, &vertexIndex,
                                    -0.28f, 0.43f, 0.34f, 0.18f,
                                    0.78f, 0.34f, 0.17f, 0.07f,
                                    BLOCK_GLASS, canopy) &&
        ShipVisualAddWing(&mesh, &vertexIndex, leftWing, -0.13f, 0.04f,
                          BLOCK_SPACESHIP, hull) &&
        ShipVisualAddWing(&mesh, &vertexIndex, rightWing, -0.13f, 0.04f,
                          BLOCK_SPACESHIP, hull) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ -0.83f, -0.25f, -1.58f },
                         (Vector3){ -0.47f, 0.19f, -0.55f },
                         BLOCK_BLACK, (Color){ 155, 166, 178, 255 }) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ 0.47f, -0.25f, -1.58f },
                         (Vector3){ 0.83f, 0.19f, -0.55f },
                         BLOCK_BLACK, (Color){ 155, 166, 178, 255 }) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ -0.80f, -0.21f, -1.72f },
                         (Vector3){ -0.50f, 0.15f, -1.57f },
                         BLOCK_GLOWSTONE, (Color){ 255, 198, 112, 255 }) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ 0.50f, -0.21f, -1.72f },
                         (Vector3){ 0.80f, 0.15f, -1.57f },
                         BLOCK_GLOWSTONE, (Color){ 255, 198, 112, 255 }) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ -0.67f, 0.12f, -1.42f },
                         (Vector3){ -0.49f, 0.67f, -0.92f },
                         BLOCK_SPACESHIP, hull) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ 0.49f, 0.12f, -1.42f },
                         (Vector3){ 0.67f, 0.67f, -0.92f },
                         BLOCK_SPACESHIP, hull) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ -1.89f, 0.00f, -0.49f },
                         (Vector3){ -1.72f, 0.17f, -0.22f },
                         BLOCK_RED, WHITE) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ 1.72f, 0.00f, -0.49f },
                         (Vector3){ 1.89f, 0.17f, -0.22f },
                         BLOCK_GREEN, WHITE) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ -0.55f, -0.04f, -0.32f },
                         (Vector3){ -0.48f, 0.10f, 0.67f },
                         BLOCK_ORANGE, WHITE) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ 0.48f, -0.04f, -0.32f },
                         (Vector3){ 0.55f, 0.10f, 0.67f },
                         BLOCK_ORANGE, WHITE);

    if (!ok || vertexIndex <= 0) {
        free(mesh.vertices);
        free(mesh.texcoords);
        free(mesh.normals);
        free(mesh.colors);
        return;
    }

    mesh.vertexCount = vertexIndex;
    mesh.triangleCount = vertexIndex / 3;
    UploadMesh(&mesh, false);
    shipModel = LoadModelFromMesh(mesh);
    if (shipModel.materialCount > 0) {
        SetMaterialTexture(&shipModel.materials[0], MATERIAL_MAP_DIFFUSE,
                           ChunksBlockAtlas());
    }
}

void ShipCleanup(void)
{
    if (shipModel.meshCount > 0) UnloadModel(shipModel);
    shipModel = (Model){ 0 };
    ShipResetVisualEffects();
}

void ShipDraw(const Player *player)
{
    if (!player || shipModel.meshCount == 0) return;
    shipModel.transform = ShipVisualRotation(player);
    Vector3 pos = ShipVisualOrigin(player);
    DrawModel(shipModel, pos, 1.0f, WHITE);
    if (shipVisualEffects.intensity <= 0.01f) return;

    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 2; i++) {
        float flicker = 1.0f + 0.075f * sinf(
            shipVisualEffects.phase * 31.0f + (float)i * 2.17f);
        float intensity = shipVisualEffects.intensity;
        float length = shipVisualEffects.profile.flameLength *
                       sqrtf(intensity) * flicker;
        float radius = shipVisualEffects.profile.outerRadius *
                       (0.55f + 0.45f * intensity);
        Vector3 nozzle = ShipVisualNozzle(player, i);
        Vector3 plumeDirection = ShipVisualPlumeDirection(player, i);
        Vector3 outerEnd = Vector3Add(
            nozzle, Vector3Scale(plumeDirection, length));
        Color outer = shipVisualEffects.profile.outerColor;
        outer.a = (unsigned char)Clamp((float)outer.a * intensity,
                                       0.0f, 255.0f);
        DrawCylinderEx(nozzle, outerEnd, radius, 0.018f, 10, outer);
        Vector3 glowCenter = Vector3Add(
            nozzle, Vector3Scale(plumeDirection, 0.025f));
        DrawSphereEx(glowCenter, radius * 1.05f, 8, 8, outer);
        DrawSphereEx(outerEnd, radius * 0.24f, 6, 6, outer);

        Vector3 coreStart = Vector3Add(nozzle,
                                       Vector3Scale(plumeDirection, 0.015f));
        Vector3 coreEnd = Vector3Add(
            nozzle, Vector3Scale(plumeDirection, length * 0.62f));
        Color core = shipVisualEffects.profile.coreColor;
        core.a = (unsigned char)Clamp((float)core.a * intensity,
                                      0.0f, 255.0f);
        DrawCylinderEx(coreStart, coreEnd, radius * 0.48f, 0.008f,
                       8, core);
        DrawSphereEx(glowCenter, radius * 0.35f, 8, 8, core);
        DrawSphereEx(coreEnd, radius * 0.12f, 6, 6, core);
    }
    EndBlendMode();
}
