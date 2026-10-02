#include "world/atmosphere_model.h"

#include <math.h>
#include <stddef.h>

#define ATMOSPHERE_GAS_CONSTANT 8.314462618
#define ATMOSPHERE_PASCALS_PER_ATM 101325.0

static double AtmosphereClamp(double value, double low, double high)
{
    return fmin(fmax(value, low), high);
}

static double AtmosphereSmooth(double value)
{
    double t = AtmosphereClamp(value, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

AtmosphereInput AtmosphereEarthInput(void)
{
    return (AtmosphereInput){
        .surfaceTemperatureK = 288.15,
        .surfacePressureAtm = 1.0,
        .gravityMps2 = 9.80665,
        .molarMassKgMol = 0.0289644,
        .ozoneStrength = 1.0,
        .exosphereTemperatureK = 1000.0
    };
}

static bool AtmosphereInputIsValid(const AtmosphereInput *input)
{
    return input && isfinite(input->surfaceTemperatureK) &&
        isfinite(input->surfacePressureAtm) && isfinite(input->gravityMps2) &&
        isfinite(input->molarMassKgMol) && isfinite(input->ozoneStrength) &&
        isfinite(input->exosphereTemperatureK) &&
        input->surfaceTemperatureK >= 40.0 && input->surfaceTemperatureK <= 4000.0 &&
        input->surfacePressureAtm >= 0.0 && input->surfacePressureAtm <= 10000.0 &&
        input->gravityMps2 >= 0.05 && input->gravityMps2 <= 1000.0 &&
        input->molarMassKgMol >= 0.001 && input->molarMassKgMol <= 0.2 &&
        input->ozoneStrength >= 0.0 && input->ozoneStrength <= 1.0 &&
        input->exosphereTemperatureK >= 40.0 && input->exosphereTemperatureK <= 10000.0;
}

/* Integrate d(log P)/dz = -M g/(R T) exactly within each linear-T segment.
 * The isothermal limit avoids division by a tiny lapse rate. */
static double AtmospherePressure(double basePressure, double baseTemperature,
                                  double temperature, double height,
                                  double gravity, double molarMass)
{
    if (basePressure <= 0.0 || height <= 0.0) return basePressure;
    double delta = temperature - baseTemperature;
    double integral = fabs(delta) < 1e-8 ? height / baseTemperature :
        height * log1p(delta / baseTemperature) / delta;
    return basePressure * exp(-gravity * molarMass * integral /
                               ATMOSPHERE_GAS_CONSTANT);
}

bool AtmosphereProfileBuild(const AtmosphereInput *input, AtmosphereProfile *out)
{
    if (!out) return false;
    *out = (AtmosphereProfile){ 0 };
    if (!AtmosphereInputIsValid(input)) return false;
    /* Standard-atmosphere lower-column breakpoints. The upper column is a
     * bounded thermosphere/exosphere approximation, not a chemistry solver. */
    static const double heights[] = {
        0, 11000, 20000, 32000, 47000, 51000, 71000, 86000,
        120000, 200000, 500000, 1000000
    };
    static const double temperatures[] = {
        288.15, 216.65, 216.65, 228.65, 270.65, 270.65, 214.65, 184.65
    };
    AtmosphereProfile profile = { .input = *input, .valid = true };
    double thermalScale = input->surfaceTemperatureK / 288.15;
    double heightScale = thermalScale *
        (0.0289644 / input->molarMassKgMol) * (9.80665 / input->gravityMps2);
    double stratopause = thermalScale *
        (216.65 + input->ozoneStrength * (270.65 - 216.65));
    for (int i = 0; i < ATMOSPHERE_LEVEL_COUNT; i++) {
        profile.altitudeM[i] = heights[i] * heightScale;
        if (i < 8) profile.temperatureK[i] = temperatures[i] * thermalScale;
        /* Without an absorbing ozone layer, do not impose Earth's inversion. */
        if (i >= 3 && i <= 5) {
            profile.temperatureK[i] = thermalScale *
                (216.65 + input->ozoneStrength * (temperatures[i] - 216.65));
        } else if (i == 6) {
            profile.temperatureK[i] = stratopause +
                (184.65 * thermalScale - stratopause) * (56.0 / 86.0);
        } else if (i >= 8) {
            double upper = fmax(input->exosphereTemperatureK,
                                184.65 * thermalScale);
            double fraction = i == 8 ? 0.30 : (i == 9 ? 0.80 : 1.0);
            profile.temperatureK[i] = 184.65 * thermalScale +
                (upper - 184.65 * thermalScale) * fraction;
        }
        /* A continuous transition to lighter species above the homopause. */
        double lightFraction = i <= 7 ? 0.0 :
            (i == 8 ? 0.20 : (i == 9 ? 0.45 : (i == 10 ? 0.80 : 0.95)));
        profile.molarMassKgMol[i] = input->molarMassKgMol +
            (fmin(0.004, input->molarMassKgMol) - input->molarMassKgMol) *
            lightFraction;
        /* Geopotential lower atmosphere; gradually declining gravity aloft.
         * The reference radius scales with the column, keeping this portable
         * to generated planets without imposing a surface-radius dependency. */
        double midHeight = i + 1 < ATMOSPHERE_LEVEL_COUNT ?
            (heights[i] + heights[i + 1]) * 0.5 : heights[i];
        profile.gravityMps2[i] = input->gravityMps2;
        if (i >= 7) profile.gravityMps2[i] /= pow(1.0 + midHeight / 6371000.0, 2.0);
    }
    profile.pressureAtm[0] = input->surfacePressureAtm;
    for (int i = 1; i < ATMOSPHERE_LEVEL_COUNT; i++) {
        double mass = (profile.molarMassKgMol[i - 1] +
                       profile.molarMassKgMol[i]) * 0.5;
        profile.pressureAtm[i] = AtmospherePressure(
            profile.pressureAtm[i - 1], profile.temperatureK[i - 1],
            profile.temperatureK[i], profile.altitudeM[i] - profile.altitudeM[i - 1],
            profile.gravityMps2[i - 1], mass);
    }
    profile.surfaceDensityKgM3 = input->surfacePressureAtm *
        ATMOSPHERE_PASCALS_PER_ATM * input->molarMassKgMol /
        (ATMOSPHERE_GAS_CONSTANT * input->surfaceTemperatureK);
    *out = profile;
    return true;
}

AtmosphereSample AtmosphereSampleAt(const AtmosphereProfile *profile,
                                    double altitudeM)
{
    AtmosphereSample sample = { 0 };
    if (!profile || !profile->valid || !isfinite(altitudeM)) return sample;
    double altitude = fmax(altitudeM, 0.0);
    sample.altitudeM = altitude;
    sample.valid = true;
    if (profile->input.surfacePressureAtm <= 0.0 ||
        altitude >= profile->altitudeM[ATMOSPHERE_LEVEL_COUNT - 1]) return sample;
    int segment = 0;
    while (segment + 1 < ATMOSPHERE_LEVEL_COUNT - 1 &&
           altitude >= profile->altitudeM[segment + 1]) segment++;
    double height = altitude - profile->altitudeM[segment];
    double fraction = height / (profile->altitudeM[segment + 1] -
                                 profile->altitudeM[segment]);
    sample.temperatureK = profile->temperatureK[segment] + fraction *
        (profile->temperatureK[segment + 1] - profile->temperatureK[segment]);
    sample.molarMassKgMol = profile->molarMassKgMol[segment] + fraction *
        (profile->molarMassKgMol[segment + 1] - profile->molarMassKgMol[segment]);
    double mass = (profile->molarMassKgMol[segment] +
                   profile->molarMassKgMol[segment + 1]) * 0.5;
    sample.pressureAtm = AtmospherePressure(profile->pressureAtm[segment],
        profile->temperatureK[segment], sample.temperatureK, height,
        profile->gravityMps2[segment], mass);
    /* Smoothly truncate the collisionless tail at the simulation boundary. */
    if (segment == ATMOSPHERE_LEVEL_COUNT - 2)
        sample.pressureAtm *= 1.0 - AtmosphereSmooth(fraction);
    sample.densityKgM3 = sample.pressureAtm * ATMOSPHERE_PASCALS_PER_ATM *
        sample.molarMassKgMol / (ATMOSPHERE_GAS_CONSTANT * sample.temperatureK);
    sample.columnFraction = sample.pressureAtm / profile->input.surfacePressureAtm;
    sample.densityRatio = sample.columnFraction *
        (sample.molarMassKgMol / profile->input.molarMassKgMol) *
        (profile->input.surfaceTemperatureK / sample.temperatureK);
    sample.layer = altitude < profile->altitudeM[1] ? ATMOSPHERE_LAYER_TROPOSPHERE :
        altitude < profile->altitudeM[5] ? ATMOSPHERE_LAYER_STRATOSPHERE :
        altitude < profile->altitudeM[7] ? ATMOSPHERE_LAYER_MESOSPHERE :
        altitude < profile->altitudeM[10] ? ATMOSPHERE_LAYER_THERMOSPHERE :
                                          ATMOSPHERE_LAYER_EXOSPHERE;
    double referenceHeight = altitude / (profile->altitudeM[1] / 11000.0);
    sample.ozone = profile->input.ozoneStrength *
        exp(-pow((referenceHeight - 25000.0) / 10000.0, 2.0)) *
        AtmosphereSmooth(referenceHeight / 11000.0);
    sample.weatherActivity = 1.0 - AtmosphereSmooth(
        (altitude / profile->altitudeM[1] - 0.75) / 0.50);
    sample.ionization = AtmosphereSmooth((referenceHeight - 86000.0) / 114000.0);
    return sample;
}

const char *AtmosphereLayerName(AtmosphereLayer layer)
{
    switch (layer) {
    case ATMOSPHERE_LAYER_VACUUM: return "Vacuum";
    case ATMOSPHERE_LAYER_TROPOSPHERE: return "Troposphere";
    case ATMOSPHERE_LAYER_STRATOSPHERE: return "Stratosphere";
    case ATMOSPHERE_LAYER_MESOSPHERE: return "Mesosphere";
    case ATMOSPHERE_LAYER_THERMOSPHERE: return "Thermosphere";
    case ATMOSPHERE_LAYER_EXOSPHERE: return "Exosphere";
    default: return "Unknown";
    }
}
