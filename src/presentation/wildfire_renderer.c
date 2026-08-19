#include "presentation/wildfire_renderer.h"

#include "core/perf.h"

#include "raymath.h"

#include <math.h>

static float WildfireRenderUnit(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    return value >= 1.0f ? 1.0f : value;
}

static uint32_t WildfireRenderMix(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static float WildfireRenderNoise(const WeatherImpactFireSnapshot *fire,
                                 unsigned lane)
{
    uint32_t value = fire->surfaceId ^ (uint32_t)fire->x * 0x9e3779b9u ^
                     (uint32_t)fire->y * 0x85ebca6bu ^
                     (uint32_t)fire->z * 0xc2b2ae35u ^
                     lane * 0x27d4eb2du;
    return (float)(WildfireRenderMix(value) & 0xffffu) / 65535.0f;
}

static void WildfireDrawPrimitiveCylinder(Vector3 start, Vector3 end,
                                          float startRadius,
                                          float endRadius, Color color)
{
    DrawCylinderEx(start, end, startRadius, endRadius, 7, color);
    PerfRecordDrawCall(PERF_DRAW_FIRE);
}

static void WildfireDrawPrimitiveSphere(Vector3 center, float radius,
                                        Color color)
{
    DrawSphereEx(center, radius, 5, 7, color);
    PerfRecordDrawCall(PERF_DRAW_FIRE);
}

static void DrawWildfireSource(const WeatherImpactFireSnapshot *fire,
                               const GraphicsQualityProfile *profile,
                               const WeatherVisualState *weather,
                               double simulationTime, float daylight)
{
    float intensity = WildfireRenderUnit(fire->state.intensity);
    float heat = WildfireRenderUnit(fire->state.heatOutput);
    float smoke = WildfireRenderUnit(fire->state.smokeOutput);
    float moisture = WildfireRenderUnit(fire->state.moisture);
    float time = isfinite(simulationTime) ? (float)simulationTime : 0.0f;
    Vector3 base = {
        (float)fire->x + 0.5f,
        (float)fire->y + 1.02f,
        (float)fire->z + 0.5f
    };

    Color ember = {
        255, (unsigned char)(72.0f + heat * 74.0f), 18,
        (unsigned char)(72.0f + heat * 130.0f)
    };
    WildfireDrawPrimitiveSphere(base, 0.16f + heat * 0.18f, ember);

    int tongues = fire->state.phase == WILDFIRE_PHASE_SMOLDERING ? 0 :
                  profile->wildfireFlameTongues;
    float phaseScale = fire->state.phase == WILDFIRE_PHASE_IGNITING ?
        0.62f : 1.0f;
    for (int tongue = 0; tongue < tongues; tongue++) {
        float lane = WildfireRenderNoise(fire, (unsigned)tongue + 1u);
        float angle = lane * 2.0f * PI + time * (0.52f + lane * 0.34f);
        float radius = 0.07f + lane * 0.16f;
        Vector3 start = {
            base.x + cosf(angle) * radius,
            base.y,
            base.z + sinf(angle) * radius
        };
        float pulse = 0.82f + 0.18f *
            sinf(time * (5.2f + lane * 2.8f) + lane * 17.0f);
        float height = (0.42f + intensity * 2.15f) * phaseScale * pulse *
                       (0.78f + lane * 0.42f);
        Vector3 tip = {
            start.x + sinf(time * 2.1f + lane * 12.0f) * 0.10f,
            start.y + height,
            start.z + cosf(time * 1.8f + lane * 9.0f) * 0.10f
        };
        Color outer = {
            238, (unsigned char)(64.0f + intensity * 82.0f), 18,
            (unsigned char)(116.0f + intensity * 106.0f)
        };
        Color inner = {
            255, (unsigned char)(188.0f + intensity * 52.0f), 72,
            (unsigned char)(142.0f + intensity * 104.0f)
        };
        WildfireDrawPrimitiveCylinder(
            start, tip, 0.17f + intensity * 0.13f, 0.015f, outer);
        Vector3 innerTip = Vector3Lerp(start, tip, 0.72f);
        WildfireDrawPrimitiveCylinder(
            start, innerTip, 0.09f + intensity * 0.08f, 0.008f, inner);
    }

    float wind = weather ? WildfireRenderUnit(weather->windDrift) : 0.0f;
    float windAngle = weather && isfinite(weather->windAngle) ?
        weather->windAngle : 0.0f;
    float windX = cosf(windAngle);
    float windZ = sinf(windAngle);
    float light = WildfireRenderUnit(daylight);
    int puffs = smoke > 0.01f ? profile->wildfireSmokePuffs : 0;
    for (int puff = 0; puff < puffs; puff++) {
        float lane = WildfireRenderNoise(fire, (unsigned)puff + 101u);
        float travel = fmodf(time * (0.22f + smoke * 0.28f) +
                             (float)puff / (float)puffs + lane, 1.0f);
        float height = 1.0f + travel * (5.5f + smoke * 7.0f);
        float lean = travel * travel * wind * (4.0f + smoke * 8.0f);
        float eddy = sinf(time * 0.72f + lane * 19.0f + travel * 8.0f) *
                     (0.14f + travel * 0.42f);
        Vector3 center = {
            base.x + windX * lean - windZ * eddy,
            base.y + height,
            base.z + windZ * lean + windX * eddy
        };
        float radius = (0.38f + travel * 0.95f) *
                       (0.65f + smoke * 0.85f);
        unsigned char shade = (unsigned char)(72.0f + light * 24.0f +
                                               moisture * 12.0f);
        Color color = {
            shade, (unsigned char)(shade - 4u), (unsigned char)(shade - 8u),
            (unsigned char)((1.0f - travel * 0.65f) *
                            (72.0f + smoke * 125.0f))
        };
        WildfireDrawPrimitiveSphere(center, radius, color);
    }
}

void DrawWildfires(const Camera3D *camera,
                   const WeatherImpactFireSnapshot *fires,
                   unsigned fireCount, GraphicsQuality quality,
                   const WeatherVisualState *weather,
                   double simulationTime, float daylight)
{
    if (!camera || !fires || fireCount == 0u) return;
    GraphicsQualityProfile profile = GraphicsQualityProfileFor(quality);
    unsigned limit = (unsigned)profile.wildfireMaxFires;
    if (limit > WILDFIRE_RENDER_MAX_FIRES) limit = WILDFIRE_RENDER_MAX_FIRES;
    if (fireCount > limit) fireCount = limit;
    for (unsigned index = 0u; index < fireCount; index++) {
        float dx = (float)fires[index].x + 0.5f - camera->position.x;
        float dy = (float)fires[index].y + 0.5f - camera->position.y;
        float dz = (float)fires[index].z + 0.5f - camera->position.z;
        if (dx * dx + dy * dy + dz * dz > 190.0f * 190.0f) continue;
        DrawWildfireSource(&fires[index], &profile, weather,
                           simulationTime, daylight);
    }
}

void DrawWildfireHaze(float smokeHaze)
{
    float alpha = fminf(WildfireRenderUnit(smokeHaze), 0.18f);
    if (alpha <= 0.001f) return;
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    if (width <= 0 || height <= 0) return;
    DrawRectangle(0, 0, width, height,
                  Fade((Color){ 66, 70, 72, 255 }, alpha * 0.34f));
    DrawRectangleGradientV(
        0, height / 3, width, height - height / 3,
        Fade((Color){ 78, 80, 80, 255 }, alpha * 0.20f),
        Fade((Color){ 54, 50, 46, 255 }, alpha));
}
