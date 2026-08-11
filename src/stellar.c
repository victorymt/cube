#include "stellar.h"

#include "space_units.h"

#include <float.h>
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

static float MainSequenceAgeLuminosityScale(float lifetimeProgress)
{
    const float solarProgress = 0.457f;
    float progress = StellarClamp(lifetimeProgress, 0.0f, 1.0f);
    float scale = 0.70f + 0.40f * progress +
                  0.55f * progress * progress;
    float solarScale = 0.70f + 0.40f * solarProgress +
                       0.55f * solarProgress * solarProgress;
    return scale / solarScale;
}

static float MainSequenceAgeRadiusScale(float lifetimeProgress)
{
    const float solarProgress = 0.457f;
    float progress = StellarClamp(lifetimeProgress, 0.0f, 1.0f);
    float scale = 0.86f + 0.20f * progress +
                  0.24f * progress * progress;
    float solarScale = 0.86f + 0.20f * solarProgress +
                       0.24f * solarProgress * solarProgress;
    return scale / solarScale;
}

static SpectrumType MainSequenceSpectrum(float temperatureK)
{
    if (temperatureK < 4000.0f) return SPECTRUM_RED_DWARF;
    if (temperatureK < 5200.0f) return SPECTRUM_ORANGE;
    if (temperatureK < 7500.0f) return SPECTRUM_YELLOW;
    return SPECTRUM_BLUE_WHITE;
}

static float StellarLuminosityFromRadiusTemperature(float radiusSolar,
                                                    float temperatureK)
{
    double temperatureRatio = (double)temperatureK /
                              STELLAR_SOLAR_TEMPERATURE_K;
    return (float)((double)radiusSolar * radiusSolar *
                   pow(temperatureRatio, 4.0));
}

static void StellarApplyRemnant(double coolingAgeGyr, StellarProfile *out)
{
    float initialMass = out->initialMassSolar;
    float remnantMass = 0.0f;
    float remnantRadius = 0.0f;
    float remnantTemperature = 0.0f;

    if (initialMass < 8.0f) {
        remnantMass = StellarClamp(0.109f * initialMass + 0.394f,
                                   0.05f, 1.37f);
        remnantMass = fminf(remnantMass, initialMass * 0.85f);
        double chandrasekharMass = 1.44;
        double massRatio = (double)remnantMass / chandrasekharMass;
        double radiusTerm = pow(1.0 / massRatio, 2.0 / 3.0) -
                            pow(massRatio, 2.0 / 3.0);
        remnantRadius = (float)(0.0112 * sqrt(fmax(radiusTerm, 0.0001)));
        remnantTemperature = (float)(100000.0 /
            pow(1.0 + coolingAgeGyr / 0.01, 0.35));
        remnantTemperature = StellarClamp(remnantTemperature,
                                           2500.0f, 100000.0f);
        out->spectrum = SPECTRUM_WHITE_DWARF;
        out->stage = STELLAR_STAGE_WHITE_DWARF;
    } else if (initialMass < 25.0f) {
        remnantMass = StellarClamp(
            1.25f + 0.04f * (initialMass - 8.0f), 1.25f, 2.10f);
        remnantRadius = (float)(12.0 / SPACE_UNITS_SOLAR_RADIUS_KM);
        remnantTemperature = (float)(1000000.0 /
            pow(1.0 + coolingAgeGyr / 0.00001, 0.25));
        remnantTemperature = StellarClamp(remnantTemperature,
                                           10000.0f, 1000000.0f);
        out->spectrum = SPECTRUM_NEUTRON_STAR;
        out->stage = STELLAR_STAGE_NEUTRON_STAR;
    } else {
        const double lightSpeedKmPerSecond = 299792.458;
        remnantMass = StellarClamp(initialMass * 0.30f, 3.0f, 20.0f);
        double remnantMassKg = (double)remnantMass *
                               SPACE_UNITS_SOLAR_MASS_KG;
        double schwarzschildRadiusKm =
            2.0 * SPACE_UNITS_GRAVITATIONAL_CONSTANT_KM3_KG_S2 *
            remnantMassKg /
            (lightSpeedKmPerSecond * lightSpeedKmPerSecond);
        remnantRadius = (float)(schwarzschildRadiusKm /
                                SPACE_UNITS_SOLAR_RADIUS_KM);
        remnantTemperature = (float)(30.0 /
            pow(1.0 + coolingAgeGyr / 0.001, 0.08));
        remnantTemperature = StellarClamp(remnantTemperature, 10.0f, 30.0f);
        out->spectrum = SPECTRUM_BLACK_HOLE;
        out->stage = STELLAR_STAGE_BLACK_HOLE;
    }

    out->massSolar = remnantMass;
    out->radiusSolar = remnantRadius;
    out->massKg = (double)remnantMass * SPACE_UNITS_SOLAR_MASS_KG;
    out->radiusKm = (double)remnantRadius * SPACE_UNITS_SOLAR_RADIUS_KM;
    out->temperatureK = remnantTemperature;
    out->luminositySolar = StellarLuminosityFromRadiusTemperature(
        remnantRadius, remnantTemperature);
}

bool StellarProfileAtAge(float initialMassSolar, double ageGyr, uint32_t seed,
                         StellarProfile *out)
{
    if (!out) return false;
    *out = (StellarProfile){ 0 };
    if (!isfinite(initialMassSolar) || !isfinite(ageGyr) ||
        initialMassSolar <= 0.0f) {
        return false;
    }

    float mass = StellarClamp(initialMassSolar, 0.08f, 50.0f);
    double age = fmax(ageGyr, 0.0);
    float storedAge = (float)fmin(age, (double)FLT_MAX);
    float referenceLuminosity = MainSequenceLuminosity(mass);
    float referenceRadius = MainSequenceRadius(mass);
    float mainLifetime = 10.0f * mass / referenceLuminosity;
    float lifetimeProgress = StellarClamp(
        (float)(age / mainLifetime), 0.0f, 1.0f);
    float mainLuminosity = referenceLuminosity *
        MainSequenceAgeLuminosityScale(lifetimeProgress);
    float mainRadius = referenceRadius *
        MainSequenceAgeRadiusScale(lifetimeProgress);
    // Stefan-Boltzmann then makes temperature a consequence of L and R.
    float mainTemperature = STELLAR_SOLAR_TEMPERATURE_K *
                            powf(mainLuminosity / (mainRadius * mainRadius), 0.25f);
    float giantDuration = StellarClamp(mainLifetime * 0.08f, 0.0001f, 0.80f);
    float luminousLifetime = mainLifetime + giantDuration;

    *out = (StellarProfile){
        .spectrum = MainSequenceSpectrum(mainTemperature),
        .stage = STELLAR_STAGE_MAIN_SEQUENCE,
        .evolutionSeed = seed,
        .initialMassSolar = mass,
        .massKg = (double)mass * SPACE_UNITS_SOLAR_MASS_KG,
        .radiusKm = (double)mainRadius * SPACE_UNITS_SOLAR_RADIUS_KM,
        .massSolar = mass,
        .radiusSolar = mainRadius,
        .temperatureK = mainTemperature,
        .luminositySolar = mainLuminosity,
        .ageGyr = storedAge,
        .mainSequenceLifetimeGyr = mainLifetime,
        .luminousLifetimeGyr = luminousLifetime
    };
    if (age > luminousLifetime) {
        StellarApplyRemnant(age - luminousLifetime, out);
        return out->massKg > 0.0 && out->radiusKm > 0.0 &&
               out->temperatureK > 0.0f && out->luminositySolar > 0.0f &&
               isfinite(out->massKg) && isfinite(out->radiusKm) &&
               isfinite(out->temperatureK) &&
               isfinite(out->luminositySolar);
    }
    if (age <= mainLifetime) return true;

    float phase = StellarClamp(
        (float)((age - mainLifetime) / giantDuration), 0.0f, 1.0f);
    float temperatureVariation = (StellarUnit(seed ^ 0x51ed270bu) - 0.5f) * 120.0f;
    float targetTemperature = StellarClamp(
        4700.0f - 1250.0f * phase + temperatureVariation,
        3200.0f, 5000.0f);
    float giantTemperature = mainTemperature +
        (targetTemperature - mainTemperature) * powf(phase, 0.45f);
    float giantRadius = mainRadius *
        (1.0f + 58.0f * powf(phase, 0.72f));

    out->spectrum = SPECTRUM_RED_GIANT;
    out->stage = STELLAR_STAGE_RED_GIANT;
    out->massSolar = mass * (1.0f - 0.12f * phase);
    out->radiusSolar = giantRadius;
    out->massKg = (double)out->massSolar * SPACE_UNITS_SOLAR_MASS_KG;
    out->radiusKm = (double)giantRadius * SPACE_UNITS_SOLAR_RADIUS_KM;
    out->temperatureK = giantTemperature;
    out->luminositySolar = StellarLuminosityFromRadiusTemperature(
        giantRadius, giantTemperature);
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
        if (StellarProfileAtAge(mass, age, candidateSeed, &profile) &&
            profile.stage <= STELLAR_STAGE_RED_GIANT) {
            return profile;
        }
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
