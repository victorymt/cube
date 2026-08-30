#include "world/radiation_balance.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define RADIATION_PI 3.141592653589793238462643383279502884
#define RADIATION_TWO_PI (2.0 * RADIATION_PI)

static double RadiationClamp(double value, double minimum, double maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static double RadiationNormalizeAngle(double angle)
{
    double normalized = fmod(angle, RADIATION_TWO_PI);
    if (normalized > RADIATION_PI) normalized -= RADIATION_TWO_PI;
    if (normalized < -RADIATION_PI) normalized += RADIATION_TWO_PI;
    return normalized;
}

double RadiationSolveKepler(double meanAnomalyRad, double eccentricity)
{
    if (!isfinite(meanAnomalyRad) || !isfinite(eccentricity) ||
        eccentricity < 0.0 || eccentricity >= 1.0) {
        return NAN;
    }

    double mean = RadiationNormalizeAngle(meanAnomalyRad);
    double eccentricAnomaly = eccentricity < 0.8 ? mean :
        (mean >= 0.0 ? RADIATION_PI : -RADIATION_PI);
    for (int iteration = 0; iteration < 16; iteration++) {
        double sine = sin(eccentricAnomaly);
        double cosine = cos(eccentricAnomaly);
        double residual = eccentricAnomaly - eccentricity * sine - mean;
        double derivative = 1.0 - eccentricity * cosine;
        if (!(derivative > DBL_MIN) || !isfinite(residual)) return NAN;
        double correction = residual / derivative;
        eccentricAnomaly -= correction;
        if (fabs(correction) < 1e-14) break;
    }
    return RadiationNormalizeAngle(eccentricAnomaly);
}

double RadiationOrbitalDistanceAu(double semiMajorAxisAu,
                                  double eccentricity,
                                  double eccentricAnomalyRad)
{
    if (!(semiMajorAxisAu > 0.0) || !isfinite(semiMajorAxisAu) ||
        !isfinite(eccentricity) || eccentricity < 0.0 || eccentricity >= 1.0 ||
        !isfinite(eccentricAnomalyRad)) {
        return 0.0;
    }
    double distance = semiMajorAxisAu *
                      (1.0 - eccentricity * cos(eccentricAnomalyRad));
    return isfinite(distance) && distance > 0.0 ? distance : 0.0;
}

double RadiationSolarDeclinationRad(double axialTiltRad,
                                    double orbitalLongitudeRad)
{
    if (!isfinite(axialTiltRad) || !isfinite(orbitalLongitudeRad)) return NAN;
    double sine = sin(axialTiltRad) * sin(orbitalLongitudeRad);
    return asin(RadiationClamp(sine, -1.0, 1.0));
}

double RadiationSolarZenithCosine(double latitudeRad,
                                  double declinationRad,
                                  double localHourAngleRad)
{
    if (!isfinite(latitudeRad) || !isfinite(declinationRad) ||
        !isfinite(localHourAngleRad)) {
        return NAN;
    }
    double cosine = sin(latitudeRad) * sin(declinationRad) +
                    cos(latitudeRad) * cos(declinationRad) *
                        cos(localHourAngleRad);
    return RadiationClamp(cosine, -1.0, 1.0);
}

double RadiationDailyMeanInsolationWm2(double solarIrradianceWm2,
                                       double latitudeRad,
                                       double declinationRad)
{
    if (!(solarIrradianceWm2 >= 0.0) || !isfinite(solarIrradianceWm2) ||
        !isfinite(latitudeRad) || !isfinite(declinationRad)) {
        return 0.0;
    }

    double tangentProduct = tan(latitudeRad) * tan(declinationRad);
    double cosineHourAngle;
    if (!isfinite(tangentProduct) || tangentProduct <= -1.0) {
        /* Polar night: the sun never rises. */
        cosineHourAngle = 1.0;
    } else if (tangentProduct >= 1.0) {
        /* Polar day: the sun never sets. */
        cosineHourAngle = -1.0;
    } else {
        cosineHourAngle = -tangentProduct;
    }
    double halfDay = acos(RadiationClamp(cosineHourAngle, -1.0, 1.0));
    double sineLatitude = sin(latitudeRad);
    double cosineLatitude = cos(latitudeRad);
    double sineDeclination = sin(declinationRad);
    double cosineDeclination = cos(declinationRad);
    double mean = solarIrradianceWm2 / RADIATION_PI *
        (halfDay * sineLatitude * sineDeclination +
         cosineLatitude * cosineDeclination * sin(halfDay));
    return isfinite(mean) && mean > 0.0 ? mean : 0.0;
}

double RadiationEffectiveLongwaveEmissivity(double surfaceEmissivity,
                                            double greenhouseOpticalDepth)
{
    if (!(surfaceEmissivity > 0.0) || !isfinite(surfaceEmissivity) ||
        !(greenhouseOpticalDepth >= 0.0) ||
        !isfinite(greenhouseOpticalDepth)) {
        return 0.0;
    }
    double effective = surfaceEmissivity /
                       (1.0 + 0.75 * greenhouseOpticalDepth);
    return RadiationClamp(effective, 0.0, 1.0);
}

static bool RadiationBalanceInputIsValid(const RadiationBalanceInput *input)
{
    return input && isfinite(input->stellarLuminositySolar) &&
           input->stellarLuminositySolar > 0.0 &&
           isfinite(input->semiMajorAxisAu) && input->semiMajorAxisAu > 0.0 &&
           isfinite(input->orbitalEccentricity) &&
           input->orbitalEccentricity >= 0.0 &&
           input->orbitalEccentricity < 1.0 &&
           isfinite(input->orbitalMeanAnomalyRad) &&
           isfinite(input->seasonPhaseRad) && isfinite(input->axialTiltRad) &&
           isfinite(input->latitudeRad) &&
           input->latitudeRad >= -RADIATION_PI * 0.5 &&
           input->latitudeRad <= RADIATION_PI * 0.5 &&
           isfinite(input->localHourAngleRad) && isfinite(input->albedo) &&
           input->albedo >= 0.0 && input->albedo <= 1.0 &&
           isfinite(input->longwaveEmissivity) &&
           input->longwaveEmissivity > 0.0 &&
           input->longwaveEmissivity <= 1.0 &&
           isfinite(input->greenhouseOpticalDepth) &&
           input->greenhouseOpticalDepth >= 0.0 &&
           input->greenhouseOpticalDepth <= 1.0e6 &&
           isfinite(input->surfaceTemperatureK) &&
           input->surfaceTemperatureK > 0.0;
}

bool RadiationBalanceEvaluate(const RadiationBalanceInput *input,
                              RadiationBalanceOutput *out)
{
    if (!out) return false;
    *out = (RadiationBalanceOutput){ 0 };
    if (!RadiationBalanceInputIsValid(input)) return false;

    double eccentricAnomaly = RadiationSolveKepler(
        input->orbitalMeanAnomalyRad, input->orbitalEccentricity);
    double distance = RadiationOrbitalDistanceAu(
        input->semiMajorAxisAu, input->orbitalEccentricity, eccentricAnomaly);
    if (!isfinite(eccentricAnomaly) || !(distance > 0.0)) return false;

    double trueAnomaly = 2.0 * atan2(
        sqrt(1.0 + input->orbitalEccentricity) * sin(eccentricAnomaly * 0.5),
        sqrt(1.0 - input->orbitalEccentricity) * cos(eccentricAnomaly * 0.5));
    double longitude = trueAnomaly + input->seasonPhaseRad;
    double declination = RadiationSolarDeclinationRad(
        input->axialTiltRad, longitude);
    double zenith = RadiationSolarZenithCosine(
        input->latitudeRad, declination, input->localHourAngleRad);
    double irradiance = RADIATION_SOLAR_CONSTANT_WM2 *
                        input->stellarLuminositySolar / (distance * distance);
    double instantaneous = irradiance * fmax(zenith, 0.0);
    double daily = RadiationDailyMeanInsolationWm2(
        irradiance, input->latitudeRad, declination);
    double absorbed = daily * (1.0 - input->albedo);
    double effectiveEmissivity = RadiationEffectiveLongwaveEmissivity(
        input->longwaveEmissivity, input->greenhouseOpticalDepth);
    if (!isfinite(irradiance) || !isfinite(instantaneous) ||
        !isfinite(daily) || !(effectiveEmissivity > 0.0)) return false;
    double outgoing = effectiveEmissivity *
                      RADIATION_STEFAN_BOLTZMANN_WM2K4 *
                      pow(input->surfaceTemperatureK, 4.0);
    double equilibrium = absorbed > 0.0 ?
        pow(absorbed /
                (effectiveEmissivity * RADIATION_STEFAN_BOLTZMANN_WM2K4),
            0.25) : 0.0;
    double net = absorbed - outgoing;
    if (!isfinite(outgoing) || !isfinite(equilibrium) || !isfinite(net)) {
        return false;
    }

    *out = (RadiationBalanceOutput){
        .eccentricAnomalyRad = eccentricAnomaly,
        .trueAnomalyRad = RadiationNormalizeAngle(trueAnomaly),
        .orbitalDistanceAu = distance,
        .solarDeclinationRad = declination,
        .solarZenithCosine = zenith,
        .solarIrradianceWm2 = irradiance,
        .instantaneousInsolationWm2 = instantaneous,
        .dailyMeanInsolationWm2 = daily,
        .absorbedShortwaveWm2 = absorbed,
        .effectiveLongwaveEmissivity = effectiveEmissivity,
        .outgoingLongwaveWm2 = outgoing,
        .netRadiativeFluxWm2 = net,
        .equilibriumTemperatureK = equilibrium
    };
    return true;
}

bool RadiationColumnStateInit(RadiationColumnState *state,
                              double initialTemperatureK,
                              double heatCapacityJm2K)
{
    if (!state) return false;
    *state = (RadiationColumnState){ 0 };
    if (!(initialTemperatureK > 0.0) || !isfinite(initialTemperatureK) ||
        !(heatCapacityJm2K > 0.0) || !isfinite(heatCapacityJm2K)) return false;
    double energy = initialTemperatureK * heatCapacityJm2K;
    if (!isfinite(energy)) return false;
    *state = (RadiationColumnState){
        .energyJm2 = energy,
        .temperatureK = initialTemperatureK,
        .heatCapacityJm2K = heatCapacityJm2K
    };
    return true;
}

bool RadiationColumnStateStep(RadiationColumnState *state,
                              const RadiationBalanceInput *forcing,
                              double deltaSeconds,
                              RadiationBalanceOutput *out)
{
    if (!state || !out) return false;
    *out = (RadiationBalanceOutput){ 0 };
    if (!(deltaSeconds > 0.0) || !isfinite(deltaSeconds) ||
        !(state->temperatureK > 0.0) || !isfinite(state->temperatureK) ||
        !(state->heatCapacityJm2K > 0.0) ||
        !isfinite(state->heatCapacityJm2K) || !isfinite(state->energyJm2) ||
        !RadiationBalanceInputIsValid(forcing)) return false;

    RadiationBalanceInput input = *forcing;
    input.surfaceTemperatureK = state->temperatureK;
    RadiationBalanceOutput evaluated;
    if (!RadiationBalanceEvaluate(&input, &evaluated)) return false;
    double energy = state->energyJm2 +
                    evaluated.netRadiativeFluxWm2 * deltaSeconds;
    double temperature = energy / state->heatCapacityJm2K;
    if (!(energy > 0.0) || !(temperature > 0.0) ||
        !isfinite(energy) || !isfinite(temperature)) return false;
    state->energyJm2 = energy;
    state->temperatureK = temperature;
    *out = evaluated;
    return true;
}
