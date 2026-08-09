#include "stellar.h"

#include <math.h>

#define STELLAR_SOLAR_TEMPERATURE_K 5772.0f
#define STELLAR_GALAXY_AGE_GYR 10.0f

static uint32_t StellarMix32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static float StellarUnit(uint32_t seed)
{
    return (float)(StellarMix32(seed) >> 8) * (1.0f / 16777216.0f);
}

static float StellarClamp(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static float PowerLawIntegral(float minimum, float maximum, float exponent)
{
    float power = 1.0f - exponent;
    return (powf(maximum, power) - powf(minimum, power)) / power;
}

static float SamplePowerLaw(float unit, float minimum, float maximum,
                            float exponent)
{
    float power = 1.0f - exponent;
    float low = powf(minimum, power);
    float high = powf(maximum, power);
    return powf(low + unit * (high - low), 1.0f / power);
}

float StellarSampleInitialMass(uint32_t seed)
{
    const float breakMass = 0.50f;
    const float lowWeight = PowerLawIntegral(0.08f, breakMass, 1.30f);
    // Kroupa's second segment is multiplied by 0.5 for continuity at 0.5 Msun.
    const float highWeight = 0.50f * PowerLawIntegral(breakMass, 50.0f, 2.30f);
    float draw = StellarUnit(seed) * (lowWeight + highWeight);
    if (draw < lowWeight) {
        return StellarClamp(SamplePowerLaw(draw / lowWeight, 0.08f,
                                           breakMass, 1.30f),
                            0.08f, breakMass);
    }
    return StellarClamp(SamplePowerLaw((draw - lowWeight) / highWeight,
                                       breakMass, 50.0f, 2.30f),
                        breakMass, 50.0f);
}

static float MainSequenceLuminosity(float massSolar)
{
    // Empirical piecewise main-sequence mass-luminosity relation in solar units.
    if (massSolar < 0.43f) return 0.23f * powf(massSolar, 2.30f);
    if (massSolar < 2.0f) return powf(massSolar, 4.0f);
    return 1.40f * powf(massSolar, 3.50f);
}

static float MainSequenceRadius(float massSolar)
{
    return massSolar < 1.0f ? powf(massSolar, 0.80f) :
                              powf(massSolar, 0.57f);
}

static SpectrumType MainSequenceSpectrum(float temperatureK)
{
    if (temperatureK < 4000.0f) return SPECTRUM_RED_DWARF;
    if (temperatureK < 5200.0f) return SPECTRUM_ORANGE;
    if (temperatureK < 7500.0f) return SPECTRUM_YELLOW;
    return SPECTRUM_BLUE_WHITE;
}

bool StellarProfileAtAge(float initialMassSolar, float ageGyr, uint32_t seed,
                         StellarProfile *out)
{
    if (!out || !isfinite(initialMassSolar) || !isfinite(ageGyr) ||
        initialMassSolar <= 0.0f) {
        return false;
    }

    float mass = StellarClamp(initialMassSolar, 0.08f, 50.0f);
    float age = fmaxf(ageGyr, 0.0f);
    float mainLuminosity = MainSequenceLuminosity(mass);
    float mainRadius = MainSequenceRadius(mass);
    // Stefan-Boltzmann then makes temperature a consequence of L and R.
    float mainTemperature = STELLAR_SOLAR_TEMPERATURE_K *
                            powf(mainLuminosity / (mainRadius * mainRadius), 0.25f);
    float mainLifetime = 10.0f * mass / mainLuminosity;
    float giantDuration = StellarClamp(mainLifetime * 0.08f, 0.0001f, 0.80f);
    float luminousLifetime = mainLifetime + giantDuration;
    if (age > luminousLifetime) return false;

    *out = (StellarProfile){
        .spectrum = MainSequenceSpectrum(mainTemperature),
        .stage = STELLAR_STAGE_MAIN_SEQUENCE,
        .initialMassSolar = mass,
        .massSolar = mass,
        .radiusSolar = mainRadius,
        .temperatureK = mainTemperature,
        .luminositySolar = mainLuminosity,
        .ageGyr = age,
        .mainSequenceLifetimeGyr = mainLifetime,
        .luminousLifetimeGyr = luminousLifetime
    };
    if (age <= mainLifetime) return true;

    float phase = StellarClamp((age - mainLifetime) / giantDuration, 0.0f, 1.0f);
    float temperatureVariation = (StellarUnit(seed ^ 0x51ed270bu) - 0.5f) * 120.0f;
    float giantTemperature = StellarClamp(4800.0f - 1350.0f * phase +
                                           temperatureVariation,
                                           3200.0f, 5000.0f);
    float giantRadius = mainRadius * (8.0f + 68.0f * powf(phase, 0.72f));
    float temperatureRatio = giantTemperature / STELLAR_SOLAR_TEMPERATURE_K;

    out->spectrum = SPECTRUM_RED_GIANT;
    out->stage = STELLAR_STAGE_RED_GIANT;
    out->massSolar = mass * (1.0f - 0.12f * phase);
    out->radiusSolar = giantRadius;
    out->temperatureK = giantTemperature;
    out->luminositySolar = giantRadius * giantRadius *
                           powf(temperatureRatio, 4.0f);
    return true;
}

StellarProfile StellarGenerate(uint32_t seed)
{
    // Sample the birth IMF, then reject remnants to obtain the present-day
    // luminous population without changing which procedural system cells exist.
    for (uint32_t attempt = 0; attempt < 8u; attempt++) {
        uint32_t candidateSeed = StellarMix32(seed ^
                                             (0x9e3779b9u * (attempt + 1u)));
        float mass = StellarSampleInitialMass(candidateSeed);
        float age = StellarUnit(candidateSeed ^ 0xa511e9b3u) *
                    STELLAR_GALAXY_AGE_GYR;
        StellarProfile profile;
        if (StellarProfileAtAge(mass, age, candidateSeed, &profile)) return profile;
    }

    float fallbackMass = 0.08f + StellarUnit(seed ^ 0x68bc21ebu) * 0.42f;
    float fallbackAge = StellarUnit(seed ^ 0x02e5be93u) * STELLAR_GALAXY_AGE_GYR;
    StellarProfile fallback = { 0 };
    StellarProfileAtAge(fallbackMass, fallbackAge, seed, &fallback);
    return fallback;
}

StellarProfile StellarSolarProfile(void)
{
    StellarProfile solar = { 0 };
    StellarProfileAtAge(1.0f, 4.57f, 0u, &solar);
    return solar;
}
