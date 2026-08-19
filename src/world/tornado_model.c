#include "world/tornado_model.h"

#include <math.h>

static float TornadoClamp(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    return value >= 1.0f ? 1.0f : value;
}

static float TornadoSmooth(float edge0, float edge1, float value)
{
    if (!isfinite(value) || edge1 <= edge0) return 0.0f;
    float t = TornadoClamp((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

static float TornadoMinimum(float left, float right)
{
    return left < right ? left : right;
}

float TornadoFormationPotential(const TornadoFormationInput *input)
{
    if (!input || !input->atmosphereActive || !input->supportsWaterCycle) {
        return 0.0f;
    }
    WeatherFieldSample weather = input->weather;
    if (!isfinite(weather.temperatureK) || !isfinite(weather.pressureAtm) ||
        !isfinite(weather.pressureAnomaly) ||
        !isfinite(weather.relativeHumidity) ||
        !isfinite(weather.instability) || !isfinite(weather.storm) ||
        !isfinite(weather.precipitation) || !isfinite(weather.wind) ||
        !isfinite(weather.gust)) {
        return 0.0f;
    }
    float cumulonimbus = weather.cloudGenusCoverage[
        WEATHER_CLOUD_GENUS_CUMULONIMBUS];
    float shear = weather.gust - weather.wind;
    if (weather.temperatureK < 285.0f || weather.temperatureK > 326.0f ||
        weather.pressureAtm < 0.50f || weather.relativeHumidity < 0.66f ||
        weather.instability < 0.54f || weather.storm < 0.42f ||
        weather.precipitation < 0.24f || cumulonimbus < 0.34f ||
        shear < 0.045f) {
        return 0.0f;
    }

    float warmth = TornadoSmooth(285.0f, 299.0f, weather.temperatureK) *
        (1.0f - TornadoSmooth(316.0f, 328.0f, weather.temperatureK));
    float humidity = TornadoSmooth(0.66f, 0.94f,
                                    weather.relativeHumidity);
    float instability = TornadoSmooth(0.54f, 0.94f, weather.instability);
    float storm = TornadoSmooth(0.42f, 0.92f, weather.storm);
    float deepCloud = TornadoSmooth(0.34f, 0.86f, cumulonimbus);
    float windShear = TornadoSmooth(0.045f, 0.24f, shear);
    float lowPressure = TornadoSmooth(0.002f, 0.050f,
                                      -weather.pressureAnomaly);
    float essential = TornadoMinimum(
        TornadoMinimum(humidity, instability),
        TornadoMinimum(storm, TornadoMinimum(deepCloud, windShear)));
    float composite = warmth * 0.10f + humidity * 0.16f +
        instability * 0.22f + storm * 0.20f + deepCloud * 0.16f +
        windShear * 0.12f + lowPressure * 0.04f;
    return TornadoClamp(essential * (0.42f + composite * 0.58f));
}

static void TornadoModelDerive(TornadoState *state,
                               WeatherFieldSample weather)
{
    float intensity = TornadoClamp(state->intensity);
    state->radius = 2.5f + intensity * 10.5f;
    state->influenceRadius = state->radius * (3.0f + intensity);
    state->funnelHeight = 24.0f + intensity * 54.0f;
    state->maximumWindMps = intensity > 0.0f ? 18.0f + intensity * 70.0f : 0.0f;
    state->condensation = TornadoClamp(
        (weather.relativeHumidity - 0.52f) / 0.42f *
        (0.32f + intensity * 0.78f));
    state->dustLoading = TornadoClamp(
        intensity * (0.72f + weather.dust * 0.42f) *
        (1.0f - weather.precipitation * 0.32f));
}

TornadoState TornadoModelCreate(uint32_t seed, uint32_t surfaceId,
                                Vector3 center, float peakIntensity,
                                float lifetime, bool forced)
{
    TornadoState state = { 0 };
    if (!isfinite(center.x) || !isfinite(center.y) || !isfinite(center.z) ||
        !isfinite(peakIntensity) || peakIntensity < 0.0f ||
        peakIntensity > 1.0f || !isfinite(lifetime) || lifetime <= 0.0f) {
        return state;
    }
    state.active = true;
    state.forced = forced;
    state.phase = TORNADO_PHASE_FORMING;
    state.seed = seed;
    state.surfaceId = surfaceId;
    state.center = center;
    state.peakIntensity = peakIntensity;
    state.intensity = peakIntensity * 0.06f;
    state.rotationSign = (seed & 1u) != 0u ? 1.0f : -1.0f;
    state.lifetime = lifetime;
    TornadoModelDerive(&state, (WeatherFieldSample){
        .relativeHumidity = 0.82f
    });
    return state;
}

static void TornadoPhaseSchedule(float lifetime, float *forming,
                                 float *intensifying, float *dissipating)
{
    if (lifetime < 16.0f) {
        *forming = lifetime * 0.20f;
        *intensifying = lifetime * 0.25f;
        *dissipating = lifetime * 0.25f;
        return;
    }
    *forming = fminf(3.0f, lifetime * 0.12f);
    *intensifying = fminf(5.0f, lifetime * 0.16f);
    *dissipating = fminf(8.0f, lifetime * 0.22f);
}

void TornadoModelAdvance(TornadoState *state, float dt,
                         WeatherFieldSample weather, float groundY)
{
    if (!state || !state->active || !isfinite(dt) || dt <= 0.0f ||
        !isfinite(groundY) || !isfinite(weather.windAngle) ||
        !isfinite(weather.wind)) {
        return;
    }
    float step = fminf(dt, 0.25f);
    state->age += step;
    if (state->age >= state->lifetime) {
        state->active = false;
        state->phase = TORNADO_PHASE_INACTIVE;
        state->intensity = 0.0f;
        TornadoModelDerive(state, weather);
        return;
    }

    float forming = 0.0f;
    float intensifying = 0.0f;
    float dissipating = 0.0f;
    TornadoPhaseSchedule(state->lifetime, &forming, &intensifying,
                         &dissipating);
    float matureStart = forming + intensifying;
    float dissipatingStart = state->lifetime - dissipating;
    if (state->age < forming) {
        state->phase = TORNADO_PHASE_FORMING;
        float progress = TornadoSmooth(0.0f, forming, state->age);
        state->intensity = state->peakIntensity *
            (0.06f + progress * 0.30f);
    } else if (state->age < matureStart) {
        state->phase = TORNADO_PHASE_INTENSIFYING;
        float progress = TornadoSmooth(forming, matureStart, state->age);
        state->intensity = state->peakIntensity *
            (0.36f + progress * 0.64f);
    } else if (state->age < dissipatingStart) {
        state->phase = TORNADO_PHASE_MATURE;
        float pulse = 0.965f + 0.035f * sinf(state->age * 0.73f);
        state->intensity = state->peakIntensity * pulse;
    } else {
        state->phase = TORNADO_PHASE_DISSIPATING;
        float progress = TornadoSmooth(dissipatingStart, state->lifetime,
                                        state->age);
        state->intensity = state->peakIntensity * (1.0f - progress);
    }
    state->intensity = TornadoClamp(state->intensity);

    float meander = sinf(state->age * 0.19f +
                         (float)(state->seed & 1023u) * 0.011f) * 0.24f;
    float heading = weather.windAngle + meander;
    float translationSpeed = 1.3f + TornadoClamp(weather.wind) * 3.2f;
    state->velocity = (Vector3){
        cosf(heading) * translationSpeed,
        0.0f,
        sinf(heading) * translationSpeed
    };
    state->center.x += state->velocity.x * step;
    state->center.z += state->velocity.z * step;
    float groundBlend = 1.0f - expf(-step * 5.0f);
    state->center.y += (groundY - state->center.y) * groundBlend;
    TornadoModelDerive(state, weather);
    float angularSpeed = state->radius > 0.01f ?
        state->maximumWindMps / state->radius : 0.0f;
    state->rotation = fmodf(
        state->rotation + state->rotationSign * angularSpeed * step,
        2.0f * PI);
}

TornadoForceSample TornadoModelForceAt(const TornadoState *state,
                                       Vector3 position)
{
    TornadoForceSample sample = { 0 };
    if (!state || !state->active || state->intensity <= 0.0f ||
        !isfinite(position.x) || !isfinite(position.y) ||
        !isfinite(position.z) || state->radius <= 0.0f ||
        state->influenceRadius <= state->radius ||
        state->funnelHeight <= 0.0f) {
        return sample;
    }
    float dx = position.x - state->center.x;
    float dz = position.z - state->center.z;
    float distance = sqrtf(dx * dx + dz * dz);
    float height = position.y - state->center.y;
    sample.horizontalDistance = distance;
    if (distance >= state->influenceRadius || height < -1.5f ||
        height > state->funnelHeight * 1.15f) {
        return sample;
    }

    float radialX = distance > 0.001f ? dx / distance : 1.0f;
    float radialZ = distance > 0.001f ? dz / distance : 0.0f;
    float tangentX = -radialZ * state->rotationSign;
    float tangentZ = radialX * state->rotationSign;
    float horizontalTaper = distance <= state->radius ? 1.0f :
        1.0f - TornadoSmooth(state->radius, state->influenceRadius,
                             distance);
    float normalizedHeight = fmaxf(height, 0.0f) / state->funnelHeight;
    float verticalTaper = 1.0f - TornadoSmooth(0.72f, 1.15f,
                                               normalizedHeight);
    float exposure = TornadoClamp(
        state->intensity * horizontalTaper * verticalTaper);
    if (exposure <= 0.0f) return sample;

    float tangentialWind = distance < state->radius ?
        state->maximumWindMps * distance / state->radius :
        state->maximumWindMps * state->radius / fmaxf(distance, 0.001f);
    tangentialWind *= horizontalTaper * verticalTaper;
    float core = 1.0f - TornadoSmooth(
        state->radius * 0.25f, state->influenceRadius, distance);
    float inflowAcceleration = state->maximumWindMps * 0.055f *
        horizontalTaper * verticalTaper;
    float tangentAcceleration = tangentialWind * 0.18f * exposure;
    float updraftAcceleration = state->maximumWindMps * 0.14f *
        core * core * exposure;
    sample.acceleration = (Vector3){
        tangentX * tangentAcceleration - radialX * inflowAcceleration,
        updraftAcceleration,
        tangentZ * tangentAcceleration - radialZ * inflowAcceleration
    };
    float magnitude = sqrtf(
        sample.acceleration.x * sample.acceleration.x +
        sample.acceleration.y * sample.acceleration.y +
        sample.acceleration.z * sample.acceleration.z);
    if (magnitude > 28.0f) {
        float scale = 28.0f / magnitude;
        sample.acceleration.x *= scale;
        sample.acceleration.y *= scale;
        sample.acceleration.z *= scale;
    }
    sample.exposure = exposure;
    sample.localWindMps = tangentialWind;
    return sample;
}

const char *TornadoPhaseName(TornadoPhase phase)
{
    static const char *const names[] = {
        "inactive", "forming", "intensifying", "mature", "dissipating"
    };
    if (phase < TORNADO_PHASE_INACTIVE ||
        phase > TORNADO_PHASE_DISSIPATING) {
        return "unknown";
    }
    return names[phase];
}
