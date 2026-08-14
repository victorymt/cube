#include "render.h"

#include "raymath.h"
#include "rlgl.h"
#include "block_atlas.h"
#include "chunks.h"
#include "inventory.h"
#include "world.h"
#include "interaction.h"
#include "planet_material.h"
#include "planet_observation.h"
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
#include "perf.h"
#include "render_sort.h"
#include "world_lighting.h"
#include "world_renderer.h"

#include <math.h>
#include <limits.h>
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

#define UI_FONT_PATH "assets/fonts/FSEX302-alt.ttf"
#define UI_FONT_BASE_SIZE 32

static Font uiFont = { 0 };
static bool uiFontReady = false;

void UiFontShutdown(void)
{
    if (uiFontReady) UnloadFont(uiFont);
    uiFont = (Font){ 0 };
    uiFontReady = false;
}

void UiFontInit(void)
{
    UiFontShutdown();

    char applicationPath[512] = { 0 };
    const char *applicationDirectory = GetApplicationDirectory();
    if (applicationDirectory) {
        size_t length = strlen(applicationDirectory);
        const char *separator = length > 0 && applicationDirectory[length - 1] == '/'
                                    ? "" : "/";
        snprintf(applicationPath, sizeof(applicationPath), "%s%s%s",
                 applicationDirectory, separator, UI_FONT_PATH);
    }
    const char *paths[] = { UI_FONT_PATH, applicationPath };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        if (paths[i][0] == '\0' || !FileExists(paths[i])) continue;
        Font loaded = LoadFontEx(paths[i], UI_FONT_BASE_SIZE, NULL, 0);
        if (!IsFontValid(loaded) || loaded.texture.id == 0 ||
            loaded.texture.id == GetFontDefault().texture.id) continue;
        uiFont = loaded;
        uiFontReady = true;
        SetTextureFilter(uiFont.texture, TEXTURE_FILTER_POINT);
        return;
    }

    TraceLog(LOG_WARNING, "UI: Fixedsys font was not found; using default font");
}

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

Color MixWeather(Color color, float daylight,
                 const WeatherVisualState *weatherVisual)
{
    if (!HomeWorldSurfaceIsActive() && !PlanetWorldIsActive()) return color;

    float factor = weatherVisual && weatherVisual->active ?
                       weatherVisual->stormDarkening : WeatherSkyFactor();
    if (factor <= 0.0f) return color;

    float snowFraction = weatherVisual ? weatherVisual->snowFraction :
                         (WeatherGetCurrent() == WEATHER_SNOW ? 1.0f : 0.0f);
    Color overcast = ColorLerp((Color){ 84, 96, 118, 255 },
                               (Color){ 168, 180, 196, 255 }, snowFraction);
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

PlanetObservationState PlanetObservationForCamera(
    const Camera3D *camera, const PlanetLightState *light)
{
    PlanetObservationState state = { 0 };
    if (!camera || !light || !PlanetWorldIsActive()) return state;

    PlanetAtmosphereVisual visual = PlanetAtmosphereVisualFor(PlanetWorldProfile());
    float atmosphereVisibility = 1.0f - PlanetWorldAtmosphereFade(camera->position);
    return PlanetObservationEvaluate(light, visual.opticalDepth, visual.mieStrength,
                                     atmosphereVisibility);
}

void ApplyPlanetWorldPaletteWithLight(Color *top, Color *horizon, Color *worldTint,
                                      const PlanetLightState *light)
{
    ApplyPlanetWorldPaletteWithObservation(top, horizon, worldTint, light, NULL);
}

void ApplyPlanetWorldPaletteWithObservation(
    Color *top, Color *horizon, Color *worldTint,
    const PlanetLightState *light, const PlanetObservationState *observation)
{
    if (!PlanetWorldIsActive()) return;

    const PlanetProfile *profile = PlanetWorldProfile();
    PlanetAtmosphereVisual visual = PlanetAtmosphereVisualFor(profile);
    Color starColor = light && light->sourceCount > 0 ? light->starColor : WHITE;
    float daylight = light ? Clamp(light->daylight, 0.0f, 1.0f) : 1.0f;
    float sunset = light ? Clamp(light->sunset, 0.0f, 1.0f) : 0.0f;
    float skyBrightness = observation && observation->valid ?
                          observation->skyBrightness : daylight;
    float horizonWarmth = observation && observation->valid ?
                           observation->horizonWarmth : sunset;
    float eclipseDarkening = observation && observation->valid ?
                              observation->eclipseDarkening : 0.0f;

    if (profile->atmosphereType == PLANET_ATMOSPHERE_NONE) {
        *top = ColorLerp(*top, visual.zenith, 0.94f);
        *horizon = ColorLerp(*horizon, visual.horizon, 0.90f);
        *worldTint = ColorLerp(*worldTint, starColor, 0.10f);
        return;
    }

    float daylightResponse = 0.18f + skyBrightness * 0.82f;
    float topBlend = Clamp(0.12f + visual.opticalDepth * 0.58f * daylightResponse,
                           0.0f, 0.88f);
    float horizonBlend = Clamp(0.20f + visual.opticalDepth * 0.66f * daylightResponse,
                               0.0f, 0.94f);
    Color litHorizon = ColorLerp(visual.horizon, starColor, 0.18f);
    Color atmosphereLight = ColorLerp(visual.groundLight, starColor, 0.22f);
    *top = ColorLerp(*top, visual.zenith, topBlend);
    *horizon = ColorLerp(*horizon, litHorizon, horizonBlend);
    *worldTint = ColorLerp(*worldTint, atmosphereLight,
                           Clamp(visual.opticalDepth * (0.08f + skyBrightness * 0.20f),
                                 0.0f, 0.34f));

    Color sunsetColor = ColorLerp((Color){ 255, 104, 44, 255 }, starColor, 0.22f);
    float sunsetStrength = Clamp(horizonWarmth * visual.mieStrength * 0.82f,
                                 0.0f, 0.76f);
    *horizon = ColorLerp(*horizon, sunsetColor, sunsetStrength);
    *top = ColorLerp(*top, sunsetColor, sunsetStrength * 0.18f);
    if (eclipseDarkening > 0.0f && daylight > 0.01f) {
        float eclipseShade = Clamp(eclipseDarkening * 0.70f, 0.0f, 0.70f);
        *top = ColorLerp(*top, (Color){ 8, 12, 22, 255 }, eclipseShade);
        *horizon = ColorLerp(*horizon, (Color){ 42, 34, 38, 255 },
                             eclipseShade * 0.72f);
        *worldTint = ColorLerp(*worldTint, (Color){ 74, 78, 96, 255 },
                               eclipseShade * 0.58f);
    }
}

void ApplyPlanetWorldPalette(Color *top, Color *horizon, Color *worldTint)
{
    ApplyPlanetWorldPaletteWithLight(top, horizon, worldTint, NULL);
}

void DrawPlanetAtmosphereSky(const Camera3D *camera, const PlanetLightState *light,
                             const PlanetObservationState *observation,
                             const WeatherVisualState *weatherVisual)
{
    if (!camera || !light || !PlanetWorldIsActive()) return;

    const PlanetProfile *profile = PlanetWorldProfile();
    PlanetAtmosphereVisual visual = PlanetAtmosphereVisualFor(profile);
    float atmosphereVisibility = 1.0f - PlanetWorldAtmosphereFade(camera->position);
    if (visual.opticalDepth <= 0.01f || atmosphereVisibility <= 0.01f) return;
    PlanetObservationState fallback = PlanetObservationEvaluate(
        light, visual.opticalDepth, visual.mieStrength, atmosphereVisibility);
    const PlanetObservationState *observed = observation && observation->valid ?
                                              observation : &fallback;

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
            float hazeAlpha = (0.026f + visual.opticalDepth * 0.112f) *
                              (0.20f + observed->skyBrightness * 0.80f);
            hazeAlpha += observed->twilightStrength * visual.mieStrength * 0.10f;
            if (weatherVisual && weatherVisual->active) {
                hazeAlpha += weatherVisual->fogDensity * 0.16f;
            }
            hazeAlpha *= atmosphereVisibility;
            Color haze = ColorLerp(visual.haze, light->starColor, 0.16f);
            Color warmHaze = ColorLerp((Color){ 255, 96, 38, 255 },
                                       light->starColor, 0.22f);
            haze = ColorLerp(haze, warmHaze, observed->horizonWarmth * 0.82f);
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
        if (weatherVisual && weatherVisual->active) {
            visibility *= weatherVisual->visibility *
                          (1.0f - weatherVisual->cloudOpacity * 0.72f);
        }
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
        float lowSunWarmth = observed->horizonWarmth *
                             (1.0f - Clamp(direction.y * 2.5f, 0.0f, 1.0f));
        scatter = ColorLerp(scatter, (Color){ 255, 82, 34, 255 },
                            lowSunWarmth * 0.70f);
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

void DrawStars(const Camera3D *camera, float daylight,
               const PlanetObservationState *observation,
               const WeatherVisualState *weatherVisual)
{
    float atmosphericDaylight = daylight;
    float observationVisibility = -1.0f;
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
        if (observation && observation->valid) {
            observationVisibility = observation->starVisibility;
        }
    } else if (HomeWorldSurfaceIsActive()) {
        // The home world has a breathable atmosphere even though it predates
        // the generated PlanetProfile system.
        atmosphereActive = true;
        atmosphereVisibility = 1.0f;
        atmosphereDensity = 0.62f;
    }
    float visibility = observationVisibility;
    if (visibility < 0.0f) {
        if (atmosphericDaylight > 0.15f) return;
        visibility = (0.15f - atmosphericDaylight) / 0.15f;
    }
    if (weatherVisual && weatherVisual->active) {
        visibility *= weatherVisual->visibility *
                      (1.0f - weatherVisual->cloudOpacity * 0.92f);
    }
    if (visibility <= 0.005f) return;
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
            float horizonTransmission = 1.0f;
            if (atmosphereActive) {
                float airMass = Clamp(1.0f / (0.20f + fmaxf(sourceDir.y, 0.0f)),
                                      0.85f, 4.20f);
                float scintillation = Clamp(atmosphereDensity * atmosphereVisibility *
                                            (airMass - 0.85f) / 3.35f,
                                            0.0f, 1.0f);
                twinkle = 1.0f + scintillation * 0.18f * sinf(time * 1.35f + phase);
                float opticalDepth = observation && observation->valid ?
                                     observation->opticalDepth : atmosphereDensity;
                horizonTransmission = expf(-opticalDepth *
                                            fmaxf(airMass - 0.85f, 0.0f) * 0.20f);
            }
            float distanceFade = 1.0f - 0.58f * Clamp(sourceDistance / STAR_SKY_RANGE,
                                                       0.0f, 1.0f);
            float apparentIrradiance = SolarLightIrradianceAt(&sources[sourceIndex], observer);
            float irradianceBrightness = PlanetExposureBrightness(apparentIrradiance);
            float luminosityScale = Clamp(0.24f + sqrtf(irradianceBrightness) * 1.10f,
                                          0.24f, 1.20f);
            unsigned char alpha = (unsigned char)Clamp(visibility * 235.0f * twinkle *
                                                        horizonTransmission * distanceFade *
                                                        luminosityScale,
                                                        0.0f, 255.0f);
            Color color = SpectrumColor(sources[sourceIndex].spectrum);
            color.a = alpha;
            float size = 1.0f + (float)(sourceHash % 5u) * 0.18f;
            if (sources[sourceIndex].spectrum == SPECTRUM_RED_GIANT) size += 0.55f;
            if (sources[sourceIndex].spectrum == SPECTRUM_WHITE_DWARF) size *= 0.72f;
            if (sources[sourceIndex].spectrum == SPECTRUM_NEUTRON_STAR) size *= 0.58f;
            if (sources[sourceIndex].spectrum == SPECTRUM_BLACK_HOLE) size *= 0.45f;
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
                          Vector3 sunDirection, Color light, float visibility)
{
    Color dark = (Color){ 24, 30, 52, 235 };
    illumination = Clamp(illumination, 0.0f, 1.0f);
    visibility = Clamp(visibility, 0.0f, 1.0f);
    dark.a = (unsigned char)Clamp(235.0f * visibility, 0.0f, 235.0f);
    light.a = (unsigned char)Clamp((float)light.a * visibility, 0.0f, 255.0f);
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

void DrawCelestial(const Camera3D *camera, float currentDayTime, float daylight,
                   const PlanetLightState *planetLight,
                   const PlanetObservationState *observation,
                   const WeatherVisualState *weatherVisual)
{
  float weatherVisibility = 1.0f;
  if (weatherVisual && weatherVisual->active) {
    weatherVisibility = Clamp(weatherVisual->visibility *
                                  (1.0f - weatherVisual->cloudOpacity * 0.72f),
                              0.02f, 1.0f);
  }
  if (PlanetWorldIsActive() && planetLight && planetLight->sourceCount > 0) {
    const PlanetLightState *state = planetLight;
    Vector3 forward =
        Vector3Normalize(Vector3Subtract(camera->target, camera->position));
    PlanetAtmosphereVisual atmosphere =
        PlanetAtmosphereVisualFor(PlanetWorldProfile());
    atmosphere.opticalDepth *=
        1.0f - PlanetWorldAtmosphereFade(camera->position);
    int sourceCount = state->sourceCount;
    if (sourceCount > MAX_SOLAR_LIGHTS)
      sourceCount = MAX_SOLAR_LIGHTS;
    for (int sourceIndex = 0; sourceIndex < sourceCount; sourceIndex++) {
      Vector3 sourceDir = state->sourceDirections[sourceIndex];
      if (Vector3LengthSqr(sourceDir) < 0.0001f ||
          Vector3DotProduct(sourceDir, forward) <= 0.01f)
        continue;

      float relativeContribution =
          Clamp(state->sourceIntensities[sourceIndex] /
                    fmaxf(state->totalIntensity, 0.001f),
                0.0f, 1.0f);
      float absoluteContribution =
          PlanetExposureBrightness(state->sourceIntensities[sourceIndex]);
      float contribution =
          Clamp(absoluteContribution * (0.45f + relativeContribution * 0.55f),
                0.0f, 1.0f);
      float sourceVisibility = state->sourceVisibility[sourceIndex];
      if (sourceVisibility <= 0.0f)
        sourceVisibility = 1.0f;
      sourceVisibility *= weatherVisibility;
      Color sourceColor =
          ColorLerp(BLACK, state->sourceColors[sourceIndex], sourceVisibility);
      float sourceOccultation = state->sourceOccultations[sourceIndex];
      if (sourceOccultation > 0.1f) {
        sourceColor = ColorLerp(sourceColor, (Color){255, 92, 40, 255},
                                0.34f * sourceOccultation);
      }
      float airMass =
          Clamp(1.0f / (0.20f + fmaxf(sourceDir.y, 0.0f)), 0.85f, 4.20f);
      float reddening = Clamp(
          (airMass - 0.85f) * atmosphere.opticalDepth * 0.10f, 0.0f, 0.38f);
      sourceColor = ColorLerp(sourceColor, atmosphere.haze, reddening);
      Vector3 sourcePos =
          Vector3Add(camera->position, Vector3Scale(sourceDir, SUN_DISTANCE));
      Vector2 sourceScreen = GetWorldToScreen(sourcePos, *camera);
      float glowRadius = 12.0f + sqrtf(contribution) * 14.0f +
                         atmosphere.opticalDepth * airMass * 5.0f;
      float glowAlpha = Clamp(0.12f + contribution * 0.12f +
                                  atmosphere.opticalDepth * airMass * 0.025f,
                              0.0f, 0.34f);
      glowAlpha *= weatherVisibility;
      DrawCircleGradient((int)sourceScreen.x, (int)sourceScreen.y, glowRadius,
                         Fade(sourceColor, glowAlpha), BLANK);
      Color sourceCore = ColorLerp((Color){12, 16, 28, 255},
                                   ColorLerp(sourceColor, WHITE, 0.48f),
                                   0.18f + contribution * 0.82f);
      sourceCore.a = (unsigned char)Clamp(
          (0.24f + contribution * 0.76f) * weatherVisibility * 255.0f,
          0.0f, 255.0f);
      DrawCircle((int)sourceScreen.x, (int)sourceScreen.y,
                 10.0f + sqrtf(contribution) * 6.0f, sourceCore);
      if (sourceOccultation > 0.1f) {
        DrawCircle((int)sourceScreen.x, (int)sourceScreen.y, 11.0f,
                   Fade((Color){18, 18, 28, 255}, 0.74f * sourceOccultation));
      }
    }

    float moonVisibility =
        observation && observation->valid ? observation->moonVisibility : 1.0f;
    moonVisibility *= weatherVisibility;
    if (state->hasMoon && moonVisibility > 0.005f &&
        Vector3DotProduct(state->moonDirection, forward) > 0.01f) {
      Vector3 moonPos =
          Vector3Add(camera->position,
                     Vector3Scale(state->moonDirection, SUN_DISTANCE * 0.96f));
      Vector2 moonScreen = GetWorldToScreen(moonPos, *camera);
      float referenceAngularRadius = 0.25f * DEG2RAD;
      float moonRadius =
          Clamp(12.0f * state->moonAngularRadius / referenceAngularRadius, 3.0f,
                30.0f);
      Color moonLight =
          ColorLerp((Color){214, 226, 244, 240}, (Color){172, 62, 44, 240},
                    Clamp(state->moonUmbra * 0.82f, 0.0f, 0.82f));
      float haloStrength = observation && observation->valid
                               ? observation->moonHaloStrength
                               : 0.0f;
      if (haloStrength > 0.005f) {
        DrawCircleGradient((int)moonScreen.x, (int)moonScreen.y,
                           moonRadius * (2.7f + atmosphere.opticalDepth),
                           Fade(moonLight, haloStrength), BLANK);
      }
      DrawMoonPhase(moonScreen, moonRadius, state->moonIllumination,
                    state->sunDirection, moonLight, moonVisibility);
    }
    return;
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
                           Fade(sunColor, 0.28f * weatherVisibility), BLANK);
        DrawCircle((int)sunScreen.x, (int)sunScreen.y, 15.0f,
                   Fade(ColorLerp(sunColor, WHITE, 0.48f), weatherVisibility));
    }

    if (sinf(theta) < 0.0f && Vector3DotProduct(moonDir, forward) > 0.05f) {
        Vector3 moonPos = Vector3Add(camera->position, Vector3Scale(moonDir, SUN_DISTANCE));
        Vector2 moonScreen = GetWorldToScreen(moonPos, *camera);
        DrawCircle((int)moonScreen.x, (int)moonScreen.y, 12.0f,
                   Fade((Color){ 214, 226, 244, 240 }, weatherVisibility));
        DrawCircle((int)moonScreen.x - 5, (int)moonScreen.y - 3, 10.0f,
                   Fade((Color){ 24, 30, 52, 235 }, weatherVisibility));
    }
}

typedef struct CloudRenderResources {
    Shader shader;
    Texture2D noiseTexture;
    int cameraPositionLoc;
    int boxMinLoc;
    int boxMaxLoc;
    int windOffsetLoc;
    int coverageLoc;
    int opacityLoc;
    int stormLoc;
    int daylightLoc;
    int drawDistanceLoc;
    int sunDirectionLoc;
    int lightColorLoc;
    int lightStrengthLoc;
    int rayStepsLoc;
    int lightStepsLoc;
    bool ready;
} CloudRenderResources;

static CloudRenderResources cloudResources = { 0 };

static const char *cloudVertexShader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "out vec3 fragPosition;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 world = matModel*vec4(vertexPosition, 1.0);\n"
    "    fragPosition = world.xyz;\n"
    "    fragColor = vertexColor;\n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *cloudFragmentShader =
    "#version 330\n"
    "in vec3 fragPosition;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 cameraPosition;\n"
    "uniform vec3 cloudBoxMin;\n"
    "uniform vec3 cloudBoxMax;\n"
    "uniform vec2 windOffset;\n"
    "uniform float cloudCoverage;\n"
    "uniform float cloudOpacity;\n"
    "uniform float stormAmount;\n"
    "uniform float daylight;\n"
    "uniform float drawDistance;\n"
    "uniform vec3 sunDirection;\n"
    "uniform vec3 lightColor;\n"
    "uniform float lightStrength;\n"
    "uniform int raySteps;\n"
    "uniform int lightSteps;\n"
    "out vec4 finalColor;\n"
    "vec3 safeDirection(vec3 value) {\n"
    "    return vec3(value.x < 0.0 ? min(value.x, -0.0001) : max(value.x, 0.0001),\n"
    "                value.y < 0.0 ? min(value.y, -0.0001) : max(value.y, 0.0001),\n"
    "                value.z < 0.0 ? min(value.z, -0.0001) : max(value.z, 0.0001));\n"
    "}\n"
    "vec2 boxInterval(vec3 origin, vec3 direction) {\n"
    "    vec3 inverseDirection = 1.0/safeDirection(direction);\n"
    "    vec3 first = (cloudBoxMin - origin)*inverseDirection;\n"
    "    vec3 second = (cloudBoxMax - origin)*inverseDirection;\n"
    "    vec3 nearValues = min(first, second);\n"
    "    vec3 farValues = max(first, second);\n"
    "    return vec2(max(max(nearValues.x, nearValues.y), nearValues.z),\n"
    "                min(min(farValues.x, farValues.y), farValues.z));\n"
    "}\n"
    "float cloudNoise(vec3 point) {\n"
    "    vec2 warped = vec2(point.x + point.y*0.63, point.z - point.y*0.37);\n"
    "    vec2 drifted = warped - windOffset;\n"
    "    float broad = texture(texture0, drifted*0.0046).r;\n"
    "    float billow = texture(texture0, drifted*0.0127 + vec2(0.17, 0.43)).r;\n"
    "    float detail = texture(texture0, drifted*0.0340 + vec2(0.61, 0.09)).r;\n"
    "    return broad*0.58 + billow*0.29 + detail*0.13;\n"
    "}\n"
    "float cloudDensity(vec3 point) {\n"
    "    float height = clamp((point.y - cloudBoxMin.y)/\n"
    "                         max(cloudBoxMax.y - cloudBoxMin.y, 0.001), 0.0, 1.0);\n"
    "    float bottom = smoothstep(0.0, 0.16, height);\n"
    "    float top = 1.0 - smoothstep(0.64, 1.0, height);\n"
    "    float verticalShape = bottom*top;\n"
    "    float noiseValue = cloudNoise(point);\n"
    "    float threshold = mix(0.73, 0.34, clamp(cloudCoverage, 0.0, 1.0));\n"
    "    threshold -= stormAmount*0.06;\n"
    "    float density = smoothstep(threshold - 0.08, threshold + 0.11, noiseValue);\n"
    "    float baseWeight = mix(0.72, 1.08, 1.0 - height);\n"
    "    return clamp(density*verticalShape*baseWeight, 0.0, 1.0);\n"
    "}\n"
    "float cloudLight(vec3 point) {\n"
    "    float obstruction = 0.0;\n"
    "    vec3 direction = normalize(sunDirection);\n"
    "    for (int index = 0; index < 4; index++) {\n"
    "        if (index >= lightSteps) break;\n"
    "        float distanceAlongLight = 2.4 + float(index)*3.8;\n"
    "        obstruction += cloudDensity(point + direction*distanceAlongLight);\n"
    "    }\n"
    "    return exp(-obstruction*0.62);\n"
    "}\n"
    "void main() {\n"
    "    vec3 rayDirection = normalize(fragPosition - cameraPosition);\n"
    "    vec2 interval = boxInterval(cameraPosition, rayDirection);\n"
    "    float nearDistance = max(interval.x, 0.0);\n"
    "    float farDistance = interval.y;\n"
    "    if (farDistance <= nearDistance) discard;\n"
    "    bool cameraInside = all(greaterThanEqual(cameraPosition, cloudBoxMin)) &&\n"
    "                        all(lessThanEqual(cameraPosition, cloudBoxMax));\n"
    "    float surfaceDistance = length(fragPosition - cameraPosition);\n"
    "    float segmentLength = farDistance - nearDistance;\n"
    "    float stepLength = segmentLength/float(max(raySteps, 1));\n"
    "    if (!cameraInside && abs(surfaceDistance - nearDistance) > max(2.0, stepLength*1.5)) discard;\n"
    "    float jitter = texture(texture0, fragPosition.xz*0.021).r;\n"
    "    float travel = nearDistance + stepLength*(0.18 + jitter*0.64);\n"
    "    float accumulatedAlpha = 0.0;\n"
    "    vec3 accumulatedColor = vec3(0.0);\n"
    "    float forwardScatter = pow(max(dot(rayDirection, normalize(sunDirection)), 0.0), 8.0);\n"
    "    for (int index = 0; index < 24; index++) {\n"
    "        if (index >= raySteps || travel >= farDistance || accumulatedAlpha > 0.985) break;\n"
    "        vec3 point = cameraPosition + rayDirection*travel;\n"
    "        float edgeFade = 1.0 - smoothstep(drawDistance*0.72, drawDistance,\n"
    "                                           length(point.xz - cameraPosition.xz));\n"
    "        float density = cloudDensity(point)*edgeFade;\n"
    "        if (density > 0.015) {\n"
    "            float transmission = cloudLight(point);\n"
    "            float sampleAlpha = 1.0 - exp(-density*stepLength*0.105);\n"
    "            vec3 shadowColor = mix(vec3(0.32, 0.37, 0.45), colDiffuse.rgb, 0.26);\n"
    "            vec3 litColor = colDiffuse.rgb*lightColor*(0.62 + lightStrength*0.34);\n"
    "            vec3 sampleColor = mix(shadowColor, litColor, 0.20 + transmission*0.80);\n"
    "            sampleColor += lightColor*forwardScatter*transmission*0.22;\n"
    "            float remaining = 1.0 - accumulatedAlpha;\n"
    "            accumulatedColor += remaining*sampleColor*sampleAlpha;\n"
    "            accumulatedAlpha += remaining*sampleAlpha;\n"
    "        }\n"
    "        travel += stepLength;\n"
    "    }\n"
    "    float rawAlpha = accumulatedAlpha;\n"
    "    accumulatedAlpha *= cloudOpacity*colDiffuse.a*fragColor.a;\n"
    "    if (accumulatedAlpha < 0.008) discard;\n"
    "    vec3 color = accumulatedColor/max(rawAlpha, 0.001);\n"
    "    color = mix(color, color*vec3(0.72, 0.76, 0.84), stormAmount*0.34);\n"
    "    color *= 0.72 + daylight*0.28;\n"
    "    finalColor = vec4(color, clamp(accumulatedAlpha, 0.0, 0.96));\n"
    "}\n";

static uint32_t CloudNoiseHash(int x, int y)
{
    uint32_t hash = (uint32_t)x*0x8da6b343u ^ (uint32_t)y*0xd8163841u;
    hash ^= hash >> 13;
    hash *= 0x85ebca6bu;
    hash ^= hash >> 16;
    return hash;
}

static float CloudNoiseLattice(int x, int y, int period)
{
    x %= period;
    y %= period;
    if (x < 0) x += period;
    if (y < 0) y += period;
    return (float)(CloudNoiseHash(x, y) & 0x00ffffffu)/16777215.0f;
}

static float CloudValueNoise(float x, float y, int period)
{
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    float tx = x - (float)x0;
    float ty = y - (float)y0;
    tx = tx*tx*(3.0f - 2.0f*tx);
    ty = ty*ty*(3.0f - 2.0f*ty);
    float a = Lerp(CloudNoiseLattice(x0, y0, period),
                   CloudNoiseLattice(x0 + 1, y0, period), tx);
    float b = Lerp(CloudNoiseLattice(x0, y0 + 1, period),
                   CloudNoiseLattice(x0 + 1, y0 + 1, period), tx);
    return Lerp(a, b, ty);
}

static Texture2D MakeCloudNoiseTexture(void)
{
    enum { CLOUD_NOISE_SIZE = 128 };
    Color *pixels = malloc(sizeof(*pixels)*CLOUD_NOISE_SIZE*CLOUD_NOISE_SIZE);
    if (!pixels) return (Texture2D){ 0 };

    for (int y = 0; y < CLOUD_NOISE_SIZE; y++) {
        for (int x = 0; x < CLOUD_NOISE_SIZE; x++) {
            float nx = (float)x/(float)CLOUD_NOISE_SIZE;
            float ny = (float)y/(float)CLOUD_NOISE_SIZE;
            float value = 0.0f;
            float weight = 0.0f;
            for (int octave = 0; octave < 5; octave++) {
                int period = 4 << octave;
                float amplitude = powf(0.55f, (float)octave);
                value += CloudValueNoise(nx*(float)period,
                                         ny*(float)period, period)*amplitude;
                weight += amplitude;
            }
            unsigned char gray = (unsigned char)Clamp(value/weight*255.0f,
                                                       0.0f, 255.0f);
            pixels[y*CLOUD_NOISE_SIZE + x] = (Color){ gray, gray, gray, 255 };
        }
    }

    Image image = {
        .data = pixels,
        .width = CLOUD_NOISE_SIZE,
        .height = CLOUD_NOISE_SIZE,
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

void UnloadCloudRenderResources(void)
{
    if (cloudModel.meshCount > 0) UnloadModel(cloudModel);
    if (cloudResources.noiseTexture.id != 0) {
        UnloadTexture(cloudResources.noiseTexture);
    }
    if (cloudResources.shader.id != 0) UnloadShader(cloudResources.shader);
    cloudModel = (Model){ 0 };
    cloudResources = (CloudRenderResources){ 0 };
}

Model LoadCloudModel(void)
{
    UnloadCloudRenderResources();
    cloudResources.shader = LoadShaderFromMemory(cloudVertexShader,
                                                  cloudFragmentShader);
    if (cloudResources.shader.id == 0) return (Model){ 0 };

#define CLOUD_LOCATION(field, name) \
    cloudResources.field = GetShaderLocation(cloudResources.shader, name)
    CLOUD_LOCATION(cameraPositionLoc, "cameraPosition");
    CLOUD_LOCATION(boxMinLoc, "cloudBoxMin");
    CLOUD_LOCATION(boxMaxLoc, "cloudBoxMax");
    CLOUD_LOCATION(windOffsetLoc, "windOffset");
    CLOUD_LOCATION(coverageLoc, "cloudCoverage");
    CLOUD_LOCATION(opacityLoc, "cloudOpacity");
    CLOUD_LOCATION(stormLoc, "stormAmount");
    CLOUD_LOCATION(daylightLoc, "daylight");
    CLOUD_LOCATION(drawDistanceLoc, "drawDistance");
    CLOUD_LOCATION(sunDirectionLoc, "sunDirection");
    CLOUD_LOCATION(lightColorLoc, "lightColor");
    CLOUD_LOCATION(lightStrengthLoc, "lightStrength");
    CLOUD_LOCATION(rayStepsLoc, "raySteps");
    CLOUD_LOCATION(lightStepsLoc, "lightSteps");
#undef CLOUD_LOCATION
    if (cloudResources.cameraPositionLoc < 0 || cloudResources.boxMinLoc < 0 ||
        cloudResources.boxMaxLoc < 0 || cloudResources.windOffsetLoc < 0 ||
        cloudResources.coverageLoc < 0 || cloudResources.opacityLoc < 0 ||
        cloudResources.stormLoc < 0 || cloudResources.daylightLoc < 0 ||
        cloudResources.drawDistanceLoc < 0 || cloudResources.sunDirectionLoc < 0 ||
        cloudResources.lightColorLoc < 0 || cloudResources.lightStrengthLoc < 0 ||
        cloudResources.rayStepsLoc < 0 || cloudResources.lightStepsLoc < 0) {
        UnloadCloudRenderResources();
        return (Model){ 0 };
    }

    cloudResources.noiseTexture = MakeCloudNoiseTexture();
    if (cloudResources.noiseTexture.id == 0) {
        UnloadCloudRenderResources();
        return (Model){ 0 };
    }

    Model model = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    if (model.meshCount <= 0 || model.materialCount <= 0) {
        if (model.meshCount > 0) UnloadModel(model);
        UnloadCloudRenderResources();
        return (Model){ 0 };
    }
    model.materials[0].shader = cloudResources.shader;
    SetMaterialTexture(&model.materials[0], MATERIAL_MAP_DIFFUSE,
                       cloudResources.noiseTexture);
    cloudResources.ready = true;
    return model;
}

static Color WeatherCloudColor(const WeatherVisualState *visual, Color tint)
{
    float daylight = visual ? visual->daylight : 0.5f;
    float storm = visual ? visual->stormDarkening : 0.0f;
    float snow = visual ? visual->snowFraction : 0.0f;
    Color cloud = ColorLerp((Color){ 96, 105, 122, 255 },
                            (Color){ 238, 242, 246, 255 }, daylight);
    cloud = ColorLerp(cloud, (Color){ 72, 80, 96, 255 }, storm * 0.72f);
    cloud = ColorLerp(cloud, (Color){ 224, 232, 240, 255 }, snow * 0.36f);
    return ColorLerp(cloud, tint, 0.18f);
}

void DrawClouds(const Camera3D *camera, Color tint, double simulationTime,
                const WeatherVisualState *weatherVisual,
                const EnvironmentPresentationState *presentation,
                const WorldLightingState *lighting)
{
    if (!camera || !weatherVisual || !weatherVisual->active ||
        weatherVisual->cloudCover <= 0.03f || !isfinite(simulationTime) ||
        !cloudResources.ready || cloudModel.meshCount <= 0) {
        return;
    }

    double phaseTime = fmod(simulationTime, 1000000.0);
    double driftSpeed = 1.2 + (double)weatherVisual->windDrift * 3.6;
    double driftDistance = fmod(phaseTime*driftSpeed, 8192.0);
    Vector2 windOffset = {
        (float)(cos((double)weatherVisual->windAngle)*driftDistance),
        (float)(sin((double)weatherVisual->windAngle)*driftDistance)
    };
    double cameraX = floor((double)camera->position.x);
    double cameraZ = floor((double)camera->position.z);
    if (cameraX < (double)INT_MIN || cameraX > (double)INT_MAX ||
        cameraZ < (double)INT_MIN || cameraZ > (double)INT_MAX) {
        return;
    }

    int gridRadius = 2;
    int raySteps = 12;
    int lightSteps = 2;
    float opacity = weatherVisual->cloudOpacity;
    if (presentation) {
        gridRadius = (int)roundf((presentation->cloudDistanceScale - 0.72f) / 0.14f);
        if (gridRadius < 1) gridRadius = 1;
        if (gridRadius > 3) gridRadius = 3;
        raySteps = presentation->cloudRaySteps;
        lightSteps = presentation->cloudLightSteps;
        opacity = presentation->cloudOpacity;
    }
    raySteps = raySteps < 6 ? 6 : (raySteps > 24 ? 24 : raySteps);
    lightSteps = lightSteps < 1 ? 1 : (lightSteps > 4 ? 4 : lightSteps);
    float drawDistance = 120.0f + (float)gridRadius*60.0f;
    int sampleX = (int)cameraX;
    int sampleZ = (int)cameraZ;
    int seaLevel = PlanetWorldIsActive() ? PlanetTerrainSeaLevel() :
                                           TerrainSeaLevel(terrainMode);
    float altitudeReference = seaLevel >= 0 ? (float)seaLevel :
        (PlanetWorldIsActive() ? (float)PlanetTerrainHeight(sampleX, sampleZ) :
                                 (float)WorldSurfaceHeightAt(sampleX, sampleZ));
    float cloudBottom = altitudeReference + weatherVisual->cloudBaseHeight;
    float cloudThickness = fmaxf(weatherVisual->cloudThickness, 4.0f);
    Vector3 boxMin = {
        camera->position.x - drawDistance,
        cloudBottom,
        camera->position.z - drawDistance
    };
    Vector3 boxMax = {
        camera->position.x + drawDistance,
        cloudBottom + cloudThickness,
        camera->position.z + drawDistance
    };
    Vector3 center = Vector3Scale(Vector3Add(boxMin, boxMax), 0.5f);
    Vector3 scale = Vector3Subtract(boxMax, boxMin);
    Vector3 sunDirection = lighting ? lighting->sunDirection :
                           (Vector3){ 0.32f, 0.88f, 0.18f };
    Color sun = lighting ? lighting->sunColor : WHITE;
    Vector3 lightColor = {
        (float)sun.r/255.0f,
        (float)sun.g/255.0f,
        (float)sun.b/255.0f
    };
    float lightStrength = lighting ? Clamp(lighting->directStrength, 0.0f, 2.0f) :
                                     weatherVisual->daylight;
    float coverage = Clamp(weatherVisual->cloudCover, 0.0f, 1.0f);
    opacity = Clamp(opacity, 0.0f, 1.0f);
    float storm = Clamp(weatherVisual->stormDarkening, 0.0f, 1.0f);
    float daylight = Clamp(weatherVisual->daylight, 0.0f, 1.0f);

#define SET_CLOUD_UNIFORM(location, value, type) \
    SetShaderValue(cloudResources.shader, location, &(value), type)
    SET_CLOUD_UNIFORM(cloudResources.cameraPositionLoc, camera->position,
                      SHADER_UNIFORM_VEC3);
    SET_CLOUD_UNIFORM(cloudResources.boxMinLoc, boxMin, SHADER_UNIFORM_VEC3);
    SET_CLOUD_UNIFORM(cloudResources.boxMaxLoc, boxMax, SHADER_UNIFORM_VEC3);
    SET_CLOUD_UNIFORM(cloudResources.windOffsetLoc, windOffset, SHADER_UNIFORM_VEC2);
    SET_CLOUD_UNIFORM(cloudResources.coverageLoc, coverage, SHADER_UNIFORM_FLOAT);
    SET_CLOUD_UNIFORM(cloudResources.opacityLoc, opacity, SHADER_UNIFORM_FLOAT);
    SET_CLOUD_UNIFORM(cloudResources.stormLoc, storm, SHADER_UNIFORM_FLOAT);
    SET_CLOUD_UNIFORM(cloudResources.daylightLoc, daylight, SHADER_UNIFORM_FLOAT);
    SET_CLOUD_UNIFORM(cloudResources.drawDistanceLoc, drawDistance,
                      SHADER_UNIFORM_FLOAT);
    SET_CLOUD_UNIFORM(cloudResources.sunDirectionLoc, sunDirection,
                      SHADER_UNIFORM_VEC3);
    SET_CLOUD_UNIFORM(cloudResources.lightColorLoc, lightColor,
                      SHADER_UNIFORM_VEC3);
    SET_CLOUD_UNIFORM(cloudResources.lightStrengthLoc, lightStrength,
                      SHADER_UNIFORM_FLOAT);
    SET_CLOUD_UNIFORM(cloudResources.rayStepsLoc, raySteps, SHADER_UNIFORM_INT);
    SET_CLOUD_UNIFORM(cloudResources.lightStepsLoc, lightSteps, SHADER_UNIFORM_INT);
#undef SET_CLOUD_UNIFORM

    Color cloudTint = WeatherCloudColor(weatherVisual, tint);
    cloudTint.a = tint.a;
    BeginBlendMode(BLEND_ALPHA);
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    PerfRecordDrawCall(PERF_DRAW_CLOUD);
    DrawModelEx(cloudModel, center, (Vector3){ 0.0f, 1.0f, 0.0f }, 0.0f,
                scale, cloudTint);
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    EndBlendMode();
}

void DrawEnvironmentPostProcess(
    const EnvironmentPresentationState *presentation)
{
    if (!presentation) return;
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    if (presentation->skyDarkening > 0.01f) {
        DrawRectangle(0, 0, width, height,
                      Fade((Color){ 16, 22, 32, 255 },
                           Clamp(presentation->skyDarkening * 0.10f, 0.0f, 0.12f)));
    }
    if (presentation->warmth > 0.01f) {
        DrawRectangle(0, 0, width, height,
                      Fade((Color){ 255, 112, 42, 255 },
                           Clamp(presentation->warmth * 0.035f, 0.0f, 0.04f)));
    }
    if (presentation->lightningFlash > 0.01f) {
        DrawRectangle(0, 0, width, height,
                      Fade((Color){ 218, 230, 255, 255 },
                           Clamp(presentation->lightningFlash * 0.32f,
                                 0.0f, 0.34f)));
    }
}

void DrawWeatherOverlay(const Camera3D *camera,
                        const WeatherVisualState *weatherVisual)
{
    if (!camera || !weatherVisual || !weatherVisual->active) return;
    float fog = weatherVisual->fogDensity;
    float veil = weatherVisual->precipitationVeil;
    if (fog <= 0.005f && veil <= 0.005f) return;

    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    if (screenWidth <= 0 || screenHeight <= 0) return;

    Vector3 forward = Vector3Normalize(
        Vector3Subtract(camera->target, camera->position));
    Vector3 flatForward = { forward.x, 0.0f, forward.z };
    float horizonY = (float)screenHeight * 0.52f;
    if (Vector3LengthSqr(flatForward) > 0.0001f) {
        flatForward = Vector3Normalize(flatForward);
        Vector3 horizonPoint = Vector3Add(
            camera->position, Vector3Scale(flatForward, SUN_DISTANCE));
        horizonY = GetWorldToScreen(horizonPoint, *camera).y;
    }
    int fogTop = (int)Clamp(horizonY - (float)screenHeight * 0.16f,
                            0.0f, (float)screenHeight);
    Color rainFog = ColorLerp((Color){ 64, 76, 92, 255 },
                              (Color){ 142, 154, 166, 255 },
                              weatherVisual->daylight);
    Color snowFog = ColorLerp((Color){ 112, 126, 148, 255 },
                              (Color){ 216, 226, 235, 255 },
                              weatherVisual->daylight);
    Color fogColor = ColorLerp(rainFog, snowFog,
                               weatherVisual->snowFraction);
    float topAlpha = Clamp(fog * 0.12f + veil * 0.04f, 0.0f, 0.14f);
    float bottomAlpha = Clamp(fog * 0.34f + veil * 0.10f, 0.0f, 0.38f);
    if (fogTop < screenHeight) {
        DrawRectangleGradientV(0, fogTop, screenWidth, screenHeight - fogTop,
                               Fade(fogColor, topAlpha),
                               Fade(fogColor, bottomAlpha));
    }
    if (veil > 0.01f) {
        DrawRectangle(0, 0, screenWidth, screenHeight,
                      Fade(fogColor, Clamp(veil * 0.10f, 0.0f, 0.11f)));
    }
}

WorldLightingState WorldLightingForScene(
    const Camera3D *camera, float currentDayTime, float daylight, float sunset,
    const PlanetLightState *planetLight,
    const WeatherVisualState *weatherVisual, Color skyHorizon, bool inNether,
    const EnvironmentPresentationState *presentation)
{
    float theta = (currentDayTime - 0.25f) * (2.0f * PI);
    Vector3 sunDirection = Vector3Normalize(
        (Vector3){ cosf(theta), sinf(theta), 0.18f });
    Color sunColor = ColorLerp((Color){ 255, 150, 94, 255 },
                               (Color){ 255, 236, 208, 255 },
                               Clamp(daylight * 1.35f, 0.0f, 1.0f));
    float sourceStrength = daylight;
    if (PlanetWorldIsActive() && planetLight &&
        planetLight->sourceCount > 0) {
        sunDirection = planetLight->sourceDirections[0];
        sunColor = planetLight->sourceColors[0];
        float intensity = planetLight->sourceIntensities[0];
        sourceStrength = Clamp((1.0f - expf(-fmaxf(intensity, 0.0f))) *
                                   planetLight->sourceVisibility[0],
                               0.0f, 1.6f);
    }
    float night = 1.0f - Clamp(daylight, 0.0f, 1.0f);
    WorldLightingState state = {
        .sunDirection = sunDirection,
        .sunColor = sunColor,
        .ambientColor = ColorLerp((Color){ 76, 94, 146, 255 },
                                  (Color){ 194, 214, 232, 255 }, daylight),
        .fogColor = skyHorizon,
        .cameraPosition = camera ? camera->position : Vector3Zero(),
        .directStrength = sourceStrength * 2.1f,
        .ambientStrength = 0.20f + daylight * 0.42f +
                           night * 0.08f,
        .shadowStrength = 0.42f + daylight * 0.34f,
        .fogDensity = 0.0f,
        .fogStart = 38.0f,
        .wetness = 0.0f,
        .exposure = 1.0f,
        .saturation = 1.0f,
        .warmth = 0.0f,
        .waveStrength = 0.18f,
        .time = (float)fmod(SpaceElapsedSimulationTime(), 1000000.0),
        .shadowsEnabled = daylight > 0.05f
    };
    state.ambientColor = ColorLerp(state.ambientColor,
                                   (Color){ 255, 146, 94, 255 },
                                   sunset * 0.18f);
    if (inNether) {
        state.sunDirection = (Vector3){ 0.25f, 0.88f, 0.18f };
        state.sunColor = (Color){ 192, 62, 34, 255 };
        state.ambientColor = (Color){ 128, 34, 28, 255 };
        state.fogColor = (Color){ 40, 10, 8, 255 };
        state.directStrength = 1.0f;
        state.ambientStrength = 1.0f;
    }
    EnvironmentPresentationState fallback;
    if (!presentation) {
        fallback = WorldLightingFallbackPresentation(
            daylight, sunset, weatherVisual, inNether);
        presentation = &fallback;
    }
    return WorldLightingCompose(state, presentation);
}

#define MAX_TRANSPARENT_RENDER_ITEMS \
    (MAX_ACTIVE_CHUNKS * SURFACE_SECTION_COUNT + MAX_SPACE_CHUNKS + \
     MAX_NETHER_CHUNKS)

static Vector3 ChunkRenderCenter(int cx, int cz, float centerY)
{
    return (Vector3){
        (float)(cx * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f,
        centerY,
        (float)(cz * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f
    };
}

static void CollectSurfaceRenderItems(
    const Camera3D *camera, int effectiveRenderDistance, Color tint,
    TransparentRenderItem *transparent, int *transparentCount)
{
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        Chunk *chunk = &chunks[i];
        if (!chunk->loaded) continue;
        bool distanceVisible = ChunkWithinDrawDistance(
            chunk, camera->position, effectiveRenderDistance);
        if (!distanceVisible) continue;
        for (int sy = 0; sy < SURFACE_SECTION_COUNT; sy++) {
            ChunkSection *section = chunk->sections[sy];
            if (!section) continue;
            bool frustumVisible = ChunkSectionIntersectsCameraView(
                chunk, section, camera);
            PerfRecordWorldCandidate(distanceVisible, frustumVisible);
            if (!frustumVisible) continue;
            Vector3 translation = {
                0.0f, (float)(sy * SURFACE_SECTION_HEIGHT), 0.0f
            };
            if (section->hasModel) {
                PerfRecordDrawCall(PERF_DRAW_SOLID);
                WorldRendererDrawModel(&section->model, translation, tint, false);
            }
            if (section->hasFloraModel) {
                PerfRecordDrawCall(PERF_DRAW_FLORA);
                WorldRendererDrawModel(&section->floraModel, translation, tint,
                                       false);
            }
            if (section->hasWaterModel) {
                TransparentRenderItemAppend(
                    transparent, MAX_TRANSPARENT_RENDER_ITEMS, transparentCount,
                    &section->waterModel, translation,
                    ChunkRenderCenter(
                        chunk->cx, chunk->cz,
                        (float)(sy * SURFACE_SECTION_HEIGHT) +
                            (float)SURFACE_SECTION_HEIGHT * 0.5f),
                    camera->position, TRANSPARENT_RENDER_SURFACE,
                    chunk->cx, chunk->cz, i * SURFACE_SECTION_COUNT + sy);
            }
        }
    }
}

static void CollectSpaceRenderItems(
    const Camera3D *camera, int cameraCx, int cameraCz, Color tint,
    TransparentRenderItem *transparent, int *transparentCount)
{
    Vector3 translation = { 0.0f, (float)SPACE_LAYER_Y, 0.0f };
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        SpaceChunk *chunk = &spaceChunks[i];
        if (!chunk->loaded) continue;
        bool distanceVisible = abs(chunk->cx - cameraCx) <= SPACE_RENDER_DISTANCE_CHUNKS &&
                               abs(chunk->cz - cameraCz) <= SPACE_RENDER_DISTANCE_CHUNKS;
        Vector3 center = ChunkRenderCenter(
            chunk->cx, chunk->cz,
            (float)SPACE_LAYER_Y + (float)SPACE_LAYER_HEIGHT * 0.5f);
        bool frustumVisible = distanceVisible && SphereInFrustum(camera, center, 66.0f);
        PerfRecordWorldCandidate(distanceVisible, frustumVisible);
        if (!distanceVisible || !frustumVisible) continue;
        if (chunk->hasModel) {
            PerfRecordDrawCall(PERF_DRAW_SPACE);
            WorldRendererDrawModel(&chunk->model, translation, tint, false);
        }
        if (chunk->hasWaterModel) {
            TransparentRenderItemAppend(
                transparent, MAX_TRANSPARENT_RENDER_ITEMS, transparentCount,
                &chunk->waterModel, translation, center, camera->position,
                TRANSPARENT_RENDER_SPACE, chunk->cx, chunk->cz, i);
        }
    }
}

static void CollectNetherRenderItems(
    const Camera3D *camera, int cameraCx, int cameraCz, Color tint,
    TransparentRenderItem *transparent, int *transparentCount)
{
    Vector3 translation = { 0.0f, (float)NETHER_LAYER_Y, 0.0f };
    for (int i = 0; i < MAX_NETHER_CHUNKS; i++) {
        NetherChunk *chunk = &netherChunks[i];
        if (!chunk->loaded) continue;
        bool distanceVisible = abs(chunk->cx - cameraCx) <= NETHER_RENDER_DISTANCE_CHUNKS &&
                               abs(chunk->cz - cameraCz) <= NETHER_RENDER_DISTANCE_CHUNKS;
        Vector3 center = ChunkRenderCenter(
            chunk->cx, chunk->cz, (float)NETHER_LAYER_Y + 16.0f);
        bool frustumVisible = distanceVisible && SphereInFrustum(camera, center, 34.0f);
        PerfRecordWorldCandidate(distanceVisible, frustumVisible);
        if (!distanceVisible || !frustumVisible) continue;
        if (chunk->hasModel) {
            PerfRecordDrawCall(PERF_DRAW_NETHER);
            WorldRendererDrawModel(&chunk->model, translation, tint, false);
        }
        if (chunk->hasWaterModel) {
            TransparentRenderItemAppend(
                transparent, MAX_TRANSPARENT_RENDER_ITEMS, transparentCount,
                &chunk->waterModel, translation, center, camera->position,
                TRANSPARENT_RENDER_NETHER, chunk->cx, chunk->cz, i);
        }
    }
}

void DrawWorld(const Camera3D *camera, int effectiveRenderDistance, Color tint,
               bool drawSurfaceChunks, bool drawNetherChunks,
               const WorldLightingState *lighting)
{
    if (lighting) WorldRendererPrepare(lighting);
    TransparentRenderItem transparent[MAX_TRANSPARENT_RENDER_ITEMS];
    int transparentCount = 0;
    if (drawSurfaceChunks) {
        CollectSurfaceRenderItems(camera, effectiveRenderDistance, tint,
                                  transparent, &transparentCount);
    }

    int cameraCx = 0;
    int cameraCz = 0;
    int localX = 0;
    int localZ = 0;
    WorldToChunkLocal((int)floorf(camera->position.x),
                      (int)floorf(camera->position.z),
                      &cameraCx, &cameraCz, &localX, &localZ);
    CollectSpaceRenderItems(camera, cameraCx, cameraCz, tint,
                            transparent, &transparentCount);
    if (drawNetherChunks) {
        CollectNetherRenderItems(camera, cameraCx, cameraCz, tint,
                                 transparent, &transparentCount);
    }

    SortTransparentRenderItems(transparent, transparentCount);
    BeginBlendMode(BLEND_ALPHA);
    for (int i = 0; i < transparentCount; i++) {
        PerfDrawKind kind = PERF_DRAW_WATER;
        if (transparent[i].dimension == TRANSPARENT_RENDER_SPACE) kind = PERF_DRAW_SPACE;
        else if (transparent[i].dimension == TRANSPARENT_RENDER_NETHER) kind = PERF_DRAW_NETHER;
        PerfRecordDrawCall(kind);
        WorldRendererDrawModel(transparent[i].model, transparent[i].translation,
                               tint, true);
    }
    EndBlendMode();
}

void DrawWorldShadowMap(const Camera3D *camera, int effectiveRenderDistance,
                        bool drawSurfaceChunks, bool drawNetherChunks,
                        const WorldLightingState *lighting)
{
    if (!WorldRendererBeginShadow(camera, lighting)) return;
    int cameraCx = 0, cameraCz = 0, localX = 0, localZ = 0;
    WorldToChunkLocal((int)floorf(camera->position.x),
                      (int)floorf(camera->position.z),
                      &cameraCx, &cameraCz, &localX, &localZ);
    int shadowChunkRadius = WorldRendererShadowChunkRadius();
    if (shadowChunkRadius > effectiveRenderDistance) {
        shadowChunkRadius = effectiveRenderDistance;
    }
    if (drawSurfaceChunks) {
        for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
            Chunk *chunk = &chunks[i];
            if (!chunk->loaded || abs(chunk->cx - cameraCx) > shadowChunkRadius ||
                abs(chunk->cz - cameraCz) > shadowChunkRadius) continue;
            for (int sy = 0; sy < SURFACE_SECTION_COUNT; sy++) {
                ChunkSection *section = chunk->sections[sy];
                if (!section) continue;
                Vector3 translation = {
                    0.0f, (float)(sy * SURFACE_SECTION_HEIGHT), 0.0f
                };
                if (section->hasModel) {
                    WorldRendererDrawShadowModel(&section->model, translation);
                }
                if (section->hasFloraModel) {
                    WorldRendererDrawShadowModel(&section->floraModel,
                                                 translation);
                }
            }
        }
    }
    if (drawNetherChunks) {
        Vector3 translation = { 0.0f, (float)NETHER_LAYER_Y, 0.0f };
        for (int i = 0; i < MAX_NETHER_CHUNKS; i++) {
            NetherChunk *chunk = &netherChunks[i];
            if (!chunk->loaded || !chunk->hasModel ||
                abs(chunk->cx - cameraCx) > shadowChunkRadius ||
                abs(chunk->cz - cameraCz) > shadowChunkRadius) continue;
            WorldRendererDrawShadowModel(&chunk->model, translation);
        }
    }
    WorldRendererEndShadow();
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
        UiDrawText(TextFormat("%s - %.0f blocks", label, dist),
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
    double now = SpacePeriodicSimulationTime(SpaceElapsedSimulationTime());
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
                    UiDrawText(TextFormat("%s Prime", bodies[i].name), (int)px + (int)scale + 6, (int)py - 8, 15,
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
                UiDrawText(TextFormat("%s %c", bodies[i].name,
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
            UiDrawText(TextFormat("Homeworld - %.0f blocks", homeDist),
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

#ifndef PLANET_TEXTURE_WIDTH
#define PLANET_TEXTURE_WIDTH 384
#endif
#ifndef PLANET_TEXTURE_HEIGHT
#define PLANET_TEXTURE_HEIGHT 192
#endif
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

static bool PlanetTextureSetIsReady(const PlanetTextureSet *textures)
{
    return textures && textures->albedo.id != 0 && textures->material.id != 0;
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
    if (!PlanetTextureSetIsReady(&textures)) {
        UnloadPlanetTextureSet(&textures);
    }
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
    double cycle = fmod(SpacePeriodicSimulationTime(
                            SpaceElapsedSimulationTime()) /
                        (double)profile->yearLength, 1.0);
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

    PlanetTextureSet textures = MakePlanetSurfaceTextures(&body->profile,
                                                          body->worldSeed);
    if (!PlanetTextureSetIsReady(&textures)) return (PlanetTextureSet){ 0 };

    PlanetTextureCacheEntry *entry = &planetTextures.planetTextures[replacement];
    if (entry->valid) UnloadPlanetTextureSet(&entry->textures);
    *entry = (PlanetTextureCacheEntry){
        .valid = true,
        .seed = body->worldSeed,
        .style = body->style,
        .oceanKey = oceanKey,
        .seasonKey = seasonKey,
        .lastUse = planetTextures.textureCacheTick,
        .textures = textures
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

    Texture2D texture = MakePlanetCloudTexture(&body->profile,
                                               body->worldSeed ^ 0x8392f5u);
    if (texture.id == 0) return (Texture2D){ 0 };

    PlanetCloudCacheEntry *entry = &planetTextures.cloudTextures[replacement];
    if (entry->valid && entry->texture.id != 0) UnloadTexture(entry->texture);
    *entry = (PlanetCloudCacheEntry){
        .valid = true,
        .seed = body->worldSeed,
        .profileKey = profileKey,
        .lastUse = planetTextures.textureCacheTick,
        .texture = texture
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
    double angle = (double)phase +
                   SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()) *
                   (double)speed * (double)direction;
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
    if (!PlanetTextureSetIsReady(&planetTextures.home)) {
        PlanetTextureSet home = MakePlanetSurfaceTextures(&homeProfile, 0x48a1c3u);
        if (PlanetTextureSetIsReady(&home)) {
            UnloadPlanetTextureSet(&planetTextures.home);
            planetTextures.home = home;
        }
    }

    if (planetTextures.homeClouds.id == 0 ||
        planetTextures.homeCloudSeed != homeSeed) {
        Texture2D clouds = MakePlanetCloudTexture(&homeProfile,
                                                  homeSeed ^ 0x8392f5u);
        if (clouds.id != 0) {
            if (planetTextures.homeClouds.id != 0) {
                UnloadTexture(planetTextures.homeClouds);
            }
            planetTextures.homeClouds = clouds;
            planetTextures.homeCloudSeed = homeSeed;
        }
    }
    planetTextures.initialized = PlanetTextureSetIsReady(&planetTextures.home);
}

void UnloadPlanetRenderResources(void)
{
    PlanetRendererShutdown();

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
            if (bodies[i].remnant.active) {
                Color remnantColor = bodies[i].remnant.blackHole
                    ? (Color){ 120, 150, 255, 255 }
                    : (Color){ 255, 120, 90, 255 };
                float remnantAlpha = (0.10f +
                                      bodies[i].remnant.ejectaStrength * 0.24f) *
                                     spaceFade;
                DrawSphereWires(
                    bodies[i].center,
                    bodies[i].remnant.proxyShockRadiusGame,
                    20, 20, Fade(remnantColor, remnantAlpha));
            }
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
    float homeRotation = -18.0f +
                         (float)SpacePeriodicSimulationTime(
                             SpaceElapsedSimulationTime()) * 1.2f;
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
        if (body->remnant.active) {
            line3 = TextFormat(
                "SNR %.2g kyr  shock %.2g pc  hazard %.2f",
                body->remnant.ageYears / 1000.0,
                body->remnant.physicalShockRadiusKm /
                    SPACE_REMNANT_PARSEC_KM,
                SpaceRemnantRadiationHazardAtDistance(
                    &body->remnant, body->dist));
        } else {
            line3 = TextFormat("Age %.2g Gyr  Luminous life %.2g Gyr",
                               body->hostStar.ageGyr,
                               body->hostStar.luminousLifetimeGyr);
        }
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
        if (body->remnantEnvironment.active) {
            line3 = TextFormat(
                "remnant hazard %.2f  ejecta %.2f  shell %.0f blocks",
                body->remnantEnvironment.radiationHazard,
                body->remnantEnvironment.ejectaDensity,
                body->remnantEnvironment.nearestShellDistanceGame);
        }
    }

    int sw = GetScreenWidth();
    int maxWidth = (int)fmaxf((float)sw - 64.0f, 120.0f);
    int fs = 18;
    int detailFs = 16;
    while (fs > 13 && UiMeasureText(line1, fs) > maxWidth) fs--;
    while (detailFs > 12 &&
           ((line2 && UiMeasureText(line2, detailFs) > maxWidth) ||
            (line3 && UiMeasureText(line3, detailFs) > maxWidth))) {
        detailFs--;
    }
    int width = UiMeasureText(line1, fs);
    if (line2) width = fmaxf((float)width, (float)UiMeasureText(line2, detailFs));
    if (line3) width = fmaxf((float)width, (float)UiMeasureText(line3, detailFs));
    int x = sw / 2 - width / 2;
    int y = 64;
    float height = line3 ? 84.0f : (line2 ? 62.0f : 40.0f);
    DrawRectangleRounded((Rectangle){ (float)x - 16, (float)y - 8, (float)width + 32, height },
                         0.10f, 6, Fade(BLACK, 0.55f));
    DrawRectangleRoundedLinesEx((Rectangle){ (float)x - 16, (float)y - 8, (float)width + 32, height },
                                0.10f, 6, 1.5f, Fade(WHITE, 0.30f));
    UiDrawText(line1, x, y, fs, WHITE);
    if (line2) UiDrawText(line2, x, y + 24, detailFs, Fade(WHITE, 0.82f));
    if (line3) UiDrawText(line3, x, y + 46, detailFs, Fade(WHITE, 0.72f));
}

int UiMeasureText(const char *text, int fontSize)
{
    if (!text) return 0;
    if (!uiFontReady) return MeasureText(text, fontSize);
    return (int)ceilf(MeasureTextEx(uiFont, text, (float)fontSize, 0.0f).x);
}

static void DrawUiTextLayer(const char *text, int x, int y, int fontSize,
                            Color color)
{
    if (uiFontReady) {
        DrawTextEx(uiFont, text, (Vector2){ (float)x, (float)y },
                   (float)fontSize, 0.0f, color);
    } else {
        DrawText(text, x, y, fontSize, color);
    }
}

void UiDrawText(const char *text, int x, int y, int fontSize, Color color)
{
    DrawUiTextLayer(text, x + 1, y + 2, fontSize, Fade(BLACK, 0.92f));
    DrawUiTextLayer(text, x, y, fontSize, color);
}

static float NormalizeHudHeading(float heading)
{
    heading = fmodf(heading, 360.0f);
    return heading < 0.0f ? heading + 360.0f : heading;
}

static void DrawShipHudLamp(int x, int y, Color color, const char *label)
{
    DrawCircle(x, y, 5.0f, Fade(BLACK, 0.90f));
    DrawCircle(x, y, 3.0f, color);
    DrawCircleLines(x, y, 5.0f, Fade(color, 0.55f));
    UiDrawText(label, x + 10, y - 7, 14, Fade(WHITE, 0.88f));
}

static void DrawShipHeadingTape(Rectangle tape, float heading, Color accent)
{
    float centerX = tape.x + tape.width * 0.5f;
    float baseline = tape.y + tape.height - 8.0f;
    DrawLine((int)tape.x, (int)baseline,
             (int)(tape.x + tape.width), (int)baseline,
             Fade(WHITE, 0.24f));

    for (int offset = -45; offset <= 45; offset += 15) {
        float x = centerX + (float)offset * tape.width / 90.0f;
        float tickHeight = offset % 30 == 0 ? 8.0f : 5.0f;
        DrawLine((int)x, (int)(baseline - tickHeight),
                 (int)x, (int)baseline, Fade(WHITE, 0.56f));
    }

    DrawTriangle((Vector2){ centerX, tape.y + 3.0f },
                 (Vector2){ centerX - 5.0f, tape.y + 10.0f },
                 (Vector2){ centerX + 5.0f, tape.y + 10.0f }, accent);
    const char *headingText = TextFormat("HDG %03.0f", NormalizeHudHeading(heading));
    int headingWidth = UiMeasureText(headingText, 15);
    UiDrawText(headingText, (int)(centerX - (float)headingWidth * 0.5f),
               (int)tape.y + 9, 15, accent);
}

void DrawShipHud(void)
{
    int sw = GetScreenWidth();
    float panelWidth = fminf(500.0f, (float)sw - 24.0f);
    Rectangle panel = {
        (float)sw - panelWidth - 12.0f, 12.0f, panelWidth, 216.0f
    };
    const Color cyan = { 137, 217, 235, 255 };
    const Color amber = { 255, 198, 76, 255 };
    const Color green = { 123, 218, 157, 255 };
    const Color red = { 238, 100, 82, 255 };
    DrawRectangleRounded(panel, 0.035f, 6, (Color){ 12, 18, 22, 224 });
    DrawRectangleRoundedLinesEx(panel, 0.035f, 6, 1.5f,
                                Fade(cyan, 0.42f));

    int left = (int)panel.x + 16;
    int top = (int)panel.y;
    bool warping = ShipIsWarping();
    bool assist = ShipFlightAssistEnabled();
    Color modeColor = warping ? cyan : (shipHudCruising ? cyan :
                                      (assist ? green : amber));
    const char *driveMode = warping ? "WARP" :
                            (shipHudCruising ? "CRUISE" :
                             (assist ? "ASSIST" : "INERTIA"));

    UiDrawText("FLIGHT COMPUTER", left, top + 10, 15, Fade(cyan, 0.82f));
    int modeWidth = UiMeasureText(driveMode, 14) + 20;
    DrawShipHudLamp((int)(panel.x + panel.width) - modeWidth - 8,
                    top + 19, modeColor, driveMode);
    DrawLine(left, top + 36, (int)(panel.x + panel.width) - 16, top + 36,
             Fade(cyan, 0.24f));

    int rightColumn = (int)(panel.x + panel.width * 0.56f);
    UiDrawText("VELOCITY", left, top + 45, 13, Fade(WHITE, 0.50f));
    UiDrawText(TextFormat("%.0f", shipHudSpeed), left, top + 57, 34, modeColor);
    int speedWidth = UiMeasureText(TextFormat("%.0f", shipHudSpeed), 34);
    UiDrawText("BLK/S", left + speedWidth + 8, top + 72, 13,
               Fade(WHITE, 0.52f));

    UiDrawText(shipHudNearPlanet ? "SURFACE ALT" : "ALTITUDE",
               rightColumn, top + 45, 13, Fade(WHITE, 0.50f));
    UiDrawText(TextFormat("%.0f", shipHudAlt), rightColumn, top + 60, 28,
               Fade(WHITE, 0.94f));
    int altitudeWidth = UiMeasureText(TextFormat("%.0f", shipHudAlt), 28);
    UiDrawText("BLK", rightColumn + altitudeWidth + 7, top + 72, 13,
               Fade(WHITE, 0.52f));

    DrawShipHeadingTape(
        (Rectangle){ panel.x + 16.0f, panel.y + 96.0f,
                     panel.width - 32.0f, 43.0f },
        shipHudHeading, amber);

    float fuelRatio = Clamp(ShipGetFuel() / SHIP_MAX_FUEL, 0.0f, 1.0f);
    Color fuelColor = fuelRatio > 0.20f ? amber : red;
    UiDrawText("FUEL", left, top + 146, 13, Fade(WHITE, 0.54f));
    UiDrawText(TextFormat("%03.0f%%", fuelRatio * 100.0f), left + 42,
               top + 145, 15, fuelColor);
    Rectangle fuelTrack = {
        panel.x + 16.0f, panel.y + 166.0f,
        panel.width * 0.48f - 22.0f, 7.0f
    };
    DrawRectangleRec(fuelTrack, Fade(WHITE, 0.13f));
    Rectangle fuelFill = fuelTrack;
    fuelFill.width *= fuelRatio;
    DrawRectangleRec(fuelFill, fuelColor);

    char environment[64];
    if (shipHudAtmosphere >= 0.0f) {
        snprintf(environment, sizeof(environment), "ATM %03.0f%%",
                 Clamp(shipHudAtmosphere, 0.0f, 100.0f));
    } else {
        snprintf(environment, sizeof(environment), "%s",
                 shipHudNearPlanet ? "SURFACE REF" : "VACUUM");
    }
    UiDrawText("ENV", rightColumn, top + 146, 13, Fade(WHITE, 0.54f));
    UiDrawText(environment, rightColumn + 34, top + 145, 15,
               shipHudAtmosphere > 70.0f ? amber : cyan);

    char gravity[128];
    if (ShipHasGravityPrimary()) {
        snprintf(gravity, sizeof(gravity), "GRAV %s  %.0f/%.0f",
                 ShipGravityPrimaryName(), ShipGravityPrimaryDistance(),
                 ShipGravitySphereOfInfluence());
    } else {
        snprintf(gravity, sizeof(gravity), "GRAV INTERPLANETARY");
    }

    char navigation[160];
    if (ShipHasWarpTarget()) {
        const char *targetKind = ShipWarpTargetIsSystem() ? "SYS" : "PLANET";
        snprintf(navigation, sizeof(navigation), "%s // %s // %s",
                 targetKind, ShipWarpTargetName(), warping ? "WARP" : "LOCK");
    } else {
        snprintf(navigation, sizeof(navigation), "SYS // %s // NO TARGET",
                 shipHudSystem);
    }
    int statusFont = 14;
    int statusWidth = (int)panel.width - 32;
    while (statusFont > 11 &&
           (UiMeasureText(gravity, statusFont) > statusWidth ||
            UiMeasureText(navigation, statusFont) > statusWidth)) statusFont--;
    UiDrawText(gravity, left, top + 180, statusFont, Fade(WHITE, 0.60f));
    UiDrawText(navigation, left, top + 198, statusFont,
               ShipHasWarpTarget() ? modeColor : Fade(WHITE, 0.76f));
}

static const char *ShipLocatorReturnHint(WorldDimension dimension)
{
    switch (dimension) {
    case WORLD_DIMENSION_HOME: return "return to Homeworld";
    case WORLD_DIMENSION_PLANET: return "travel to that planet";
    case WORLD_DIMENSION_SPACE: return "launch into space";
    case WORLD_DIMENSION_NETHER: return "enter the Nether";
    default: return "return to its location";
    }
}

void DrawShipLocator(const Camera3D *camera, const ShipLocatorTarget *target)
{
    if (!camera || !target || target->status == SHIP_LOCATOR_TARGET_NONE) return;

    const Color accent = (Color){ 255, 198, 76, 255 };
    if (target->status == SHIP_LOCATOR_TARGET_REMOTE) {
        int screenWidth = GetScreenWidth();
        if (screenWidth <= 64) return;
        char text[128];
        snprintf(text, sizeof(text), "SHIP  %s  |  %s", target->location,
                 ShipLocatorReturnHint(target->dimension));
        int fontSize = 17;
        int maxWidth = screenWidth - 32;
        while (fontSize > 12 && UiMeasureText(text, fontSize) + 28 > maxWidth) {
            fontSize--;
        }
        if (UiMeasureText(text, fontSize) + 28 > maxWidth) {
            snprintf(text, sizeof(text), "SHIP  %.24s", target->location);
            while (strlen(text) > 4u &&
                   UiMeasureText(text, fontSize) + 28 > maxWidth) {
                text[strlen(text) - 1u] = '\0';
            }
        }
        int width = fminf((float)(UiMeasureText(text, fontSize) + 28),
                          (float)maxWidth);
        Rectangle bar = {
            ((float)screenWidth - (float)width) * 0.5f,
            18.0f,
            (float)width,
            34.0f
        };
        DrawRectangleRounded(bar, 0.08f, 5, Fade(BLACK, 0.76f));
        DrawRectangleRoundedLinesEx(bar, 0.08f, 5, 1.0f, Fade(accent, 0.68f));
        UiDrawText(text, (int)bar.x + 12, (int)bar.y + 8, fontSize, accent);
        return;
    }

    Vector3 cameraForward = Vector3Normalize(
        Vector3Subtract(camera->target, camera->position));
    Vector3 toTarget = Vector3Subtract(target->position, camera->position);
    bool behind = Vector3DotProduct(cameraForward, toTarget) <= 0.0f;
    Vector2 projected = GetWorldToScreen(target->position, *camera);
    ShipLocatorMarkerLayout layout = ShipLocatorMarkerLayoutEvaluate(
        projected, behind, GetScreenWidth(), GetScreenHeight(), 54.0f);
    if (!layout.visible) return;

    if (layout.onScreen) {
        DrawCircleV(layout.position, 13.0f, Fade(BLACK, 0.68f));
        DrawCircleLines((int)layout.position.x, (int)layout.position.y,
                        12.0f, accent);
        DrawTriangle(
            (Vector2){ layout.position.x, layout.position.y - 7.0f },
            (Vector2){ layout.position.x - 6.0f, layout.position.y + 6.0f },
            (Vector2){ layout.position.x + 6.0f, layout.position.y + 6.0f },
            accent);
    } else {
        Vector2 perpendicular = {
            -layout.direction.y,
            layout.direction.x
        };
        DrawTriangle(
            Vector2Add(layout.position, Vector2Scale(layout.direction, 13.0f)),
            Vector2Add(layout.position, Vector2Scale(perpendicular, 7.0f)),
            Vector2Subtract(layout.position, Vector2Scale(perpendicular, 7.0f)),
            accent);
    }

    const char *label = TextFormat("SHIP  %.0f", target->distance);
    int fontSize = 16;
    int labelWidth = UiMeasureText(label, fontSize);
    int labelX = (int)(layout.position.x - (float)labelWidth * 0.5f);
    int labelY = (int)layout.position.y + 17;
    labelX = (int)Clamp((float)labelX, 10.0f,
                        (float)GetScreenWidth() - (float)labelWidth - 10.0f);
    labelY = (int)Clamp((float)labelY, 10.0f,
                        (float)GetScreenHeight() - 24.0f);
    UiDrawText(label, labelX, labelY, fontSize, accent);
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
        UiDrawText(i == 9 ? "0" : TextFormat("%d", i + 1), (int)rect.x + 6, (int)rect.y + 31, 14, Fade(WHITE, 0.85f));
        int count = InventoryCount(block);
        const char *countText = TextFormat("%d", count);
        int countFont = count >= 100 ? 11 : 13;
        int countWidth = UiMeasureText(countText, countFont);
        UiDrawText(countText, (int)(rect.x + rect.width - 5.0f - (float)countWidth), (int)rect.y + 31, countFont,
                 count > 0 ? WHITE : Fade((Color){ 238, 100, 82, 255 }, 0.9f));
    }

    UiDrawText(TextFormat("%s  x%d", BlockName(hotbar[selectedIndex]), InventoryCount(hotbar[selectedIndex])),
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
    int width = UiMeasureText(text, fontSize);
    UiDrawText(text, GetScreenWidth() / 2 - width / 2, y, fontSize, color);
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
    int textWidth = UiMeasureText(label, fontSize);
    UiDrawText(label, (int)(rect.x + rect.width * 0.5f - textWidth * 0.5f),
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

    UiDrawText(title, (int)rect.x + 18, (int)rect.y + 13, 22, WHITE);
    UiDrawText(subtitle, (int)rect.x + 18, (int)rect.y + 43, 15, Fade(WHITE, 0.72f));
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

    Rectangle seedRect = { sw / 2 - 252.0f, sh / 2 + 106.0f, 356.0f, 48.0f };
    Rectangle randomRect = { sw / 2 + 116.0f, sh / 2 + 106.0f, 136.0f, 48.0f };
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

    UiDrawText("World seed", (int)seedRect.x, (int)seedRect.y - 22, 16, Fade(WHITE, 0.78f));
    DrawRectangleRounded(seedRect, 0.07f, 8, (Color){ 28, 35, 42, 255 });
    DrawRectangleRoundedLinesEx(seedRect, 0.07f, 8, 2.0f,
                                validSeed ? (seedFocused ? WHITE : Fade(WHITE, 0.48f)) : (Color){ 230, 92, 82, 255 });
    UiDrawText(seedText, (int)seedRect.x + 15, (int)seedRect.y + 12, 22, WHITE);
    if (seedFocused) {
        int caretX = (int)seedRect.x + 15 + UiMeasureText(seedText, 22);
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

    Rectangle startRect = { sw / 2 - 130.0f, sh / 2 + 170.0f, 260.0f, 54.0f };
    Rectangle quitRect = { sw / 2 - 130.0f, sh / 2 + 236.0f, 260.0f, 48.0f };
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
    UiDrawText("Voxelcraft", x + 14, y + 12, 24, WHITE);
    UiDrawText("WASD move    Shift sprint    Space jump/swim", x + 14, y + 48, 17, RAYWHITE);
    UiDrawText("LMB break    RMB place    MMB pick block", x + 14, y + 73, 17, RAYWHITE);
    UiDrawText("F float    Ctrl down (float)    Wheel hotbar", x + 14, y + 98, 17, RAYWHITE);
    UiDrawText("Tab mouse    M star map/warp    L locate ship", x + 14, y + 123, 17, RAYWHITE);
    UiDrawText("RMB on placed album opens it", x + 14, y + 148, 17, RAYWHITE);
    UiDrawText("Esc pause    F6 day/night    O orbit paths", x + 14, y + 173, 17, RAYWHITE);
    UiDrawText("F4 view    F5 save    F9 load    F10 shot", x + 14, y + 198, 17, RAYWHITE);
    UiDrawText("Fly above y=120 to reach space", x + 14, y + 223, 17, RAYWHITE);
    UiDrawText("Break collects; place consumes blocks", x + 14, y + 248, 15, RAYWHITE);
    UiDrawText("Ship: RMB enter, Q lock planet, G warp/cancel", x + 14, y + 272, 15, RAYWHITE);
    UiDrawText("WASD thrust, X cruise, F assist, E exit", x + 14, y + 296, 15, RAYWHITE);
    UiDrawText("1-0 blocks    [ ] distance    Flat: I import image", x + 14, y + 320, 15, RAYWHITE);
    UiDrawText("Planet: C scanner, break cores for discoveries", x + 14, y + 344, 15, RAYWHITE);
    const char *mode = ShipIsDriving() ? "Ship" : (floating ? "Floating" : "Walking");
    UiDrawText(TextFormat("%s    %s    View %d    FPS %d", mode,
                          cursorReleased ? "Mouse free" : "Mouse locked", viewDistance, GetFPS()),
               x + 14, y + 372, 16, Fade(RAYWHITE, 0.9f));
}

void DrawCursorReleasedOverlay(void)
{
    const char *text = "Mouse released - press Tab to return";
    int fontSize = 20;
    int width = UiMeasureText(text, fontSize) + 28;
    int x = GetScreenWidth() / 2 - width / 2;
    int y = GetScreenHeight() - 132;
    Rectangle rect = { (float)x, (float)y, (float)width, 46.0f };
    DrawRectangleRounded(rect, 0.08f, 8, Fade(BLACK, 0.58f));
    DrawRectangleRoundedLinesEx(rect, 0.08f, 8, 1.5f, Fade(WHITE, 0.42f));
    UiDrawText(text, x + 14, y + 13, fontSize, WHITE);
}

void DrawImportStatus(void)
{
    float timer = WorldGetImportMessageTimer();
    if (timer <= 0.0f) return;

    const char *message = WorldGetImportMessage();
    int fontSize = 18;
    int padding = 12;
    int width = UiMeasureText(message, fontSize) + padding * 2;
    int x = GetScreenWidth() / 2 - width / 2;
    int y = 18;
    Rectangle rect = { (float)x, (float)y, (float)width, 42.0f };
    DrawRectangleRounded(rect, 0.08f, 8, Fade(BLACK, 0.54f));
    DrawRectangleRoundedLinesEx(rect, 0.08f, 8, 1.5f, Fade(WHITE, 0.38f));
    UiDrawText(message, x + padding, y + 12, fontSize, WHITE);
}

const char *VisiblePathTail(const char *path, int maxWidth, int fontSize)
{
    if (UiMeasureText(path, fontSize) <= maxWidth) return path;

    const char *tail = path;
    int available = maxWidth - UiMeasureText("...", fontSize);
    while (*tail && UiMeasureText(tail, fontSize) > available) tail++;
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

    UiDrawText("Import image as blocks", (int)panel.x + 30, (int)panel.y + 24, 28, WHITE);
    UiDrawText("Flat mode only. PNG, JPG, BMP, TGA, GIF, QOI, PSD, HDR.",
             (int)panel.x + 30, (int)panel.y + 62, 16, Fade(WHITE, 0.72f));

    DrawRectangleRounded(input, 0.05f, 8, (Color){ 15, 20, 25, 255 });
    DrawRectangleRoundedLinesEx(input, 0.05f, 8, 2.0f, (Color){ 98, 160, 115, 255 });

    const char *shown = dialog->path[0] ? VisiblePathTail(dialog->path, (int)input.width - 44, 20) : "";
    int textX = (int)input.x + 16;
    if (shown != dialog->path) {
        UiDrawText("...", textX, (int)input.y + 13, 20, Fade(WHITE, 0.85f));
        textX += UiMeasureText("...", 20);
    }
    UiDrawText(shown, textX, (int)input.y + 13, 20, dialog->path[0] ? WHITE : Fade(WHITE, 0.38f));

    if (((int)(GetTime() * 2.0) % 2) == 0) {
        int cursorX = textX + UiMeasureText(shown, 20) + 2;
        DrawLine(cursorX, (int)input.y + 11, cursorX, (int)input.y + 35, WHITE);
    }

    const char *modeText = TextFormat("Mode: %s  (Tab toggles)",
                                      dialog->relief ? "Grayscale relief" : "Flat color");
    int modeWidth = UiMeasureText(modeText, 18);
    UiDrawText(modeText, sw / 2 - modeWidth / 2, (int)panel.y + 140, 18, WHITE);

    Rectangle minusRect = { panel.x + 30.0f, panel.y + 166.0f, 44.0f, 36.0f };
    Rectangle plusRect = { panel.x + panel.width - 74.0f, panel.y + 166.0f, 44.0f, 36.0f };
    if (DrawMenuButton(minusRect, "-", false)) {
        dialog->maxBlocks = AdjustImportPrecision(dialog->maxBlocks, -IMPORT_PRECISION_STEP);
    }
    if (DrawMenuButton(plusRect, "+", false)) {
        dialog->maxBlocks = AdjustImportPrecision(dialog->maxBlocks, IMPORT_PRECISION_STEP);
    }

    const char *precisionText = TextFormat("Precision: max %d blocks per side (min 16)", dialog->maxBlocks);
    int precisionWidth = UiMeasureText(precisionText, 18);
    UiDrawText(precisionText, sw / 2 - precisionWidth / 2, (int)panel.y + 174, 18, WHITE);

    UiDrawText("Type path or Ctrl+V paste. Tab mode. [ ] adjusts. Enter imports. Esc cancels.",
             (int)panel.x + 30, (int)panel.y + 218, 16, Fade(WHITE, 0.76f));
}

void DrawDebugHUD(Vector3 playerPosition, float yaw, float pitch, float daylight,
                  const PlanetLightState *light,
                  const PlanetObservationState *observation,
                  float seasonProgress,
                  const WeatherVisualState *weatherVisual)
{
    int x = 18;
    int y = 76;
    int line = 20;
    int fs = 14;

    UiDrawText(TextFormat("XYZ %.1f %.1f %.1f   yaw %.1f pitch %.1f",
                        playerPosition.x, playerPosition.y, playerPosition.z, yaw * RAD2DEG, pitch * RAD2DEG),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    UiDrawText(TextFormat("FPS %d   frame %.2f ms", GetFPS(), GetFrameTime() * 1000.0f), x, y, fs, Fade(WHITE, 0.85f)); y += line;
    UiDrawText(TextFormat("Chunks loaded %d   gen queue %d   mesh queue %d",
                        GetActiveChunkCount(), GetPendingGenJobCount(), GetPendingMeshJobCount()),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    ChunkStreamingStats streaming = ChunksGetStreamingStats();
    UiDrawText(TextFormat("Stream gen %.1fms  mesh %.1fms  upload %.1fms (max %.2f)  sync %llu",
                        streaming.generationCpuMs, streaming.meshCpuMs,
                        streaming.uploadCpuMs, streaming.maxUploadCpuMs,
                        (unsigned long long)streaming.syncRebuilds),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    UiDrawText(TextFormat("Stream peak queues gen %llu  mesh %llu  upload defers %llu",
                        (unsigned long long)streaming.generationQueuePeak,
                        (unsigned long long)streaming.meshQueuePeak,
                        (unsigned long long)streaming.uploadBudgetDeferrals),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    UiDrawText(TextFormat("Particles %d   edits %d   render dist %d",
                        ParticlesActiveCount(), WorldGetEditCount(), renderDistanceChunks),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    UiDrawText(TextFormat("Space %d/%d   nether %d   entities %d",
                        GetActiveSpaceChunkCount(), SpaceEditCountForHud,
                        GetActiveNetherChunkCount(), GetActiveEntityCount()),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    UiDrawText(TextFormat("Weather %s   time %02d:00   auto-save %s",
                        WeatherName(), (int)(dayTimeForHud * 24.0f) % 24,
                        autoSaveForHud ? "on" : "off"),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    if (HomeWorldSurfaceIsActive() || PlanetWorldIsActive()) {
        UiDrawText(TextFormat("Weather cloud %.2f   precip %.2f   storm %.2f   wind %.2f",
                            WeatherCloudCover(), WeatherPrecipitationRate(),
                            WeatherStormIntensity(), WeatherWindIntensity()),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        if (weatherVisual && weatherVisual->active) {
            UiDrawText(TextFormat("Visibility %.2f   fog %.2f   veil %.2f   cloud AGL %.1f",
                                weatherVisual->visibility,
                                weatherVisual->fogDensity,
                                weatherVisual->precipitationVeil,
                                weatherVisual->cloudBaseHeight),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
        }
    }
    SolarSystemDef hudSystem;
    float hudSystemDist = 0.0f;
    if (FindSystemForGuide(playerPosition, &hudSystem, &hudSystemDist)) {
        UiDrawText(TextFormat("System %s Prime (%.0f)", hudSystem.name, hudSystemDist),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    } else {
        UiDrawText("Deep space", x, y, fs, Fade(WHITE, 0.85f)); y += line;
    }
    if (PlanetWorldIsActive()) {
        if (light && observation && observation->valid) {
            UiDrawText(TextFormat(
                         "Sky %s   season %.0f%%   day %.1fh   local flux %.3f   stars %.2f   moon %.2f   eclipse %.2f",
                         PlanetObservationPhaseName(observation->phase),
                         Clamp(seasonProgress, 0.0f, 1.0f) * 100.0f,
                         Clamp(light->dayLengthFraction, 0.0f, 1.0f) * 24.0f,
                         fmaxf(light->incidentIrradiance, 0.0f),
                         observation->starVisibility,
                         observation->moonVisibility,
                         observation->eclipseDarkening),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
        }
        PlanetLocalEcology ecology = PlanetEcologyLocalAt(
            (int)floorf(playerPosition.x), (int)floorf(playerPosition.z), daylight);
        UiDrawText(TextFormat("Ecology capacity %.2f   flora %.2f   fauna %.2f   limit %s",
                            ecology.suitability.carryingCapacity,
                            ecology.suitability.floraCapacity,
                            ecology.suitability.faunaCapacity,
                            PlanetEcologyLimitingFactorName(
                                ecology.suitability.limitingFactor)),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Activity flora %.2f   fauna %.2f   water %.2f   rain %.2f",
                            ecology.suitability.floraActivity,
                            ecology.suitability.faunaActivity,
                            ecology.environment.liquidWaterAccess,
                            ecology.environment.precipitationRate),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Region (%d,%d)   disturbance %.3f   flora stress %.3f",
                            ecology.diagnostics.regionX,
                            ecology.diagnostics.regionZ,
                            ecology.environment.disturbance,
                            ecology.environment.disturbance * 0.82f),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Fauna harvest %.3f   stress %.3f   net %+.5f/day",
                            ecology.population.faunaHarvestPressure,
                            ecology.diagnostics.faunaStress,
                            ecology.diagnostics.faunaNetRecoveryRate),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Population flora %.2f/%.2f   fauna %.2f/%.2f   seasonal %.2f   radiation memory %.2f",
                            ecology.population.floraDensity,
                            ecology.population.floraCarryingCapacity,
                            ecology.population.faunaDensity,
                            ecology.population.faunaCarryingCapacity,
                            ecology.population.seasonalMemory,
                            ecology.diagnostics.radiationMemory),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Migration net flora %+.3f   fauna %+.3f",
                            ecology.migration.floraNet,
                            ecology.migration.faunaNet),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Flow flora (%+.3f,%+.3f)   fauna (%+.3f,%+.3f)",
                            ecology.migration.floraFlowX,
                            ecology.migration.floraFlowZ,
                            ecology.migration.faunaFlowX,
                            ecology.migration.faunaFlowZ),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Climate %.0f/%.0f K   light %.2f/%.2f   storm %.2f   radiation %.2f   ejecta %.2f",
                            ecology.environment.meanTemperatureK,
                            ecology.environment.currentTemperatureK,
                            ecology.environment.meanUsableLight,
                            ecology.environment.currentUsableLight,
                            ecology.environment.currentStorm,
                            ecology.environment.radiationExposure,
                            ecology.environment.ejectaExposure),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Terrain elevation %.2f   slope %.2f   shelter %.2f",
                            ecology.environment.elevation,
                            ecology.environment.slope,
                            ecology.environment.shelter),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    }
    SpaceScaleDiagnostics scale;
    if (SpaceScaleDiagnosticsAt(playerPosition, &scale)) {
        UiDrawText(TextFormat("Scale %.0f u/AU   1 play s = 1 sim day   error %.3f ppm [%s]",
                            SPACE_UNITS_GAME_DISTANCE_PER_AU,
                            scale.maxRelativeError * 1000000.0,
                            scale.withinErrorBudget ? "OK" : "OUT"),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("%s radius %.0f km = %.5f linear u",
                            scale.bodyName, scale.physicalRadiusKm,
                            scale.physicalRadiusGame),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Proxy visual %.1f u   landing %.1f u   x%.0f",
                            scale.visualRadiusGame, scale.landingRadiusGame,
                            scale.landingRadiusScale),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Gravity %.2f m/s2 (%.2f g)   gameplay %.2f u/s2",
                            scale.physicalGravityMetersPerSecondSquared,
                            scale.physicalGravityEarth,
                            scale.gameplaySurfaceGravity),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Orbit speed %.2f km/s   %.3f u/play-s",
                            scale.orbitalSpeedKilometersPerSecond,
                            scale.orbitalSpeedGame),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("SOI %.0f km (%.3f linear u)   Hill %.0f km",
                            scale.sphereOfInfluenceKm,
                            scale.physicalSphereOfInfluenceGame,
                            scale.hillSphereKm),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Encounter %.1f u   x%.1f [%s]",
                            scale.encounterRadiusGame,
                            scale.encounterRadiusScale,
                            scale.encounterRadiusClamped ? "proxy clamp" : "physical"),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Flux now %.3f Earth   climate mean %.3f Earth",
                            scale.currentIrradianceEarth,
                            scale.climateIrradianceEarth),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Temperature radiative %.0f K   surface %.0f K",
                            scale.radiativeTemperatureK,
                            scale.surfaceTemperatureK),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    } else {
        UiDrawText("Scale target: no planet within 700 u", x, y, fs,
                 Fade(WHITE, 0.68f)); y += line;
    }
    if (!PlanetWorldIsActive() && !HomeWorldSurfaceIsActive()) {
        SpaceSatelliteScaleDiagnostics satelliteScale;
        if (SpaceSatelliteScaleDiagnosticsAt(playerPosition,
                                             &satelliteScale)) {
            UiDrawText(TextFormat("Moon %s radius %.0f km = %.5f linear u",
                                satelliteScale.bodyName,
                                satelliteScale.physicalRadiusKm,
                                satelliteScale.physicalRadiusGame),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
            UiDrawText(TextFormat("Moon gravity %.2f m/s2   orbit %.2f km/s",
                                satelliteScale.physicalGravityMetersPerSecondSquared,
                                satelliteScale.orbitalSpeedKilometersPerSecond),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
            UiDrawText(TextFormat("Moon SOI %.0f km   Hill %.0f km",
                                satelliteScale.sphereOfInfluenceKm,
                                satelliteScale.hillSphereKm),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
            UiDrawText(TextFormat("Moon encounter %.2f u [%s]",
                                satelliteScale.encounterRadiusGame,
                                satelliteScale.withinErrorBudget ? "OK" : "OUT"),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
        }
    }
    UiDrawText(TextFormat("Block %s   music %s", BlockName(blockForHud),
                        AudioIsMusicEnabled() ? "on" : "off"),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    if (ShipIsDriving()) {
        UiDrawText(TextFormat("Ship speed %.1f blocks/s", shipSpeedForHud), x, y, fs, Fade(WHITE, 0.85f)); y += line;
    }
}

static bool DrawSettingStepper(Rectangle row, const char *label, float *value)
{
    bool changed = false;
    UiDrawText(label, (int)row.x, (int)row.y + 10, 17, Fade(WHITE, 0.84f));
    Rectangle minus = { row.x + row.width - 126.0f, row.y, 38.0f, 38.0f };
    Rectangle plus = { row.x + row.width - 38.0f, row.y, 38.0f, 38.0f };
    if (DrawMenuButton(minus, "-", false)) {
        *value = fmaxf(0.0f, *value - 0.10f);
        changed = true;
    }
    if (DrawMenuButton(plus, "+", false)) {
        *value = fminf(1.0f, *value + 0.10f);
        changed = true;
    }
    const char *percentage = TextFormat("%d%%", (int)roundf(*value * 100.0f));
    int textWidth = UiMeasureText(percentage, 16);
    UiDrawText(percentage, (int)(row.x + row.width - 63.0f - textWidth * 0.5f),
               (int)row.y + 11, 16, WHITE);
    return changed;
}

void DrawPauseMenu(GameSettings *settings, PauseMenuActions *actions)
{
    if (!settings || !actions) return;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.55f));

    float panelHeight = fminf(660.0f, (float)sh - 36.0f);
    Rectangle panel = { sw / 2 - 270.0f, ((float)sh - panelHeight) * 0.5f,
                        540.0f, panelHeight };
    DrawRectangleRounded(panel, 0.05f, 8, (Color){ 30, 38, 45, 245 });
    DrawRectangleRoundedLinesEx(panel, 0.05f, 8, 2.0f, Fade(WHITE, 0.45f));

    int left = (int)panel.x + 36;
    int contentWidth = (int)panel.width - 72;
    int y = (int)panel.y + 26;
    DrawCenteredText("Paused", y, 34, WHITE);
    y += 58;

    UiDrawText("Graphics quality", left, y, 17, Fade(WHITE, 0.84f));
    y += 30;
    float segmentWidth = ((float)contentWidth - 12.0f) / 3.0f;
    for (int quality = 0; quality < GRAPHICS_QUALITY_COUNT; quality++) {
        Rectangle segment = { (float)left + quality * (segmentWidth + 6.0f),
                              (float)y, segmentWidth, 40.0f };
        bool selected = settings->graphicsQuality == (GraphicsQuality)quality;
        if (DrawMenuButton(segment,
                           GraphicsQualityName((GraphicsQuality)quality), selected) &&
            !selected) {
            settings->graphicsQuality = (GraphicsQuality)quality;
            actions->settingsChanged = true;
            actions->qualityChanged = true;
        }
    }
    y += 58;
    Rectangle volumeRow = { (float)left, (float)y, (float)contentWidth, 38.0f };
    if (DrawSettingStepper(volumeRow, "Master volume", &settings->masterVolume)) {
        actions->settingsChanged = true;
    }
    volumeRow.y += 48.0f;
    if (DrawSettingStepper(volumeRow, "Environment", &settings->ambientVolume)) {
        actions->settingsChanged = true;
    }
    volumeRow.y += 48.0f;
    if (DrawSettingStepper(volumeRow, "Music volume", &settings->musicVolume)) {
        actions->settingsChanged = true;
    }
    y += 160;
    Rectangle musicRect = { (float)left, (float)y, (float)contentWidth, 40.0f };
    if (DrawMenuButton(musicRect,
                       TextFormat("Music: %s", settings->musicEnabled ? "On" : "Off"),
                       settings->musicEnabled)) {
        settings->musicEnabled = !settings->musicEnabled;
        actions->settingsChanged = true;
    }
    y += 58;
    Rectangle resumeRect = { (float)left, (float)y, (float)contentWidth, 44.0f };
    if (DrawMenuButton(resumeRect, "Resume", true)) actions->resume = true;
    y += 54;
    Rectangle saveRect = { (float)left, (float)y, 226.0f, 42.0f };
    Rectangle menuRect = { (float)left + 242.0f, (float)y, 226.0f, 42.0f };
    if (DrawMenuButton(saveRect, "Save World", false)) actions->saveWorld = true;
    if (DrawMenuButton(menuRect, "Return to Menu", false)) actions->returnToMenu = true;
    y += 52;
    Rectangle quitRect = { (float)left, (float)y, (float)contentWidth, 42.0f };
    if (DrawMenuButton(quitRect, "Save & Quit", false)) actions->saveAndQuit = true;
}
