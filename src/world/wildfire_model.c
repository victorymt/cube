#include "world/wildfire_model.h"

#include <math.h>

static float WildfireUnit(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    return value >= 1.0f ? 1.0f : value;
}

static float WildfireApproach(float current, float target, float rate,
                              float dt)
{
    float blend = 1.0f - expf(-fmaxf(rate, 0.0f) * dt);
    return current + (target - current) * blend;
}

static bool WildfireStateValid(const WildfireState *state)
{
    return state && state->phase >= WILDFIRE_PHASE_INACTIVE &&
        state->phase <= WILDFIRE_PHASE_SMOLDERING &&
        isfinite(state->intensity) && state->intensity >= 0.0f &&
        state->intensity <= 1.0f && isfinite(state->fuel) &&
        state->fuel >= 0.0f && state->fuel <= 4.0f &&
        isfinite(state->moisture) && state->moisture >= 0.0f &&
        state->moisture <= 1.0f && isfinite(state->ageSeconds) &&
        state->ageSeconds >= 0.0f;
}

static void WildfireDerive(WildfireState *state)
{
    if (!state || state->phase == WILDFIRE_PHASE_INACTIVE) {
        if (state) {
            state->intensity = 0.0f;
            state->heatOutput = 0.0f;
            state->smokeOutput = 0.0f;
        }
        return;
    }
    float phaseHeat = state->phase == WILDFIRE_PHASE_FLAMING ? 1.0f :
                      (state->phase == WILDFIRE_PHASE_IGNITING ? 0.58f :
                       0.28f);
    float phaseSmoke = state->phase == WILDFIRE_PHASE_SMOLDERING ? 0.72f :
                       (state->phase == WILDFIRE_PHASE_IGNITING ? 0.46f :
                        0.62f);
    state->heatOutput = WildfireUnit(state->intensity * phaseHeat);
    state->smokeOutput = WildfireUnit(
        state->intensity * phaseSmoke * (0.72f + state->moisture * 0.62f));
}

bool WildfireModelNormalize(WildfireState *state)
{
    if (!WildfireStateValid(state)) return false;
    WildfireDerive(state);
    return true;
}

float WildfireEquilibriumMoisture(WildfireEnvironment environment)
{
    float humidity = WildfireUnit(environment.relativeHumidity);
    float rain = WildfireUnit(environment.rain);
    float water = WildfireUnit(environment.waterExposure);
    float warmth = WildfireUnit((environment.temperatureK - 273.15f) / 45.0f);
    return WildfireUnit(0.05f + humidity * 0.62f + rain * 0.50f +
                        water * 0.80f - warmth * 0.10f);
}

bool WildfireCanIgnite(float flammability, float moisture,
                       float ignitionIntensity)
{
    if (!isfinite(flammability) || !isfinite(moisture) ||
        !isfinite(ignitionIntensity) || flammability <= 0.12f ||
        moisture < 0.0f || moisture > 1.0f || ignitionIntensity <= 0.0f) {
        return false;
    }
    if (moisture >= 0.72f) return false;
    return flammability * (1.0f - moisture) *
        WildfireUnit(ignitionIntensity) >= 0.075f;
}

WildfireState WildfireModelCreate(float flammability, float fuelLoad,
                                  float moisture, float ignitionIntensity)
{
    WildfireState state = { 0 };
    if (!isfinite(fuelLoad) || fuelLoad <= 0.0f || fuelLoad > 4.0f ||
        !WildfireCanIgnite(flammability, moisture, ignitionIntensity)) {
        return state;
    }
    state.phase = WILDFIRE_PHASE_IGNITING;
    state.intensity = WildfireUnit(
        ignitionIntensity * flammability * (1.0f - moisture));
    state.fuel = fuelLoad;
    state.moisture = moisture;
    if (state.intensity >= 0.55f) state.phase = WILDFIRE_PHASE_FLAMING;
    WildfireDerive(&state);
    return state;
}

void WildfireModelAdvance(WildfireState *state, float dt,
                          float flammability,
                          WildfireEnvironment environment)
{
    if (!WildfireStateValid(state) || state->phase == WILDFIRE_PHASE_INACTIVE ||
        !isfinite(dt) || dt <= 0.0f || !isfinite(flammability)) {
        return;
    }
    float step = fminf(dt, 1.0f);
    float rain = WildfireUnit(environment.rain);
    float water = WildfireUnit(environment.waterExposure);
    float suppression = WildfireUnit(environment.suppression);
    float wind = WildfireUnit(environment.wind);
    float gust = WildfireUnit(environment.gust);
    float equilibrium = WildfireEquilibriumMoisture(environment);
    float warmth = WildfireUnit((environment.temperatureK - 273.15f) / 45.0f);
    float moistureRate = equilibrium > state->moisture ?
        0.18f + rain * 0.92f + water * 1.20f + suppression * 1.35f :
        0.025f + warmth * 0.05f + wind * 0.03f;
    state->moisture = WildfireUnit(WildfireApproach(
        state->moisture, equilibrium, moistureRate, step));
    state->ageSeconds += step;

    float cooling = rain * 0.92f + water * 1.25f + suppression * 1.45f +
                    state->moisture * 0.34f;
    float combustion = WildfireUnit(flammability) *
        (1.0f - state->moisture) *
        (0.42f + wind * 0.20f + gust * 0.18f);
    switch (state->phase) {
    case WILDFIRE_PHASE_IGNITING:
        state->intensity = WildfireUnit(
            state->intensity + (combustion * 0.82f - cooling) * step);
        if (state->intensity >= 0.28f) {
            state->phase = WILDFIRE_PHASE_FLAMING;
        } else if (state->intensity <= 0.005f && state->ageSeconds >= 1.0f) {
            state->phase = WILDFIRE_PHASE_INACTIVE;
        }
        break;
    case WILDFIRE_PHASE_FLAMING: {
        float target = WildfireUnit(combustion * 1.35f - cooling * 0.35f);
        state->intensity = WildfireUnit(WildfireApproach(
            state->intensity, target, 1.15f + gust * 0.40f, step));
        if (cooling > combustion * 0.88f || state->intensity < 0.16f ||
            state->fuel < 0.08f) {
            state->phase = WILDFIRE_PHASE_SMOLDERING;
        }
        break;
    }
    case WILDFIRE_PHASE_SMOLDERING:
        if (combustion > 0.48f && cooling < 0.10f && state->fuel > 0.08f) {
            state->phase = WILDFIRE_PHASE_FLAMING;
            state->intensity = fmaxf(state->intensity, 0.18f);
        } else {
            state->intensity = WildfireUnit(
                state->intensity -
                (0.022f + cooling * 0.34f + state->moisture * 0.03f) * step);
            if (state->intensity <= 0.005f) {
                state->phase = WILDFIRE_PHASE_INACTIVE;
            }
        }
        break;
    case WILDFIRE_PHASE_INACTIVE:
        break;
    }

    float burnRate = state->phase == WILDFIRE_PHASE_FLAMING ?
        0.012f + state->intensity * 0.045f :
        (state->phase == WILDFIRE_PHASE_IGNITING ?
         0.003f + state->intensity * 0.006f :
         0.002f + state->intensity * 0.007f);
    state->fuel = fmaxf(
        0.0f, state->fuel - burnRate *
        (0.72f + WildfireUnit(flammability) * 0.42f) * step);
    if (state->fuel <= 0.0f && state->phase != WILDFIRE_PHASE_INACTIVE) {
        state->phase = WILDFIRE_PHASE_SMOLDERING;
        state->intensity = fminf(state->intensity, 0.12f);
    }
    WildfireDerive(state);
}

void WildfireModelApplySuppression(WildfireState *state, float amount)
{
    if (!WildfireStateValid(state) || state->phase == WILDFIRE_PHASE_INACTIVE ||
        !isfinite(amount) || amount <= 0.0f) {
        return;
    }
    WildfirePhase previousPhase = state->phase;
    float strength = WildfireUnit(amount);
    state->moisture = WildfireUnit(state->moisture + strength * 0.58f);
    state->intensity = WildfireUnit(state->intensity - strength * 0.72f);
    if (state->phase == WILDFIRE_PHASE_FLAMING &&
        (state->intensity < 0.18f || strength >= 0.55f)) {
        state->phase = WILDFIRE_PHASE_SMOLDERING;
    }
    if (state->intensity <= 0.005f) {
        if (previousPhase == WILDFIRE_PHASE_FLAMING) {
            state->phase = WILDFIRE_PHASE_SMOLDERING;
            state->intensity = 0.012f;
        } else {
            state->phase = WILDFIRE_PHASE_INACTIVE;
        }
    }
    WildfireDerive(state);
}

float WildfireSpreadProbability(WildfireSpreadInput input)
{
    if (!isfinite(input.windAngle) || !isfinite(input.offsetX) ||
        !isfinite(input.offsetZ) || !isfinite(input.slope)) {
        return 0.0f;
    }
    float distance = hypotf(input.offsetX, input.offsetZ);
    if (distance <= 0.001f) return 0.0f;
    float directionX = input.offsetX / distance;
    float directionZ = input.offsetZ / distance;
    float alignment = directionX * cosf(input.windAngle) +
                      directionZ * sinf(input.windAngle);
    float wind = WildfireUnit(input.wind);
    float gust = WildfireUnit(input.gust);
    float windFactor = 1.0f + fmaxf(alignment, 0.0f) *
        (wind * 1.25f + gust * 0.85f) -
        fmaxf(-alignment, 0.0f) * wind * 0.42f;
    float slopeFactor = expf(fmaxf(-1.0f, fminf(input.slope, 1.0f)) * 0.78f);
    float availableFuel = WildfireUnit(input.targetFlammability) *
                          (1.0f - WildfireUnit(input.targetMoisture));
    float base = 0.035f + WildfireUnit(input.sourceIntensity) * 0.22f;
    return WildfireUnit(base * availableFuel * windFactor * slopeFactor);
}

float WildfireHeatExposure(const WildfireState *state, float distance,
                           float shelter, float immersion)
{
    if (!WildfireStateValid(state) || state->phase == WILDFIRE_PHASE_INACTIVE ||
        !isfinite(distance) || distance < 0.0f) {
        return 0.0f;
    }
    float radius = 2.5f + state->heatOutput * 5.5f;
    float falloff = WildfireUnit(1.0f - distance / radius);
    return WildfireUnit(state->heatOutput * falloff * falloff *
                        (1.0f - WildfireUnit(shelter) * 0.72f) *
                        (1.0f - WildfireUnit(immersion) * 0.92f));
}

float WildfireSmokeExposure(const WildfireState *state, float distance,
                            float shelter, float immersion)
{
    if (!WildfireStateValid(state) || state->phase == WILDFIRE_PHASE_INACTIVE ||
        !isfinite(distance) || distance < 0.0f) {
        return 0.0f;
    }
    float radius = 6.0f + state->smokeOutput * 16.0f;
    float falloff = WildfireUnit(1.0f - distance / radius);
    return WildfireUnit(state->smokeOutput * falloff *
                        (1.0f - WildfireUnit(shelter) * 0.58f) *
                        (1.0f - WildfireUnit(immersion) * 0.96f));
}

const char *WildfirePhaseName(WildfirePhase phase)
{
    switch (phase) {
    case WILDFIRE_PHASE_IGNITING: return "igniting";
    case WILDFIRE_PHASE_FLAMING: return "flaming";
    case WILDFIRE_PHASE_SMOLDERING: return "smoldering";
    case WILDFIRE_PHASE_INACTIVE:
    default: return "inactive";
    }
}
