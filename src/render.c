#include "render.h"

#include "raymath.h"
#include "chunks.h"
#include "world.h"
#include "interaction.h"
#include "terrain.h"
#include "particles.h"
#include "space.h"
#include "nether.h"
#include "entity.h"
#include "ship.h"
#include "audio.h"
#include "weather.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

float dayTimeForHud = 0.30f;
bool autoSaveForHud = true;
BlockType blockForHud = BLOCK_AIR;
int SpaceEditCountForHud = 0;
float shipSpeedForHud = 0.0f;

#include "chunks.h"
#include "world.h"
#include "interaction.h"
#include "terrain.h"
#include "particles.h"
#include "space.h"
#include "nether.h"
#include "entity.h"
#include "ship.h"
#include "audio.h"
#include "weather.h"
Model cloudModel = { 0 };
void DayNightFactors(float currentDayTime, float *daylight, float *sunset)
{
    float theta = (currentDayTime - 0.25f) * (2.0f * PI);
    float sunHeight = sinf(theta);
    *daylight = Clamp(sunHeight, 0.0f, 1.0f);
    *sunset = (sunHeight > 0.0f) ? powf(1.0f - *daylight, 2.0f) : 0.0f;
}

Color WorldTintForLight(float daylight, float sunset)
{
    float light = 0.14f + daylight * 0.86f;
    Color tint = {
        (unsigned char)(light * 255.0f),
        (unsigned char)(light * 255.0f),
        (unsigned char)(light * 255.0f),
        255
    };
    tint = ColorLerp(tint, (Color){ 255, 152, 108, 255 }, sunset * 0.22f);
    tint = ColorLerp(tint, (Color){ 96, 116, 190, 255 }, (1.0f - daylight) * 0.28f);
    return tint;
}

Color MixWeather(Color color, float daylight)
{
    float factor = WeatherSkyFactor();
    if (factor <= 0.0f) return color;

    Color overcast = WeatherGetCurrent() == WEATHER_SNOW ?
                     (Color){ 168, 180, 196, 255 } : (Color){ 84, 96, 118, 255 };
    factor *= 0.35f + 0.65f * daylight;
    return ColorLerp(color, overcast, factor);
}

void SkyColorsForLight(float daylight, float sunset, Color *top, Color *horizon)
{
    Color nightTop = { 8, 12, 34, 255 };
    Color dayTop = { 98, 168, 226, 255 };
    Color nightHorizon = { 18, 22, 46, 255 };
    Color dayHorizon = { 188, 224, 248, 255 };
    Color sunsetGlow = { 255, 130, 66, 255 };

    *top = ColorLerp(nightTop, dayTop, daylight);
    *horizon = ColorLerp(nightHorizon, dayHorizon, daylight);
    *top = ColorLerp(*top, sunsetGlow, sunset * 0.40f);
    *horizon = ColorLerp(*horizon, sunsetGlow, sunset * 0.90f);
}

typedef struct Meteor {
    bool active;
    Vector2 pos;
    Vector2 vel;
    float life;
    float maxLife;
} Meteor;

static Meteor meteor = { 0 };
static float meteorTimer = 8.0f;

static void UpdateMeteors(float spaceFade, int sw, int sh)
{
    if (spaceFade <= 0.3f) return;

    float dt = GetFrameTime();
    meteorTimer -= dt;
    if (!meteor.active && meteorTimer <= 0.0f) {
        meteor.active = true;
        meteor.pos = (Vector2){
            (float)(rand() % sw) * 0.5f + (float)sw * 0.25f,
            (float)(rand() % sh) * 0.12f + 20.0f
        };
        meteor.vel = (Vector2){
            -(260.0f + (float)(rand() % 380)),
            180.0f + (float)(rand() % 260)
        };
        meteor.life = 0.0f;
        meteor.maxLife = 0.8f + (float)(rand() % 50) / 100.0f;
        meteorTimer = 5.0f + (float)(rand() % 110) / 10.0f;
    }
    if (!meteor.active) return;

    meteor.life += dt;
    meteor.pos = Vector2Add(meteor.pos, Vector2Scale(meteor.vel, dt));
    float t = meteor.life / meteor.maxLife;
    unsigned char mAlpha = (unsigned char)((1.0f - t) * 255.0f * spaceFade);
    Vector2 tail = Vector2Subtract(meteor.pos, Vector2Scale(Vector2Normalize(meteor.vel), 90.0f));
    DrawLineEx(tail, meteor.pos, 2.5f, Fade((Color){ 255, 200, 120, 255 }, (float)mAlpha * 0.7f));
    DrawLineEx(Vector2Subtract(meteor.pos, Vector2Scale(Vector2Normalize(meteor.vel), 40.0f)),
               meteor.pos, 1.5f, Fade((Color){ 255, 240, 210, 255 }, (float)mAlpha));
    DrawCircle((int)meteor.pos.x, (int)meteor.pos.y, 3.0f, Fade((Color){ 255, 250, 230, 255 }, (float)mAlpha));
    if (t >= 1.0f) meteor.active = false;
}

#define NEBULA_COUNT 36
#define NEBULA_SPAN 2600.0f
#define NEBULA_DIST 1700.0f

static void DrawNebulae(const Camera3D *camera, float spaceFade)
{
    if (spaceFade <= 0.01f) return;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    static const Color nebulaColors[4] = {
        { 150, 96, 210, 255 },
        { 84, 124, 224, 255 },
        { 222, 140, 82, 255 },
        { 96, 186, 204, 255 }
    };

    for (int i = 0; i < NEBULA_COUNT; i++) {
        unsigned int h1 = Hash2D(i * 13 + 5, 71);
        unsigned int h2 = Hash2D(i * 29 + 3, 97);
        float theta = (float)(h1 % 6283u) / 1000.0f;
        float phi = (float)(h2 % 2000u) / 2000.0f * 1.1f;
        float radius = (float)(140 + (h2 >> 10) % 90);
        float dist = NEBULA_DIST + (float)((h1 >> 12) % 800u);

        Vector3 dir = {
            sinf(phi) * cosf(theta),
            fmaxf(cosf(phi), 0.05f),
            sinf(phi) * sinf(theta)
        };
        Vector3 pos = Vector3Add(camera->position, Vector3Scale(Vector3Normalize(dir), dist));
        Vector2 screen = GetWorldToScreen(pos, *camera);
        if (screen.x < -radius || screen.x > (float)sw + radius ||
            screen.y < -radius || screen.y > (float)sh + radius) continue;

        float scale = Clamp(radius * 420.0f / dist, 20.0f, 140.0f);
        Color color = nebulaColors[i % 4];
        DrawCircleGradient((int)screen.x, (int)screen.y, (int)scale,
                           Fade(color, 0.10f * spaceFade), BLANK);
        DrawCircleGradient((int)screen.x, (int)screen.y, (int)(scale * 0.55f),
                           Fade(color, 0.14f * spaceFade), BLANK);
    }
}

void DrawSpaceSky(float spaceFade, const Camera3D *camera)
{
    if (spaceFade <= 0.01f) return;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    unsigned char alpha = (unsigned char)(255.0f * spaceFade);

    UpdateMeteors(spaceFade, sw, sh);

    DrawNebulae(camera, spaceFade);

    DrawRectangleGradientV(0, sh / 2 - 90, sw, 90,
                           (Color){ 205, 205, 235, (unsigned char)(24.0f * spaceFade) }, BLANK);
    DrawRectangleGradientV(0, sh / 2 - 30, sw, 70,
                           (Color){ 175, 185, 230, (unsigned char)(16.0f * spaceFade) }, BLANK);

    Vector3 planetDir = Vector3Normalize((Vector3){ 0.45f, 0.18f, 0.85f });
    Vector3 planetPos = Vector3Add(camera->position, Vector3Scale(planetDir, 420.0f));
    Vector2 screen = GetWorldToScreen(planetPos, *camera);
    if (screen.x > -120.0f && screen.x < (float)sw + 120.0f &&
        screen.y > -120.0f && screen.y < (float)sh + 120.0f) {
        float scale = spaceFade;
        DrawCircleGradient((int)screen.x, (int)screen.y, (int)(48.0f * scale),
                           Fade((Color){ 212, 172, 112, 255 }, 0.45f * spaceFade), BLANK);
        DrawCircle((int)screen.x, (int)screen.y, (int)(20.0f * scale), (Color){ 198, 150, 96, alpha });
        DrawEllipseLines((int)screen.x, (int)screen.y, (int)(38.0f * scale), (int)(11.0f * scale),
                         (Color){ 230, 205, 165, (unsigned char)(190.0f * spaceFade) });
        DrawEllipseLines((int)screen.x, (int)screen.y, (int)(34.0f * scale), (int)(9.0f * scale),
                         (Color){ 230, 205, 165, (unsigned char)(120.0f * spaceFade) });
    }
}

#define STAR_COUNT 500
#define STAR_SHELL_DISTANCE 500.0f

typedef struct StarField {
    Vector3 dir;
    float size;
    float phase;
    Color color;
    bool bright;
} StarField;

static StarField starField[STAR_COUNT];
static bool starFieldReady = false;

static unsigned int StarSeedNext(unsigned int *seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return *seed;
}

static void InitStarField(void)
{
    unsigned int seed = 42u;
    for (int i = 0; i < STAR_COUNT; i++) {
        float u = (float)(StarSeedNext(&seed) % 10000u) / 10000.0f;
        float v = (float)(StarSeedNext(&seed) % 10000u) / 10000.0f;
        float theta = u * 2.0f * PI;
        float phi = acosf(2.0f * v - 1.0f);
        starField[i].dir = (Vector3){
            sinf(phi) * cosf(theta),
            cosf(phi),
            sinf(phi) * sinf(theta)
        };
        starField[i].size = 0.8f + v * 2.2f;
        starField[i].phase = (float)(StarSeedNext(&seed) % 6283u) / 1000.0f;
        unsigned int variant = StarSeedNext(&seed) % 10u;
        if (variant < 7u) starField[i].color = (Color){ 235, 240, 255, 255 };
        else if (variant < 9u) starField[i].color = (Color){ 220, 228, 255, 255 };
        else starField[i].color = (Color){ 255, 244, 214, 255 };
        starField[i].bright = (StarSeedNext(&seed) % 100u) < 6u;
    }
    starFieldReady = true;
}

void DrawStars(const Camera3D *camera, float daylight)
{
    if (daylight > 0.15f) return;
    if (!starFieldReady) InitStarField();

    float visibility = (0.15f - daylight) / 0.15f;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float time = GetTime();

    for (int i = 0; i < STAR_COUNT; i++) {
        if (starField[i].dir.y < -0.05f) continue;

        Vector3 pos = Vector3Add(camera->position, Vector3Scale(starField[i].dir, STAR_SHELL_DISTANCE));
        Vector2 screen = GetWorldToScreen(pos, *camera);
        if (screen.x < -20.0f || screen.x > (float)sw + 20.0f ||
            screen.y < -20.0f || screen.y > (float)sh + 20.0f) continue;

        float twinkle = 0.65f + 0.35f * sinf(time * 1.7f + starField[i].phase);
        unsigned char alpha = (unsigned char)(visibility * 220.0f * twinkle);
        Color color = starField[i].color;
        color.a = alpha;

        if (starField[i].bright) {
            DrawCircle((int)screen.x, (int)screen.y, 2.6f, color);
            DrawLine((int)screen.x - 6, (int)screen.y, (int)screen.x + 6, (int)screen.y, Fade(color, 0.35f));
            DrawLine((int)screen.x, (int)screen.y - 6, (int)screen.x, (int)screen.y + 6, Fade(color, 0.35f));
        } else {
            DrawCircle((int)screen.x, (int)screen.y, starField[i].size, color);
        }
    }
}

void DrawCelestial(const Camera3D *camera, float currentDayTime, float daylight)
{
    float theta = (currentDayTime - 0.25f) * (2.0f * PI);
    Vector3 sunDir = Vector3Normalize((Vector3){ cosf(theta), sinf(theta), 0.18f });
    Vector3 moonDir = Vector3Negate(sunDir);
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));

    if (sinf(theta) > 0.0f && Vector3DotProduct(sunDir, forward) > 0.05f) {
        Vector3 sunPos = Vector3Add(camera->position, Vector3Scale(sunDir, SUN_DISTANCE));
        Vector2 sunScreen = GetWorldToScreen(sunPos, *camera);
        float glowRadius = 28.0f + daylight * 24.0f;
        DrawCircleGradient((int)sunScreen.x, (int)sunScreen.y, glowRadius,
                           Fade(ORANGE, 0.28f), BLANK);
        DrawCircle((int)sunScreen.x, (int)sunScreen.y, 15.0f, (Color){ 255, 242, 180, 255 });
    }

    if (sinf(theta) < 0.0f && Vector3DotProduct(moonDir, forward) > 0.05f) {
        Vector3 moonPos = Vector3Add(camera->position, Vector3Scale(moonDir, SUN_DISTANCE));
        Vector2 moonScreen = GetWorldToScreen(moonPos, *camera);
        DrawCircle((int)moonScreen.x, (int)moonScreen.y, 12.0f, (Color){ 214, 226, 244, 240 });
        DrawCircle((int)moonScreen.x - 5, (int)moonScreen.y - 3, 10.0f, (Color){ 24, 30, 52, 235 });
    }
}

Model LoadCloudModel(void)
{
    static const int cubes[9][3] = {
        { -2, 0, -1 }, { -1, 0, 0 }, { 0, 0, -1 }, { 1, 0, 0 }, { 2, 0, -1 },
        { -1, 1, 0 }, { 0, 1, 1 }, { 1, 1, 0 }, { 0, 2, 0 }
    };
    Color cloudColor = { 255, 255, 255, 190 };

    Mesh mesh = { 0 };
    mesh.vertexCount = 9 * 6 * 6;
    mesh.triangleCount = 9 * 6 * 2;
    mesh.vertices = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = malloc((size_t)mesh.vertexCount * 2 * sizeof(float));
    mesh.normals = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.colors = malloc((size_t)mesh.vertexCount * 4 * sizeof(unsigned char));

    if (!mesh.vertices || !mesh.texcoords || !mesh.normals || !mesh.colors) {
        free(mesh.vertices);
        free(mesh.texcoords);
        free(mesh.normals);
        free(mesh.colors);
        return (Model){ 0 };
    }

    int vertexIndex = 0;
    for (int i = 0; i < 9; i++) {
        for (int face = 0; face < 6; face++) {
            AddBlockFace(&mesh, &vertexIndex, cubes[i][0], cubes[i][1], cubes[i][2],
                         face, BLOCK_WHITE, cloudColor, 0.0f);
        }
    }

    UploadMesh(&mesh, false);
    Model model = LoadModelFromMesh(mesh);
    SetMaterialTexture(&model.materials[0], MATERIAL_MAP_DIFFUSE, blockAtlas);
    return model;
}

void DrawClouds(const Camera3D *camera, Color tint)
{
    float drift = GetTime() * CLOUD_DRIFT;
    float wrappedDrift = fmodf(drift, CLOUD_SPAN);
    float wrappedDriftZ = fmodf(drift * 0.6f, CLOUD_SPAN);
    BeginBlendMode(BLEND_ALPHA);
    for (int i = 0; i < CLOUD_COUNT; i++) {
        unsigned int h1 = Hash2D(i * 3 + 1, 17);
        unsigned int h2 = Hash2D(i * 7 + 5, 23);
        float baseX = (float)(h1 % 10000u) / 10.0f;
        float baseZ = (float)(h2 % 10000u) / 10.0f;
        float cx = camera->position.x + fmodf(baseX + wrappedDrift, CLOUD_SPAN) - CLOUD_SPAN * 0.5f;
        float cz = camera->position.z + fmodf(baseZ + wrappedDriftZ, CLOUD_SPAN) - CLOUD_SPAN * 0.5f;
        float cy = CLOUD_BASE_HEIGHT + (float)((h1 >> 16) % 5u);
        float scale = 1.5f + (float)((h2 >> 16) % 10u) / 10.0f;
        DrawModel(cloudModel, (Vector3){ cx, cy, cz }, scale, tint);
    }
    EndBlendMode();
}

void DrawWorld(const Camera3D *camera, int effectiveRenderDistance, Color tint)
{
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        Chunk *chunk = &chunks[i];
        if (!chunk->loaded) continue;
        if (!ChunkWithinDrawDistance(chunk, camera->position, effectiveRenderDistance)) continue;
        if (!ChunkIntersectsCameraView(chunk, camera)) continue;
        if (chunk->hasModel) DrawModel(chunk->model, Vector3Zero(), 1.0f, tint);
    }

    int camCx = 0;
    int camCz = 0;
    int camLx = 0;
    int camLz = 0;
    WorldToChunkLocal((int)floorf(camera->position.x), (int)floorf(camera->position.z),
                      &camCx, &camCz, &camLx, &camLz);

    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        SpaceChunk *chunk = &spaceChunks[i];
        if (!chunk->loaded) continue;
        if (abs(chunk->cx - camCx) > SPACE_RENDER_DISTANCE_CHUNKS ||
            abs(chunk->cz - camCz) > SPACE_RENDER_DISTANCE_CHUNKS) continue;
        Vector3 center = {
            (float)(chunk->cx * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f,
            (float)SPACE_LAYER_Y + (float)SPACE_LAYER_HEIGHT * 0.5f,
            (float)(chunk->cz * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f
        };
        if (!SphereInFrustum(camera, center, 66.0f)) continue;
        if (chunk->hasModel) DrawModel(chunk->model, (Vector3){ 0.0f, (float)SPACE_LAYER_Y, 0.0f }, 1.0f, tint);
    }

    int netherCamCx = 0;
    int netherCamCz = 0;
    int netherCamLx = 0;
    int netherCamLz = 0;
    WorldToChunkLocal((int)floorf(camera->position.x), (int)floorf(camera->position.z),
                      &netherCamCx, &netherCamCz, &netherCamLx, &netherCamLz);

    for (int i = 0; i < MAX_NETHER_CHUNKS; i++) {
        NetherChunk *chunk = &netherChunks[i];
        if (!chunk->loaded) continue;
        if (abs(chunk->cx - netherCamCx) > NETHER_RENDER_DISTANCE_CHUNKS ||
            abs(chunk->cz - netherCamCz) > NETHER_RENDER_DISTANCE_CHUNKS) continue;
        Vector3 center = {
            (float)(chunk->cx * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f,
            (float)NETHER_LAYER_Y + 16.0f,
            (float)(chunk->cz * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f
        };
        if (!SphereInFrustum(camera, center, 34.0f)) continue;
        if (chunk->hasModel) DrawModel(chunk->model, (Vector3){ 0.0f, (float)NETHER_LAYER_Y, 0.0f }, 1.0f, tint);
    }

    BeginBlendMode(BLEND_ALPHA);
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        Chunk *chunk = &chunks[i];
        if (!chunk->loaded) continue;
        if (!ChunkWithinDrawDistance(chunk, camera->position, effectiveRenderDistance)) continue;
        if (!ChunkIntersectsCameraView(chunk, camera)) continue;
        if (chunk->hasWaterModel) DrawModel(chunk->waterModel, Vector3Zero(), 1.0f, tint);
    }
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        SpaceChunk *chunk = &spaceChunks[i];
        if (!chunk->loaded) continue;
        if (abs(chunk->cx - camCx) > SPACE_RENDER_DISTANCE_CHUNKS ||
            abs(chunk->cz - camCz) > SPACE_RENDER_DISTANCE_CHUNKS) continue;
        Vector3 center = {
            (float)(chunk->cx * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f,
            (float)SPACE_LAYER_Y + (float)SPACE_LAYER_HEIGHT * 0.5f,
            (float)(chunk->cz * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f
        };
        if (!SphereInFrustum(camera, center, 66.0f)) continue;
        if (chunk->hasWaterModel) DrawModel(chunk->waterModel, (Vector3){ 0.0f, (float)SPACE_LAYER_Y, 0.0f }, 1.0f, tint);
    }
    for (int i = 0; i < MAX_NETHER_CHUNKS; i++) {
        NetherChunk *chunk = &netherChunks[i];
        if (!chunk->loaded) continue;
        if (abs(chunk->cx - netherCamCx) > NETHER_RENDER_DISTANCE_CHUNKS ||
            abs(chunk->cz - netherCamCz) > NETHER_RENDER_DISTANCE_CHUNKS) continue;
        Vector3 center = {
            (float)(chunk->cx * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f,
            (float)NETHER_LAYER_Y + 16.0f,
            (float)(chunk->cz * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f
        };
        if (!SphereInFrustum(camera, center, 34.0f)) continue;
        if (chunk->hasWaterModel) DrawModel(chunk->waterModel, (Vector3){ 0.0f, (float)NETHER_LAYER_Y, 0.0f }, 1.0f, tint);
    }
    EndBlendMode();
}

static Color SolarStyleColor(SolarBodyStyle style)
{
    switch (style) {
    case SOLAR_STYLE_LAVA:   return (Color){ 235, 120, 70, 255 };
    case SOLAR_STYLE_ICE:    return (Color){ 170, 210, 240, 255 };
    case SOLAR_STYLE_DESERT: return (Color){ 226, 196, 132, 255 };
    case SOLAR_STYLE_GAS:    return (Color){ 190, 170, 230, 255 };
    case SOLAR_STYLE_CRATER: return (Color){ 150, 152, 158, 255 };
    default:                 return (Color){ 200, 200, 200, 255 };
    }
}

static void DrawEdgeIndicator(float px, float py, bool behind, Vector3 origin, Vector3 center,
                              Color color, float spaceFade, const char *label)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float cx = (float)sw * 0.5f;
    float cy = (float)sh * 0.5f;
    float dx = px - cx;
    float dy = py - cy;
    if (behind) {
        dx = -dx;
        dy = -dy;
    }
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) {
        dx = 0.0f;
        dy = -1.0f;
        len = 1.0f;
    }
    dx /= len;
    dy /= len;
    float margin = 30.0f;
    float tx = ((float)sw * 0.5f - margin) / fmaxf(fabsf(dx), 1e-5f);
    float ty = ((float)sh * 0.5f - margin) / fmaxf(fabsf(dy), 1e-5f);
    float t = fminf(tx, ty);
    float ex = cx + dx * t;
    float ey = cy + dy * t;

    DrawTriangle((Vector2){ ex - dy * 5.0f, ey + dx * 5.0f },
                 (Vector2){ ex + dy * 5.0f, ey - dx * 5.0f },
                 (Vector2){ ex + dx * 12.0f, ey + dy * 12.0f },
                 Fade(color, 0.9f * spaceFade));

    if (label) {
        float dist = Vector3Distance(origin, center);
        DrawText(TextFormat("%s - %.0f blocks", label, dist),
                 (int)ex + 16, (int)ey - 10, 15, Fade(WHITE, 0.9f * spaceFade));
    }
}

void DrawSolarGuide(const Camera3D *camera, float spaceFade)
{
    if (spaceFade <= 0.05f) return;

    SpaceBodyInfo bodies[48];
    int count = SpaceBodiesNear(camera->position, 700.0f, bodies, 48);
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    for (int i = 0; i < count; i++) {
        Vector3 toBody = Vector3Subtract(bodies[i].center, camera->position);
        bool behind = Vector3DotProduct(toBody, forward) < 0.0f;
        Vector2 screen = GetWorldToScreen(bodies[i].center, *camera);
        float px = screen.x;
        float py = screen.y;
        bool onScreen = !behind && px > -10.0f && px < (float)sw + 10.0f &&
                        py > -10.0f && py < (float)sh + 10.0f;

        if (bodies[i].isStar) {
            Color color = SpectrumColor(bodies[i].spectrum);
            if (onScreen) {
                float scale = Clamp(1400.0f / bodies[i].dist, 5.0f, 28.0f);
                DrawCircleGradient((int)px, (int)py, (int)(scale * 1.7f),
                                   Fade(color, 0.30f * spaceFade), BLANK);
                DrawCircle((int)px, (int)py, (int)scale,
                           Fade((Color){ 255, 244, 200, 255 }, spaceFade));
                DrawLine((int)px - (int)(scale * 2.0f), (int)py, (int)px + (int)(scale * 2.0f), (int)py,
                         Fade(color, 0.30f * spaceFade));
                DrawLine((int)px, (int)py - (int)(scale * 2.0f), (int)px, (int)py + (int)(scale * 2.0f),
                         Fade(color, 0.30f * spaceFade));
                if (bodies[i].dist < 350.0f) {
                    DrawText(TextFormat("%s Prime", bodies[i].name), (int)px + (int)scale + 6, (int)py - 8, 15,
                             Fade(WHITE, 0.85f * spaceFade));
                }
            } else {
                DrawEdgeIndicator(px, py, behind, camera->position, bodies[i].center, color, spaceFade,
                                  TextFormat("%s Prime", bodies[i].name));
            }
            continue;
        }

        Color color = SolarStyleColor(bodies[i].style);
        if (onScreen) {
            DrawCircle((int)px, (int)py, 4.0f, Fade(color, spaceFade));
            if (bodies[i].dist < 350.0f) {
                DrawText(TextFormat("%s %c", bodies[i].name, 'a' + bodies[i].index),
                         (int)px + 7, (int)py - 8, 15, Fade(WHITE, 0.85f * spaceFade));
            }
        }
    }

    if (count == 0) {
        SolarSystemDef sys;
        float sysDist = 0.0f;
        if (FindNearestSystem(camera->position, 9000.0f, &sys, &sysDist)) {
            Vector3 toSys = Vector3Subtract(sys.center, camera->position);
            bool behind = Vector3DotProduct(toSys, forward) < 0.0f;
            Vector2 screen = GetWorldToScreen(sys.center, *camera);
            Color color = SpectrumColor(sys.spectrum);
            DrawEdgeIndicator(screen.x, screen.y, behind, camera->position, sys.center, color, spaceFade,
                              TextFormat("%s Prime", sys.name));
        }
    }
}

void DrawBodyInfoPanel(const SpaceBodyInfo *body)
{
    if (!body) return;

    const char *typeName = body->isStar ? SpectrumName(body->spectrum) : SolarStyleName(body->style);
    const char *text;
    if (body->isStar) {
        text = TextFormat("%s Prime - %s - %.0f blocks", body->name, typeName, body->dist);
    } else {
        text = TextFormat("%s %c - %s - %.0f blocks", body->name, 'a' + body->index, typeName, body->dist);
    }

    int fs = 18;
    int width = MeasureText(text, fs);
    int sw = GetScreenWidth();
    int x = sw / 2 - width / 2;
    int y = 64;
    DrawRectangleRounded((Rectangle){ (float)x - 16, (float)y - 8, (float)width + 32, 40.0f },
                         0.10f, 6, Fade(BLACK, 0.55f));
    DrawRectangleRoundedLinesEx((Rectangle){ (float)x - 16, (float)y - 8, (float)width + 32, 40.0f },
                                0.10f, 6, 1.5f, Fade(WHITE, 0.30f));
    DrawText(text, x, y, fs, WHITE);
}

void DrawCrosshair(int screenWidth, int screenHeight)
{
    int cx = screenWidth / 2;
    int cy = screenHeight / 2;
    DrawLine(cx - 9, cy, cx - 3, cy, WHITE);
    DrawLine(cx + 3, cy, cx + 9, cy, WHITE);
    DrawLine(cx, cy - 9, cx, cy - 3, WHITE);
    DrawLine(cx, cy + 3, cx, cy + 9, WHITE);
}

void DrawHotbar(const BlockType *hotbar, int selectedIndex)
{
    int sw = GetScreenWidth();
    int y = GetScreenHeight() - 72;
    int slot = 50;
    int gap = 8;
    int total = HOTBAR_SIZE * slot + (HOTBAR_SIZE - 1) * gap;
    int x0 = sw / 2 - total / 2;

    for (int i = 0; i < HOTBAR_SIZE; i++) {
        BlockType block = hotbar[i];
        Rectangle rect = { (float)(x0 + i * (slot + gap)), (float)y, (float)slot, (float)slot };
        DrawRectangleRounded(rect, 0.12f, 6, Fade(BLACK, i == selectedIndex ? 0.72f : 0.45f));
        DrawRectangleRoundedLinesEx(rect, 0.12f, 6, i == selectedIndex ? 3.0f : 1.0f,
                                    i == selectedIndex ? WHITE : Fade(WHITE, 0.35f));

        BlockTexture texture = TextureForBlockFace(block, 2);
        Rectangle source = AtlasSourceRect(texture);
        Rectangle dest = { rect.x + 14.0f, rect.y + 11.0f, 22.0f, 22.0f };
        DrawTexturePro(blockAtlas, source, dest, Vector2Zero(), 0.0f, WHITE);
        DrawRectangleLines((int)rect.x + 14, (int)rect.y + 11, 22, 22, Fade(BLACK, 0.35f));
        DrawText(i == 9 ? "0" : TextFormat("%d", i + 1), (int)rect.x + 6, (int)rect.y + 31, 14, Fade(WHITE, 0.85f));
    }

    DrawText(BlockName(hotbar[selectedIndex]), x0, y - 24, 18, WHITE);
}

int HotbarKeyToIndex(void)
{
    if (IsKeyPressed(KEY_ZERO)) return 9;
    for (int i = 0; i < 9 && i < HOTBAR_SIZE; i++) {
        if (IsKeyPressed(KEY_ONE + i)) return i;
    }
    return -1;
}

void DrawCenteredText(const char *text, int y, int fontSize, Color color)
{
    int width = MeasureText(text, fontSize);
    DrawText(text, GetScreenWidth() / 2 - width / 2, y, fontSize, color);
}

bool DrawMenuButton(Rectangle rect, const char *label, bool primary)
{
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, rect);
    Color fill = primary ? (Color){ 68, 142, 90, 255 } : (Color){ 38, 45, 53, 255 };
    Color hoverFill = primary ? (Color){ 83, 164, 104, 255 } : (Color){ 52, 61, 70, 255 };

    DrawRectangleRounded(rect, 0.08f, 8, hovered ? hoverFill : fill);
    DrawRectangleRoundedLinesEx(rect, 0.08f, 8, 2.0f, hovered ? WHITE : Fade(WHITE, 0.55f));

    int fontSize = 24;
    int textWidth = MeasureText(label, fontSize);
    DrawText(label, (int)(rect.x + rect.width * 0.5f - textWidth * 0.5f),
             (int)(rect.y + rect.height * 0.5f - fontSize * 0.5f), fontSize, WHITE);

    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool DrawTerrainOption(Rectangle rect, const char *title, const char *subtitle, bool selected)
{
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, rect);
    Color fill = selected ? (Color){ 45, 105, 78, 255 } : (Color){ 35, 44, 52, 255 };
    if (hovered && !selected) fill = (Color){ 45, 56, 66, 255 };

    DrawRectangleRounded(rect, 0.07f, 8, fill);
    DrawRectangleRoundedLinesEx(rect, 0.07f, 8, selected ? 3.0f : 1.5f,
                                selected ? (Color){ 224, 241, 202, 255 } : Fade(WHITE, 0.42f));

    DrawText(title, (int)rect.x + 18, (int)rect.y + 13, 22, WHITE);
    DrawText(subtitle, (int)rect.x + 18, (int)rect.y + 43, 15, Fade(WHITE, 0.72f));
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void DrawStartPage(bool *startGame, bool *quitGame, TerrainMode *selectedTerrain)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    ClearBackground((Color){ 96, 157, 213, 255 });
    DrawRectangleGradientV(0, 0, sw, sh, (Color){ 99, 166, 221, 255 }, (Color){ 58, 91, 78, 255 });

    int tileSize = 54;
    int gap = 10;
    BlockType previewBlocks[HOTBAR_SIZE] = {
        BLOCK_GRASS, BLOCK_DIRT, BLOCK_STONE, BLOCK_WOOD, BLOCK_PLANK,
        BLOCK_BRICK, BLOCK_SAND, BLOCK_SNOW, BLOCK_GLASS, BLOCK_WATER
    };
    int total = HOTBAR_SIZE * tileSize + (HOTBAR_SIZE - 1) * gap;
    int x0 = sw / 2 - total / 2;
    int previewY = sh / 2 - 82;

    DrawCenteredText("Voxelcraft", sh / 2 - 190, 64, WHITE);
    DrawCenteredText("Infinite block world", sh / 2 - 116, 24, Fade(WHITE, 0.86f));

    for (int i = 0; i < HOTBAR_SIZE; i++) {
        Rectangle tile = { (float)(x0 + i * (tileSize + gap)), (float)previewY, (float)tileSize, (float)tileSize };
        BlockTexture texture = TextureForBlockFace(previewBlocks[i], 2);
        Rectangle source = AtlasSourceRect(texture);
        DrawRectangleRounded((Rectangle){ tile.x - 4.0f, tile.y - 4.0f, tile.width + 8.0f, tile.height + 8.0f },
                             0.08f, 6, Fade(BLACK, 0.22f));
        DrawTexturePro(blockAtlas, source, tile, Vector2Zero(), 0.0f, WHITE);
        DrawRectangleLinesEx(tile, 2.0f, Fade(WHITE, 0.48f));
    }

    Rectangle variedRect = { sw / 2 - 252.0f, sh / 2 + 6.0f, 240.0f, 72.0f };
    Rectangle flatRect = { sw / 2 + 12.0f, sh / 2 + 6.0f, 240.0f, 72.0f };
    if (DrawTerrainOption(variedRect, "Varied", "Rolling hills and trees", *selectedTerrain == TERRAIN_VARIED)) {
        *selectedTerrain = TERRAIN_VARIED;
    }
    if (DrawTerrainOption(flatRect, "Flat", "Plain with image import", *selectedTerrain == TERRAIN_FLAT)) {
        *selectedTerrain = TERRAIN_FLAT;
    }

    Rectangle startRect = { sw / 2 - 130.0f, sh / 2 + 106.0f, 260.0f, 56.0f };
    Rectangle quitRect = { sw / 2 - 130.0f, sh / 2 + 176.0f, 260.0f, 52.0f };
    if (DrawMenuButton(startRect, "Start", true) || IsKeyPressed(KEY_ENTER)) *startGame = true;
    if (DrawMenuButton(quitRect, "Quit", false)) *quitGame = true;

    DrawCenteredText("Press Enter to start", sh - 54, 18, Fade(WHITE, 0.72f));
}

void DrawHelpPanel(bool floating, bool cursorReleased, int viewDistance)
{
    int x = 18;
    int y = 18;
    int w = 315;
    int h = 304;
    DrawRectangleRounded((Rectangle){ (float)x, (float)y, (float)w, (float)h }, 0.05f, 6, Fade(BLACK, 0.5f));
    DrawText("Voxelcraft", x + 14, y + 12, 22, WHITE);
    DrawText("WASD move    Shift sprint    Space jump/swim", x + 14, y + 46, 16, RAYWHITE);
    DrawText("LMB break    RMB place    MMB pick block", x + 14, y + 70, 16, RAYWHITE);
    DrawText("F float    Ctrl down (float)    Wheel hotbar", x + 14, y + 94, 16, RAYWHITE);
    DrawText("Tab mouse    1-0 blocks    P album", x + 14, y + 118, 16, RAYWHITE);
    DrawText("RMB on placed album opens it", x + 14, y + 142, 16, RAYWHITE);
    DrawText("Esc pause    F6 day/night cycle", x + 14, y + 166, 16, RAYWHITE);
    DrawText("F4 view    F5 save    F9 load    F10 shot", x + 14, y + 190, 16, RAYWHITE);
    DrawText("Fly above y=120 to reach space", x + 14, y + 214, 16, RAYWHITE);
    DrawText("Ship: RMB enter, W/S/A/D thrust, E exit", x + 14, y + 234, 16, RAYWHITE);
    DrawText("Flat: I import image, [ ] adjusts precision", x + 14, y + 214, 16, RAYWHITE);
    const char *mode = ShipIsDriving() ? "Ship" : (floating ? "Floating" : "Walking");
    DrawText(TextFormat("%s    %s    View %d    FPS %d", mode,
                        cursorReleased ? "Mouse free" : "Mouse locked", viewDistance, GetFPS()),
             x + 14, y + 272, 16, Fade(RAYWHITE, 0.8f));
}

void DrawCursorReleasedOverlay(void)
{
    const char *text = "Mouse released - press Tab to return";
    int fontSize = 20;
    int width = MeasureText(text, fontSize) + 28;
    int x = GetScreenWidth() / 2 - width / 2;
    int y = GetScreenHeight() - 132;
    Rectangle rect = { (float)x, (float)y, (float)width, 46.0f };
    DrawRectangleRounded(rect, 0.08f, 8, Fade(BLACK, 0.58f));
    DrawRectangleRoundedLinesEx(rect, 0.08f, 8, 1.5f, Fade(WHITE, 0.42f));
    DrawText(text, x + 14, y + 13, fontSize, WHITE);
}

void DrawImportStatus(void)
{
    float timer = WorldGetImportMessageTimer();
    if (timer <= 0.0f) return;

    const char *message = WorldGetImportMessage();
    int fontSize = 18;
    int padding = 12;
    int width = MeasureText(message, fontSize) + padding * 2;
    int x = GetScreenWidth() / 2 - width / 2;
    int y = 18;
    Rectangle rect = { (float)x, (float)y, (float)width, 42.0f };
    DrawRectangleRounded(rect, 0.08f, 8, Fade(BLACK, 0.54f));
    DrawRectangleRoundedLinesEx(rect, 0.08f, 8, 1.5f, Fade(WHITE, 0.38f));
    DrawText(message, x + padding, y + 12, fontSize, WHITE);
}

const char *VisiblePathTail(const char *path, int maxWidth, int fontSize)
{
    if (MeasureText(path, fontSize) <= maxWidth) return path;

    const char *tail = path;
    int available = maxWidth - MeasureText("...", fontSize);
    while (*tail && MeasureText(tail, fontSize) > available) tail++;
    return tail;
}

void DrawImportDialog(ImportDialog *dialog)
{
    if (!dialog->open) return;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    Rectangle panel = { sw / 2 - 360.0f, sh / 2 - 128.0f, 720.0f, 256.0f };
    Rectangle input = { panel.x + 30.0f, panel.y + 100.0f, panel.width - 60.0f, 46.0f };

    DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.42f));
    DrawRectangleRounded(panel, 0.04f, 8, (Color){ 30, 38, 45, 245 });
    DrawRectangleRoundedLinesEx(panel, 0.04f, 8, 2.0f, Fade(WHITE, 0.45f));

    DrawText("Import image as blocks", (int)panel.x + 30, (int)panel.y + 24, 28, WHITE);
    DrawText("Flat mode only. PNG, JPG, BMP, TGA, GIF, QOI, PSD, HDR.",
             (int)panel.x + 30, (int)panel.y + 62, 16, Fade(WHITE, 0.72f));

    DrawRectangleRounded(input, 0.05f, 8, (Color){ 15, 20, 25, 255 });
    DrawRectangleRoundedLinesEx(input, 0.05f, 8, 2.0f, (Color){ 98, 160, 115, 255 });

    const char *shown = dialog->path[0] ? VisiblePathTail(dialog->path, (int)input.width - 44, 20) : "";
    int textX = (int)input.x + 16;
    if (shown != dialog->path) {
        DrawText("...", textX, (int)input.y + 13, 20, Fade(WHITE, 0.85f));
        textX += MeasureText("...", 20);
    }
    DrawText(shown, textX, (int)input.y + 13, 20, dialog->path[0] ? WHITE : Fade(WHITE, 0.38f));

    if (((int)(GetTime() * 2.0) % 2) == 0) {
        int cursorX = textX + MeasureText(shown, 20) + 2;
        DrawLine(cursorX, (int)input.y + 11, cursorX, (int)input.y + 35, WHITE);
    }

    const char *modeText = TextFormat("Mode: %s  (Tab toggles)",
                                      dialog->relief ? "Grayscale relief" : "Flat color");
    int modeWidth = MeasureText(modeText, 18);
    DrawText(modeText, sw / 2 - modeWidth / 2, (int)panel.y + 140, 18, WHITE);

    Rectangle minusRect = { panel.x + 30.0f, panel.y + 166.0f, 44.0f, 36.0f };
    Rectangle plusRect = { panel.x + panel.width - 74.0f, panel.y + 166.0f, 44.0f, 36.0f };
    if (DrawMenuButton(minusRect, "-", false)) {
        dialog->maxBlocks = AdjustImportPrecision(dialog->maxBlocks, -IMPORT_PRECISION_STEP);
    }
    if (DrawMenuButton(plusRect, "+", false)) {
        dialog->maxBlocks = AdjustImportPrecision(dialog->maxBlocks, IMPORT_PRECISION_STEP);
    }

    const char *precisionText = TextFormat("Precision: max %d blocks per side (min 16)", dialog->maxBlocks);
    int precisionWidth = MeasureText(precisionText, 18);
    DrawText(precisionText, sw / 2 - precisionWidth / 2, (int)panel.y + 174, 18, WHITE);

    DrawText("Type path or Ctrl+V paste. Tab mode. [ ] adjusts. Enter imports. Esc cancels.",
             (int)panel.x + 30, (int)panel.y + 218, 16, Fade(WHITE, 0.76f));
}

void DrawDebugHUD(Vector3 playerPosition, float yaw, float pitch)
{
    int x = 18;
    int y = 76;
    int line = 20;
    int fs = 14;

    DrawText(TextFormat("XYZ %.1f %.1f %.1f   yaw %.1f pitch %.1f",
                        playerPosition.x, playerPosition.y, playerPosition.z, yaw * RAD2DEG, pitch * RAD2DEG),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    DrawText(TextFormat("FPS %d   frame %.2f ms", GetFPS(), GetFrameTime() * 1000.0f), x, y, fs, Fade(WHITE, 0.85f)); y += line;
    DrawText(TextFormat("Chunks loaded %d   gen queue %d   mesh queue %d",
                        GetActiveChunkCount(), GetPendingGenJobCount(), GetPendingMeshJobCount()),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    DrawText(TextFormat("Particles %d   edits %d   render dist %d",
                        ParticlesActiveCount(), WorldGetEditCount(), renderDistanceChunks),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    DrawText(TextFormat("Space %d/%d   nether %d   entities %d",
                        GetActiveSpaceChunkCount(), SpaceEditCountForHud,
                        GetActiveNetherChunkCount(), GetActiveEntityCount()),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    DrawText(TextFormat("Weather %s   time %02d:00   auto-save %s",
                        WeatherName(), (int)(dayTimeForHud * 24.0f) % 24,
                        autoSaveForHud ? "on" : "off"),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    SolarSystemDef hudSystem;
    float hudSystemDist = 0.0f;
    if (FindNearestSystem(playerPosition, 9000.0f, &hudSystem, &hudSystemDist)) {
        DrawText(TextFormat("System %s Prime (%.0f)", hudSystem.name, hudSystemDist),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    } else {
        DrawText("Deep space", x, y, fs, Fade(WHITE, 0.85f)); y += line;
    }
    DrawText(TextFormat("Block %s   music %s", BlockName(blockForHud),
                        AudioIsMusicEnabled() ? "on" : "off"),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    if (ShipIsDriving()) {
        DrawText(TextFormat("Ship speed %.1f blocks/s", shipSpeedForHud), x, y, fs, Fade(WHITE, 0.85f)); y += line;
    }
}

void DrawPauseMenu(bool *resume, bool *saveWorld, bool *saveAndQuit,
                   bool *toggleMusic, bool *returnToMenu)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.55f));

    Rectangle panel = { sw / 2 - 210.0f, sh / 2 - 185.0f, 420.0f, 370.0f };
    DrawRectangleRounded(panel, 0.05f, 8, (Color){ 30, 38, 45, 245 });
    DrawRectangleRoundedLinesEx(panel, 0.05f, 8, 2.0f, Fade(WHITE, 0.45f));

    DrawCenteredText("Paused", sh / 2 - 155, 38, WHITE);
    DrawCenteredText("The world is frozen. Day/night paused.", sh / 2 - 110, 16, Fade(WHITE, 0.72f));

    Rectangle resumeRect = { sw / 2 - 130.0f, sh / 2 - 74.0f, 260.0f, 48.0f };
    Rectangle saveRect = { sw / 2 - 130.0f, sh / 2 - 14.0f, 260.0f, 48.0f };
    Rectangle musicRect = { sw / 2 - 130.0f, sh / 2 + 46.0f, 260.0f, 48.0f };
    Rectangle menuRect = { sw / 2 - 130.0f, sh / 2 + 106.0f, 260.0f, 48.0f };
    Rectangle quitRect = { sw / 2 - 130.0f, sh / 2 + 166.0f, 260.0f, 48.0f };

    if (DrawMenuButton(resumeRect, "Resume (Esc)", true)) *resume = true;
    if (DrawMenuButton(saveRect, "Save World", false)) *saveWorld = true;
    if (DrawMenuButton(musicRect, TextFormat("Music: %s", AudioIsMusicEnabled() ? "On" : "Off"), false)) *toggleMusic = true;
    if (DrawMenuButton(menuRect, "Return to Menu", false)) *returnToMenu = true;
    if (DrawMenuButton(quitRect, "Save & Quit", false)) *saveAndQuit = true;

    DrawText(TextFormat("Volume: %d%%   (- / + to adjust)", (int)(GetMasterVolume() * 100.0f)),
             (int)panel.x + 30, (int)(panel.y + panel.height - 34), 16, Fade(WHITE, 0.72f));
}

