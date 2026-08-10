#include "render.h"

#include "raymath.h"
#include "rlgl.h"
#include "chunks.h"
#include "inventory.h"
#include "world.h"
#include "interaction.h"
#include "planet_material.h"
#include "planet_renderer.h"
#include "planet_surface.h"
#include "terrain.h"
#include "particles.h"
#include "space.h"
#include "space_units.h"
#include "world_environment.h"
#include "nether.h"
#include "entity.h"
#include "ship.h"
#include "audio.h"
#include "weather.h"
#include "ecology.h"

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
float shipHudAtmosphere = -1.0f;
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
static float planetSceneExposure = 1.12f;
static bool planetExposureInitialized = false;

static float PlanetExposureBrightness(float irradiance)
{
    return 1.0f - expf(-fmaxf(irradiance, 0.0f) * planetSceneExposure);
}

void UpdatePlanetSceneExposure(const Camera3D *camera)
{
    if (!camera) return;

    Vector3 observer = camera->position;
    SolarSystemDef system = { 0 };
    bool haveSystem = false;
    if (PlanetWorldIsActive()) {
        observer = PlanetWorldSpaceReference();
        haveSystem = SurfaceHostSystem(&system);
    } else if (HomeWorldSurfaceIsActive()) {
        observer = HomeWorldCenter();
        haveSystem = StarSystemAt(0, 0, &system);
    } else {
        haveSystem = FindNearestSystem(observer, STAR_SYSTEM_SPACING * 0.90f,
                                       &system, NULL);
    }

    float irradiance = 0.02f;
    if (haveSystem) {
        SolarLightSource sources[MAX_SOLAR_LIGHTS];
        int sourceCount = SolarSystemLightSources(&system, sources, MAX_SOLAR_LIGHTS);
        irradiance = SolarSystemIrradianceAt(sources, sourceCount, observer);
    }

    // Keep some physical contrast: exposure compensates slowly and only partially,
    // so distant worlds remain cold and dim while close worlds do not clip white.
    float target = 1.12f * powf(fmaxf(irradiance, 0.02f), -0.16f);
    target = Clamp(target, 0.45f, 1.55f);
    if (!planetExposureInitialized) {
        planetSceneExposure = target;
        planetExposureInitialized = true;
        return;
    }

    float dt = Clamp(GetFrameTime(), 0.0f, 0.12f);
    float relativeDelta = fabsf(logf(fmaxf(target, 0.001f) /
                                    fmaxf(planetSceneExposure, 0.001f)));
    if (relativeDelta <= 0.02f || dt <= 0.0f) return;
    float response = target < planetSceneExposure ? 0.30f : 0.16f;
    float blend = 1.0f - expf(-response * dt);
    planetSceneExposure += (target - planetSceneExposure) * blend;
}

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
    if (!HomeWorldSurfaceIsActive() && !PlanetWorldIsActive()) return color;

    float factor = WeatherSkyFactor();
    if (factor <= 0.0f) return color;

    Color overcast = WeatherGetCurrent() == WEATHER_SNOW ?
                     (Color){ 168, 180, 196, 255 } : (Color){ 84, 96, 118, 255 };
    factor *= 0.35f + 0.65f * daylight;
    return ColorLerp(color, overcast, factor);
}

typedef struct PlanetAtmosphereVisual {
    Color zenith;
    Color horizon;
    Color haze;
    Color groundLight;
    float opticalDepth;
    float mieStrength;
    float scaleHeight;
} PlanetAtmosphereVisual;

static Color PlanetAtmosphereBaseColor(SolarBodyStyle style)
{
    switch (style) {
    case SOLAR_STYLE_LAVA:      return (Color){ 232, 88, 42, 255 };
    case SOLAR_STYLE_ICE:       return (Color){ 116, 196, 232, 255 };
    case SOLAR_STYLE_DESERT:    return (Color){ 222, 154, 88, 255 };
    case SOLAR_STYLE_GAS:       return (Color){ 178, 126, 210, 255 };
    case SOLAR_STYLE_CRATER:    return (Color){ 132, 148, 174, 255 };
    case SOLAR_STYLE_TEMPERATE: return (Color){ 82, 154, 218, 255 };
    default:                    return (Color){ 150, 174, 204, 255 };
    }
}

static PlanetAtmosphereVisual PlanetAtmosphereVisualFor(const PlanetProfile *profile)
{
    PlanetAtmosphereVisual visual = { 0 };
    if (!profile) return visual;

    float density = Clamp(profile->atmosphereDensity, 0.0f, 1.0f);
    float gravity = fmaxf(profile->surfaceGravity, 0.20f);
    float temperature = fmaxf(profile->equilibriumTempK, 80.0f);
    visual.scaleHeight = Clamp((temperature / 288.0f) / gravity, 0.52f, 1.85f);
    Color base = PlanetAtmosphereBaseColor(profile->style);

    switch (profile->atmosphereType) {
    case PLANET_ATMOSPHERE_NONE:
        visual.zenith = (Color){ 2, 4, 10, 255 };
        visual.horizon = (Color){ 6, 8, 14, 255 };
        visual.haze = (Color){ 20, 24, 34, 255 };
        visual.groundLight = (Color){ 188, 194, 210, 255 };
        visual.opticalDepth = 0.0f;
        visual.mieStrength = 0.0f;
        break;
    case PLANET_ATMOSPHERE_THIN:
        visual.zenith = ColorLerp((Color){ 6, 9, 18, 255 }, base, 0.28f);
        visual.horizon = ColorLerp(base, WHITE, 0.40f);
        visual.haze = ColorLerp(base, WHITE, 0.24f);
        visual.groundLight = ColorLerp(WHITE, base, 0.18f);
        visual.opticalDepth = 0.12f + density * 0.42f;
        visual.mieStrength = 0.14f + density * 0.22f;
        break;
    case PLANET_ATMOSPHERE_BREATHABLE:
        base = ColorLerp(base, (Color){ 74, 148, 222, 255 }, 0.58f);
        visual.zenith = ColorLerp((Color){ 18, 58, 126, 255 }, base, 0.28f);
        visual.horizon = ColorLerp((Color){ 174, 210, 236, 255 }, base, 0.24f);
        visual.haze = ColorLerp((Color){ 188, 216, 234, 255 }, base, 0.30f);
        visual.groundLight = ColorLerp(WHITE, base, 0.12f);
        visual.opticalDepth = 0.52f + density * 0.42f;
        visual.mieStrength = 0.38f + density * 0.18f;
        break;
    case PLANET_ATMOSPHERE_CORROSIVE:
        base = ColorLerp(base, (Color){ 218, 172, 56, 255 }, 0.56f);
        visual.zenith = ColorLerp((Color){ 54, 38, 24, 255 }, base, 0.58f);
        visual.horizon = ColorLerp(base, (Color){ 255, 214, 104, 255 }, 0.42f);
        visual.haze = ColorLerp(base, (Color){ 244, 198, 102, 255 }, 0.32f);
        visual.groundLight = ColorLerp((Color){ 255, 232, 176, 255 }, base, 0.30f);
        visual.opticalDepth = 0.66f + density * 0.48f;
        visual.mieStrength = 0.68f + density * 0.20f;
        break;
    case PLANET_ATMOSPHERE_DENSE:
    default:
        visual.zenith = ColorLerp((Color){ 28, 38, 62, 255 }, base, 0.66f);
        visual.horizon = ColorLerp(base, (Color){ 235, 220, 205, 255 }, 0.38f);
        visual.haze = ColorLerp(base, WHITE, 0.28f);
        visual.groundLight = ColorLerp(WHITE, base, 0.30f);
        visual.opticalDepth = 0.74f + density * 0.44f;
        visual.mieStrength = 0.58f + density * 0.26f;
        break;
    }

    visual.opticalDepth = Clamp(visual.opticalDepth *
                                (0.76f + visual.scaleHeight * 0.24f) +
                                profile->greenhouseEffect * 0.16f,
                                0.0f, 1.35f);
    visual.mieStrength = Clamp(visual.mieStrength + profile->greenhouseEffect * 0.12f,
                               0.0f, 1.25f);
    return visual;
}

void ApplyPlanetWorldPaletteWithLight(Color *top, Color *horizon, Color *worldTint,
                                      const PlanetLightState *light)
{
    if (!PlanetWorldIsActive()) return;

    const PlanetProfile *profile = PlanetWorldProfile();
    PlanetAtmosphereVisual visual = PlanetAtmosphereVisualFor(profile);
    Color starColor = light && light->sourceCount > 0 ? light->starColor : WHITE;
    float daylight = light ? Clamp(light->daylight, 0.0f, 1.0f) : 1.0f;
    float sunset = light ? Clamp(light->sunset, 0.0f, 1.0f) : 0.0f;

    if (profile->atmosphereType == PLANET_ATMOSPHERE_NONE) {
        *top = ColorLerp(*top, visual.zenith, 0.94f);
        *horizon = ColorLerp(*horizon, visual.horizon, 0.90f);
        *worldTint = ColorLerp(*worldTint, starColor, 0.10f);
        return;
    }

    float daylightResponse = 0.24f + daylight * 0.76f;
    float topBlend = Clamp(0.12f + visual.opticalDepth * 0.58f * daylightResponse,
                           0.0f, 0.88f);
    float horizonBlend = Clamp(0.20f + visual.opticalDepth * 0.66f * daylightResponse,
                               0.0f, 0.94f);
    Color litHorizon = ColorLerp(visual.horizon, starColor, 0.18f);
    Color atmosphereLight = ColorLerp(visual.groundLight, starColor, 0.22f);
    *top = ColorLerp(*top, visual.zenith, topBlend);
    *horizon = ColorLerp(*horizon, litHorizon, horizonBlend);
    *worldTint = ColorLerp(*worldTint, atmosphereLight,
                           Clamp(visual.opticalDepth * (0.10f + daylight * 0.18f),
                                 0.0f, 0.34f));

    Color sunsetColor = ColorLerp((Color){ 255, 104, 44, 255 }, starColor, 0.22f);
    float sunsetStrength = Clamp(sunset * visual.mieStrength * 0.82f, 0.0f, 0.76f);
    *horizon = ColorLerp(*horizon, sunsetColor, sunsetStrength);
    *top = ColorLerp(*top, sunsetColor, sunsetStrength * 0.18f);
}

void ApplyPlanetWorldPalette(Color *top, Color *horizon, Color *worldTint)
{
    ApplyPlanetWorldPaletteWithLight(top, horizon, worldTint, NULL);
}

void DrawPlanetAtmosphereSky(const Camera3D *camera, const PlanetLightState *light)
{
    if (!camera || !light || !PlanetWorldIsActive()) return;

    const PlanetProfile *profile = PlanetWorldProfile();
    PlanetAtmosphereVisual visual = PlanetAtmosphereVisualFor(profile);
    float atmosphereVisibility = 1.0f - PlanetWorldAtmosphereFade(camera->position);
    if (visual.opticalDepth <= 0.01f || atmosphereVisibility <= 0.01f) return;

    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
    Vector3 flatForward = { forward.x, 0.0f, forward.z };
    if (Vector3LengthSqr(flatForward) > 0.0001f) {
        flatForward = Vector3Normalize(flatForward);
        Vector3 horizonPoint = Vector3Add(camera->position,
                                          Vector3Scale(flatForward, SUN_DISTANCE));
        float horizonY = GetWorldToScreen(horizonPoint, *camera).y;
        int band = (int)(54.0f + visual.opticalDepth * 126.0f);
        if (horizonY > (float)-band && horizonY < (float)(screenHeight + band)) {
            int centerY = (int)Clamp(horizonY, 0.0f, (float)screenHeight);
            int topY = centerY - band;
            int bottomY = centerY + band / 3;
            if (topY < 0) topY = 0;
            if (bottomY > screenHeight) bottomY = screenHeight;
            float hazeAlpha = (0.035f + visual.opticalDepth * 0.105f) *
                              (0.42f + light->daylight * 0.58f);
            hazeAlpha += light->sunset * visual.mieStrength * 0.12f;
            hazeAlpha *= atmosphereVisibility;
            Color haze = ColorLerp(visual.haze, light->starColor, 0.16f);
            if (centerY > topY) {
                DrawRectangleGradientV(0, topY, screenWidth, centerY - topY,
                                       BLANK, Fade(haze, hazeAlpha));
            }
            if (bottomY > centerY) {
                DrawRectangleGradientV(0, centerY, screenWidth, bottomY - centerY,
                                       Fade(haze, hazeAlpha), BLANK);
            }
        }
    }

    int sourceCount = light->sourceCount;
    if (sourceCount > MAX_SOLAR_LIGHTS) sourceCount = MAX_SOLAR_LIGHTS;
    for (int i = 0; i < sourceCount; i++) {
        Vector3 direction = light->sourceDirections[i];
        if (direction.y < -0.08f || Vector3DotProduct(direction, forward) <= 0.01f) continue;

        Vector3 sourcePoint = Vector3Add(camera->position,
                                         Vector3Scale(direction, SUN_DISTANCE));
        Vector2 screen = GetWorldToScreen(sourcePoint, *camera);
        if (screen.x < -260.0f || screen.x > (float)screenWidth + 260.0f ||
            screen.y < -260.0f || screen.y > (float)screenHeight + 260.0f) continue;

        float airMass = Clamp(1.0f / (0.20f + fmaxf(direction.y, 0.0f)), 0.85f, 4.20f);
        float visibility = Clamp(light->sourceVisibility[i], 0.0f, 1.0f);
        float sourceBrightness = PlanetExposureBrightness(light->sourceIntensities[i]);
        float scatterAlpha = Clamp(visual.opticalDepth *
                                   (0.018f + visual.mieStrength * 0.022f) * airMass *
                                   visibility * atmosphereVisibility *
                                   (0.22f + sourceBrightness * 0.78f),
                                   0.0f, 0.20f);
        float radius = Clamp((32.0f + visual.opticalDepth * 42.0f) * airMass *
                             (0.78f + sourceBrightness * 0.42f),
                             42.0f, 230.0f);
        Color scatter = ColorLerp(visual.haze, light->sourceColors[i], 0.48f);
        DrawCircleGradient((int)screen.x, (int)screen.y, radius,
                           Fade(scatter, scatterAlpha), BLANK);
        DrawCircleGradient((int)screen.x, (int)screen.y, radius * 0.38f,
                           Fade(scatter, scatterAlpha * 0.72f), BLANK);
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

static float AtmosphericMeteorVisibility(const Camera3D *camera, float daylight)
{
    if (!camera ||
        WorldCurrentDimensionAt(camera->position.y) == WORLD_DIMENSION_NETHER) {
        return 0.0f;
    }

    float atmosphere = 0.0f;
    if (PlanetWorldIsActive()) {
        const PlanetProfile *profile = PlanetWorldProfile();
        if (!profile || profile->atmosphereType == PLANET_ATMOSPHERE_NONE ||
            profile->atmosphereDensity <= 0.01f) {
            return 0.0f;
        }
        float density = Clamp(profile->atmosphereDensity / 0.35f, 0.0f, 1.0f);
        atmosphere = (1.0f - PlanetWorldAtmosphereFade(camera->position)) * density;
    } else if (HomeWorldSurfaceIsActive()) {
        atmosphere = 1.0f - HomeWorldSpaceFade(camera->position);
    }

    float nightVisibility = Clamp(1.0f - daylight * 1.15f, 0.0f, 1.0f);
    return Clamp(atmosphere * nightVisibility, 0.0f, 1.0f);
}

static void UpdateAtmosphericMeteors(float visibility, int sw, int sh)
{
    if (visibility <= 0.05f || sw <= 0 || sh <= 0) {
        meteor.active = false;
        return;
    }

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
        meteorTimer = 12.0f + (float)(rand() % 240) / 10.0f;
    }
    if (!meteor.active) return;

    meteor.life += dt;
    meteor.pos = Vector2Add(meteor.pos, Vector2Scale(meteor.vel, dt));
    float t = Clamp(meteor.life / meteor.maxLife, 0.0f, 1.0f);
    float alpha = (1.0f - t) * visibility;
    Vector2 tail = Vector2Subtract(meteor.pos, Vector2Scale(Vector2Normalize(meteor.vel), 90.0f));
    DrawLineEx(tail, meteor.pos, 2.5f,
               Fade((Color){ 255, 200, 120, 255 }, alpha * 0.70f));
    DrawLineEx(Vector2Subtract(meteor.pos, Vector2Scale(Vector2Normalize(meteor.vel), 40.0f)),
               meteor.pos, 1.5f, Fade((Color){ 255, 240, 210, 255 }, alpha));
    DrawCircle((int)meteor.pos.x, (int)meteor.pos.y, 3.0f,
               Fade((Color){ 255, 250, 230, 255 }, alpha));
    if (t >= 1.0f) meteor.active = false;
}

#define GALAXY_DENSITY_SAMPLES 220
#define GALAXY_STRUCTURE_COUNT 6
#define GALAXY_DIST 2100.0f

static Vector3 GalaxyDirection(Vector3 axisA, Vector3 axisB, Vector3 normal,
                               float longitude, float latitude)
{
    float cosLatitude = cosf(latitude);
    return Vector3Normalize(Vector3Add(
        Vector3Add(Vector3Scale(axisA, cosLatitude * cosf(longitude)),
                   Vector3Scale(axisB, cosLatitude * sinf(longitude))),
        Vector3Scale(normal, sinf(latitude))));
}

static bool ProjectGalaxyDirection(const Camera3D *camera, Vector3 forward, Vector3 direction,
                                   float distance, Vector2 *outScreen)
{
    if (Vector3DotProduct(direction, forward) <= 0.005f) return false;
    Vector3 point = Vector3Add(camera->position, Vector3Scale(direction, distance));
    Vector2 screen = GetWorldToScreen(point, *camera);
    if (outScreen) *outScreen = screen;
    return true;
}

static void DrawNebulae(const Camera3D *camera, float spaceFade)
{
    if (!camera || spaceFade <= 0.01f) return;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    uint32_t seed = WorldGetSeed();
    unsigned int orientationHash = Hash2D((int)(seed ^ 0x41a7u), (int)(seed >> 16));
    Vector3 normal = Vector3Normalize((Vector3){
        0.20f + (float)(orientationHash & 0xffu) / 255.0f * 0.22f,
        0.76f + (float)((orientationHash >> 8) & 0xffu) / 255.0f * 0.18f,
        0.34f + (float)((orientationHash >> 16) & 0xffu) / 255.0f * 0.26f
    });
    Vector3 axisA = Vector3Normalize(Vector3CrossProduct(normal, (Vector3){ 0, 1, 0 }));
    if (Vector3LengthSqr(axisA) < 0.001f) axisA = (Vector3){ 1, 0, 0 };
    Vector3 axisB = Vector3Normalize(Vector3CrossProduct(normal, axisA));
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));

    // A thin, seeded band gives the Milky Way a coherent plane instead of
    // isolated glowing blobs. Each segment is a short density filament.
    for (int i = 0; i < GALAXY_DENSITY_SAMPLES; i++) {
        unsigned int h1 = Hash2D(i * 17 + 31, (int)(seed ^ 0x9e37u));
        unsigned int h2 = Hash2D(i * 43 + 7, (int)(seed >> 9));
        float longitude = (float)(h1 % 6283u) / 1000.0f;
        float latitude = ((float)(h2 % 1001u) / 1000.0f - 0.5f) * 0.34f;
        Vector3 direction = GalaxyDirection(axisA, axisB, normal, longitude, latitude);
        Vector3 nextDirection = GalaxyDirection(axisA, axisB, normal,
                                                longitude + 0.022f, latitude);
        Vector2 screen;
        Vector2 nextScreen;
        if (!ProjectGalaxyDirection(camera, forward, direction, GALAXY_DIST, &screen) ||
            !ProjectGalaxyDirection(camera, forward, nextDirection, GALAXY_DIST, &nextScreen)) {
            continue;
        }
        if (screen.x < -80.0f || screen.x > (float)sw + 80.0f ||
            screen.y < -80.0f || screen.y > (float)sh + 80.0f) continue;

        float bandDensity = expf(-fabsf(latitude) * 7.2f);
        float variation = 0.45f + (float)((h2 >> 11) & 0xffu) / 255.0f * 0.55f;
        Color bandColor = (h1 & 3u) == 0u ? (Color){ 165, 151, 137, 255 } :
                          (h1 & 1u) ? (Color){ 101, 121, 155, 255 } :
                                      (Color){ 131, 129, 171, 255 };
        float alpha = (0.026f + bandDensity * 0.072f) * variation * spaceFade;
        float width = 1.0f + bandDensity * 2.4f;
        DrawLineEx(screen, nextScreen, width, Fade(bandColor, alpha));
    }

    // Dark dust lanes interrupt the bright band and provide depth cues.
    for (int i = 0; i < 34; i++) {
        unsigned int h1 = Hash2D(i * 29 + 113, (int)(seed ^ 0x5bd1u));
        unsigned int h2 = Hash2D(i * 47 + 61, (int)(seed >> 5));
        float longitude = (float)(h1 % 6283u) / 1000.0f;
        float latitude = ((float)(h2 % 1001u) / 1000.0f - 0.5f) * 0.24f;
        Vector3 direction = GalaxyDirection(axisA, axisB, normal, longitude, latitude);
        Vector3 nextDirection = GalaxyDirection(axisA, axisB, normal,
                                                longitude + 0.045f, latitude + 0.012f);
        Vector2 screen;
        Vector2 nextScreen;
        if (!ProjectGalaxyDirection(camera, forward, direction, GALAXY_DIST * 0.98f, &screen) ||
            !ProjectGalaxyDirection(camera, forward, nextDirection, GALAXY_DIST * 0.98f, &nextScreen)) {
            continue;
        }
        float width = 2.0f + (float)((h1 >> 13) % 6u);
        DrawLineEx(screen, nextScreen, width, Fade((Color){ 5, 8, 18, 255 },
                                                   0.055f * spaceFade));
    }

    // A few structured nebulae are drawn as curved filaments, rather than
    // circular gradients, so they read as real clouds embedded in the band.
    static const Color structureColors[4] = {
        { 92, 132, 205, 255 }, { 160, 104, 184, 255 },
        { 191, 123, 94, 255 }, { 90, 168, 175, 255 }
    };
    for (int patch = 0; patch < GALAXY_STRUCTURE_COUNT; patch++) {
        unsigned int h = Hash2D(patch * 71 + 19, (int)(seed ^ 0x27d4u));
        float centerLongitude = (float)(h % 6283u) / 1000.0f;
        float centerLatitude = ((float)((h >> 12) % 1001u) / 1000.0f - 0.5f) * 0.20f;
        float span = 0.055f + (float)((h >> 22) % 35u) / 1000.0f;
        for (int strand = 0; strand < 3; strand++) {
            Vector2 previous = { 0 };
            bool havePrevious = false;
            for (int segment = 0; segment < 14; segment++) {
                float t = (float)segment / 13.0f;
                float longitude = centerLongitude + (t - 0.5f) * span;
                float latitude = centerLatitude + sinf(t * PI + (float)strand * 1.7f) *
                                 (0.018f + strand * 0.006f);
                Vector3 direction = GalaxyDirection(axisA, axisB, normal, longitude, latitude);
                Vector2 screen;
                if (!ProjectGalaxyDirection(camera, forward, direction, GALAXY_DIST * 0.94f,
                                            &screen)) {
                    havePrevious = false;
                    continue;
                }
                if (havePrevious) {
                    float alpha = (0.025f + (float)(h & 7u) * 0.004f) * spaceFade;
                    DrawLineEx(previous, screen, 2.0f + strand * 0.45f,
                               Fade(structureColors[patch % 4], alpha));
                }
                previous = screen;
                havePrevious = true;
            }
        }
    }
}

void DrawSpaceSky(float spaceFade, float daylight, const Camera3D *camera)
{
    if (!camera) return;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    UpdateAtmosphericMeteors(AtmosphericMeteorVisibility(camera, daylight), sw, sh);

    if (spaceFade <= 0.01f) return;
    DrawNebulae(camera, spaceFade);
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
    float atmosphericDaylight = daylight;
    bool atmosphereActive = false;
    float atmosphereVisibility = 0.0f;
    float atmosphereDensity = 0.0f;
    if (PlanetWorldIsActive()) {
        const PlanetProfile *profile = PlanetWorldProfile();
        atmosphereVisibility = 1.0f - PlanetWorldAtmosphereFade(camera->position);
        float extinction = 0.0f;
        if (profile->atmosphereType != PLANET_ATMOSPHERE_NONE) {
            atmosphereActive = atmosphereVisibility > 0.01f;
            atmosphereDensity = Clamp(profile->atmosphereDensity, 0.0f, 1.0f);
            float typeScale = profile->atmosphereType == PLANET_ATMOSPHERE_THIN ? 0.72f : 1.22f;
            extinction = Clamp(profile->atmosphereDensity * typeScale, 0.0f, 1.0f);
        }
        extinction *= atmosphereVisibility;
        atmosphericDaylight *= extinction;
    } else if (HomeWorldSurfaceIsActive()) {
        // The home world has a breathable atmosphere even though it predates
        // the generated PlanetProfile system.
        atmosphereActive = true;
        atmosphereVisibility = 1.0f;
        atmosphereDensity = 0.62f;
    }
    if (atmosphericDaylight > 0.15f) return;

    float visibility = (0.15f - atmosphericDaylight) / 0.15f;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float time = GetTime();
    bool planetSurface = PlanetWorldIsActive();
    bool surfaceActive = WorldIsSurfaceActive();
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

        Vector3 primaryToStar = Vector3Subtract(system->center, observer);
        float primaryDistance = Vector3Length(primaryToStar);
        if (primaryDistance < 0.01f) continue;
        if (!surfaceActive && primaryDistance < 700.0f) continue;

        SolarLightSource sources[MAX_SOLAR_LIGHTS];
        int sourceCount = SolarSystemLightSources(system, sources, MAX_SOLAR_LIGHTS);
        for (int sourceIndex = 0; sourceIndex < sourceCount; sourceIndex++) {
            SolarSystemDef apparent = *system;
            apparent.center = sources[sourceIndex].center;
            apparent.spectrum = sources[sourceIndex].spectrum;

            Vector3 sourceToStar = Vector3Subtract(apparent.center, observer);
            float sourceDistance = Vector3Length(sourceToStar);
            if (sourceDistance < 0.01f) continue;
            Vector3 sourceDir = SolarSystemApparentDirection(&apparent, observer);
            if (planetSurface) sourceDir = PlanetWorldSkyDirection(sourceDir);
            if (surfaceActive && sourceDir.y < -0.05f) continue;
            if (Vector3DotProduct(sourceDir, forward) <= 0.01f) continue;

            Vector3 sourcePos = Vector3Add(camera->position,
                                          Vector3Scale(sourceDir, STAR_SHELL_DISTANCE));
            Vector2 sourceScreen = GetWorldToScreen(sourcePos, *camera);
            if (sourceScreen.x < -20.0f || sourceScreen.x > (float)sw + 20.0f ||
                sourceScreen.y < -20.0f || sourceScreen.y > (float)sh + 20.0f) continue;

            unsigned int sourceHash = WorldHash2D(system->anchorX, system->anchorZ) ^
                                       (0x9e3779b9u * (unsigned int)(sourceIndex + 1));
            float phase = (float)(sourceHash % 6283u) / 1000.0f;
            float twinkle = 1.0f;
            if (atmosphereActive) {
                float airMass = Clamp(1.0f / (0.20f + fmaxf(sourceDir.y, 0.0f)),
                                      0.85f, 4.20f);
                float scintillation = Clamp(atmosphereDensity * atmosphereVisibility *
                                            (airMass - 0.85f) / 3.35f,
                                            0.0f, 1.0f);
                twinkle = 1.0f + scintillation * 0.18f * sinf(time * 1.35f + phase);
            }
            float distanceFade = 1.0f - 0.58f * Clamp(sourceDistance / STAR_SKY_RANGE,
                                                       0.0f, 1.0f);
            float apparentIrradiance = SolarLightIrradianceAt(&sources[sourceIndex], observer);
            float irradianceBrightness = PlanetExposureBrightness(apparentIrradiance);
            float luminosityScale = Clamp(0.24f + sqrtf(irradianceBrightness) * 1.10f,
                                          0.24f, 1.20f);
            unsigned char alpha = (unsigned char)Clamp(visibility * 235.0f * twinkle *
                                                        distanceFade * luminosityScale,
                                                        0.0f, 255.0f);
            Color color = SpectrumColor(sources[sourceIndex].spectrum);
            color.a = alpha;
            float size = 1.0f + (float)(sourceHash % 5u) * 0.18f;
            if (sources[sourceIndex].spectrum == SPECTRUM_RED_GIANT) size += 0.55f;
            if (sourceIndex > 0) size *= 0.82f;
            bool bright = (sourceHash % 17u) == 0u;

            if (bright) {
                DrawCircle((int)sourceScreen.x, (int)sourceScreen.y, 2.6f, color);
                DrawLine((int)sourceScreen.x - 6, (int)sourceScreen.y,
                         (int)sourceScreen.x + 6, (int)sourceScreen.y,
                         Fade(color, 0.35f));
                DrawLine((int)sourceScreen.x, (int)sourceScreen.y - 6,
                         (int)sourceScreen.x, (int)sourceScreen.y + 6,
                         Fade(color, 0.35f));
            } else {
                DrawCircle((int)sourceScreen.x, (int)sourceScreen.y, size, color);
            }
        }
    }
}

static void DrawMoonPhase(Vector2 center, float radius, float illumination,
                          Vector3 sunDirection, Color light)
{
    Color dark = (Color){ 24, 30, 52, 235 };
    illumination = Clamp(illumination, 0.0f, 1.0f);
    DrawCircleV(center, radius, dark);
    if (illumination > 0.01f) {
        Vector2 lightAxis = Vector2Normalize((Vector2){ sunDirection.x, sunDirection.z });
        if (Vector2LengthSqr(lightAxis) < 0.001f) lightAxis = (Vector2){ 1.0f, 0.0f };
        if (illumination < 0.5f) {
            float width = radius * illumination * 2.0f;
            float offset = radius * (1.0f - illumination * 2.0f);
            DrawEllipseV(Vector2Add(center, Vector2Scale(lightAxis, offset)), width,
                         radius, light);
        } else {
            DrawCircleV(center, radius, light);
            float width = radius * (2.0f - illumination * 2.0f);
            float offset = radius * (illumination * 2.0f - 1.0f);
            if (width > 0.01f) {
                DrawEllipseV(Vector2Subtract(center, Vector2Scale(lightAxis, offset)),
                             width, radius, dark);
            }
        }
    }
    DrawCircleLines((int)center.x, (int)center.y, radius, Fade(light, 0.50f));
}

void DrawCelestial(const Camera3D *camera, float currentDayTime, float daylight)
{
    if (PlanetWorldIsActive()) {
        PlanetLightState state = { 0 };
        if (PlanetWorldLightStateAt(camera->position, &state)) {
            Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
            PlanetAtmosphereVisual atmosphere =
                PlanetAtmosphereVisualFor(PlanetWorldProfile());
            atmosphere.opticalDepth *=
                1.0f - PlanetWorldAtmosphereFade(camera->position);
            int sourceCount = state.sourceCount;
            if (sourceCount > MAX_SOLAR_LIGHTS) sourceCount = MAX_SOLAR_LIGHTS;
            for (int sourceIndex = 0; sourceIndex < sourceCount; sourceIndex++) {
                Vector3 sourceDir = state.sourceDirections[sourceIndex];
                if (Vector3LengthSqr(sourceDir) < 0.0001f ||
                    Vector3DotProduct(sourceDir, forward) <= 0.01f) continue;

                float relativeContribution = Clamp(state.sourceIntensities[sourceIndex] /
                                                   fmaxf(state.totalIntensity, 0.001f),
                                                   0.0f, 1.0f);
                float absoluteContribution = PlanetExposureBrightness(
                    state.sourceIntensities[sourceIndex]);
                float contribution = Clamp(absoluteContribution *
                                           (0.45f + relativeContribution * 0.55f),
                                           0.0f, 1.0f);
                float sourceVisibility = state.sourceVisibility[sourceIndex];
                if (sourceVisibility <= 0.0f) sourceVisibility = 1.0f;
                Color sourceColor = ColorLerp(BLACK, state.sourceColors[sourceIndex],
                                              sourceVisibility);
                float sourceOccultation =
                    state.sourceOccultations[sourceIndex];
                if (sourceOccultation > 0.1f) {
                    sourceColor = ColorLerp(sourceColor, (Color){ 255, 92, 40, 255 },
                                            0.34f * sourceOccultation);
                }
                float airMass = Clamp(1.0f / (0.20f + fmaxf(sourceDir.y, 0.0f)),
                                      0.85f, 4.20f);
                float reddening = Clamp((airMass - 0.85f) * atmosphere.opticalDepth * 0.10f,
                                        0.0f, 0.38f);
                sourceColor = ColorLerp(sourceColor, atmosphere.haze, reddening);
                Vector3 sourcePos = Vector3Add(camera->position,
                                               Vector3Scale(sourceDir, SUN_DISTANCE));
                Vector2 sourceScreen = GetWorldToScreen(sourcePos, *camera);
                float glowRadius = 12.0f + sqrtf(contribution) * 14.0f +
                                   atmosphere.opticalDepth * airMass * 5.0f;
                float glowAlpha = Clamp(0.12f + contribution * 0.12f +
                                        atmosphere.opticalDepth * airMass * 0.025f,
                                        0.0f, 0.34f);
                DrawCircleGradient((int)sourceScreen.x, (int)sourceScreen.y, glowRadius,
                                   Fade(sourceColor, glowAlpha), BLANK);
                Color sourceCore = ColorLerp((Color){ 12, 16, 28, 255 },
                                             ColorLerp(sourceColor, WHITE, 0.48f),
                                             0.18f + contribution * 0.82f);
                sourceCore.a = (unsigned char)Clamp((0.24f + contribution * 0.76f) *
                                                     255.0f, 0.0f, 255.0f);
                DrawCircle((int)sourceScreen.x, (int)sourceScreen.y,
                           10.0f + sqrtf(contribution) * 6.0f,
                           sourceCore);
                if (sourceOccultation > 0.1f) {
                    DrawCircle((int)sourceScreen.x, (int)sourceScreen.y, 11.0f,
                               Fade((Color){ 18, 18, 28, 255 },
                                    0.74f * sourceOccultation));
                }
            }

            if (state.hasMoon &&
                Vector3DotProduct(state.moonDirection, forward) > 0.01f) {
                Vector3 moonPos = Vector3Add(camera->position,
                                             Vector3Scale(state.moonDirection, SUN_DISTANCE * 0.96f));
                Vector2 moonScreen = GetWorldToScreen(moonPos, *camera);
                float referenceAngularRadius = 0.25f * DEG2RAD;
                float moonRadius = Clamp(12.0f * state.moonAngularRadius /
                                         referenceAngularRadius,
                                         3.0f, 30.0f);
                Color moonLight = ColorLerp(
                    (Color){ 214, 226, 244, 240 },
                    (Color){ 172, 62, 44, 240 },
                    Clamp(state.moonUmbra * 0.82f, 0.0f, 0.82f));
                DrawMoonPhase(moonScreen, moonRadius, state.moonIllumination,
                              state.sunDirection, moonLight);
            }
            return;
        }
    }

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

static Color FloraRuntimeTint(Color worldTint, const Chunk *chunk)
{
    PlanetFloraRuntimeState runtime = PlanetEcologyFloraRuntime(
        chunk->floraActivity, chunk->floraCapacity);
    Color dormantTint = {
        (unsigned char)((float)worldTint.r * 0.55f),
        (unsigned char)((float)worldTint.g * 0.42f),
        (unsigned char)((float)worldTint.b * 0.32f),
        worldTint.a
    };
    return ColorLerp(dormantTint, worldTint, runtime.visualPresence);
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
            if (chunk->hasFloraModel) {
                DrawModel(chunk->floraModel, Vector3Zero(), 1.0f,
                          FloraRuntimeTint(tint, chunk));
            }
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

void DrawSolarOrbitTrajectories(const Camera3D *camera, float spaceFade)
{
    if (!camera || spaceFade <= 0.05f) return;

    SolarSystemDef system = { 0 };
    float systemDistance = 0.0f;
    if (!FindNearestSystem(camera->position, 2600.0f, &system, &systemDistance)) return;

    const int samples = 64;
    double now = SpaceSimulationTime();
    for (int i = 0; i < system.planetCount; i++) {
        PlanetProfile profile = SolarPlanetProfile(&system, i);
        double period = SolarSystemPlanetOrbitPeriodGameTime(&system, i);
        if (period <= 0.0) continue;

        Color color = ColorLerp(SpectrumColor(system.spectrum),
                                SolarStyleColor(profile.style), 0.45f);
        color.a = (unsigned char)(Clamp(spaceFade, 0.0f, 1.0f) * 88.0f);

        Vector3 previous = SolarSystemPlanetPositionAtTime(&system, i, now);
        for (int sample = 1; sample <= samples; sample++) {
            double sampleTime = now + (double)period * (double)sample / (double)samples;
            Vector3 current = SolarSystemPlanetPositionAtTime(&system, i, sampleTime);
            DrawLine3D(previous, current, color);
            previous = current;
        }
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
#define PLANET_TEXTURE_CACHE_CAPACITY 24
#define PLANET_CLOUD_CACHE_CAPACITY 24

typedef struct PlanetTextureCacheEntry {
    bool valid;
    uint32_t seed;
    SolarBodyStyle style;
    uint32_t oceanKey;
    uint32_t seasonKey;
    uint64_t lastUse;
    PlanetTextureSet textures;
} PlanetTextureCacheEntry;

typedef struct PlanetCloudCacheEntry {
    bool valid;
    uint32_t seed;
    uint32_t profileKey;
    uint64_t lastUse;
    Texture2D texture;
} PlanetCloudCacheEntry;

typedef struct PlanetTextureResources {
    bool initialized;
    PlanetTextureSet home;
    Texture2D homeClouds;
    uint32_t homeCloudSeed;
    uint64_t textureCacheTick;
    PlanetTextureCacheEntry planetTextures[PLANET_TEXTURE_CACHE_CAPACITY];
    PlanetCloudCacheEntry cloudTextures[PLANET_CLOUD_CACHE_CAPACITY];
} PlanetTextureResources;

static PlanetTextureResources planetTextures = { 0 };

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

static Color ShadePlanetColor(Color color, float shade)
{
    return (Color){
        PlanetColorChannel((float)color.r * shade),
        PlanetColorChannel((float)color.g * shade),
        PlanetColorChannel((float)color.b * shade),
        color.a
    };
}

static Color ApplyPlanetClimateColor(Color color, const PlanetProfile *profile,
                                     const PlanetSurfaceSample *surface)
{
    if (!profile || !surface) return color;

    float cold = Clamp((248.0f - surface->temperature) / 62.0f, 0.0f, 1.0f);
    float warm = Clamp((surface->temperature - 304.0f) / 92.0f, 0.0f, 1.0f);
    color = ColorLerp(color, (Color){ 178, 211, 235, 255 }, cold * 0.16f);
    color = ColorLerp(color, (Color){ 224, 111, 54, 255 }, warm *
                      (0.10f + profile->greenhouseEffect * 0.10f));
    if (surface->iceCoverage > 0.01f) {
        color = ColorLerp(color, (Color){ 218, 240, 247, 255 },
                          Clamp(surface->iceCoverage * 0.86f, 0.0f, 0.92f));
    }
    color = ColorLerp(color, (Color){ 38, 43, 49, 255 }, surface->impactDepth * 0.42f);
    color = ColorLerp(color, (Color){ 177, 167, 145, 255 }, surface->ejecta * 0.24f);
    color = ColorLerp(color, (Color){ 218, 204, 166, 255 }, surface->impactRim * 0.28f);
    if (surface->lavaFlow > 0.05f) {
        color = ColorLerp(color, (Color){ 255, 82, 19, 255 }, surface->lavaFlow * 0.42f);
    }
    if (surface->glacierCracks > 0.05f) {
        color = ColorLerp(color, (Color){ 25, 71, 119, 255 },
                          surface->glacierCracks * 0.48f);
    }
    float albedoShade = 0.88f + Clamp(profile->albedo, 0.0f, 1.0f) * 0.28f;
    return ShadePlanetColor(color, albedoShade);
}

static Color TemperatePlanetPixel(const PlanetProfile *profile, float ny,
                                  const PlanetSurfaceSample *surfaceSample)
{
    PlanetSurfaceSample surface = *surfaceSample;
    float continents = surface.continentalness;
    float detail = surface.detail;
    Color color;

    if (surface.biome == PLANET_BIOME_OCEAN) {
        float waterline = 0.27f + Clamp(profile->oceanCoverage, 0.0f, 1.0f) * 0.36f;
        float depth = Clamp((waterline - continents) * 6.0f, 0.0f, 1.0f);
        color = ColorLerp((Color){ 35, 139, 176, 255 },
                          (Color){ 9, 43, 103, 255 }, depth * 0.82f);
    } else if (surface.biome == PLANET_BIOME_COAST) {
        color = ColorLerp((Color){ 82, 158, 166, 255 },
                          (Color){ 202, 181, 120, 255 }, detail * 0.64f);
    } else {
        float height = Clamp((continents - 0.42f) * 4.6f + detail * 0.18f, 0.0f, 1.0f);
        Color lowland = surface.biome == PLANET_BIOME_FOREST
                            ? (Color){ 39, 112, 61, 255 }
                            : (Color){ 91, 140, 76, 255 };
        if (surface.biome == PLANET_BIOME_ALPINE) {
            lowland = (Color){ 118, 120, 98, 255 };
            height = fmaxf(height, 0.54f + surface.regionalness * 0.36f);
        }
        if (fabsf(ny) > 0.63f) {
            lowland = ColorLerp(lowland, (Color){ 104, 130, 102, 255 }, 0.45f);
        }
        color = ColorLerp(lowland, (Color){ 126, 112, 82, 255 }, height);
        if (height > 0.86f) {
            color = ColorLerp(color, (Color){ 193, 201, 198, 255 },
                              (height - 0.86f) / 0.14f);
        }
    }

    color = ApplyPlanetClimateColor(color, profile, &surface);
    return color;
}

static float PlanetCloudAmountFor(const PlanetProfile *profile)
{
    if (!profile || profile->atmosphereType == PLANET_ATMOSPHERE_NONE) return 0.0f;
    return Clamp(profile->cloudCoverage, 0.0f, 1.0f);
}

static Color PlanetCloudColorFor(const PlanetProfile *profile)
{
    if (profile->atmosphereType == PLANET_ATMOSPHERE_CORROSIVE) {
        return ColorLerp((Color){ 244, 218, 132, 255 },
                         PlanetAtmosphereBaseColor(profile->style), 0.22f);
    }
    if (profile->style == SOLAR_STYLE_ICE || profile->equilibriumTempK < 238.0f) {
        return (Color){ 222, 240, 248, 255 };
    }
    if (profile->atmosphereType == PLANET_ATMOSPHERE_DENSE) {
        return ColorLerp((Color){ 239, 240, 235, 255 },
                         PlanetAtmosphereBaseColor(profile->style), 0.12f);
    }
    return (Color){ 246, 250, 255, 255 };
}

static float PlanetCloudStorm(Vector3 point, uint32_t seed, int index, float cloudAmount)
{
    if (index > 0 && cloudAmount < 0.38f) return 0.0f;

    float longitude = PlanetHashUnit(23 + index * 17, 41, 67, seed) * 2.0f * PI;
    float latitude = (PlanetHashUnit(71, 13 + index * 19, 37, seed) - 0.5f) * 1.45f;
    float cosLatitude = cosf(latitude);
    Vector3 center = { cosLatitude * cosf(longitude), sinf(latitude),
                       cosLatitude * sinf(longitude) };
    float angularDistance = sqrtf(fmaxf(0.0f,
                                         2.0f * (1.0f - Vector3DotProduct(point, center))));
    float radius = 0.16f + PlanetHashUnit(89, 29 + index * 31, 11, seed) * 0.11f;
    if (angularDistance >= radius) return 0.0f;

    Vector3 east = Vector3Normalize(Vector3CrossProduct((Vector3){ 0.0f, 1.0f, 0.0f },
                                                                  center));
    Vector3 north = Vector3Normalize(Vector3CrossProduct(center, east));
    float azimuth = atan2f(Vector3DotProduct(point, north),
                           Vector3DotProduct(point, east));
    float winding = index == 0 ? 3.0f : -2.0f;
    float phase = PlanetHashUnit(43, 97, 17 + index * 13, seed) * 2.0f * PI;
    float spiral = 0.5f + 0.5f * sinf(angularDistance * 82.0f + azimuth * winding + phase);
    float envelope = PlanetNoiseSmooth(1.0f - angularDistance / radius);
    float eye = PlanetNoiseSmooth(Clamp((angularDistance - radius * 0.10f) /
                                        (radius * 0.18f), 0.0f, 1.0f));
    return envelope * eye * (0.42f + spiral * 0.58f);
}

static Color PlanetCloudPixel(const PlanetProfile *profile, float nx, float ny,
                              float nz, uint32_t seed)
{
    Vector3 point = { nx, ny, nz };
    float latitude = asinf(Clamp(ny, -1.0f, 1.0f));
    float absLatitude = fabsf(latitude);
    float longitude = atan2f(nz, nx);
    float cloudAmount = PlanetCloudAmountFor(profile);
    float phase = PlanetHashUnit(31, 47, 59, seed) * 2.0f * PI;
    float warp = PlanetFractalNoise(nx * 2.1f + 4.7f, ny * 2.4f - 1.3f,
                                    nz * 2.1f + 7.9f, seed ^ 0x6d2b79u);
    float broad = PlanetFractalNoise(nx * 4.0f + ny * 0.9f,
                                     ny * 3.0f, nz * 4.0f - ny * 0.9f, seed);
    float detail = PlanetFractalNoise(nx * 10.5f - 3.1f, ny * 8.0f + 5.7f,
                                      nz * 10.5f + 2.3f, seed ^ 0x9e3779u);

    float equatorialConvergence = expf(-powf(latitude / 0.24f, 2.0f));
    float midLatitudeTracks = expf(-powf((absLatitude - 0.74f) / 0.22f, 2.0f));
    float subtropicalDryBand = expf(-powf((absLatitude - 0.43f) / 0.15f, 2.0f));
    float bandWave = 0.5f + 0.5f * sinf(latitude * 18.0f + phase + warp * 3.2f);
    float circulation = Clamp(0.42f + equatorialConvergence * 0.25f +
                              midLatitudeTracks * 0.31f - subtropicalDryBand * 0.22f +
                              (bandWave - 0.5f) * 0.20f, 0.0f, 1.0f);

    float polarMask = PlanetNoiseSmooth(Clamp((fabsf(ny) - 0.68f) / 0.30f, 0.0f, 1.0f));
    float vortexDirection = ny >= 0.0f ? 3.0f : -3.0f;
    float vortexArms = 0.5f + 0.5f * sinf(longitude * vortexDirection +
                                           (1.0f - fabsf(ny)) * 34.0f + phase);
    float polarVortex = polarMask * vortexArms;
    float storms = PlanetCloudStorm(point, seed, 0, cloudAmount);
    storms = fmaxf(storms, PlanetCloudStorm(point, seed, 1, cloudAmount));

    float cloudField = broad * 0.68f + detail * 0.12f + circulation * 0.16f +
                       polarVortex * 0.10f + storms * 0.48f + cloudAmount * 0.12f;
    float threshold = 0.63f - cloudAmount * 0.17f;
    float maxOpacity = 0.38f + cloudAmount * 0.50f;
    float opacity = Clamp((cloudField - threshold) * (4.1f + cloudAmount * 4.8f),
                          0.0f, maxOpacity);
    Color color = PlanetCloudColorFor(profile);
    color.a = PlanetColorChannel(opacity * 255.0f);
    return color;
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

static Color StyledPlanetPixel(const PlanetProfile *profile, float nx, float ny, float nz,
                               float u, float v, uint32_t seed,
                               const PlanetSurfaceSample *surfaceSample)
{
    SolarBodyStyle style = profile->style;
    PlanetSurfaceSample surface = *surfaceSample;
    float noise = surface.continentalness;
    float fine = surface.detail;
    Color color = (Color){ 120, 120, 120, 255 };

    switch (style) {
    case SOLAR_STYLE_LAVA: {
        color = ColorLerp((Color){ 23, 18, 21, 255 }, (Color){ 82, 38, 25, 255 }, noise);
        if (surface.biome == PLANET_BIOME_LAVA_SEA) {
            color = ColorLerp((Color){ 180, 48, 16, 255 }, (Color){ 255, 154, 32, 255 },
                              fine);
        }
        float fissure = PlanetLavaFissure(&surface);
        color = ColorLerp(color, (Color){ 255, 115, 18, 255 }, fissure);
        if (fissure > 0.72f) color = ColorLerp(color, (Color){ 255, 225, 88, 255 },
                                               (fissure - 0.72f) / 0.28f);
        break;
    }
    case SOLAR_STYLE_ICE: {
        color = ColorLerp((Color){ 78, 139, 176, 255 },
                          (Color){ 219, 240, 246, 255 }, noise * 0.82f + fabsf(ny) * 0.18f);
        color = ColorLerp(color, (Color){ 24, 76, 126, 255 }, surface.glacierCracks * 0.72f);
        break;
    }
    case SOLAR_STYLE_DESERT: {
        float dunes = surface.duneBand;
        color = ColorLerp((Color){ 139, 72, 36, 255 },
                          (Color){ 238, 183, 91, 255 }, noise * 0.74f + dunes * 0.10f);
        if (fine > 0.72f) color = ColorLerp(color, (Color){ 91, 48, 37, 255 },
                                            (fine - 0.72f) * 2.2f);
        if (surface.biome == PLANET_BIOME_OASIS) {
            color = ColorLerp(color, (Color){ 58, 132, 112, 255 }, 0.66f);
        }
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
        if (surface.biome == PLANET_BIOME_IMPACT_BASIN) {
            color = ColorLerp(color, (Color){ 51, 56, 63, 255 }, 0.46f);
        }
        break;
    case SOLAR_STYLE_TEMPERATE:
        return TemperatePlanetPixel(profile, ny, &surface);
    default:
        break;
    }

    color = ApplyPlanetClimateColor(color, profile, &surface);
    return color;
}

static Texture2D LoadPlanetTexturePixels(Color *pixels)
{
    Image image = {
        .data = pixels,
        .width = PLANET_TEXTURE_WIDTH,
        .height = PLANET_TEXTURE_HEIGHT,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };
    Texture2D texture = LoadTextureFromImage(image);
    if (texture.id != 0) {
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
    }
    return texture;
}

static void UnloadPlanetTextureSet(PlanetTextureSet *textures)
{
    if (!textures) return;
    if (textures->albedo.id != 0) UnloadTexture(textures->albedo);
    if (textures->material.id != 0) UnloadTexture(textures->material);
    *textures = (PlanetTextureSet){ 0 };
}

static PlanetTextureSet MakePlanetSurfaceTextures(const PlanetProfile *profile,
                                                  uint32_t seed)
{
    PlanetTextureSet textures = { 0 };
    size_t pixelCount = (size_t)PLANET_TEXTURE_WIDTH * PLANET_TEXTURE_HEIGHT;
    Color *albedoPixels = malloc(pixelCount * sizeof(*albedoPixels));
    Color *materialPixels = malloc(pixelCount * sizeof(*materialPixels));
    if (!albedoPixels || !materialPixels) {
        free(albedoPixels);
        free(materialPixels);
        return textures;
    }

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
            PlanetSurfaceSample surface = PlanetSampleGlobalSurface(seed, profile,
                                                                     longitude, latitude);
            size_t index = (size_t)y * PLANET_TEXTURE_WIDTH + x;
            albedoPixels[index] = StyledPlanetPixel(profile, nx, ny, nz, u, v, seed,
                                                     &surface);
            materialPixels[index] = PlanetMaterialPixel(profile, &surface);
        }
    }

    textures.albedo = LoadPlanetTexturePixels(albedoPixels);
    textures.material = LoadPlanetTexturePixels(materialPixels);
    free(albedoPixels);
    free(materialPixels);
    return textures;
}

static Texture2D MakePlanetCloudTexture(const PlanetProfile *profile, uint32_t seed)
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
            pixels[(size_t)y * PLANET_TEXTURE_WIDTH + x] =
                PlanetCloudPixel(profile, nx, ny, nz, seed);
        }
    }

    Texture2D texture = LoadPlanetTexturePixels(pixels);
    free(pixels);
    return texture;
}

static uint32_t PlanetTextureOceanKey(const PlanetProfile *profile)
{
    return (uint32_t)lroundf(Clamp(profile->oceanCoverage, 0.0f, 1.0f) * 1000.0f);
}

static uint32_t PlanetTextureSeasonKey(const PlanetProfile *profile)
{
    if (!profile || profile->yearLength <= 0.0f) return 0u;
    double cycle = fmod(SpaceSimulationTime() / (double)profile->yearLength, 1.0);
    if (cycle < 0.0) cycle += 1.0;
    return (uint32_t)floor(cycle * 8.0);
}

static PlanetTextureSet PlanetTextureForBody(const SpaceBodyInfo *body)
{
    uint32_t oceanKey = PlanetTextureOceanKey(&body->profile);
    uint32_t seasonKey = PlanetTextureSeasonKey(&body->profile);
    planetTextures.textureCacheTick++;
    for (int i = 0; i < PLANET_TEXTURE_CACHE_CAPACITY; i++) {
        PlanetTextureCacheEntry *entry = &planetTextures.planetTextures[i];
        if (!entry->valid || entry->seed != body->worldSeed ||
            entry->style != body->style || entry->oceanKey != oceanKey ||
            entry->seasonKey != seasonKey) {
            continue;
        }
        entry->lastUse = planetTextures.textureCacheTick;
        return entry->textures;
    }

    int replacement = 0;
    uint64_t oldestUse = UINT64_MAX;
    for (int i = 0; i < PLANET_TEXTURE_CACHE_CAPACITY; i++) {
        PlanetTextureCacheEntry *entry = &planetTextures.planetTextures[i];
        if (!entry->valid) {
            replacement = i;
            break;
        }
        if (entry->lastUse < oldestUse) {
            oldestUse = entry->lastUse;
            replacement = i;
        }
    }

    PlanetTextureCacheEntry *entry = &planetTextures.planetTextures[replacement];
    if (entry->valid) UnloadPlanetTextureSet(&entry->textures);
    *entry = (PlanetTextureCacheEntry){
        .valid = true,
        .seed = body->worldSeed,
        .style = body->style,
        .oceanKey = oceanKey,
        .seasonKey = seasonKey,
        .lastUse = planetTextures.textureCacheTick,
        .textures = MakePlanetSurfaceTextures(&body->profile, body->worldSeed)
    };
    return entry->textures;
}

static uint32_t PlanetCloudProfileKey(const PlanetProfile *profile)
{
    if (!profile) return 0u;
    int cloudKey = (int)lroundf(Clamp(profile->cloudCoverage, 0.0f, 1.0f) * 1023.0f);
    int temperatureKey = (int)lroundf(Clamp(profile->equilibriumTempK, 80.0f, 900.0f));
    int windKey = (int)lroundf(Clamp(profile->windStrength, 0.0f, 1.0f) * 1023.0f);
    uint32_t lanes = (uint32_t)profile->style * 0x9e3779b9u ^
                     (uint32_t)profile->atmosphereType * 0x85ebca6bu;
    return PlanetTextureHash(cloudKey, temperatureKey, windKey, lanes);
}

static bool PlanetHasCloudLayer(const PlanetProfile *profile)
{
    if (!profile || profile->atmosphereType == PLANET_ATMOSPHERE_NONE) return false;
    return PlanetCloudAmountFor(profile) > 0.055f;
}

static Texture2D PlanetCloudTextureForBody(const SpaceBodyInfo *body)
{
    if (!body || !PlanetHasCloudLayer(&body->profile)) return (Texture2D){ 0 };

    uint32_t profileKey = PlanetCloudProfileKey(&body->profile);
    planetTextures.textureCacheTick++;
    for (int i = 0; i < PLANET_CLOUD_CACHE_CAPACITY; i++) {
        PlanetCloudCacheEntry *entry = &planetTextures.cloudTextures[i];
        if (!entry->valid || entry->seed != body->worldSeed ||
            entry->profileKey != profileKey) continue;
        entry->lastUse = planetTextures.textureCacheTick;
        return entry->texture;
    }

    int replacement = 0;
    uint64_t oldestUse = UINT64_MAX;
    for (int i = 0; i < PLANET_CLOUD_CACHE_CAPACITY; i++) {
        PlanetCloudCacheEntry *entry = &planetTextures.cloudTextures[i];
        if (!entry->valid) {
            replacement = i;
            break;
        }
        if (entry->lastUse < oldestUse) {
            oldestUse = entry->lastUse;
            replacement = i;
        }
    }

    PlanetCloudCacheEntry *entry = &planetTextures.cloudTextures[replacement];
    if (entry->valid && entry->texture.id != 0) UnloadTexture(entry->texture);
    *entry = (PlanetCloudCacheEntry){
        .valid = true,
        .seed = body->worldSeed,
        .profileKey = profileKey,
        .lastUse = planetTextures.textureCacheTick,
        .texture = MakePlanetCloudTexture(&body->profile, body->worldSeed ^ 0x8392f5u)
    };
    return entry->texture;
}

static float PlanetCloudRotation(const PlanetProfile *profile, uint32_t seed)
{
    float wind = Clamp(profile ? profile->windStrength : 0.0f, 0.0f, 1.0f);
    float phase = PlanetHashUnit(17, 73, 101, seed) * 360.0f;
    float baseRate = profile ? fmaxf(profile->rotationRate, 0.05f) : 1.0f;
    float speed = baseRate * (0.25f + wind * 0.35f) +
                  (0.08f + wind * 1.20f) *
                  (0.82f + PlanetHashUnit(107, 19, 53, seed) * 0.36f);
    float direction = PlanetHashUnit(61, 83, 7, seed) < 0.5f ? -1.0f : 1.0f;
    double angle = (double)phase + SpaceSimulationTime() * (double)speed * (double)direction;
    angle = fmod(angle, 360.0);
    if (angle < 0.0) angle += 360.0;
    return (float)angle;
}

static float PlanetCloudShadowStrength(const PlanetProfile *profile)
{
    if (!profile || !profile->hasSolidSurface) return 0.0f;
    float amount = PlanetCloudAmountFor(profile);
    float density = Clamp(profile->atmosphereDensity, 0.0f, 1.0f);
    return Clamp(amount * (0.82f + density * 0.42f), 0.0f, 0.78f);
}

static PlanetProfile HomePlanetRenderProfile(void)
{
    return (PlanetProfile){
        .style = SOLAR_STYLE_TEMPERATE,
        .atmosphereType = PLANET_ATMOSPHERE_BREATHABLE,
        .physicalRadiusKm = SPACE_UNITS_EARTH_RADIUS_KM,
        .massKg = SPACE_UNITS_EARTH_MASS_KG,
        .spaceProxyRadius = 62.0f,
        .hasSolidSurface = true,
        .surfaceGravity = 1.0f,
        .receivedIrradiance = 1.0,
        .radiativeTempK = 255.0f,
        .equilibriumTempK = 288.0f,
        .surfacePressureAtm = 1.0f,
        .atmosphereDensity = 0.78f,
        .oceanCoverage = 0.48f,
        .iceCoverage = 0.10f,
        .cloudCoverage = 0.58f,
        .rotationRate = 1.2f,
        .albedo = 0.30f,
        .greenhouseEffect = 0.84f,
        .axialTilt = 23.4f * DEG2RAD,
        .yearLength = 6400.0f,
        .windStrength = 0.42f
    };
}

static void EnsurePlanetRenderResources(void)
{
    PlanetRendererEnsureResources();

    uint32_t homeSeed = WorldGetSeed();
    PlanetProfile homeProfile = HomePlanetRenderProfile();
    if (planetTextures.initialized) {
        if (planetTextures.homeCloudSeed != homeSeed) {
            if (planetTextures.homeClouds.id != 0) {
                UnloadTexture(planetTextures.homeClouds);
            }
            planetTextures.homeClouds = MakePlanetCloudTexture(
                &homeProfile, homeSeed ^ 0x8392f5u);
            planetTextures.homeCloudSeed = homeSeed;
        }
        return;
    }

    planetTextures.initialized = true;
    planetTextures.home = MakePlanetSurfaceTextures(&homeProfile, 0x48a1c3u);
    planetTextures.homeClouds = MakePlanetCloudTexture(
        &homeProfile, homeSeed ^ 0x8392f5u);
    planetTextures.homeCloudSeed = homeSeed;
}

void UnloadPlanetRenderResources(void)
{
    PlanetRendererShutdown();
    if (!planetTextures.initialized) return;

    UnloadPlanetTextureSet(&planetTextures.home);
    if (planetTextures.homeClouds.id != 0) {
        UnloadTexture(planetTextures.homeClouds);
    }
    for (int i = 0; i < PLANET_TEXTURE_CACHE_CAPACITY; i++) {
        if (planetTextures.planetTextures[i].valid) {
            UnloadPlanetTextureSet(&planetTextures.planetTextures[i].textures);
        }
    }
    for (int i = 0; i < PLANET_CLOUD_CACHE_CAPACITY; i++) {
        if (planetTextures.cloudTextures[i].valid &&
            planetTextures.cloudTextures[i].texture.id != 0) {
            UnloadTexture(planetTextures.cloudTextures[i].texture);
        }
    }
    planetTextures = (PlanetTextureResources){ 0 };
}

static Vector3 PlanetShaderColor(Color color)
{
    return (Vector3){ (float)color.r / 255.0f,
                      (float)color.g / 255.0f,
                      (float)color.b / 255.0f };
}

static PlanetMaterialResponse PlanetMaterialResponseFor(const PlanetProfile *profile,
                                                        bool cloudLayer)
{
    if (cloudLayer) {
        return (PlanetMaterialResponse){
            .roughness = 0.82f,
            .specular = 0.36f,
            .metallic = 0.0f,
            .model = 7
        };
    }

    PlanetMaterialResponse response = {
        .roughness = 0.78f,
        .specular = 0.24f,
        .metallic = 0.0f,
        .model = profile ? (int)profile->style : 0
    };
    if (!profile) return response;

    switch (profile->style) {
    case SOLAR_STYLE_LAVA:
        response.roughness = 0.58f;
        response.specular = 0.42f;
        response.metallic = 0.08f;
        break;
    case SOLAR_STYLE_ICE:
        response.roughness = 0.28f;
        response.specular = 0.78f;
        response.metallic = 0.02f;
        break;
    case SOLAR_STYLE_DESERT:
        response.roughness = 0.86f;
        response.specular = 0.18f;
        break;
    case SOLAR_STYLE_GAS:
        response.roughness = 0.41f;
        response.specular = 0.72f;
        break;
    case SOLAR_STYLE_CRATER:
        response.roughness = 0.92f;
        response.specular = 0.14f;
        break;
    case SOLAR_STYLE_TEMPERATE:
        response.roughness = 0.64f;
        response.specular = 0.38f;
        break;
    default:
        break;
    }

    if (profile->atmosphereType == PLANET_ATMOSPHERE_NONE) {
        response.roughness = Clamp(response.roughness + 0.035f, 0.045f, 1.0f);
    }
    return response;
}

static PlanetSpaceLighting PlanetSpaceLightingFor(int systemAnchorX, int systemAnchorZ,
                                                   Vector3 planetCenter)
{
    PlanetSpaceLighting lighting = { 0 };
    SolarSystemDef system = { 0 };
    SolarLightSource sources[MAX_SOLAR_LIGHTS];
    if (!StarSystemAt(systemAnchorX, systemAnchorZ, &system)) return lighting;

    lighting.count = SolarSystemLightSources(&system, sources, MAX_SOLAR_LIGHTS);
    if (lighting.count <= 0) return lighting;

    lighting.exposure = planetSceneExposure;
    for (int i = 0; i < lighting.count; i++) {
        Color color = SpectrumColor(sources[i].spectrum);
        lighting.positions[i] = sources[i].center;
        lighting.colors[i] = PlanetShaderColor(color);
        lighting.intensities[i] = SolarLightIrradianceAt(&sources[i], planetCenter);
    }
    return lighting;
}

static void DrawPlanetAtmosphere(const Camera3D *camera, Vector3 center, float radius,
                                 const PlanetProfile *profile,
                                 const PlanetSpaceLighting *lighting, float alpha)
{
    if (!camera || !profile ||
        profile->atmosphereType == PLANET_ATMOSPHERE_NONE || alpha <= 0.0f) {
        return;
    }

    PlanetAtmosphereVisual visual = PlanetAtmosphereVisualFor(profile);
    PlanetRendererDrawAtmosphere(&(PlanetAtmosphereDrawParams){
        .center = center,
        .radius = radius,
        .cameraPosition = camera->position,
        .rayleighColor = visual.haze,
        .horizonColor = visual.horizon,
        .density = profile->atmosphereDensity,
        .opticalDepth = visual.opticalDepth,
        .mieStrength = visual.mieStrength,
        .scaleHeight = visual.scaleHeight,
        .alpha = alpha,
        .sceneExposure = planetSceneExposure,
        .lighting = lighting
    });
}

static PlanetRingLayer PlanetRingLayerFor(Vector3 center, float radius, float tilt,
                                          uint32_t seed)
{
    return (PlanetRingLayer){
        .center = center,
        .normal = { 0.0f, cosf(tilt), sinf(tilt) },
        .radii = { radius * 1.30f, radius * 1.86f },
        .shadowParams = {
            PlanetHashUnit(131, 47, 19, seed) * 2.0f * PI,
            0.78f
        }
    };
}

static Vector3 PlanetRingPoint(const PlanetRingLayer *ring, float radius, float angle)
{
    float c = cosf(angle);
    float s = sinf(angle);
    return (Vector3){ ring->center.x + c * radius,
                      ring->center.y - s * radius * ring->normal.z,
                      ring->center.z + s * radius * ring->normal.y };
}

static float PlanetRingDensity(float radialFraction, float phase)
{
    float broad = 0.5f + 0.5f * sinf(radialFraction * 29.0f + phase);
    float fine = 0.5f + 0.5f * sinf(radialFraction * 73.0f + phase * 1.73f);
    float gapCenterA = 0.22f + 0.18f * (0.5f + 0.5f * sinf(phase * 0.71f));
    float gapCenterB = 0.62f + 0.18f * (0.5f + 0.5f * sinf(phase * 1.13f + 1.7f));
    float gapACoord = (radialFraction - gapCenterA) / 0.028f;
    float gapBCoord = (radialFraction - gapCenterB) / 0.045f;
    float gapA = expf(-gapACoord * gapACoord);
    float gapB = expf(-gapBCoord * gapBCoord);
    return Clamp(0.10f + broad * 0.54f + fine * 0.24f -
                 fmaxf(gapA, gapB) * 0.72f, 0.008f, 0.94f);
}

static Color PlanetRingParticleColor(SolarBodyStyle style, float density,
                                     float radialFraction, float phase)
{
    Color sparse = { 132, 122, 126, 255 };
    Color dense = { 224, 207, 184, 255 };
    if (style == SOLAR_STYLE_ICE) {
        sparse = (Color){ 132, 150, 166, 255 };
        dense = (Color){ 221, 233, 236, 255 };
    } else if (style == SOLAR_STYLE_CRATER) {
        sparse = (Color){ 128, 127, 125, 255 };
        dense = (Color){ 211, 205, 194, 255 };
    } else if (style == SOLAR_STYLE_LAVA) {
        sparse = (Color){ 116, 101, 101, 255 };
        dense = (Color){ 195, 164, 142, 255 };
    }
    float mineralVariation = 0.5f + 0.5f * sinf(radialFraction * 23.0f + phase * 0.61f);
    float colorMix = Clamp(0.12f + density * 0.74f + mineralVariation * 0.14f,
                           0.0f, 1.0f);
    return ColorLerp(sparse, dense, colorMix);
}

static float PlanetRingPlanetTransmission(Vector3 ringPoint, Vector3 lightPosition,
                                           Vector3 planetCenter, float planetRadius)
{
    Vector3 toLight = Vector3Subtract(lightPosition, ringPoint);
    float lightDistanceSqr = Vector3LengthSqr(toLight);
    if (lightDistanceSqr <= 0.0001f) return 1.0f;
    Vector3 lightDirection = Vector3Scale(toLight, 1.0f / sqrtf(lightDistanceSqr));
    Vector3 toCenter = Vector3Subtract(planetCenter, ringPoint);
    float alongRay = Vector3DotProduct(toCenter, lightDirection);
    if (alongRay <= 0.0f) return 1.0f;

    Vector3 closestPoint = Vector3Add(ringPoint,
                                      Vector3Scale(lightDirection, alongRay));
    float closestDistanceSqr = Vector3DistanceSqr(closestPoint, planetCenter);
    float radiusSqr = planetRadius * planetRadius;
    if (closestDistanceSqr >= radiusSqr) return 1.0f;
    float halfChord = sqrtf(fmaxf(radiusSqr - closestDistanceSqr, 0.0f));
    return alongRay - halfChord > 0.0f ? 0.025f : 1.0f;
}

static Color PlanetRingLitColor(Color albedo, Vector3 ringPoint, Vector3 ringNormal,
                                Vector3 planetCenter, float planetRadius, float density,
                                float alpha, const PlanetSpaceLighting *lighting)
{
    Vector3 illumination = { 0.055f, 0.058f, 0.064f };
    if (lighting) {
        for (int i = 0; i < lighting->count; i++) {
            Vector3 toLight = Vector3Subtract(lighting->positions[i], ringPoint);
            float distanceSqr = Vector3LengthSqr(toLight);
            if (distanceSqr <= 0.0001f) continue;
            Vector3 lightDirection = Vector3Scale(toLight, 1.0f / sqrtf(distanceSqr));
            float incidence = fabsf(Vector3DotProduct(ringNormal, lightDirection));
            float scattering = 0.24f + incidence * 0.76f;
            float transmission = PlanetRingPlanetTransmission(
                ringPoint, lighting->positions[i], planetCenter, planetRadius);
            float strength = lighting->intensities[i] * scattering * transmission;
            illumination.x += lighting->colors[i].x * strength;
            illumination.y += lighting->colors[i].y * strength;
            illumination.z += lighting->colors[i].z * strength;
        }
    }

    float opacity = alpha * (0.015f + density * 0.84f);
    float exposure = lighting && lighting->exposure > 0.0f ?
                     lighting->exposure : planetSceneExposure;
    float mappedR = 1.0f - expf(-Clamp(illumination.x, 0.0f, 8.0f) * exposure);
    float mappedG = 1.0f - expf(-Clamp(illumination.y, 0.0f, 8.0f) * exposure);
    float mappedB = 1.0f - expf(-Clamp(illumination.z, 0.0f, 8.0f) * exposure);
    return (Color){
        PlanetColorChannel((float)albedo.r * mappedR),
        PlanetColorChannel((float)albedo.g * mappedG),
        PlanetColorChannel((float)albedo.b * mappedB),
        PlanetColorChannel(Clamp(opacity, 0.0f, 0.94f) * 255.0f)
    };
}

static void DrawPlanetRings(const PlanetRingLayer *ring, float planetRadius,
                            SolarBodyStyle style, float alpha,
                            const PlanetSpaceLighting *lighting)
{
    if (!ring || alpha <= 0.0f) return;
    const int angularSegments = 72;
    const int radialSegments = 24;
    float ringWidth = ring->radii.y - ring->radii.x;

    BeginBlendMode(BLEND_ALPHA);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    for (int radial = 0; radial < radialSegments; radial++) {
        float radial0 = (float)radial / (float)radialSegments;
        float radial1 = (float)(radial + 1) / (float)radialSegments;
        float radialCenter = (radial0 + radial1) * 0.5f;
        float density = PlanetRingDensity(radialCenter, ring->shadowParams.x);
        float innerRadius = ring->radii.x + ringWidth * radial0;
        float outerRadius = ring->radii.x + ringWidth * radial1;
        Color albedo = PlanetRingParticleColor(style, density, radialCenter,
                                               ring->shadowParams.x);
        for (int segment = 0; segment < angularSegments; segment++) {
            float a0 = (float)segment * 2.0f * PI / (float)angularSegments;
            float a1 = (float)(segment + 1) * 2.0f * PI / (float)angularSegments;
            float centerAngle = (a0 + a1) * 0.5f;
            Vector3 midpoint = PlanetRingPoint(ring,
                                                (innerRadius + outerRadius) * 0.5f,
                                                centerAngle);
            Color color = PlanetRingLitColor(albedo, midpoint, ring->normal,
                                             ring->center, planetRadius, density,
                                             alpha, lighting);
            Vector3 i0 = PlanetRingPoint(ring, innerRadius, a0);
            Vector3 i1 = PlanetRingPoint(ring, innerRadius, a1);
            Vector3 o0 = PlanetRingPoint(ring, outerRadius, a0);
            Vector3 o1 = PlanetRingPoint(ring, outerRadius, a1);
            DrawTriangle3D(i0, o0, o1, color);
            DrawTriangle3D(i0, o1, i1, color);
        }
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    EndBlendMode();
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
            float radius = bodies[i].spaceProxyRadius;
            DrawSphere(bodies[i].center, radius * 1.08f, color);
            DrawSphere(bodies[i].center, radius * 1.15f,
                       Fade(color, 0.12f * spaceFade));
        } else {
            float radius = SolarBodyTerrainProxyRadius(
                bodies[i].spaceProxyRadius);
            PlanetTextureSet textures = PlanetTextureForBody(&bodies[i]);
            float rotation = PlanetBodyTextureRotation(&bodies[i]);
            PlanetSpaceLighting lighting = PlanetSpaceLightingFor(
                bodies[i].systemAnchorX, bodies[i].systemAnchorZ, bodies[i].center);
            float atmosphereAlpha = 0.08f + bodies[i].profile.atmosphereDensity * 0.54f;
            float ambientLight = 0.025f + bodies[i].profile.atmosphereDensity * 0.040f;
            float emissiveStrength = bodies[i].style == SOLAR_STYLE_LAVA ? 0.82f : 0.0f;
            PlanetCloudLayer cloudLayer = { 0 };
            if (PlanetHasCloudLayer(&bodies[i].profile)) {
                cloudLayer.texture = PlanetCloudTextureForBody(&bodies[i]);
                cloudLayer.rotation = PlanetCloudRotation(&bodies[i].profile,
                                                          bodies[i].worldSeed ^ 0x8392f5u);
                cloudLayer.shadowStrength = PlanetCloudShadowStrength(&bodies[i].profile);
            }
            PlanetRingLayer ringLayer = { 0 };
            PlanetRingLayer *activeRing = NULL;
            if (bodies[i].profile.hasRings) {
                ringLayer = PlanetRingLayerFor(bodies[i].center, radius,
                                               bodies[i].profile.ringTilt,
                                               bodies[i].worldSeed);
                activeRing = &ringLayer;
            }
            PlanetMaterialResponse material = PlanetMaterialResponseFor(
                &bodies[i].profile, false);
            PlanetRendererDrawSurface(&(PlanetSurfaceDrawParams){
                .center = bodies[i].center,
                .radius = radius + 0.08f,
                .textures = textures,
                .rotation = rotation,
                .fallback = color,
                .cameraPosition = camera->position,
                .lighting = &lighting,
                .material = &material,
                .ambientLight = ambientLight,
                .emissiveStrength = emissiveStrength,
                .sceneExposure = planetSceneExposure,
                .cloudLayer = &cloudLayer,
                .ringLayer = activeRing
            });
            if (cloudLayer.texture.id != 0) {
                PlanetMaterialResponse cloudMaterial = PlanetMaterialResponseFor(
                    &bodies[i].profile, true);
                PlanetRendererDrawSurface(&(PlanetSurfaceDrawParams){
                    .center = bodies[i].center,
                    .radius = radius * 1.014f,
                    .textures = { .albedo = cloudLayer.texture },
                    .rotation = cloudLayer.rotation,
                    .fallback = WHITE,
                    .cameraPosition = camera->position,
                    .lighting = &lighting,
                    .material = &cloudMaterial,
                    .ambientLight = ambientLight,
                    .emissiveStrength = 0.0f,
                    .sceneExposure = planetSceneExposure
                });
            }
            DrawPlanetAtmosphere(camera, bodies[i].center, radius, &bodies[i].profile,
                                 &lighting, atmosphereAlpha * spaceFade);
            if (activeRing) {
                DrawPlanetRings(activeRing, radius + 0.08f, bodies[i].style, spaceFade,
                                &lighting);
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
    const float radius = HomeWorldProxyRadius();
    float distance = Vector3Distance(camera->position, center);
    if (distance <= radius + 0.5f || distance > 24000.0f) return;

    EnsurePlanetRenderResources();
    PlanetProfile homeAtmosphere = HomePlanetRenderProfile();
    float homeRotation = -18.0f + (float)SpaceSimulationTime() * 1.2f;
    PlanetSpaceLighting lighting = PlanetSpaceLightingFor(0, 0, center);
    PlanetCloudLayer cloudLayer = {
        .texture = planetTextures.homeClouds,
        .rotation = PlanetCloudRotation(&homeAtmosphere,
                                         planetTextures.homeCloudSeed ^ 0x8392f5u),
        .shadowStrength = PlanetCloudShadowStrength(&homeAtmosphere)
    };
    PlanetMaterialResponse material = PlanetMaterialResponseFor(&homeAtmosphere, false);
    PlanetRendererDrawSurface(&(PlanetSurfaceDrawParams){
        .center = center,
        .radius = radius,
        .textures = planetTextures.home,
        .rotation = homeRotation,
        .fallback = HomePlanetColor(),
        .cameraPosition = camera->position,
        .lighting = &lighting,
        .material = &material,
        .ambientLight = 0.056f,
        .emissiveStrength = 0.0f,
        .sceneExposure = planetSceneExposure,
        .cloudLayer = &cloudLayer
    });
    if (cloudLayer.texture.id != 0) {
        PlanetMaterialResponse cloudMaterial = PlanetMaterialResponseFor(
            &homeAtmosphere, true);
        PlanetRendererDrawSurface(&(PlanetSurfaceDrawParams){
            .center = center,
            .radius = radius * 1.014f,
            .textures = { .albedo = cloudLayer.texture },
            .rotation = cloudLayer.rotation,
            .fallback = WHITE,
            .cameraPosition = camera->position,
            .lighting = &lighting,
            .material = &cloudMaterial,
            .ambientLight = 0.056f,
            .emissiveStrength = 0.0f,
            .sceneExposure = planetSceneExposure
        });
    }
    DrawPlanetAtmosphere(camera, center, radius, &homeAtmosphere,
                         &lighting, 0.62f * spaceFade);
}

void DrawBodyInfoPanel(const SpaceBodyInfo *body)
{
    if (!body) return;

    const char *typeName = body->isStar ? SpectrumName(body->spectrum) : SolarStyleName(body->style);
    const char *line1;
    const char *line2 = NULL;
    const char *line3 = NULL;
    if (body->isStar) {
        line1 = TextFormat("%s Prime - %s - %.0f blocks", body->name, typeName, body->dist);
        line2 = TextFormat("M %.2f Msol  L %.2g Lsol  T %.0f K",
                           body->hostStar.massSolar,
                           body->hostStar.luminositySolar,
                           body->hostStar.temperatureK);
        line3 = TextFormat("Age %.2g Gyr  Luminous life %.2g Gyr",
                           body->hostStar.ageGyr,
                           body->hostStar.luminousLifetimeGyr);
    } else {
        float surfaceGap = fabsf(body->dist - SolarBodyTerrainProxyRadius(
            body->spaceProxyRadius));
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

    int sw = GetScreenWidth();
    int maxWidth = (int)fmaxf((float)sw - 64.0f, 120.0f);
    int fs = 18;
    int detailFs = 16;
    while (fs > 13 && MeasureText(line1, fs) > maxWidth) fs--;
    while (detailFs > 12 &&
           ((line2 && MeasureText(line2, detailFs) > maxWidth) ||
            (line3 && MeasureText(line3, detailFs) > maxWidth))) {
        detailFs--;
    }
    int width = MeasureText(line1, fs);
    if (line2) width = fmaxf((float)width, (float)MeasureText(line2, detailFs));
    if (line3) width = fmaxf((float)width, (float)MeasureText(line3, detailFs));
    int x = sw / 2 - width / 2;
    int y = 64;
    float height = line3 ? 84.0f : (line2 ? 62.0f : 40.0f);
    DrawRectangleRounded((Rectangle){ (float)x - 16, (float)y - 8, (float)width + 32, height },
                         0.10f, 6, Fade(BLACK, 0.55f));
    DrawRectangleRoundedLinesEx((Rectangle){ (float)x - 16, (float)y - 8, (float)width + 32, height },
                                0.10f, 6, 1.5f, Fade(WHITE, 0.30f));
    DrawText(line1, x, y, fs, WHITE);
    if (line2) DrawText(line2, x, y + 24, detailFs, Fade(WHITE, 0.82f));
    if (line3) DrawText(line3, x, y + 46, detailFs, Fade(WHITE, 0.72f));
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
    Rectangle panel = { (float)sw - panelWidth - 18.0f, 16.0f, panelWidth, 166.0f };
    DrawRectangleRounded(panel, 0.06f, 6, Fade(BLACK, 0.72f));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 6, 1.5f, Fade(WHITE, 0.30f));

    int textX = (int)panel.x + 16;
    bool warping = ShipIsWarping();
    Color speedColor = warping ? (Color){ 166, 228, 255, 255 } :
                       (shipHudCruising ? (Color){ 130, 200, 255, 255 } : WHITE);
    const char *driveMode = warping ? "WARP" :
                            (shipHudCruising ?
                             (ShipFlightAssistEnabled() ? "CRUISE+ASSIST" :
                                                          "CRUISE+INERTIA") :
                             (ShipFlightAssistEnabled() ? "ASSIST" : "INERTIA"));
    DrawUiText(TextFormat("VEL %.0f blk/s  [%s]", shipHudSpeed, driveMode),
               textX, (int)panel.y + 10, 22, speedColor);
    if (shipHudAtmosphere >= 0.0f) {
        DrawUiText(TextFormat("ALT %.0f (surface)  ATM %.0f%%  HDG %03.0f",
                              shipHudAlt, shipHudAtmosphere, shipHudHeading),
                   textX, (int)panel.y + 42, 18, Fade(WHITE, 0.95f));
    } else {
        DrawUiText(TextFormat("ALT %.0f%s   HDG %03.0f", shipHudAlt,
                              shipHudNearPlanet ? " (surface)" : "", shipHudHeading),
                   textX, (int)panel.y + 42, 18, Fade(WHITE, 0.95f));
    }
    Color fuelColor = ShipGetFuel() > 20.0f ? (Color){ 255, 204, 94, 255 } : (Color){ 238, 100, 82, 255 };
    DrawUiText(TextFormat("FUEL %.0f / %.0f   R restore", ShipGetFuel(), SHIP_MAX_FUEL),
               textX, (int)panel.y + 68, 17, fuelColor);
    DrawUiText(TextFormat("SYS %s", shipHudSystem),
               textX, (int)panel.y + 94, 17, Fade(WHITE, 0.84f));
    if (ShipHasGravityPrimary()) {
        DrawUiText(TextFormat("SOI %s  %.0f / %.0f", ShipGravityPrimaryName(),
                              ShipGravityPrimaryDistance(),
                              ShipGravitySphereOfInfluence()),
                   textX, (int)panel.y + 118, 16, Fade(WHITE, 0.78f));
    } else {
        DrawUiText("SOI Interplanetary", textX, (int)panel.y + 118, 16,
                   Fade(WHITE, 0.62f));
    }
    if (ShipHasWarpTarget()) {
        const char *targetKind = ShipWarpTargetIsSystem() ? "SYS" : "PLANET";
        DrawUiText(TextFormat("%s %s %s", targetKind, warping ? "WARP" : "LOCK",
                              ShipWarpTargetName()),
                   textX, (int)panel.y + 142, 17, warping ? speedColor : Fade(WHITE, 0.84f));
    } else {
        DrawUiText("Q lock planet    G engage warp", textX, (int)panel.y + 142, 16,
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
    int h = 398;
    DrawRectangleRounded((Rectangle){ (float)x, (float)y, (float)w, (float)h }, 0.05f, 6, Fade(BLACK, 0.68f));
    DrawUiText("Voxelcraft", x + 14, y + 12, 24, WHITE);
    DrawUiText("WASD move    Shift sprint    Space jump/swim", x + 14, y + 48, 17, RAYWHITE);
    DrawUiText("LMB break    RMB place    MMB pick block", x + 14, y + 73, 17, RAYWHITE);
    DrawUiText("F float    Ctrl down (float)    Wheel hotbar", x + 14, y + 98, 17, RAYWHITE);
    DrawUiText("Tab mouse    M star map/warp    1-0 blocks    P album", x + 14, y + 123, 17, RAYWHITE);
    DrawUiText("RMB on placed album opens it", x + 14, y + 148, 17, RAYWHITE);
    DrawUiText("Esc pause    F6 day/night    O orbit paths", x + 14, y + 173, 17, RAYWHITE);
    DrawUiText("F4 view    F5 save    F9 load    F10 shot", x + 14, y + 198, 17, RAYWHITE);
    DrawUiText("Fly above y=120 to reach space", x + 14, y + 223, 17, RAYWHITE);
    DrawUiText("Break collects; place consumes blocks", x + 14, y + 248, 15, RAYWHITE);
    DrawUiText("Ship: RMB enter, Q lock planet, G warp/cancel", x + 14, y + 272, 15, RAYWHITE);
    DrawUiText("WASD thrust, X cruise, F assist, E exit", x + 14, y + 296, 15, RAYWHITE);
    DrawUiText("View: [ ] distance    Flat: I import image", x + 14, y + 320, 15, RAYWHITE);
    DrawUiText("Planet: C scanner, break cores for discoveries", x + 14, y + 344, 15, RAYWHITE);
    const char *mode = ShipIsDriving() ? "Ship" : (floating ? "Floating" : "Walking");
    DrawUiText(TextFormat("%s    %s    View %d    FPS %d", mode,
                          cursorReleased ? "Mouse free" : "Mouse locked", viewDistance, GetFPS()),
               x + 14, y + 372, 16, Fade(RAYWHITE, 0.9f));
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

void DrawDebugHUD(Vector3 playerPosition, float yaw, float pitch, float daylight)
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
    if (HomeWorldSurfaceIsActive() || PlanetWorldIsActive()) {
        DrawText(TextFormat("Weather cloud %.2f   precip %.2f   storm %.2f   wind %.2f",
                            WeatherCloudCover(), WeatherPrecipitationRate(),
                            WeatherStormIntensity(), WeatherWindIntensity()),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    }
    SolarSystemDef hudSystem;
    float hudSystemDist = 0.0f;
    if (FindSystemForGuide(playerPosition, &hudSystem, &hudSystemDist)) {
        DrawText(TextFormat("System %s Prime (%.0f)", hudSystem.name, hudSystemDist),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    } else {
        DrawText("Deep space", x, y, fs, Fade(WHITE, 0.85f)); y += line;
    }
    if (PlanetWorldIsActive()) {
        PlanetLocalEcology ecology = PlanetEcologyLocalAt(
            (int)floorf(playerPosition.x), (int)floorf(playerPosition.z), daylight);
        DrawText(TextFormat("Ecology capacity %.2f   flora %.2f   fauna %.2f   limit %s",
                            ecology.suitability.carryingCapacity,
                            ecology.suitability.floraCapacity,
                            ecology.suitability.faunaCapacity,
                            PlanetEcologyLimitingFactorName(
                                ecology.suitability.limitingFactor)),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DrawText(TextFormat("Activity flora %.2f   fauna %.2f   water %.2f   rain %.2f",
                            ecology.suitability.floraActivity,
                            ecology.suitability.faunaActivity,
                            ecology.environment.liquidWaterAccess,
                            ecology.environment.precipitationRate),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DrawText(TextFormat("Climate %.0f/%.0f K   light %.2f/%.2f   storm %.2f",
                            ecology.environment.meanTemperatureK,
                            ecology.environment.currentTemperatureK,
                            ecology.environment.meanUsableLight,
                            ecology.environment.currentUsableLight,
                            ecology.environment.currentStorm),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DrawText(TextFormat("Terrain elevation %.2f   slope %.2f   shelter %.2f",
                            ecology.environment.elevation,
                            ecology.environment.slope,
                            ecology.environment.shelter),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    }
    SpaceScaleDiagnostics scale;
    if (SpaceScaleDiagnosticsAt(playerPosition, &scale)) {
        DrawText(TextFormat("Scale %.0f u/AU   1 play s = 1 sim day   error %.3f ppm [%s]",
                            SPACE_UNITS_GAME_DISTANCE_PER_AU,
                            scale.maxRelativeError * 1000000.0,
                            scale.withinErrorBudget ? "OK" : "OUT"),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DrawText(TextFormat("%s radius %.0f km = %.5f linear u",
                            scale.bodyName, scale.physicalRadiusKm,
                            scale.physicalRadiusGame),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DrawText(TextFormat("Proxy visual %.1f u   landing %.1f u   x%.0f",
                            scale.visualRadiusGame, scale.landingRadiusGame,
                            scale.landingRadiusScale),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DrawText(TextFormat("Gravity %.2f m/s2 (%.2f g)   gameplay %.2f u/s2",
                            scale.physicalGravityMetersPerSecondSquared,
                            scale.physicalGravityEarth,
                            scale.gameplaySurfaceGravity),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DrawText(TextFormat("Orbit speed %.2f km/s   %.3f u/play-s",
                            scale.orbitalSpeedKilometersPerSecond,
                            scale.orbitalSpeedGame),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DrawText(TextFormat("SOI %.0f km (%.3f linear u)   Hill %.0f km",
                            scale.sphereOfInfluenceKm,
                            scale.physicalSphereOfInfluenceGame,
                            scale.hillSphereKm),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DrawText(TextFormat("Encounter %.1f u   x%.1f [%s]",
                            scale.encounterRadiusGame,
                            scale.encounterRadiusScale,
                            scale.encounterRadiusClamped ? "proxy clamp" : "physical"),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DrawText(TextFormat("Flux now %.3f Earth   climate mean %.3f Earth",
                            scale.currentIrradianceEarth,
                            scale.climateIrradianceEarth),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DrawText(TextFormat("Temperature radiative %.0f K   surface %.0f K",
                            scale.radiativeTemperatureK,
                            scale.surfaceTemperatureK),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    } else {
        DrawText("Scale target: no planet within 700 u", x, y, fs,
                 Fade(WHITE, 0.68f)); y += line;
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
