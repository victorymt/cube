#include "space_orbit.h"

#include "space_units.h"

#include <math.h>

bool SpaceKeplerOrbitIsValid(const SpaceKeplerOrbit *orbit)
{
    return orbit && orbit->semiMajorAxisKm > 0.0 &&
           isfinite(orbit->semiMajorAxisKm) && orbit->centralMassKg > 0.0 &&
           isfinite(orbit->centralMassKg) && orbit->eccentricity >= 0.0 &&
           orbit->eccentricity < 1.0 && isfinite(orbit->eccentricity) &&
           isfinite(orbit->inclinationRad) &&
           isfinite(orbit->longitudeAscendingNodeRad) &&
           isfinite(orbit->argumentPeriapsisRad) &&
           isfinite(orbit->meanAnomalyAtEpochRad);
}

static Vector3 SpaceKeplerRotateFromOrbitalPlane(double x, double z,
                                                  double inclination,
                                                  double node,
                                                  double periapsis)
{
    double periapsisCosine = cos(periapsis);
    double periapsisSine = sin(periapsis);
    double orbitX = x * periapsisCosine - z * periapsisSine;
    double orbitZ = x * periapsisSine + z * periapsisCosine;
    double inclinationCosine = cos(inclination);
    double inclinationSine = sin(inclination);
    double planeY = orbitZ * inclinationSine;
    double planeZ = orbitZ * inclinationCosine;
    double nodeCosine = cos(node);
    double nodeSine = sin(node);
    return (Vector3){
        (float)(orbitX * nodeCosine - planeZ * nodeSine),
        (float)planeY,
        (float)(orbitX * nodeSine + planeZ * nodeCosine)
    };
}

static bool SpaceKeplerVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

bool SpaceKeplerStateAtTime(const SpaceKeplerOrbit *orbit,
                            double simulationTime,
                            SpaceKeplerState *out)
{
    if (!out) return false;
    *out = (SpaceKeplerState){ 0 };
    if (!SpaceKeplerOrbitIsValid(orbit) || !isfinite(simulationTime)) {
        return false;
    }

    double meanMotion = SpaceUnitsKeplerMeanMotionGame(
        orbit->semiMajorAxisKm, orbit->centralMassKg);
    if (!(meanMotion > 0.0) || !isfinite(meanMotion)) return false;

    double meanAnomaly = 0.0;
    if (!SpaceUnitsMeanAnomalyAtTime(
            orbit->meanAnomalyAtEpochRad, meanMotion, simulationTime,
            &meanAnomaly)) return false;

    double eccentricAnomaly = 0.0;
    if (!SpaceUnitsSolveEccentricAnomaly(
            meanAnomaly, orbit->eccentricity, &eccentricAnomaly)) {
        return false;
    }

    double eccentricityScale = sqrt(
        1.0 - orbit->eccentricity * orbit->eccentricity);
    double semiMajorAxisGame = SpaceUnitsKilometersToGameDistance(
        orbit->semiMajorAxisKm);
    double sine = sin(eccentricAnomaly);
    double cosine = cos(eccentricAnomaly);
    double eccentricAnomalyRate = meanMotion /
        (1.0 - orbit->eccentricity * cosine);

    out->positionGame = SpaceKeplerRotateFromOrbitalPlane(
        semiMajorAxisGame * (cosine - orbit->eccentricity),
        semiMajorAxisGame * eccentricityScale * sine,
        orbit->inclinationRad, orbit->longitudeAscendingNodeRad,
        orbit->argumentPeriapsisRad);
    out->velocityGame = SpaceKeplerRotateFromOrbitalPlane(
        -semiMajorAxisGame * sine * eccentricAnomalyRate,
        semiMajorAxisGame * eccentricityScale * cosine * eccentricAnomalyRate,
        orbit->inclinationRad, orbit->longitudeAscendingNodeRad,
        orbit->argumentPeriapsisRad);
    if (!SpaceKeplerVectorIsFinite(out->positionGame) ||
        !SpaceKeplerVectorIsFinite(out->velocityGame)) {
        *out = (SpaceKeplerState){ 0 };
        return false;
    }
    return true;
}
