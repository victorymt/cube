#include "space/space_satellite.h"

#include "space/space_illumination.h"
#include "space/space_units.h"

#include <math.h>
#include <string.h>

#define SPACE_SATELLITE_PI 3.14159265358979323846

static uint32_t SpaceSatelliteMix32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static double SpaceSatelliteUnit(uint32_t seed, uint32_t lane)
{
    uint32_t mixed = SpaceSatelliteMix32(seed ^ (lane * 0x9e3779b9u));
    return (double)(mixed >> 8) / 16777216.0;
}

static double SpaceSatelliteClamp(double value, double minimum, double maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static SpaceSatelliteVector3 SpaceSatelliteSubtract(SpaceSatelliteVector3 a,
                                                     SpaceSatelliteVector3 b)
{
    return (SpaceSatelliteVector3){ a.x - b.x, a.y - b.y, a.z - b.z };
}

static double SpaceSatelliteDot(SpaceSatelliteVector3 a,
                                SpaceSatelliteVector3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static double SpaceSatelliteLength(SpaceSatelliteVector3 value)
{
    return sqrt(SpaceSatelliteDot(value, value));
}

static SpaceSatelliteVector3 SpaceSatelliteScale(SpaceSatelliteVector3 value,
                                                  double scale)
{
    return (SpaceSatelliteVector3){ value.x * scale, value.y * scale,
                                    value.z * scale };
}

static bool SpaceSatelliteVectorIsFinite(SpaceSatelliteVector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static bool SpaceSatelliteOrbitIsValid(const SpaceSatelliteOrbit *orbit,
                                       double planetMassKg)
{
    return orbit && orbit->exists && planetMassKg > 0.0 &&
           isfinite(planetMassKg) && orbit->massKg > 0.0 &&
           isfinite(orbit->massKg) &&
           isfinite(planetMassKg + orbit->massKg) &&
           orbit->radiusKm > 0.0 && isfinite(orbit->radiusKm) &&
           orbit->semiMajorAxisKm > 0.0 &&
           isfinite(orbit->semiMajorAxisKm) &&
           orbit->eccentricity >= 0.0 && orbit->eccentricity < 1.0 &&
           isfinite(orbit->eccentricity) &&
           isfinite(orbit->inclinationRad) &&
           isfinite(orbit->longitudeAscendingNodeRad) &&
           isfinite(orbit->argumentPeriapsisRad) &&
           isfinite(orbit->meanAnomalyAtEpochRad);
}

bool SpaceSatelliteGenerate(uint32_t seed, double planetMassKg,
                            double planetRadiusKm,
                            double planetSemiMajorAxisKm,
                            double starMassKg, double occurrenceProbability,
                            bool forceExists, SpaceSatelliteOrbit *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!(planetMassKg > 0.0) || !(planetRadiusKm > 0.0) ||
        !(planetSemiMajorAxisKm > 0.0) || !(starMassKg > 0.0) ||
        !isfinite(planetMassKg) || !isfinite(planetRadiusKm) ||
        !isfinite(planetSemiMajorAxisKm) || !isfinite(starMassKg) ||
        !isfinite(occurrenceProbability)) {
        return false;
    }

    occurrenceProbability = SpaceSatelliteClamp(occurrenceProbability, 0.0, 1.0);
    if (!forceExists && SpaceSatelliteUnit(seed, 1u) >= occurrenceProbability) {
        return true;
    }

    double radiusRatio = 0.035 +
                         pow(SpaceSatelliteUnit(seed, 3u), 2.2) * 0.245;
    double densityRatio = 0.52 + SpaceSatelliteUnit(seed, 4u) * 0.38;
    double satelliteRadiusKm = planetRadiusKm * radiusRatio;
    double satelliteMassKg = planetMassKg * densityRatio *
                             radiusRatio * radiusRatio * radiusRatio;
    double eccentricity = 0.002 +
                          pow(SpaceSatelliteUnit(seed, 5u), 1.7) * 0.078;
    double hillRadiusKm = planetSemiMajorAxisKm *
                          cbrt(planetMassKg / (3.0 * starMassKg));
    double rocheLimitKm = SpaceSatelliteFluidRocheLimitKm(
        planetMassKg, planetRadiusKm, satelliteMassKg, satelliteRadiusKm);
    // This system models the dominant sky-visible moon, not tiny inner rocks.
    double minimumPeriapsisKm = fmax(planetRadiusKm * 6.0,
                                     rocheLimitKm * 1.15);
    double maximumApoapsisKm = fmin(planetRadiusKm * 70.0,
                                    hillRadiusKm * 0.35);
    double minimumOrbitKm = minimumPeriapsisKm / (1.0 - eccentricity);
    double maximumOrbitKm = maximumApoapsisKm / (1.0 + eccentricity);
    if (!(maximumOrbitKm > minimumOrbitKm * 1.15)) return true;

    double orbitUnit = 0.20 +
                       pow(SpaceSatelliteUnit(seed, 2u), 0.65) * 0.80;
    out->exists = true;
    out->semiMajorAxisKm = minimumOrbitKm *
                           pow(maximumOrbitKm / minimumOrbitKm, orbitUnit);
    out->eccentricity = eccentricity;
    out->inclinationRad = (1.5 +
                               pow(SpaceSatelliteUnit(seed, 6u), 1.25) * 23.5) *
                          (SPACE_SATELLITE_PI / 180.0);
    out->longitudeAscendingNodeRad = SpaceSatelliteUnit(seed, 7u) *
                                     2.0 * SPACE_SATELLITE_PI;
    out->argumentPeriapsisRad = SpaceSatelliteUnit(seed, 8u) *
                                2.0 * SPACE_SATELLITE_PI;
    out->meanAnomalyAtEpochRad = SpaceSatelliteUnit(seed, 9u) *
                                 2.0 * SPACE_SATELLITE_PI;
    out->radiusKm = satelliteRadiusKm;
    out->massKg = satelliteMassKg;
    if (!SpaceSatelliteOrbitIsValid(out, planetMassKg)) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    return true;
}

double SpaceSatelliteFluidRocheLimitKm(double planetMassKg,
                                       double planetRadiusKm,
                                       double satelliteMassKg,
                                       double satelliteRadiusKm)
{
    if (!(planetMassKg > 0.0) || !(planetRadiusKm > 0.0) ||
        !(satelliteMassKg > 0.0) || !(satelliteRadiusKm > 0.0) ||
        !isfinite(planetMassKg) || !isfinite(planetRadiusKm) ||
        !isfinite(satelliteMassKg) || !isfinite(satelliteRadiusKm)) {
        return 0.0;
    }
    double densityRatio = (planetMassKg / satelliteMassKg) *
                          pow(satelliteRadiusKm / planetRadiusKm, 3.0);
    double result = 2.44 * planetRadiusKm * cbrt(densityRatio);
    return isfinite(result) && result > 0.0 ? result : 0.0;
}

double SpaceSatelliteOrbitalPeriodSeconds(const SpaceSatelliteOrbit *orbit,
                                          double planetMassKg)
{
    if (!SpaceSatelliteOrbitIsValid(orbit, planetMassKg)) return 0.0;
    double period = SpaceUnitsKeplerPeriodSeconds(
        orbit->semiMajorAxisKm, planetMassKg + orbit->massKg);
    return isfinite(period) && period > 0.0 ? period : 0.0;
}

static SpaceSatelliteVector3 SpaceSatelliteRotateFromOrbitalPlane(
    double x, double z, double inclination, double node, double periapsis)
{
    double periCos = cos(periapsis);
    double periSin = sin(periapsis);
    double periX = x * periCos - z * periSin;
    double periZ = x * periSin + z * periCos;
    double inclinedY = periZ * sin(inclination);
    double inclinedZ = periZ * cos(inclination);
    double nodeCos = cos(node);
    double nodeSin = sin(node);
    return (SpaceSatelliteVector3){
        periX * nodeCos - inclinedZ * nodeSin,
        inclinedY,
        periX * nodeSin + inclinedZ * nodeCos
    };
}

bool SpaceSatelliteStateAtSeconds(const SpaceSatelliteOrbit *orbit,
                                  double planetMassKg,
                                  double physicalTimeSeconds,
                                  SpaceSatelliteState *out)
{
    if (!out) return false;
    *out = (SpaceSatelliteState){ 0 };
    if (!SpaceSatelliteOrbitIsValid(orbit, planetMassKg) ||
        !isfinite(physicalTimeSeconds)) {
        return false;
    }

    double mu = SpaceUnitsGravitationalParameterKm(planetMassKg +
                                                    orbit->massKg);
    double meanMotion = sqrt(mu /
                             (orbit->semiMajorAxisKm * orbit->semiMajorAxisKm *
                              orbit->semiMajorAxisKm));
    if (!(meanMotion > 0.0) || !isfinite(meanMotion)) return false;
    double meanAnomaly = 0.0;
    if (!SpaceUnitsMeanAnomalyAtTime(
            orbit->meanAnomalyAtEpochRad, meanMotion,
            physicalTimeSeconds, &meanAnomaly)) {
        return false;
    }
    double eccentricAnomaly = 0.0;
    if (!SpaceUnitsSolveEccentricAnomaly(
            meanAnomaly, orbit->eccentricity, &eccentricAnomaly)) {
        return false;
    }

    double eccentricityScale = sqrt(
        (1.0 - orbit->eccentricity) * (1.0 + orbit->eccentricity));
    if (!(eccentricityScale > 0.0) || !isfinite(eccentricityScale)) {
        return false;
    }
    double eccentricAnomalyDenominator =
        SpaceUnitsEccentricAnomalyDerivative(eccentricAnomaly,
                                             orbit->eccentricity);
    if (!(eccentricAnomalyDenominator > 0.0) ||
        !isfinite(eccentricAnomalyDenominator)) {
        return false;
    }
    double eccentricAnomalyRate = meanMotion /
        eccentricAnomalyDenominator;
    if (!(eccentricAnomalyRate > 0.0) || !isfinite(eccentricAnomalyRate)) {
        return false;
    }
    double x = orbit->semiMajorAxisKm *
               (cos(eccentricAnomaly) - orbit->eccentricity);
    double z = orbit->semiMajorAxisKm * eccentricityScale *
               sin(eccentricAnomaly);
    out->positionKm = SpaceSatelliteRotateFromOrbitalPlane(
        x, z, orbit->inclinationRad, orbit->longitudeAscendingNodeRad,
        orbit->argumentPeriapsisRad);
    out->velocityKmPerSecond = SpaceSatelliteRotateFromOrbitalPlane(
        -orbit->semiMajorAxisKm * sin(eccentricAnomaly) *
            eccentricAnomalyRate,
        orbit->semiMajorAxisKm * eccentricityScale *
            cos(eccentricAnomaly) * eccentricAnomalyRate,
        orbit->inclinationRad, orbit->longitudeAscendingNodeRad,
        orbit->argumentPeriapsisRad);
    if (!SpaceSatelliteVectorIsFinite(out->positionKm) ||
        !SpaceSatelliteVectorIsFinite(out->velocityKmPerSecond)) {
        *out = (SpaceSatelliteState){ 0 };
        return false;
    }
    return true;
}

SpaceSatelliteVector3 SpaceSatellitePositionAtSeconds(
    const SpaceSatelliteOrbit *orbit, double planetMassKg,
    double physicalTimeSeconds)
{
    SpaceSatelliteState state;
    return SpaceSatelliteStateAtSeconds(orbit, planetMassKg,
                                        physicalTimeSeconds, &state)
        ? state.positionKm : (SpaceSatelliteVector3){ 0 };
}

double SpaceSatelliteSolarOccultationFraction(
    SpaceSatelliteVector3 observerPositionKm,
    SpaceSatelliteVector3 satellitePositionKm, double satelliteRadiusKm,
    SpaceSatelliteVector3 sourcePositionKm, double sourceRadiusKm)
{
    if (!SpaceSatelliteVectorIsFinite(observerPositionKm) ||
        !SpaceSatelliteVectorIsFinite(satellitePositionKm) ||
        !SpaceSatelliteVectorIsFinite(sourcePositionKm) ||
        !(satelliteRadiusKm > 0.0) || !(sourceRadiusKm > 0.0) ||
        !isfinite(satelliteRadiusKm) || !isfinite(sourceRadiusKm)) {
        return 0.0;
    }
    SpaceSatelliteVector3 toSatellite = SpaceSatelliteSubtract(
        satellitePositionKm, observerPositionKm);
    SpaceSatelliteVector3 toSource = SpaceSatelliteSubtract(
        sourcePositionKm, observerPositionKm);
    double satelliteDistance = SpaceSatelliteLength(toSatellite);
    double sourceDistance = SpaceSatelliteLength(toSource);
    if (!isfinite(satelliteDistance) || !isfinite(sourceDistance) ||
        !(satelliteDistance > satelliteRadiusKm) ||
        !(sourceDistance > sourceRadiusKm) ||
        satelliteDistance >= sourceDistance) {
        return 0.0;
    }

    return SpaceIlluminationOccultationFraction(
        (SpaceIlluminationBody){
            .positionKm = {
                toSatellite.x, toSatellite.y, toSatellite.z
            },
            .radiusKm = satelliteRadiusKm
        },
        (SpaceIlluminationBody){
            .positionKm = { toSource.x, toSource.y, toSource.z },
            .radiusKm = sourceRadiusKm
        });
}

double SpaceSatellitePlanetUmbraFraction(
    SpaceSatelliteVector3 satellitePositionKm, double satelliteRadiusKm,
    double planetRadiusKm, SpaceSatelliteVector3 sourcePositionKm,
    double sourceRadiusKm)
{
    if (!SpaceSatelliteVectorIsFinite(satellitePositionKm) ||
        !SpaceSatelliteVectorIsFinite(sourcePositionKm) ||
        !(satelliteRadiusKm > 0.0) || !(planetRadiusKm > 0.0) ||
        !(sourceRadiusKm > planetRadiusKm) ||
        !isfinite(satelliteRadiusKm) || !isfinite(planetRadiusKm) ||
        !isfinite(sourceRadiusKm)) {
        return 0.0;
    }
    double sourceDistance = SpaceSatelliteLength(sourcePositionKm);
    double satelliteDistance = SpaceSatelliteLength(satellitePositionKm);
    if (!isfinite(sourceDistance) || !isfinite(satelliteDistance) ||
        !(sourceDistance > sourceRadiusKm) || !(satelliteDistance > 0.0)) {
        return 0.0;
    }

    SpaceSatelliteVector3 sourceDirection = SpaceSatelliteScale(
        sourcePositionKm, 1.0 / sourceDistance);
    double behindDistance = -SpaceSatelliteDot(satellitePositionKm,
                                                sourceDirection);
    if (!(behindDistance > 0.0) || !isfinite(behindDistance)) return 0.0;

    double umbraLength = sourceDistance * planetRadiusKm /
                         (sourceRadiusKm - planetRadiusKm);
    if (!(umbraLength > 0.0) || !isfinite(umbraLength) ||
        behindDistance >= umbraLength) return 0.0;
    double umbraRadius = planetRadiusKm *
                         (1.0 - behindDistance / umbraLength);
    if (!(umbraRadius > 0.0) || !isfinite(umbraRadius)) return 0.0;
    SpaceSatelliteVector3 shadowAxisPoint = SpaceSatelliteScale(
        sourceDirection, -behindDistance);
    double axisDistance = SpaceSatelliteLength(SpaceSatelliteSubtract(
        satellitePositionKm, shadowAxisPoint));
    if (!isfinite(axisDistance)) return 0.0;
    return SpaceIlluminationCircleCoverage(satelliteRadiusKm, umbraRadius,
                                           axisDistance);
}
