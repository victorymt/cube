#include "render.h"

#include "raymath.h"
#include "chunks.h"
#include "inventory.h"
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
float shipHudSpeed = 0.0f;
float shipHudAlt = 0.0f;
float shipHudHeading = 0.0f;
char shipHudSystem[48] = "---";
bool shipHudCruising = false;
bool shipHudNearPlanet = false;

#define STAR_SKY_RANGE (STAR_NAVIGATION_RANGE * 4.0f)
#define STAR_SKY_REFRESH_DISTANCE 4200.0f

static SolarSystemDef skySystems[STAR_SYSTEM_QUERY_MAX];
static int skySystemCount = 0;
static Vector3 skySystemCenter = { 0 };
static bool skySystemCacheValid = false;
static uint32_t skySystemWorldSeed = 0;

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

void ApplyPlanetWorldPalette(Color *top, Color *horizon, Color *worldTint)
{
    if (!PlanetWorldIsActive()) return;

    const PlanetProfile *profile = PlanetWorldProfile();
    Color planetTop = { 40, 70, 110, 255 };
    Color planetHorizon = { 120, 150, 180, 255 };
    Color planetLight = WHITE;
    switch (PlanetWorldStyle()) {
    case SOLAR_STYLE_LAVA:
        planetTop = (Color){ 52, 12, 14, 255 };
        planetHorizon = (Color){ 196, 58, 24, 255 };
        planetLight = (Color){ 255, 150, 112, 255 };
        break;
    case SOLAR_STYLE_ICE:
        planetTop = (Color){ 68, 116, 154, 255 };
        planetHorizon = (Color){ 196, 226, 238, 255 };
        planetLight = (Color){ 196, 226, 255, 255 };
        break;
    case SOLAR_STYLE_DESERT:
        planetTop = (Color){ 118, 74, 48, 255 };
        planetHorizon = (Color){ 226, 164, 94, 255 };
        planetLight = (Color){ 255, 214, 160, 255 };
        break;
    case SOLAR_STYLE_GAS:
        planetTop = (Color){ 74, 48, 104, 255 };
        planetHorizon = (Color){ 182, 132, 190, 255 };
        planetLight = (Color){ 222, 186, 255, 255 };
        break;
    case SOLAR_STYLE_CRATER:
        planetTop = (Color){ 28, 30, 40, 255 };
        planetHorizon = (Color){ 94, 92, 104, 255 };
        planetLight = (Color){ 184, 188, 204, 255 };
        break;
    case SOLAR_STYLE_TEMPERATE:
        planetTop = (Color){ 42, 105, 164, 255 };
        planetHorizon = (Color){ 168, 208, 226, 255 };
        planetLight = (Color){ 232, 242, 224, 255 };
        break;
    default:
        break;
    }

    float atmosphere = Clamp(profile->atmosphereDensity, 0.0f, 1.0f);
    float skyBlend = 0.28f + atmosphere * 0.58f;
    *top = ColorLerp(*top, planetTop, skyBlend);
    *horizon = ColorLerp(*horizon, planetHorizon, skyBlend * 0.92f);
    *worldTint = ColorLerp(*worldTint, planetLight, 0.16f + atmosphere * 0.24f);
    if (profile->atmosphereType == PLANET_ATMOSPHERE_NONE) {
        *top = ColorLerp(*top, BLACK, 0.78f);
        *horizon = ColorLerp(*horizon, BLACK, 0.68f);
    }
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
    UpdateMeteors(spaceFade, sw, sh);

    DrawNebulae(camera, spaceFade);

    DrawRectangleGradientV(0, sh / 2 - 90, sw, 90,
                           (Color){ 205, 205, 235, (unsigned char)(24.0f * spaceFade) }, BLANK);
    DrawRectangleGradientV(0, sh / 2 - 30, sw, 70,
                           (Color){ 175, 185, 230, (unsigned char)(16.0f * spaceFade) }, BLANK);

}

#define STAR_SHELL_DISTANCE 500.0f

static void RefreshSkySystems(Vector3 observer)
{
    uint32_t worldSeed = WorldGetSeed();
    Vector3 delta = Vector3Subtract(observer, skySystemCenter);
    delta.y = 0.0f;
    if (skySystemCacheValid && skySystemWorldSeed == worldSeed &&
        Vector3LengthSqr(delta) < STAR_SKY_REFRESH_DISTANCE * STAR_SKY_REFRESH_DISTANCE) {
        return;
    }

    skySystemCount = StarSystemsNear(observer, STAR_SKY_RANGE, skySystems,
                                     STAR_SYSTEM_QUERY_MAX);
    skySystemCenter = observer;
    skySystemWorldSeed = worldSeed;
    skySystemCacheValid = true;
}

void DrawStars(const Camera3D *camera, float daylight)
{
    if (daylight > 0.15f) return;

    float visibility = (0.15f - daylight) / 0.15f;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float time = GetTime();
    bool planetSurface = PlanetWorldIsActive();
    bool surfaceActive = HomeWorldSurfaceIsActive() || planetSurface;
    Vector3 observer = planetSurface ? PlanetWorldSpaceReference() : camera->position;
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));

    RefreshSkySystems(observer);
    SolarSystemDef host = { 0 };
    bool haveHost = surfaceActive && SurfaceHostSystem(&host);

    for (int i = 0; i < skySystemCount; i++) {
        const SolarSystemDef *system = &skySystems[i];
        if (haveHost && system->anchorX == host.anchorX &&
            system->anchorZ == host.anchorZ) {
            continue;
        }

        Vector3 toStar = Vector3Subtract(system->center, observer);
        float distance = Vector3Length(toStar);
        if (distance < 0.01f) continue;
        if (!surfaceActive && distance < 700.0f) continue;

        Vector3 dir = Vector3Scale(toStar, 1.0f / distance);
        if (planetSurface) dir = PlanetWorldSkyDirection(dir);
        if (surfaceActive && dir.y < -0.05f) continue;
        if (Vector3DotProduct(dir, forward) <= 0.01f) continue;

        Vector3 pos = Vector3Add(camera->position, Vector3Scale(dir, STAR_SHELL_DISTANCE));
        Vector2 screen = GetWorldToScreen(pos, *camera);
        if (screen.x < -20.0f || screen.x > (float)sw + 20.0f ||
            screen.y < -20.0f || screen.y > (float)sh + 20.0f) continue;

        unsigned int hash = WorldHash2D(system->anchorX, system->anchorZ);
        float phase = (float)(hash % 6283u) / 1000.0f;
        float twinkle = 0.72f + 0.28f * sinf(time * 1.35f + phase);
        float distanceFade = 1.0f - 0.58f * Clamp(distance / STAR_SKY_RANGE,
                                                   0.0f, 1.0f);
        unsigned char alpha = (unsigned char)(visibility * 235.0f * twinkle * distanceFade);
        Color color = SpectrumColor(system->spectrum);
        color.a = alpha;
        float size = 1.0f + (float)(hash % 5u) * 0.18f;
        if (system->spectrum == SPECTRUM_RED_GIANT) size += 0.55f;
        bool bright = (hash % 17u) == 0u;

        if (bright) {
            DrawCircle((int)screen.x, (int)screen.y, 2.6f, color);
            DrawLine((int)screen.x - 6, (int)screen.y, (int)screen.x + 6, (int)screen.y, Fade(color, 0.35f));
            DrawLine((int)screen.x, (int)screen.y - 6, (int)screen.x, (int)screen.y + 6, Fade(color, 0.35f));
        } else {
            DrawCircle((int)screen.x, (int)screen.y, size, color);
        }
    }
}

void DrawCelestial(const Camera3D *camera, float currentDayTime, float daylight)
{
    float theta = (currentDayTime - 0.25f) * (2.0f * PI);
    Vector3 sunDir = Vector3Normalize((Vector3){ cosf(theta), sinf(theta), 0.18f });
    Vector3 moonDir = Vector3Negate(sunDir);
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
    SolarSystemDef host = { 0 };
    Color sunColor = (Color){ 255, 214, 120, 255 };
    if (SurfaceHostSystem(&host)) sunColor = SpectrumColor(host.spectrum);

    if (sinf(theta) > 0.0f && Vector3DotProduct(sunDir, forward) > 0.05f) {
        Vector3 sunPos = Vector3Add(camera->position, Vector3Scale(sunDir, SUN_DISTANCE));
        Vector2 sunScreen = GetWorldToScreen(sunPos, *camera);
        float glowRadius = 28.0f + daylight * 24.0f;
        DrawCircleGradient((int)sunScreen.x, (int)sunScreen.y, glowRadius,
                           Fade(sunColor, 0.28f), BLANK);
        DrawCircle((int)sunScreen.x, (int)sunScreen.y, 15.0f,
                   ColorLerp(sunColor, WHITE, 0.48f));
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

void DrawWorld(const Camera3D *camera, int effectiveRenderDistance, Color tint,
               bool drawSurfaceChunks, bool drawNetherChunks)
{
    if (drawSurfaceChunks) {
        for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
            Chunk *chunk = &chunks[i];
            if (!chunk->loaded) continue;
            if (!ChunkWithinDrawDistance(chunk, camera->position, effectiveRenderDistance)) continue;
            if (!ChunkIntersectsCameraView(chunk, camera)) continue;
            if (chunk->hasModel) DrawModel(chunk->model, Vector3Zero(), 1.0f, tint);
        }
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

    if (drawNetherChunks) {
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
    }

    BeginBlendMode(BLEND_ALPHA);
    if (drawSurfaceChunks) {
        for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
            Chunk *chunk = &chunks[i];
            if (!chunk->loaded) continue;
            if (!ChunkWithinDrawDistance(chunk, camera->position, effectiveRenderDistance)) continue;
            if (!ChunkIntersectsCameraView(chunk, camera)) continue;
            if (chunk->hasWaterModel) DrawModel(chunk->waterModel, Vector3Zero(), 1.0f, tint);
        }
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
    if (drawNetherChunks) {
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
    case SOLAR_STYLE_TEMPERATE: return (Color){ 74, 152, 104, 255 };
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

static bool FindSystemForGuide(Vector3 pos, SolarSystemDef *sys, float *dist)
{
    static const float probes[3] = { 3000.0f, 7000.0f, 14000.0f };
    for (int i = 0; i < 3; i++) {
        if (FindNearestSystem(pos, probes[i], sys, dist)) return true;
    }
    return false;
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
                DrawText(TextFormat("%s %c", bodies[i].name,
                                    'a' + (bodies[i].index > 0 ? bodies[i].index - 1 : 0)),
                         (int)px + 7, (int)py - 8, 15, Fade(WHITE, 0.85f * spaceFade));
            }
        }
    }

    Vector3 homeCenter = HomeWorldCenter();
    float homeDist = Vector3Distance(camera->position, homeCenter);
    if (homeDist > 90.0f) {
        Vector3 toHome = Vector3Subtract(homeCenter, camera->position);
        bool behind = Vector3DotProduct(toHome, forward) < 0.0f;
        Vector2 homeScreen = GetWorldToScreen(homeCenter, *camera);
        bool onScreen = !behind && homeScreen.x > -10.0f && homeScreen.x < (float)sw + 10.0f &&
                        homeScreen.y > -10.0f && homeScreen.y < (float)sh + 10.0f;
        Color homeColor = (Color){ 130, 202, 255, 255 };
        if (onScreen) {
            DrawCircle((int)homeScreen.x, (int)homeScreen.y, 6.0f,
                       Fade(homeColor, 0.9f * spaceFade));
            DrawText(TextFormat("Homeworld - %.0f blocks", homeDist),
                     (int)homeScreen.x + 10, (int)homeScreen.y - 8, 15,
                     Fade(WHITE, 0.9f * spaceFade));
        } else {
            DrawEdgeIndicator(homeScreen.x, homeScreen.y, behind, camera->position,
                              homeCenter, homeColor, spaceFade, "Homeworld");
        }
    }

    if (count == 0) {
        SolarSystemDef sys;
        float sysDist = 0.0f;
        if (FindSystemForGuide(camera->position, &sys, &sysDist)) {
            Vector3 toSys = Vector3Subtract(sys.center, camera->position);
            bool behind = Vector3DotProduct(toSys, forward) < 0.0f;
            Vector2 screen = GetWorldToScreen(sys.center, *camera);
            Color color = SpectrumColor(sys.spectrum);
            DrawEdgeIndicator(screen.x, screen.y, behind, camera->position, sys.center, color, spaceFade,
                              TextFormat("%s Prime", sys.name));
        }
    }
}

#define PLANET_TEXTURE_WIDTH 384
#define PLANET_TEXTURE_HEIGHT 192
#define PLANET_STYLE_COUNT 6
#define PLANET_STYLE_VARIANTS 3

typedef struct PlanetRenderResources {
    bool initialized;
    Model sphere;
    Texture2D home;
    Texture2D clouds;
    Texture2D atmosphereGlow;
    Texture2D styles[PLANET_STYLE_COUNT][PLANET_STYLE_VARIANTS];
} PlanetRenderResources;

static PlanetRenderResources planetRender = { 0 };

static uint32_t PlanetTextureHash(int x, int y, int z, uint32_t seed)
{
    uint32_t hash = seed;
    hash ^= (uint32_t)x * 0x8da6b343u;
    hash ^= (uint32_t)y * 0xd8163841u;
    hash ^= (uint32_t)z * 0xcb1ab31fu;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    return hash ^ (hash >> 16);
}

static float PlanetHashUnit(int x, int y, int z, uint32_t seed)
{
    return (float)(PlanetTextureHash(x, y, z, seed) & 0x00ffffffu) / 16777215.0f;
}

static float PlanetNoiseSmooth(float value)
{
    return value * value * (3.0f - 2.0f * value);
}

static float PlanetValueNoise(float x, float y, float z, uint32_t seed)
{
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int z0 = (int)floorf(z);
    float tx = PlanetNoiseSmooth(x - (float)x0);
    float ty = PlanetNoiseSmooth(y - (float)y0);
    float tz = PlanetNoiseSmooth(z - (float)z0);

    float x00 = Lerp(PlanetHashUnit(x0, y0, z0, seed),
                     PlanetHashUnit(x0 + 1, y0, z0, seed), tx);
    float x10 = Lerp(PlanetHashUnit(x0, y0 + 1, z0, seed),
                     PlanetHashUnit(x0 + 1, y0 + 1, z0, seed), tx);
    float x01 = Lerp(PlanetHashUnit(x0, y0, z0 + 1, seed),
                     PlanetHashUnit(x0 + 1, y0, z0 + 1, seed), tx);
    float x11 = Lerp(PlanetHashUnit(x0, y0 + 1, z0 + 1, seed),
                     PlanetHashUnit(x0 + 1, y0 + 1, z0 + 1, seed), tx);
    return Lerp(Lerp(x00, x10, ty), Lerp(x01, x11, ty), tz);
}

static float PlanetFractalNoise(float x, float y, float z, uint32_t seed)
{
    float value = 0.0f;
    float amplitude = 0.55f;
    float total = 0.0f;
    for (int octave = 0; octave < 4; octave++) {
        value += PlanetValueNoise(x, y, z, seed + (uint32_t)octave * 1013u) * amplitude;
        total += amplitude;
        x = x * 2.03f + 7.1f;
        y = y * 2.03f - 3.7f;
        z = z * 2.03f + 5.3f;
        amplitude *= 0.5f;
    }
    return value / total;
}

static unsigned char PlanetColorChannel(float value)
{
    return (unsigned char)Clamp(value, 0.0f, 255.0f);
}

static Color ShadePlanetColor(Color color, float shade)
{
    return (Color){
        PlanetColorChannel((float)color.r * shade),
        PlanetColorChannel((float)color.g * shade),
        PlanetColorChannel((float)color.b * shade),
        color.a
    };
}

static float PlanetBakedLight(float nx, float ny, float nz)
{
    float light = nx * -0.48f + ny * 0.20f + nz * 0.85f;
    return 0.42f + 0.58f * Clamp(light * 0.5f + 0.5f, 0.0f, 1.0f);
}

static Color HomePlanetPixel(float nx, float ny, float nz, uint32_t seed,
                             float oceanCoverage)
{
    float continents = PlanetFractalNoise(nx * 2.15f, ny * 2.15f, nz * 2.15f, seed);
    float detail = PlanetFractalNoise(nx * 6.0f, ny * 6.0f, nz * 6.0f, seed + 71u);
    float latitude = fabsf(ny);
    float coast = 0.395f + Clamp(oceanCoverage, 0.0f, 0.8f) * 0.25f +
                  latitude * 0.035f;
    Color color;

    if (continents < coast) {
        float depth = Clamp((coast - continents) * 6.0f, 0.0f, 1.0f);
        color = ColorLerp((Color){ 35, 139, 176, 255 },
                          (Color){ 9, 43, 103, 255 }, depth * 0.82f);
    } else {
        float height = Clamp((continents - coast) * 4.6f + detail * 0.18f, 0.0f, 1.0f);
        Color lowland = latitude > 0.63f ? (Color){ 88, 119, 78, 255 }
                                          : (Color){ 48, 126, 66, 255 };
        color = ColorLerp(lowland, (Color){ 126, 112, 82, 255 }, height);
        if (height > 0.86f) {
            color = ColorLerp(color, (Color){ 193, 201, 198, 255 },
                              (height - 0.86f) / 0.14f);
        }
    }

    float iceEdge = 0.80f + (detail - 0.5f) * 0.10f;
    if (latitude > iceEdge) {
        float ice = Clamp((latitude - iceEdge) / 0.15f, 0.0f, 1.0f);
        color = ColorLerp(color, (Color){ 220, 240, 244, 255 }, ice);
    }
    return ShadePlanetColor(color, PlanetBakedLight(nx, ny, nz));
}

static Color PlanetCloudPixel(float nx, float ny, float nz, uint32_t seed)
{
    float clouds = PlanetFractalNoise(nx * 4.2f + ny * 0.7f,
                                      ny * 3.1f, nz * 4.2f - ny * 0.7f, seed);
    float wisps = 0.5f + 0.5f * sinf((nx - nz) * 13.0f + ny * 21.0f);
    clouds = clouds * 0.82f + wisps * 0.18f;
    float opacity = Clamp((clouds - 0.58f) * 4.0f, 0.0f, 0.58f);
    float shade = 0.78f + PlanetBakedLight(nx, ny, nz) * 0.22f;
    return (Color){ PlanetColorChannel(246.0f * shade),
                    PlanetColorChannel(250.0f * shade),
                    PlanetColorChannel(255.0f * shade),
                    PlanetColorChannel(opacity * 255.0f) };
}

static Color CraterPlanetPixel(float nx, float ny, float nz, float noise, uint32_t seed)
{
    static const Vector3 centers[] = {
        { 1.0f, 0.0f, 0.0f }, { -0.60f, 0.42f, 0.68f },
        { 0.18f, -0.82f, 0.54f }, { 0.44f, 0.76f, -0.47f },
        { -0.22f, -0.31f, -0.92f }, { 0.72f, -0.48f, -0.50f },
        { -0.82f, -0.54f, 0.20f }, { -0.35f, 0.86f, 0.36f }
    };
    float tone = 82.0f + noise * 64.0f;
    Vector3 point = { nx, ny, nz };
    for (int i = 0; i < (int)(sizeof(centers) / sizeof(centers[0])); i++) {
        Vector3 center = Vector3Normalize(centers[i]);
        float radial = sqrtf(fmaxf(0.0f, 2.0f * (1.0f - Vector3DotProduct(point, center))));
        float radius = 0.075f + 0.025f * (float)((i + (int)(seed & 3u)) % 4);
        if (radial < radius) tone -= 30.0f * (1.0f - radial / radius);
        else if (radial < radius * 1.16f) tone += 36.0f * (1.0f - (radial - radius) / (radius * 0.16f));
    }
    return (Color){ PlanetColorChannel(tone * 0.94f),
                    PlanetColorChannel(tone * 0.96f),
                    PlanetColorChannel(tone), 255 };
}

static Color StyledPlanetPixel(SolarBodyStyle style, float nx, float ny, float nz,
                               float u, float v, uint32_t seed, float oceanCoverage)
{
    float noise = PlanetFractalNoise(nx * 3.8f, ny * 3.8f, nz * 3.8f, seed);
    float fine = PlanetFractalNoise(nx * 9.5f, ny * 9.5f, nz * 9.5f, seed + 139u);
    Color color = (Color){ 120, 120, 120, 255 };

    switch (style) {
    case SOLAR_STYLE_LAVA: {
        color = ColorLerp((Color){ 23, 18, 21, 255 }, (Color){ 82, 38, 25, 255 }, noise);
        float fissure = 1.0f - Clamp(fabsf(noise - 0.53f) / 0.045f, 0.0f, 1.0f);
        fissure = fmaxf(fissure, 1.0f - Clamp(fabsf(fine - 0.66f) / 0.022f, 0.0f, 1.0f));
        color = ColorLerp(color, (Color){ 255, 115, 18, 255 }, fissure);
        if (fissure > 0.72f) color = ColorLerp(color, (Color){ 255, 225, 88, 255 },
                                               (fissure - 0.72f) / 0.28f);
        break;
    }
    case SOLAR_STYLE_ICE: {
        color = ColorLerp((Color){ 78, 139, 176, 255 },
                          (Color){ 219, 240, 246, 255 }, noise * 0.82f + fabsf(ny) * 0.18f);
        float crevasse = 1.0f - Clamp(fabsf(fine - 0.50f) / 0.035f, 0.0f, 1.0f);
        color = ColorLerp(color, (Color){ 24, 76, 126, 255 }, crevasse * 0.72f);
        break;
    }
    case SOLAR_STYLE_DESERT: {
        float dunes = 0.5f + 0.5f * sinf((nx * 0.7f + nz) * 31.0f + noise * 7.0f);
        color = ColorLerp((Color){ 139, 72, 36, 255 },
                          (Color){ 238, 183, 91, 255 }, noise * 0.74f + dunes * 0.10f);
        if (fine > 0.72f) color = ColorLerp(color, (Color){ 91, 48, 37, 255 },
                                            (fine - 0.72f) * 2.2f);
        break;
    }
    case SOLAR_STYLE_GAS: {
        float bands = 0.5f + 0.5f * sinf(ny * 56.0f + noise * 7.0f);
        Color cool = ColorLerp((Color){ 76, 51, 109, 255 },
                               (Color){ 174, 105, 141, 255 }, noise);
        color = ColorLerp(cool, (Color){ 228, 181, 139, 255 }, bands * 0.48f);
        float stormU = 0.28f + PlanetHashUnit(3, 5, 7, seed) * 0.44f;
        float stormV = 0.38f + PlanetHashUnit(11, 2, 9, seed) * 0.24f;
        float du = fabsf(u - stormU);
        du = fminf(du, 1.0f - du);
        float ellipse = sqrtf((du * du) / (0.115f * 0.115f) +
                              ((v - stormV) * (v - stormV)) / (0.043f * 0.043f));
        if (ellipse < 1.0f) {
            float swirl = 0.5f + 0.5f * sinf(ellipse * 28.0f + noise * 8.0f);
            color = ColorLerp(color, (Color){ 242, 204, 174, 255 }, swirl * (1.0f - ellipse));
        }
        break;
    }
    case SOLAR_STYLE_CRATER:
        color = CraterPlanetPixel(nx, ny, nz, noise, seed);
        break;
    case SOLAR_STYLE_TEMPERATE:
        return HomePlanetPixel(nx, ny, nz, seed, oceanCoverage);
    default:
        break;
    }

    return ShadePlanetColor(color, PlanetBakedLight(nx, ny, nz));
}

static Texture2D MakePlanetTexture(SolarBodyStyle style, uint32_t seed, bool home,
                                   bool clouds, float oceanCoverage)
{
    size_t pixelCount = (size_t)PLANET_TEXTURE_WIDTH * PLANET_TEXTURE_HEIGHT;
    Color *pixels = malloc(pixelCount * sizeof(*pixels));
    if (!pixels) return (Texture2D){ 0 };

    for (int y = 0; y < PLANET_TEXTURE_HEIGHT; y++) {
        float v = (float)y / (float)(PLANET_TEXTURE_HEIGHT - 1);
        float latitude = (0.5f - v) * PI;
        float cosLatitude = cosf(latitude);
        for (int x = 0; x < PLANET_TEXTURE_WIDTH; x++) {
            float u = (float)x / (float)PLANET_TEXTURE_WIDTH;
            float longitude = u * 2.0f * PI;
            float nx = cosLatitude * cosf(longitude);
            float ny = sinf(latitude);
            float nz = cosLatitude * sinf(longitude);
            Color color;
            if (clouds) {
                color = PlanetCloudPixel(nx, ny, nz, seed);
            } else if (home) {
                color = HomePlanetPixel(nx, ny, nz, seed, oceanCoverage);
            } else {
                color = StyledPlanetPixel(style, nx, ny, nz, u, v, seed,
                                           oceanCoverage);
            }
            pixels[(size_t)y * PLANET_TEXTURE_WIDTH + x] = color;
        }
    }

    Image image = {
        .data = pixels,
        .width = PLANET_TEXTURE_WIDTH,
        .height = PLANET_TEXTURE_HEIGHT,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };
    Texture2D texture = LoadTextureFromImage(image);
    free(pixels);
    if (texture.id != 0) {
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
    }
    return texture;
}

static Texture2D MakeAtmosphereGlowTexture(void)
{
    const int size = 96;
    Color *pixels = malloc((size_t)size * size * sizeof(*pixels));
    if (!pixels) return (Texture2D){ 0 };
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float nx = ((float)x + 0.5f) * 2.0f / (float)size - 1.0f;
            float ny = ((float)y + 0.5f) * 2.0f / (float)size - 1.0f;
            float radius = sqrtf(nx * nx + ny * ny);
            float alpha = Clamp((1.0f - radius) / 0.30f, 0.0f, 1.0f);
            pixels[y * size + x] = (Color){ 255, 255, 255,
                                            PlanetColorChannel(alpha * 190.0f) };
        }
    }
    Image image = {
        .data = pixels,
        .width = size,
        .height = size,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };
    Texture2D texture = LoadTextureFromImage(image);
    free(pixels);
    if (texture.id != 0) SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    return texture;
}

static Vector3 PlanetSpherePoint(float u, float v)
{
    float longitude = u * 2.0f * PI;
    float latitude = (0.5f - v) * PI;
    float cosLatitude = cosf(latitude);
    return (Vector3){ cosLatitude * cosf(longitude), sinf(latitude),
                      cosLatitude * sinf(longitude) };
}

static Mesh MakePlanetSphereMesh(void)
{
    const int rings = 32;
    const int slices = 64;
    const int columns = slices + 1;
    Mesh mesh = { 0 };
    mesh.vertexCount = (rings + 1) * columns;
    mesh.triangleCount = rings * slices * 2;
    mesh.vertices = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.normals = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = malloc((size_t)mesh.vertexCount * 2 * sizeof(float));
    mesh.indices = malloc((size_t)mesh.triangleCount * 3 * sizeof(unsigned short));
    if (!mesh.vertices || !mesh.normals || !mesh.texcoords || !mesh.indices) {
        free(mesh.vertices);
        free(mesh.normals);
        free(mesh.texcoords);
        free(mesh.indices);
        return (Mesh){ 0 };
    }

    for (int ring = 0; ring <= rings; ring++) {
        float v = (float)ring / (float)rings;
        for (int slice = 0; slice <= slices; slice++) {
            float u = (float)slice / (float)slices;
            int vertex = ring * columns + slice;
            Vector3 point = PlanetSpherePoint(u, v);
            mesh.vertices[vertex * 3] = point.x;
            mesh.vertices[vertex * 3 + 1] = point.y;
            mesh.vertices[vertex * 3 + 2] = point.z;
            mesh.normals[vertex * 3] = point.x;
            mesh.normals[vertex * 3 + 1] = point.y;
            mesh.normals[vertex * 3 + 2] = point.z;
            mesh.texcoords[vertex * 2] = u;
            mesh.texcoords[vertex * 2 + 1] = v;
        }
    }

    int index = 0;
    for (int ring = 0; ring < rings; ring++) {
        for (int slice = 0; slice < slices; slice++) {
            unsigned short topLeft = (unsigned short)(ring * columns + slice);
            unsigned short topRight = (unsigned short)(topLeft + 1);
            unsigned short bottomLeft = (unsigned short)(topLeft + columns);
            unsigned short bottomRight = (unsigned short)(bottomLeft + 1);
            mesh.indices[index++] = topLeft;
            mesh.indices[index++] = bottomRight;
            mesh.indices[index++] = bottomLeft;
            mesh.indices[index++] = topLeft;
            mesh.indices[index++] = topRight;
            mesh.indices[index++] = bottomRight;
        }
    }
    UploadMesh(&mesh, false);
    return mesh;
}

static void EnsurePlanetRenderResources(void)
{
    if (planetRender.initialized) return;
    planetRender.initialized = true;
    Mesh sphereMesh = MakePlanetSphereMesh();
    if (sphereMesh.vertexCount > 0) planetRender.sphere = LoadModelFromMesh(sphereMesh);
    planetRender.home = MakePlanetTexture(SOLAR_STYLE_DESERT, 0x48a1c3u, true, false, 0.48f);
    planetRender.clouds = MakePlanetTexture(SOLAR_STYLE_ICE, 0x8392f5u, false, true, 0.0f);
    planetRender.atmosphereGlow = MakeAtmosphereGlowTexture();

    for (int style = 0; style < PLANET_STYLE_COUNT; style++) {
        for (int variant = 0; variant < PLANET_STYLE_VARIANTS; variant++) {
            uint32_t seed = 0x91e10da5u + (uint32_t)style * 0x1f123bb5u +
                            (uint32_t)variant * 0x6c8e9cf5u;
            float oceanCoverage = ((float)variant + 0.7f) * 0.24f;
            planetRender.styles[style][variant] =
                MakePlanetTexture((SolarBodyStyle)(SOLAR_STYLE_LAVA + style), seed,
                                  false, false, oceanCoverage);
        }
    }
}

void UnloadPlanetRenderResources(void)
{
    if (!planetRender.initialized) return;
    if (planetRender.sphere.meshCount > 0) UnloadModel(planetRender.sphere);
    if (planetRender.home.id != 0) UnloadTexture(planetRender.home);
    if (planetRender.clouds.id != 0) UnloadTexture(planetRender.clouds);
    if (planetRender.atmosphereGlow.id != 0) UnloadTexture(planetRender.atmosphereGlow);
    for (int style = 0; style < PLANET_STYLE_COUNT; style++) {
        for (int variant = 0; variant < PLANET_STYLE_VARIANTS; variant++) {
            if (planetRender.styles[style][variant].id != 0) {
                UnloadTexture(planetRender.styles[style][variant]);
            }
        }
    }
    planetRender = (PlanetRenderResources){ 0 };
}

static void DrawTexturedPlanet(Vector3 center, float radius, Texture2D texture,
                               float rotation, Color fallback)
{
    if (planetRender.sphere.meshCount <= 0 || texture.id == 0) {
        DrawSphere(center, radius, fallback);
        return;
    }
    SetMaterialTexture(&planetRender.sphere.materials[0], MATERIAL_MAP_DIFFUSE, texture);
    DrawModelEx(planetRender.sphere, center, (Vector3){ 0.0f, 1.0f, 0.0f }, rotation,
                (Vector3){ radius, radius, radius }, WHITE);
}

static uint32_t PlanetBodyVisualHash(const SpaceBodyInfo *body)
{
    return PlanetTextureHash((int)body->worldSeed, body->index,
                             body->systemAnchorX ^ body->systemAnchorZ, 0x57ec91u);
}

static Color PlanetAtmosphereColor(SolarBodyStyle style)
{
    switch (style) {
    case SOLAR_STYLE_LAVA:   return (Color){ 255, 86, 24, 255 };
    case SOLAR_STYLE_ICE:    return (Color){ 116, 214, 255, 255 };
    case SOLAR_STYLE_DESERT: return (Color){ 244, 170, 92, 255 };
    case SOLAR_STYLE_GAS:    return (Color){ 202, 142, 234, 255 };
    case SOLAR_STYLE_CRATER: return (Color){ 150, 162, 180, 255 };
    case SOLAR_STYLE_TEMPERATE: return (Color){ 116, 194, 236, 255 };
    default:                 return WHITE;
    }
}

static void DrawPlanetAtmosphere(const Camera3D *camera, Vector3 center, float radius,
                                 Color color, float alpha)
{
    if (planetRender.atmosphereGlow.id == 0 || alpha <= 0.0f) return;
    DrawBillboard(*camera, planetRender.atmosphereGlow, center, radius * 2.40f,
                  Fade(color, alpha));
}

static Vector3 PlanetRingPoint(Vector3 center, float radius, float angle, float tilt)
{
    float c = cosf(angle);
    float s = sinf(angle);
    float ct = cosf(tilt);
    float st = sinf(tilt);
    return (Vector3){ center.x + c * radius,
                      center.y - s * radius * st,
                      center.z + s * radius * ct };
}

static void DrawPlanetRingStrip(Vector3 center, float innerRadius, float outerRadius,
                                float tilt, Color color)
{
    const int segments = 72;
    for (int i = 0; i < segments; i++) {
        float a0 = (float)i * 2.0f * PI / (float)segments;
        float a1 = (float)(i + 1) * 2.0f * PI / (float)segments;
        Vector3 i0 = PlanetRingPoint(center, innerRadius, a0, tilt);
        Vector3 i1 = PlanetRingPoint(center, innerRadius, a1, tilt);
        Vector3 o0 = PlanetRingPoint(center, outerRadius, a0, tilt);
        Vector3 o1 = PlanetRingPoint(center, outerRadius, a1, tilt);
        DrawTriangle3D(i0, o0, o1, color);
        DrawTriangle3D(i0, o1, i1, color);
        DrawTriangle3D(i0, o1, o0, color);
        DrawTriangle3D(i0, i1, o1, color);
    }
}

static void DrawPlanetRings(Vector3 center, float radius, uint32_t hash, float alpha)
{
    float tilt = (14.0f + (float)(hash % 17u)) * DEG2RAD;
    DrawPlanetRingStrip(center, radius * 1.30f, radius * 1.43f, tilt,
                        Fade((Color){ 216, 189, 166, 255 }, 0.34f * alpha));
    DrawPlanetRingStrip(center, radius * 1.47f, radius * 1.72f, tilt,
                        Fade((Color){ 164, 137, 148, 255 }, 0.24f * alpha));
    DrawPlanetRingStrip(center, radius * 1.76f, radius * 1.86f, tilt,
                        Fade((Color){ 225, 210, 190, 255 }, 0.18f * alpha));
}

void DrawSolarBodies(const Camera3D *camera, float spaceFade)
{
    if (spaceFade <= 0.05f) return;

    EnsurePlanetRenderResources();

    SpaceBodyInfo bodies[48];
    int count = SpaceBodiesNear(camera->position, 700.0f, bodies, 48);
    for (int i = 0; i < count; i++) {
        Color color = bodies[i].isStar ? SpectrumColor(bodies[i].spectrum)
                                      : SolarStyleColor(bodies[i].style);
        if (bodies[i].isStar) {
            float radius = bodies[i].radius;
            DrawSphere(bodies[i].center, radius * 1.08f, color);
            DrawSphere(bodies[i].center, radius * 1.15f,
                       Fade(color, 0.12f * spaceFade));
        } else {
            float radius = SolarBodyTerrainRadius(bodies[i].radius);
            int styleIndex = (int)bodies[i].style - (int)SOLAR_STYLE_LAVA;
            uint32_t visualHash = PlanetBodyVisualHash(&bodies[i]);
            int variant = (int)(visualHash % PLANET_STYLE_VARIANTS);
            if (bodies[i].style == SOLAR_STYLE_TEMPERATE) {
                variant = (int)Clamp(floorf(bodies[i].profile.oceanCoverage *
                                            (float)PLANET_STYLE_VARIANTS),
                                     0.0f, (float)(PLANET_STYLE_VARIANTS - 1));
            }
            Texture2D texture = (Texture2D){ 0 };
            if (styleIndex >= 0 && styleIndex < PLANET_STYLE_COUNT) {
                texture = planetRender.styles[styleIndex][variant];
            }
            float spinRate = bodies[i].profile.rotationRate;
            float rotation = (float)((visualHash >> 8) % 360u) +
                             (float)SpaceSimulationTime() * spinRate;
            Color atmosphere = PlanetAtmosphereColor(bodies[i].style);
            float atmosphereAlpha = 0.04f + bodies[i].profile.atmosphereDensity * 0.54f;
            DrawPlanetAtmosphere(camera, bodies[i].center, radius, atmosphere,
                                 atmosphereAlpha * spaceFade);
            DrawTexturedPlanet(bodies[i].center, radius + 0.08f, texture, rotation, color);
            if (bodies[i].profile.hasRings) {
                DrawPlanetRings(bodies[i].center, radius, visualHash, spaceFade);
            }
        }
    }
}

static Color HomePlanetColor(void)
{
    if (terrainMode == TERRAIN_FLAT) return (Color){ 72, 138, 88, 255 };

    switch (BiomeAt(0, 0)) {
    case BIOME_DESERT:  return (Color){ 184, 140, 76, 255 };
    case BIOME_SNOW:    return (Color){ 158, 184, 210, 255 };
    case BIOME_MOUNTAIN: return (Color){ 116, 142, 158, 255 };
    case BIOME_FOREST:  return (Color){ 48, 116, 72, 255 };
    case BIOME_PLAINS:
    default:            return (Color){ 70, 142, 92, 255 };
    }
}

void DrawHomePlanet(const Camera3D *camera, float spaceFade)
{
    if (spaceFade <= 0.05f) return;

    const Vector3 center = HomeWorldCenter();
    const float radius = HomeWorldRadius();
    float distance = Vector3Distance(camera->position, center);
    if (distance <= radius + 0.5f || distance > 24000.0f) return;

    EnsurePlanetRenderResources();
    Color atmosphere = (Color){ 130, 202, 255, 255 };
    DrawPlanetAtmosphere(camera, center, radius, atmosphere, 0.62f * spaceFade);
    float homeRotation = -18.0f + (float)SpaceSimulationTime() * 1.2f;
    DrawTexturedPlanet(center, radius, planetRender.home, homeRotation, HomePlanetColor());
    DrawTexturedPlanet(center, radius * 1.012f, planetRender.clouds,
                       homeRotation + (float)SpaceSimulationTime() * 0.7f, WHITE);
}

void DrawBodyInfoPanel(const SpaceBodyInfo *body)
{
    if (!body) return;

    const char *typeName = body->isStar ? SpectrumName(body->spectrum) : SolarStyleName(body->style);
    const char *line1;
    const char *line2 = NULL;
    if (body->isStar) {
        line1 = TextFormat("%s Prime - %s - %.0f blocks", body->name, typeName, body->dist);
    } else {
        float surfaceGap = fabsf(body->dist - SolarBodyTerrainRadius(body->radius));
        line1 = TextFormat("%s %c - %s - %.0f K - %.2f g", body->name,
                           'a' + (body->index > 0 ? body->index - 1 : 0), typeName,
                           body->profile.equilibriumTempK, body->profile.surfaceGravity);
        if (!body->profile.hasSolidSurface) {
            line2 = "Dense gas envelope - no solid surface";
        } else if (ShipIsDriving() && surfaceGap <= 20.0f) {
            line2 = TextFormat("%s - E land",
                               PlanetAtmosphereName(body->profile.atmosphereType));
        } else {
            line2 = TextFormat("%s - %.0f blocks",
                               PlanetAtmosphereName(body->profile.atmosphereType), body->dist);
        }
    }

    int fs = 18;
    int width = MeasureText(line1, fs);
    if (line2) width = fmaxf((float)width, (float)MeasureText(line2, 16));
    int sw = GetScreenWidth();
    int x = sw / 2 - width / 2;
    int y = 64;
    float height = line2 ? 62.0f : 40.0f;
    DrawRectangleRounded((Rectangle){ (float)x - 16, (float)y - 8, (float)width + 32, height },
                         0.10f, 6, Fade(BLACK, 0.55f));
    DrawRectangleRoundedLinesEx((Rectangle){ (float)x - 16, (float)y - 8, (float)width + 32, height },
                                0.10f, 6, 1.5f, Fade(WHITE, 0.30f));
    DrawText(line1, x, y, fs, WHITE);
    if (line2) DrawText(line2, x, y + 24, 16, Fade(WHITE, 0.82f));
}

static void DrawUiText(const char *text, int x, int y, int fontSize, Color color)
{
    DrawText(text, x + 1, y + 2, fontSize, Fade(BLACK, 0.92f));
    DrawText(text, x, y, fontSize, color);
}

void DrawShipHud(void)
{
    int sw = GetScreenWidth();
    float panelWidth = fminf(480.0f, (float)sw - 36.0f);
    Rectangle panel = { (float)sw - panelWidth - 18.0f, 16.0f, panelWidth, 142.0f };
    DrawRectangleRounded(panel, 0.06f, 6, Fade(BLACK, 0.72f));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 6, 1.5f, Fade(WHITE, 0.30f));

    int textX = (int)panel.x + 16;
    bool warping = ShipIsWarping();
    Color speedColor = warping ? (Color){ 166, 228, 255, 255 } :
                       (shipHudCruising ? (Color){ 130, 200, 255, 255 } : WHITE);
    DrawUiText(TextFormat("VEL %.0f blk/s%s", shipHudSpeed,
                          warping ? "  [WARP]" : (shipHudCruising ? "  [CRUISE]" : "")),
               textX, (int)panel.y + 10, 22, speedColor);
    DrawUiText(TextFormat("ALT %.0f%s   HDG %03.0f", shipHudAlt,
                          shipHudNearPlanet ? " (surface)" : "", shipHudHeading),
               textX, (int)panel.y + 42, 18, Fade(WHITE, 0.95f));
    Color fuelColor = ShipGetFuel() > 20.0f ? (Color){ 255, 204, 94, 255 } : (Color){ 238, 100, 82, 255 };
    DrawUiText(TextFormat("FUEL %.0f / %.0f   R restore", ShipGetFuel(), SHIP_MAX_FUEL),
               textX, (int)panel.y + 68, 17, fuelColor);
    DrawUiText(TextFormat("SYS %s", shipHudSystem),
               textX, (int)panel.y + 94, 17, Fade(WHITE, 0.84f));
    if (ShipHasWarpTarget()) {
        const char *targetKind = ShipWarpTargetIsSystem() ? "SYS" : "PLANET";
        DrawUiText(TextFormat("%s %s %s", targetKind, warping ? "WARP" : "LOCK",
                              ShipWarpTargetName()),
                   textX, (int)panel.y + 118, 17, warping ? speedColor : Fade(WHITE, 0.84f));
    } else {
        DrawUiText("Q lock planet    G engage warp", textX, (int)panel.y + 118, 16,
                   Fade(WHITE, 0.68f));
    }
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
        int count = InventoryCount(block);
        const char *countText = TextFormat("%d", count);
        int countFont = count >= 100 ? 11 : 13;
        int countWidth = MeasureText(countText, countFont);
        DrawText(countText, (int)(rect.x + rect.width - 5.0f - (float)countWidth), (int)rect.y + 31, countFont,
                 count > 0 ? WHITE : Fade((Color){ 238, 100, 82, 255 }, 0.9f));
    }

    DrawText(TextFormat("%s  x%d", BlockName(hotbar[selectedIndex]), InventoryCount(hotbar[selectedIndex])),
             x0, y - 24, 18, WHITE);
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

void DrawStartPage(bool *startGame, bool *quitGame, TerrainMode *selectedTerrain,
                   uint32_t *selectedSeed)
{
    static char seedText[11] = { 0 };
    static uint32_t displayedSeed = 0;
    static bool seedFocused = false;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    if (seedText[0] == '\0' || (!seedFocused && displayedSeed != *selectedSeed)) {
        snprintf(seedText, sizeof(seedText), "%u", *selectedSeed);
        displayedSeed = *selectedSeed;
    }

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

    Rectangle seedRect = { sw / 2 - 252.0f, sh / 2 + 96.0f, 356.0f, 48.0f };
    Rectangle randomRect = { sw / 2 + 116.0f, sh / 2 + 96.0f, 136.0f, 48.0f };
    Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        seedFocused = CheckCollisionPointRec(mouse, seedRect);
    }

    if (seedFocused) {
        int codepoint = GetCharPressed();
        while (codepoint > 0) {
            size_t length = strlen(seedText);
            if (codepoint >= '0' && codepoint <= '9' && length < sizeof(seedText) - 1) {
                seedText[length] = (char)codepoint;
                seedText[length + 1] = '\0';
            }
            codepoint = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            size_t length = strlen(seedText);
            if (length > 0) seedText[length - 1] = '\0';
        }
    }

    char *seedEnd = NULL;
    unsigned long long parsedSeed = strtoull(seedText, &seedEnd, 10);
    bool validSeed = seedText[0] != '\0' && seedEnd && *seedEnd == '\0' &&
                     parsedSeed > 0 && parsedSeed <= UINT32_MAX;
    if (validSeed) {
        *selectedSeed = (uint32_t)parsedSeed;
        displayedSeed = *selectedSeed;
    }

    DrawText("World seed", (int)seedRect.x, (int)seedRect.y - 22, 16, Fade(WHITE, 0.78f));
    DrawRectangleRounded(seedRect, 0.07f, 8, (Color){ 28, 35, 42, 255 });
    DrawRectangleRoundedLinesEx(seedRect, 0.07f, 8, 2.0f,
                                validSeed ? (seedFocused ? WHITE : Fade(WHITE, 0.48f)) : (Color){ 230, 92, 82, 255 });
    DrawText(seedText, (int)seedRect.x + 15, (int)seedRect.y + 12, 22, WHITE);
    if (seedFocused) {
        int caretX = (int)seedRect.x + 15 + MeasureText(seedText, 22);
        DrawRectangle(caretX + 2, (int)seedRect.y + 11, 2, 25, Fade(WHITE, 0.9f));
    }
    if (DrawMenuButton(randomRect, "Random", false)) {
        uint32_t randomSeed = ((uint32_t)GetRandomValue(0, 0xffff) << 16) |
                              (uint32_t)GetRandomValue(0, 0xffff);
        if (randomSeed == 0) randomSeed = DEFAULT_WORLD_SEED;
        *selectedSeed = randomSeed;
        displayedSeed = randomSeed;
        snprintf(seedText, sizeof(seedText), "%u", randomSeed);
        validSeed = true;
        seedFocused = false;
    }

    Rectangle startRect = { sw / 2 - 130.0f, sh / 2 + 160.0f, 260.0f, 54.0f };
    Rectangle quitRect = { sw / 2 - 130.0f, sh / 2 + 226.0f, 260.0f, 48.0f };
    if (validSeed && (DrawMenuButton(startRect, "Start", true) || IsKeyPressed(KEY_ENTER))) *startGame = true;
    else if (!validSeed) DrawMenuButton(startRect, "Enter a valid seed", false);
    if (DrawMenuButton(quitRect, "Quit", false)) *quitGame = true;

    DrawCenteredText(TextFormat("Seed %u", *selectedSeed), sh - 32, 16, Fade(WHITE, 0.68f));
}

void DrawHelpPanel(bool floating, bool cursorReleased, int viewDistance)
{
    int x = 18;
    int y = 18;
    int w = 430;
    int h = 374;
    DrawRectangleRounded((Rectangle){ (float)x, (float)y, (float)w, (float)h }, 0.05f, 6, Fade(BLACK, 0.68f));
    DrawUiText("Voxelcraft", x + 14, y + 12, 24, WHITE);
    DrawUiText("WASD move    Shift sprint    Space jump/swim", x + 14, y + 48, 17, RAYWHITE);
    DrawUiText("LMB break    RMB place    MMB pick block", x + 14, y + 73, 17, RAYWHITE);
    DrawUiText("F float    Ctrl down (float)    Wheel hotbar", x + 14, y + 98, 17, RAYWHITE);
    DrawUiText("Tab mouse    M star map/warp    1-0 blocks    P album", x + 14, y + 123, 17, RAYWHITE);
    DrawUiText("RMB on placed album opens it", x + 14, y + 148, 17, RAYWHITE);
    DrawUiText("Esc pause    F6 day/night cycle", x + 14, y + 173, 17, RAYWHITE);
    DrawUiText("F4 view    F5 save    F9 load    F10 shot", x + 14, y + 198, 17, RAYWHITE);
    DrawUiText("Fly above y=120 to reach space", x + 14, y + 223, 17, RAYWHITE);
    DrawUiText("Break collects; place consumes blocks", x + 14, y + 248, 15, RAYWHITE);
    DrawUiText("Ship: RMB enter, Q lock planet, G warp/cancel", x + 14, y + 272, 15, RAYWHITE);
    DrawUiText("WASD thrust, R restore, E land/exit", x + 14, y + 296, 15, RAYWHITE);
    DrawUiText("Flat: I import image, [ ] adjusts precision", x + 14, y + 320, 15, RAYWHITE);
    const char *mode = ShipIsDriving() ? "Ship" : (floating ? "Floating" : "Walking");
    DrawUiText(TextFormat("%s    %s    View %d    FPS %d", mode,
                          cursorReleased ? "Mouse free" : "Mouse locked", viewDistance, GetFPS()),
               x + 14, y + 348, 16, Fade(RAYWHITE, 0.9f));
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
    if (FindSystemForGuide(playerPosition, &hudSystem, &hudSystemDist)) {
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
