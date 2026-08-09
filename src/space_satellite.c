#include "space_satellite.h"

#include "space_units.h"

#include <math.h>

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

static double SpaceSatelliteCircleCoverage(double targetRadius,
                                           double occulterRadius,
                                           double separation)
{
    if (!(targetRadius > 0.0) || !(occulterRadius > 0.0) ||
        !isfinite(separation)) {
        return 0.0;
    }
    separation = fabs(separation);
    if (separation >= targetRadius + occulterRadius) return 0.0;
    if (separation <= fabs(targetRadius - occulterRadius)) {
        if (occulterRadius >= targetRadius) return 1.0;
        return (occulterRadius * occulterRadius) /
               (targetRadius * targetRadius);
    }

    double targetTerm = SpaceSatelliteClamp(
        (separation * separation + targetRadius * targetRadius -
         occulterRadius * occulterRadius) /
            (2.0 * separation * targetRadius),
        -1.0, 1.0);
    double occulterTerm = SpaceSatelliteClamp(
        (separation * separation + occulterRadius * occulterRadius -
         targetRadius * targetRadius) /
            (2.0 * separation * occulterRadius),
        -1.0, 1.0);
    double radical = (-separation + targetRadius + occulterRadius) *
                     (separation + targetRadius - occulterRadius) *
                     (separation - targetRadius + occulterRadius) *
                     (separation + targetRadius + occulterRadius);
    double overlapArea = targetRadius * targetRadius * acos(targetTerm) +
                         occulterRadius * occulterRadius * acos(occulterTerm) -
                         0.5 * sqrt(fmax(radical, 0.0));
    return SpaceSatelliteClamp(
        overlapArea / (SPACE_SATELLITE_PI * targetRadius * targetRadius),
        0.0, 1.0);
}

bool SpaceSatelliteGenerate(uint32_t seed, double planetMassKg,
                            double planetRadiusKm,
                            double planetSemiMajorAxisKm,
                            double starMassKg, double occurrenceProbability,
                            bool forceExists, SpaceSatelliteOrbit *out)
{
    if (!out) return false;
    *out = (SpaceSatelliteOrbit){ 0 };
    if (!(planetMassKg > 0.0) || !(planetRadiusKm > 0.0) ||
        !(planetSemiMajorAxisKm > 0.0) || !(starMassKg > 0.0) ||
        !isfinite(planetMassKg) || !isfinite(planetRadiusKm) ||
        !isfinite(planetSemiMajorAxisKm) || !isfinite(starMassKg)) {
        return false;
    }

    occurrenceProbability = SpaceSatelliteClamp(occurrenceProbability, 0.0, 1.0);
    if (!forceExists && SpaceSatelliteUnit(seed, 1u) >= occurrenceProbability) {
        return true;
    }

    double hillRadiusKm = planetSemiMajorAxisKm *
                          cbrt(planetMassKg / (3.0 * starMassKg));
    // This system models the dominant sky-visible moon, not tiny inner rocks.
    double minimumOrbitKm = planetRadiusKm * 6.0;
    double maximumOrbitKm = fmin(planetRadiusKm * 70.0,
                                 hillRadiusKm * 0.35);
    if (!(maximumOrbitKm > minimumOrbitKm * 1.15)) return true;

    double orbitUnit = 0.20 +
                       pow(SpaceSatelliteUnit(seed, 2u), 0.65) * 0.80;
    double radiusRatio = 0.035 +
                         pow(SpaceSatelliteUnit(seed, 3u), 2.2) * 0.245;
    double densityRatio = 0.52 + SpaceSatelliteUnit(seed, 4u) * 0.38;
    out->exists = true;
    out->semiMajorAxisKm = minimumOrbitKm *
                           pow(maximumOrbitKm / minimumOrbitKm, orbitUnit);
    out->eccentricity = 0.002 +
                        pow(SpaceSatelliteUnit(seed, 5u), 1.7) * 0.078;
    out->inclinationRad = (1.5 +
                               pow(SpaceSatelliteUnit(seed, 6u), 1.25) * 23.5) *
                          (SPACE_SATELLITE_PI / 180.0);
    out->longitudeAscendingNodeRad = SpaceSatelliteUnit(seed, 7u) *
                                     2.0 * SPACE_SATELLITE_PI;
    out->argumentPeriapsisRad = SpaceSatelliteUnit(seed, 8u) *
                                2.0 * SPACE_SATELLITE_PI;
    out->meanAnomalyAtEpochRad = SpaceSatelliteUnit(seed, 9u) *
                                 2.0 * SPACE_SATELLITE_PI;
    out->radiusKm = planetRadiusKm * radiusRatio;
    out->massKg = planetMassKg * densityRatio *
                  radiusRatio * radiusRatio * radiusRatio;
    return true;
}

double SpaceSatelliteOrbitalPeriodSeconds(const SpaceSatelliteOrbit *orbit,
                                          double planetMassKg)
{
    if (!orbit || !orbit->exists) return 0.0;
    return SpaceUnitsKeplerPeriodSeconds(orbit->semiMajorAxisKm,
                                         planetMassKg + orbit->massKg);
}

SpaceSatelliteVector3 SpaceSatellitePositionAtSeconds(
    const SpaceSatelliteOrbit *orbit, double planetMassKg,
    double physicalTimeSeconds)
{
    if (!orbit || !orbit->exists || !isfinite(physicalTimeSeconds)) {
        return (SpaceSatelliteVector3){ 0 };
    }

    double mu = SpaceUnitsGravitationalParameterKm(planetMassKg +
                                                    orbit->massKg);
    if (!(mu > 0.0) || !(orbit->semiMajorAxisKm > 0.0)) {
        return (SpaceSatelliteVector3){ 0 };
    }
    double meanMotion = sqrt(mu /
                             (orbit->semiMajorAxisKm * orbit->semiMajorAxisKm *
                              orbit->semiMajorAxisKm));
    double meanAnomaly = fmod(orbit->meanAnomalyAtEpochRad +
                              physicalTimeSeconds * meanMotion,
                              2.0 * SPACE_SATELLITE_PI);
    double eccentricAnomaly = meanAnomaly;
    for (int iteration = 0; iteration < 7; iteration++) {
        double residual = eccentricAnomaly -
                          orbit->eccentricity * sin(eccentricAnomaly) -
                          meanAnomaly;
        eccentricAnomaly -= residual /
                            (1.0 - orbit->eccentricity *
                                       cos(eccentricAnomaly));
    }

    double x = orbit->semiMajorAxisKm *
               (cos(eccentricAnomaly) - orbit->eccentricity);
    double z = orbit->semiMajorAxisKm *
               sqrt(1.0 - orbit->eccentricity * orbit->eccentricity) *
               sin(eccentricAnomaly);
    double periCos = cos(orbit->argumentPeriapsisRad);
    double periSin = sin(orbit->argumentPeriapsisRad);
    double periX = x * periCos - z * periSin;
    double periZ = x * periSin + z * periCos;
    double inclinedY = periZ * sin(orbit->inclinationRad);
    double inclinedZ = periZ * cos(orbit->inclinationRad);
    double nodeCos = cos(orbit->longitudeAscendingNodeRad);
    double nodeSin = sin(orbit->longitudeAscendingNodeRad);
    return (SpaceSatelliteVector3){
        periX * nodeCos - inclinedZ * nodeSin,
        inclinedY,
        periX * nodeSin + inclinedZ * nodeCos
    };
}

double SpaceSatelliteSolarOccultationFraction(
    SpaceSatelliteVector3 observerPositionKm,
    SpaceSatelliteVector3 satellitePositionKm, double satelliteRadiusKm,
    SpaceSatelliteVector3 sourcePositionKm, double sourceRadiusKm)
{
    SpaceSatelliteVector3 toSatellite = SpaceSatelliteSubtract(
        satellitePositionKm, observerPositionKm);
    SpaceSatelliteVector3 toSource = SpaceSatelliteSubtract(
        sourcePositionKm, observerPositionKm);
    double satelliteDistance = SpaceSatelliteLength(toSatellite);
    double sourceDistance = SpaceSatelliteLength(toSource);
    if (!(satelliteRadiusKm > 0.0) || !(sourceRadiusKm > 0.0) ||
        !(satelliteDistance > satelliteRadiusKm) ||
        !(sourceDistance > sourceRadiusKm) ||
        satelliteDistance >= sourceDistance) {
        return 0.0;
    }

    double satelliteAngularRadius = asin(SpaceSatelliteClamp(
        satelliteRadiusKm / satelliteDistance, 0.0, 1.0));
    double sourceAngularRadius = asin(SpaceSatelliteClamp(
        sourceRadiusKm / sourceDistance, 0.0, 1.0));
    double alignment = SpaceSatelliteDot(toSatellite, toSource) /
                       (satelliteDistance * sourceDistance);
    double separation = acos(SpaceSatelliteClamp(alignment, -1.0, 1.0));
    return SpaceSatelliteCircleCoverage(sourceAngularRadius,
                                        satelliteAngularRadius, separation);
}

double SpaceSatellitePlanetUmbraFraction(
    SpaceSatelliteVector3 satellitePositionKm, double satelliteRadiusKm,
    double planetRadiusKm, SpaceSatelliteVector3 sourcePositionKm,
    double sourceRadiusKm)
{
    double sourceDistance = SpaceSatelliteLength(sourcePositionKm);
    double satelliteDistance = SpaceSatelliteLength(satellitePositionKm);
    if (!(satelliteRadiusKm > 0.0) || !(planetRadiusKm > 0.0) ||
        !(sourceRadiusKm > planetRadiusKm) ||
        !(sourceDistance > sourceRadiusKm) || !(satelliteDistance > 0.0)) {
        return 0.0;
    }

    SpaceSatelliteVector3 sourceDirection = SpaceSatelliteScale(
        sourcePositionKm, 1.0 / sourceDistance);
    double behindDistance = -SpaceSatelliteDot(satellitePositionKm,
                                                sourceDirection);
    if (!(behindDistance > 0.0)) return 0.0;

    double umbraLength = sourceDistance * planetRadiusKm /
                         (sourceRadiusKm - planetRadiusKm);
    if (behindDistance >= umbraLength) return 0.0;
    double umbraRadius = planetRadiusKm *
                         (1.0 - behindDistance / umbraLength);
    SpaceSatelliteVector3 shadowAxisPoint = SpaceSatelliteScale(
        sourceDirection, -behindDistance);
    double axisDistance = SpaceSatelliteLength(SpaceSatelliteSubtract(
        satellitePositionKm, shadowAxisPoint));
    return SpaceSatelliteCircleCoverage(satelliteRadiusKm, umbraRadius,
                                        axisDistance);
}
