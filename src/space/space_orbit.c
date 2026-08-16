#include "space/space_orbit.h"

#include "space/space_units.h"

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

static CelestialVector3 SpaceKeplerRotateFromOrbitalPlane(
    double x, double z, double inclination, double node, double periapsis)
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
    return (CelestialVector3){
        orbitX * nodeCosine - planeZ * nodeSine,
        planeY,
        orbitX * nodeSine + planeZ * nodeCosine
    };
}

static bool SpaceKeplerVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static bool SpaceKeplerCelestialVectorIsFinite(CelestialVector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static Vector3 SpaceKeplerPositionToGame(CelestialVector3 value)
{
    return (Vector3){
        (float)SpaceUnitsKilometersToGameDistance(value.x),
        (float)SpaceUnitsKilometersToGameDistance(value.y),
        (float)SpaceUnitsKilometersToGameDistance(value.z)
    };
}

static Vector3 SpaceKeplerVelocityToGame(CelestialVector3 value)
{
    return (Vector3){
        (float)SpaceUnitsKilometersPerSecondToGameVelocity(value.x),
        (float)SpaceUnitsKilometersPerSecondToGameVelocity(value.y),
        (float)SpaceUnitsKilometersPerSecondToGameVelocity(value.z)
    };
}

typedef struct SpaceKeplerVectorD {
    double x;
    double y;
    double z;
} SpaceKeplerVectorD;

static double SpaceKeplerDotD(SpaceKeplerVectorD left,
                              SpaceKeplerVectorD right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

static SpaceKeplerVectorD SpaceKeplerCrossD(SpaceKeplerVectorD left,
                                             SpaceKeplerVectorD right)
{
    return (SpaceKeplerVectorD){
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x
    };
}

static double SpaceKeplerLengthD(SpaceKeplerVectorD value)
{
    return sqrt(SpaceKeplerDotD(value, value));
}

static double SpaceKeplerClampD(double value, double minimum,
                                double maximum)
{
    return fmin(fmax(value, minimum), maximum);
}

static double SpaceKeplerNormalizeAngle(double angle)
{
    const double twoPi = 6.28318530717958647692;
    double normalized = fmod(angle, twoPi);
    return normalized < 0.0 ? normalized + twoPi : normalized;
}

bool SpaceKeplerOrbitFromState(const SpaceKeplerState *state,
                               double centralMassKg,
                               SpaceKeplerOrbit *out)
{
    if (!out) return false;
    *out = (SpaceKeplerOrbit){ 0 };
    if (!state || !SpaceKeplerVectorIsFinite(state->positionGame) ||
        !SpaceKeplerVectorIsFinite(state->velocityGame) ||
        !(centralMassKg > 0.0) || !isfinite(centralMassKg)) {
        return false;
    }

    // Map the project's XZ orbital plane to the conventional XY plane.
    SpaceKeplerVectorD position = {
        SpaceUnitsGameDistanceToKilometers(state->positionGame.x),
        SpaceUnitsGameDistanceToKilometers(state->positionGame.z),
        -SpaceUnitsGameDistanceToKilometers(state->positionGame.y)
    };
    SpaceKeplerVectorD velocity = {
        SpaceUnitsGameVelocityToKilometersPerSecond(state->velocityGame.x),
        SpaceUnitsGameVelocityToKilometersPerSecond(state->velocityGame.z),
        -SpaceUnitsGameVelocityToKilometersPerSecond(state->velocityGame.y)
    };
    double radiusKm = SpaceKeplerLengthD(position);
    double speedSquared = SpaceKeplerDotD(velocity, velocity);
    double mu = SPACE_UNITS_GRAVITATIONAL_CONSTANT_KM3_KG_S2 *
                centralMassKg;
    if (!(radiusKm > 0.0) || !isfinite(radiusKm) ||
        !(mu > 0.0) || !isfinite(mu) || !isfinite(speedSquared)) {
        return false;
    }

    SpaceKeplerVectorD angularMomentum = SpaceKeplerCrossD(position,
                                                            velocity);
    double angularMomentumLength = SpaceKeplerLengthD(angularMomentum);
    double energy = 0.5 * speedSquared - mu / radiusKm;
    if (!(angularMomentumLength > 0.0) ||
        !isfinite(angularMomentumLength) || !(energy < 0.0) ||
        !isfinite(energy)) {
        return false;
    }

    SpaceKeplerVectorD velocityCrossMomentum = SpaceKeplerCrossD(
        velocity, angularMomentum);
    SpaceKeplerVectorD eccentricityVector = {
        velocityCrossMomentum.x / mu - position.x / radiusKm,
        velocityCrossMomentum.y / mu - position.y / radiusKm,
        velocityCrossMomentum.z / mu - position.z / radiusKm
    };
    double eccentricity = SpaceKeplerLengthD(eccentricityVector);
    double semiMajorAxisKm = -mu / (2.0 * energy);
    if (eccentricity < 1.0e-8) eccentricity = 0.0;
    if (!(semiMajorAxisKm > 0.0) || !isfinite(semiMajorAxisKm) ||
        eccentricity < 0.0 || eccentricity >= 1.0 ||
        !isfinite(eccentricity)) {
        return false;
    }

    SpaceKeplerVectorD node = {
        -angularMomentum.y,
        angularMomentum.x,
        0.0
    };
    double nodeLength = SpaceKeplerLengthD(node);
    double inclination = acos(SpaceKeplerClampD(
        angularMomentum.z / angularMomentumLength, -1.0, 1.0));
    double longitudeAscendingNode = nodeLength > 1.0e-10
        ? atan2(node.y, node.x) : 0.0;
    double argumentPeriapsis = 0.0;
    double trueAnomaly = 0.0;
    if (eccentricity > 0.0) {
        if (nodeLength > 1.0e-10) {
            SpaceKeplerVectorD nodeCrossEccentricity = SpaceKeplerCrossD(
                node, eccentricityVector);
            argumentPeriapsis = atan2(
                SpaceKeplerDotD(nodeCrossEccentricity,
                                angularMomentum) /
                    (nodeLength * eccentricity * angularMomentumLength),
                SpaceKeplerDotD(node, eccentricityVector) /
                    (nodeLength * eccentricity));
        } else {
            argumentPeriapsis = atan2(eccentricityVector.y,
                                      eccentricityVector.x);
        }
        SpaceKeplerVectorD eccentricityCrossPosition = SpaceKeplerCrossD(
            eccentricityVector, position);
        trueAnomaly = atan2(
            SpaceKeplerDotD(eccentricityCrossPosition,
                            angularMomentum) /
                (eccentricity * radiusKm * angularMomentumLength),
            SpaceKeplerDotD(eccentricityVector, position) /
                (eccentricity * radiusKm));
    } else if (nodeLength > 1.0e-10) {
        SpaceKeplerVectorD nodeCrossPosition = SpaceKeplerCrossD(node,
                                                                 position);
        trueAnomaly = atan2(
            SpaceKeplerDotD(nodeCrossPosition, angularMomentum) /
                (nodeLength * radiusKm * angularMomentumLength),
            SpaceKeplerDotD(node, position) / (nodeLength * radiusKm));
    } else {
        trueAnomaly = atan2(position.y, position.x);
    }

    double eccentricAnomaly = eccentricity > 0.0
        ? 2.0 * atan2(sqrt(1.0 - eccentricity) * sin(trueAnomaly * 0.5),
                      sqrt(1.0 + eccentricity) * cos(trueAnomaly * 0.5))
        : trueAnomaly;
    SpaceKeplerOrbit orbit = {
        .semiMajorAxisKm = semiMajorAxisKm,
        .centralMassKg = centralMassKg,
        .eccentricity = eccentricity,
        .inclinationRad = -inclination,
        .longitudeAscendingNodeRad =
            SpaceKeplerNormalizeAngle(longitudeAscendingNode),
        .argumentPeriapsisRad =
            SpaceKeplerNormalizeAngle(argumentPeriapsis),
        .meanAnomalyAtEpochRad = SpaceKeplerNormalizeAngle(
            eccentricAnomaly - eccentricity * sin(eccentricAnomaly))
    };
    if (!SpaceKeplerOrbitIsValid(&orbit)) return false;
    *out = orbit;
    return true;
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
        (1.0 - orbit->eccentricity) * (1.0 + orbit->eccentricity));
    double sine = sin(eccentricAnomaly);
    double cosine = cos(eccentricAnomaly);
    double eccentricAnomalyDenominator =
        SpaceUnitsEccentricAnomalyDerivative(eccentricAnomaly,
                                             orbit->eccentricity);
    if (!(eccentricAnomalyDenominator > 0.0)) return false;
    double eccentricAnomalyRate = meanMotion /
                                  eccentricAnomalyDenominator;
    if (!isfinite(eccentricAnomalyRate)) return false;

    out->positionKm = SpaceKeplerRotateFromOrbitalPlane(
        orbit->semiMajorAxisKm * (cosine - orbit->eccentricity),
        orbit->semiMajorAxisKm * eccentricityScale * sine,
        orbit->inclinationRad, orbit->longitudeAscendingNodeRad,
        orbit->argumentPeriapsisRad);
    CelestialVector3 velocityKmPerGameTime = SpaceKeplerRotateFromOrbitalPlane(
        -orbit->semiMajorAxisKm * sine * eccentricAnomalyRate,
        orbit->semiMajorAxisKm * eccentricityScale * cosine * eccentricAnomalyRate,
        orbit->inclinationRad, orbit->longitudeAscendingNodeRad,
        orbit->argumentPeriapsisRad);
    double perSecond = 1.0 / SPACE_UNITS_SECONDS_PER_GAME_TIME;
    out->velocityKmPerSecond = (CelestialVector3){
        velocityKmPerGameTime.x * perSecond,
        velocityKmPerGameTime.y * perSecond,
        velocityKmPerGameTime.z * perSecond
    };
    out->positionGame = SpaceKeplerPositionToGame(out->positionKm);
    out->velocityGame = SpaceKeplerVelocityToGame(out->velocityKmPerSecond);
    if (!SpaceKeplerCelestialVectorIsFinite(out->positionKm) ||
        !SpaceKeplerCelestialVectorIsFinite(out->velocityKmPerSecond) ||
        !SpaceKeplerVectorIsFinite(out->positionGame) ||
        !SpaceKeplerVectorIsFinite(out->velocityGame)) {
        *out = (SpaceKeplerState){ 0 };
        return false;
    }
    return true;
}
