#include "world/local_climate.h"

#include <math.h>

static float ClimateClamp(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static float ClimateUnit(float value)
{
    return ClimateClamp(value, 0.0f, 1.0f);
}

static float ClimateDewPoint(float temperatureK, float humidity)
{
    float temperatureC = ClimateClamp(temperatureK - 273.15f,
                                      -80.0f, 60.0f);
    float safeHumidity = ClimateClamp(humidity, 0.001f, 1.0f);
    const float a = 17.625f;
    const float b = 243.04f;
    float gamma = logf(safeHumidity) +
                  a * temperatureC / (b + temperatureC);
    float dewPointC = b * gamma / (a - gamma);
    return ClimateClamp(dewPointC + 273.15f, 150.0f, temperatureK);
}

static float ClimateWetBulb(float temperatureK, float humidity)
{
    float temperatureC = ClimateClamp(temperatureK - 273.15f,
                                      -80.0f, 60.0f);
    float relativePercent = ClimateUnit(humidity) * 100.0f;
    float wetBulbC = temperatureC *
        atanf(0.151977f * sqrtf(relativePercent + 8.313659f)) +
        atanf(temperatureC + relativePercent) -
        atanf(relativePercent - 1.676331f) +
        0.00391838f * powf(relativePercent, 1.5f) *
        atanf(0.023101f * relativePercent) - 4.686035f;
    if (!isfinite(wetBulbC)) wetBulbC = temperatureC;
    return ClimateClamp(wetBulbC + 273.15f, 150.0f, temperatureK);
}

static ClimateRegime ClimateClassify(const LocalClimateInput *input,
                                     const LocalClimateState *state)
{
    if (!state->atmosphereActive) return CLIMATE_REGIME_VACUUM;
    float temperature = state->temperatureK;
    float humidity = state->relativeHumidity;
    float aridity = state->aridity;
    if (temperature >= 330.0f && input->atmosphereDensity >= 0.75f) {
        return CLIMATE_REGIME_HOT_GREENHOUSE;
    }
    if (temperature < 250.0f || input->iceCoverage >= 0.82f) {
        return CLIMATE_REGIME_ICE_CAP;
    }
    if (temperature < 266.0f) return CLIMATE_REGIME_TUNDRA;
    if (temperature < 278.0f && humidity >= 0.38f) {
        return CLIMATE_REGIME_BOREAL;
    }
    if (aridity >= 0.76f) return CLIMATE_REGIME_DESERT;
    if (aridity >= 0.57f) return CLIMATE_REGIME_STEPPE;
    if (temperature >= 297.0f && humidity >= 0.78f) {
        return CLIMATE_REGIME_TROPICAL_RAINFOREST;
    }
    if (temperature >= 294.0f && humidity >= 0.58f) {
        return CLIMATE_REGIME_MONSOON;
    }
    if (temperature >= 292.0f) return CLIMATE_REGIME_SAVANNA;
    if (input->seasonalAmplitudeK >= 15.0f) {
        return CLIMATE_REGIME_HUMID_CONTINENTAL;
    }
    if (input->waterCoverage >= 0.45f && humidity >= 0.58f) {
        return CLIMATE_REGIME_OCEANIC;
    }
    if (temperature >= 285.0f && humidity < 0.52f) {
        return CLIMATE_REGIME_MEDITERRANEAN;
    }
    return CLIMATE_REGIME_SEASONAL_TEMPERATE;
}

bool LocalClimateEvaluate(const LocalClimateInput *input,
                          LocalClimateState *out)
{
    if (!input || !out || !isfinite(input->meanTemperatureK) ||
        !isfinite(input->seasonalTemperatureDeltaK) ||
        !isfinite(input->daylightTemperatureDeltaK) ||
        !isfinite(input->surfacePressureAtm) ||
        !isfinite(input->atmosphereDensity) ||
        !isfinite(input->moisture) ||
        !isfinite(input->cloudPotential) ||
        !isfinite(input->windStrength) || !isfinite(input->latitude) ||
        !isfinite(input->elevation) || !isfinite(input->waterCoverage) ||
        !isfinite(input->iceCoverage) ||
        !isfinite(input->seasonalAmplitudeK) ||
        input->meanTemperatureK < 0.0f || input->surfacePressureAtm < 0.0f) {
        return false;
    }

    LocalClimateState state = { 0 };
    state.temperatureK = ClimateClamp(
        input->meanTemperatureK + input->seasonalTemperatureDeltaK +
        input->daylightTemperatureDeltaK -
        fmaxf(input->elevation, 0.0f) * 0.0065f,
        120.0f, 500.0f);
    state.atmosphereActive = input->hasAtmosphere &&
        input->surfacePressureAtm >= 0.006f && input->atmosphereDensity > 0.01f;
    state.waterCycleActive = state.atmosphereActive &&
        input->supportsWaterCycle && state.temperatureK >= 180.0f &&
        state.temperatureK <= 373.15f;
    state.pressureAtm = state.atmosphereActive ?
        ClimateClamp(input->surfacePressureAtm *
            expf(-fmaxf(input->elevation, 0.0f) / 8400.0f), 0.0f, 100.0f) :
        0.0f;
    float latitudeDrying = ClimateUnit((fabsf(input->latitude) - 0.35f) / 1.0f) *
                          0.08f;
    state.relativeHumidity = state.waterCycleActive ? ClimateUnit(
        input->moisture * 0.72f + input->cloudPotential * 0.18f +
        input->waterCoverage * 0.14f - input->iceCoverage * 0.10f -
        latitudeDrying) : 0.0f;
    state.dewPointK = state.waterCycleActive ?
        ClimateDewPoint(state.temperatureK, state.relativeHumidity) :
        fmaxf(state.temperatureK - 80.0f, 120.0f);
    state.wetBulbK = state.waterCycleActive ?
        ClimateWetBulb(state.temperatureK, state.relativeHumidity) :
        state.temperatureK;
    state.aridity = ClimateUnit(
        0.62f - state.relativeHumidity * 0.58f - input->waterCoverage * 0.18f +
        ClimateUnit((state.temperatureK - 288.0f) / 45.0f) * 0.30f +
        ClimateUnit(input->windStrength) * 0.10f);
    state.instability = state.waterCycleActive ? ClimateUnit(
        ClimateUnit((state.temperatureK - 278.0f) / 38.0f) * 0.48f +
        state.relativeHumidity * 0.38f +
        ClimateUnit(input->seasonalAmplitudeK / 35.0f) * 0.14f) : 0.0f;
    state.orographicLift = ClimateUnit(
        fmaxf(input->elevation, 0.0f) / 2400.0f *
        (0.35f + ClimateUnit(input->windStrength) * 0.65f));
    state.cloudPotential = state.waterCycleActive ? ClimateUnit(
        input->cloudPotential * 0.62f + state.relativeHumidity * 0.30f +
        state.orographicLift * 0.18f) : 0.0f;
    state.windStrength = state.atmosphereActive ?
        ClimateUnit(input->windStrength) : 0.0f;
    state.regime = ClimateClassify(input, &state);
    *out = state;
    return true;
}

const char *ClimateRegimeName(ClimateRegime regime)
{
    static const char *const names[CLIMATE_REGIME_COUNT] = {
        "Vacuum", "Ice cap", "Tundra", "Boreal",
        "Humid continental", "Oceanic", "Seasonal temperate",
        "Mediterranean", "Steppe", "Desert", "Savanna", "Monsoon",
        "Tropical rainforest", "Hot greenhouse"
    };
    if (regime < 0 || regime >= CLIMATE_REGIME_COUNT) return "Unknown";
    return names[regime];
}
