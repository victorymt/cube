#include "presentation/render.h"
#include "presentation/render_internal.h"

#include "raymath.h"
#include "rlgl.h"
#include "world/block_atlas.h"
#include "world/chunks.h"
#include "gameplay/inventory.h"
#include "world/world.h"
#include "gameplay/interaction.h"
#include "space/planet_material.h"
#include "space/planet_observation.h"
#include "presentation/planet_renderer.h"
#include "space/planet_surface.h"
#include "world/terrain.h"
#include "presentation/particles.h"
#include "space/space_query.h"
#include "space/space_state.h"
#include "space/space_units.h"
#include "world/world_environment.h"
#include "world/nether.h"
#include "ecology/entity.h"
#include "gameplay/ship.h"
#include "presentation/audio.h"
#include "world/weather.h"
#include "ecology/ecology.h"
#include "core/perf.h"
#include "presentation/render_sort.h"
#include "world/world_lighting.h"
#include "presentation/world_renderer.h"

#include <math.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define STAR_SKY_RANGE (STAR_NAVIGATION_RANGE * 4.0f)
#define STAR_SKY_REFRESH_DISTANCE (STAR_SYSTEM_SPACING * 3.0f)

static SolarSystemDef skySystems[STAR_SYSTEM_QUERY_MAX];
static int skySystemCount = 0;
static Vector3 skySystemCenter = { 0 };
static bool skySystemCacheValid = false;
static uint32_t skySystemWorldSeed = 0;
float planetSceneExposure = 1.12f;
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

#include "world/chunks.h"
#include "world/world.h"
#include "gameplay/interaction.h"
#include "world/terrain.h"
#include "presentation/particles.h"
#include "space/space_query.h"
#include "space/space_state.h"
#include "world/nether.h"
#include "ecology/entity.h"
#include "gameplay/ship.h"
#include "presentation/audio.h"
#include "world/weather.h"
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

Color PlanetAtmosphereBaseColor(const PlanetProfile *profile)
{
    if (profile) {
        switch (profile->canonicalBodyId) {
        case 2u: return (Color){ 218, 174, 72, 255 };
        case 3u: return (Color){ 82, 154, 218, 255 };
        case 4u: return (Color){ 198, 112, 70, 255 };
        case 5u: return (Color){ 202, 164, 126, 255 };
        case 6u: return (Color){ 220, 198, 150, 255 };
        case 7u: return (Color){ 116, 205, 213, 255 };
        case 8u: return (Color){ 55, 103, 206, 255 };
        default: break;
        }
    }
    SolarBodyStyle style = profile ? profile->style : SOLAR_STYLE_SUN;
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

PlanetAtmosphereVisual PlanetAtmosphereVisualFor(const PlanetProfile *profile)
{
    PlanetAtmosphereVisual visual = { 0 };
    if (!profile) return visual;

    float density = Clamp(profile->atmosphereDensity, 0.0f, 1.0f);
    float gravity = fmaxf(profile->surfaceGravity, 0.20f);
    float temperature = fmaxf(profile->equilibriumTempK, 80.0f);
    visual.scaleHeight = Clamp((temperature / 288.0f) / gravity, 0.52f, 1.85f);
    Color base = PlanetAtmosphereBaseColor(profile);

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

static uint32_t WarpTunnelHash(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

static float WarpTunnelUnit(uint32_t value)
{
    return (float)(WarpTunnelHash(value) & 0x00ffffffu) / 16777215.0f;
}

static float WarpTunnelEnvelope(float phase)
{
    float enter = Clamp(phase / 0.10f, 0.0f, 1.0f);
    float exit = Clamp((1.0f - phase) / 0.18f, 0.0f, 1.0f);
    return fminf(enter, exit);
}

static Vector2 WarpTunnelPoint(Vector2 center, float angle, float depth,
                               float radiusX, float radiusY)
{
    return (Vector2){
        center.x + cosf(angle) * radiusX * depth,
        center.y + sinf(angle) * radiusY * depth
    };
}

void DrawWarpTunnel(const Camera3D *camera, float intensity,
                    bool supercruise)
{
    if (!camera || !isfinite(intensity) || intensity <= 0.0f) return;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    if (sw <= 0 || sh <= 0) return;
    intensity = Clamp(intensity, 0.0f, 1.0f);

    Vector3 look = Vector3Subtract(camera->target, camera->position);
    float lookLength = Vector3Length(look);
    if (!(lookLength > 0.0001f) || !isfinite(lookLength)) return;
    look = Vector3Scale(look, 1.0f / lookLength);
    Vector2 center = GetWorldToScreen(
        Vector3Add(camera->position, Vector3Scale(look, 64.0f)), *camera);
    if (!isfinite(center.x) || !isfinite(center.y)) {
        center = (Vector2){ (float)sw * 0.5f, (float)sh * 0.5f };
    }
    center.x = Clamp(center.x, (float)sw * 0.20f, (float)sw * 0.80f);
    center.y = Clamp(center.y, (float)sh * 0.20f, (float)sh * 0.80f);

    double clock = GetTime();
    float time = isfinite(clock) ? (float)fmod(clock, 4096.0) : 0.0f;
    float travel = supercruise ? 0.24f + intensity * 1.10f
                               : 0.34f + intensity * 1.85f;
    float radiusX = (float)sw * 0.78f;
    float radiusY = (float)sh * 0.78f;

    DrawRectangle(0, 0, sw, sh, supercruise
        ? (Color){ 16, 11, 3,
                   (unsigned char)(4.0f + intensity * 12.0f) }
        : (Color){ 1, 8, 15,
                   (unsigned char)(10.0f + intensity * 26.0f) });

    BeginBlendMode(BLEND_ADDITIVE);
    float corePulse = 0.88f + 0.12f * sinf(time * 8.0f);
    float coreRadius = fminf((float)sw, (float)sh) *
                       (0.055f + intensity * 0.035f) * corePulse;
    DrawCircleGradient((int)center.x, (int)center.y, coreRadius,
                       supercruise
                           ? (Color){ 255, 235, 183,
                                      (unsigned char)(22.0f + intensity * 42.0f) }
                           : (Color){ 205, 244, 255,
                                      (unsigned char)(34.0f + intensity * 54.0f) },
                       BLANK);

    const int ringCount = supercruise ? 4 : 6;
    const int ringSegments = 48;
    for (int ring = 0; ring < ringCount; ring++) {
        float phase = fmodf((float)ring / (float)ringCount +
                                time * travel * 0.38f,
                            1.0f);
        float depth = powf(phase, 1.62f);
        float alpha = WarpTunnelEnvelope(phase) *
                      (24.0f + intensity * 64.0f);
        float width = 0.75f + phase * (0.75f + intensity);
        for (int segment = 0; segment < ringSegments; segment++) {
            if ((segment + ring * 3) % 7 >= 5) continue;
            float angle0 = 2.0f * PI * (float)segment /
                           (float)ringSegments;
            float angle1 = 2.0f * PI * (float)(segment + 1) /
                           (float)ringSegments;
            Vector2 start = WarpTunnelPoint(center, angle0, depth,
                                             radiusX, radiusY);
            Vector2 end = WarpTunnelPoint(center, angle1, depth,
                                           radiusX, radiusY);
            DrawLineEx(start, end, width,
                       supercruise
                           ? (Color){ 255, 193, 91, (unsigned char)(alpha * 0.72f) }
                           : (Color){ 96, 211, 235, (unsigned char)alpha });
        }
    }

    const int streakCount = supercruise ? 56 : 88;
    for (int streak = 0; streak < streakCount; streak++) {
        uint32_t seed = WarpTunnelHash((uint32_t)streak + 0x51f2a39du);
        float angle = WarpTunnelUnit(seed) * 2.0f * PI;
        float lane = 0.72f + WarpTunnelUnit(seed ^ 0xa4c31b09u) * 0.52f;
        float pace = 0.82f + WarpTunnelUnit(seed ^ 0x1d93e5abu) * 0.42f;
        float phase = fmodf(WarpTunnelUnit(seed ^ 0xc736f821u) +
                                time * travel * pace,
                            1.0f);
        float tailPhase = fmaxf(
            phase - (0.025f + intensity * 0.12f), 0.0f);
        float headDepth = powf(phase, 1.72f);
        float tailDepth = powf(tailPhase, 1.72f);
        float twist = (1.0f - headDepth) *
                      sinf(time * 0.9f + (float)streak) * 0.035f;
        Vector2 head = WarpTunnelPoint(center, angle + twist, headDepth,
                                        radiusX * lane, radiusY * lane);
        Vector2 tail = WarpTunnelPoint(center, angle + twist, tailDepth,
                                        radiusX * lane, radiusY * lane);
        float alpha = WarpTunnelEnvelope(phase) *
                      (54.0f + intensity * 190.0f);
        float width = 0.8f + phase * intensity * 2.8f;
        Color color;
        if (supercruise) {
            alpha *= 0.72f;
            switch (seed & 3u) {
            case 0u: color = (Color){ 255, 245, 214, (unsigned char)alpha }; break;
            case 1u: color = (Color){ 255, 201, 112, (unsigned char)alpha }; break;
            case 2u: color = (Color){ 255, 224, 157, (unsigned char)alpha }; break;
            default: color = (Color){ 238, 178, 86, (unsigned char)alpha }; break;
            }
        } else {
            switch (seed & 3u) {
            case 0u: color = (Color){ 224, 249, 255, (unsigned char)alpha }; break;
            case 1u: color = (Color){ 93, 218, 235, (unsigned char)alpha }; break;
            case 2u: color = (Color){ 104, 184, 255, (unsigned char)alpha }; break;
            default: color = (Color){ 190, 255, 231, (unsigned char)alpha }; break;
            }
        }
        DrawLineEx(tail, head, width, color);
        if (phase > 0.32f) {
            DrawCircleV(head, 0.7f + width * 0.42f, color);
        }
    }

    DrawCircleGradient((int)center.x, (int)center.y,
                       coreRadius * (0.32f + intensity * 0.12f),
                       supercruise
                           ? (Color){ 255, 249, 222,
                                      (unsigned char)(42.0f + intensity * 58.0f) }
                           : (Color){ 246, 255, 255,
                                      (unsigned char)(70.0f + intensity * 80.0f) },
                       BLANK);
    EndBlendMode();
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
        if (!surfaceActive && primaryDistance < SOLAR_SYSTEM_QUERY_RADIUS) continue;

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
