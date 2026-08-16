#include "space/space_barycenter.h"

#include "space/space_orbit.h"
#include "space/space_units.h"

#include <math.h>

static bool SpaceBarycenterVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static bool SpaceBarycenterRelativeOrbit(
    double separationKm, double centralMassKg, double eccentricity,
    double phaseAtEpoch, double argumentPeriapsis, double inclination,
    double node, double simulationTime,
    SpaceBarycenterBodyState *out)
{
    if (!out) return false;
    *out = (SpaceBarycenterBodyState){ 0 };
    SpaceKeplerOrbit orbit = {
        .semiMajorAxisKm = separationKm,
        .centralMassKg = centralMassKg,
        .eccentricity = eccentricity,
        .inclinationRad = inclination,
        .longitudeAscendingNodeRad = node,
        .argumentPeriapsisRad = argumentPeriapsis,
        .meanAnomalyAtEpochRad = phaseAtEpoch
    };
    SpaceKeplerState state;
    if (!SpaceKeplerStateAtTime(&orbit, simulationTime, &state)) {
        return false;
    }
    out->offsetGame = state.positionGame;
    out->velocityGame = state.velocityGame;
    return true;
}

static Vector3 SpaceBarycenterScale(Vector3 value, double scale)
{
    return (Vector3){
        (float)((double)value.x * scale),
        (float)((double)value.y * scale),
        (float)((double)value.z * scale)
    };
}

static Vector3 SpaceBarycenterAdd(Vector3 left, Vector3 right)
{
    return (Vector3){ left.x + right.x, left.y + right.y, left.z + right.z };
}

static Vector3 SpaceBarycenterAdvance(Vector3 position, Vector3 velocity,
                                      double simulationTime)
{
    return (Vector3){
        (float)((double)position.x + (double)velocity.x * simulationTime),
        (float)((double)position.y + (double)velocity.y * simulationTime),
        (float)((double)position.z + (double)velocity.z * simulationTime)
    };
}

static void SpaceBarycenterClearStates(SpaceBarycenterBodyState *out,
                                       int count)
{
    for (int i = 0; i < count; i++) {
        out[i] = (SpaceBarycenterBodyState){ 0 };
    }
}

static bool SpaceBarycenterStatesAreFinite(
    const SpaceBarycenterBodyState *states, int count);

static bool SpaceBarycenterOrbitIsValid(const SpaceBarycenterOrbit *orbit,
                                        double simulationTime,
                                        int maxCount)
{
    if (!orbit || !isfinite(simulationTime) || orbit->bodyCount < 1 ||
        orbit->bodyCount > SPACE_BARYCENTER_MAX_BODIES ||
        maxCount < orbit->bodyCount) return false;
    for (int i = 0; i < orbit->bodyCount; i++) {
        if (!(orbit->massKg[i] > 0.0) || !isfinite(orbit->massKg[i])) {
            return false;
        }
    }
    if (orbit->motion < SPACE_BARYCENTER_BOUND ||
        orbit->motion > SPACE_BARYCENTER_FREE_FLIGHT ||
        (orbit->motion == SPACE_BARYCENTER_OUTER_FREE_FLIGHT &&
         orbit->bodyCount != 3) ||
        (orbit->bodyCount == 1 && orbit->motion != SPACE_BARYCENTER_BOUND)) {
        return false;
    }
    if (orbit->bodyCount == 1) return true;
    if (orbit->motion == SPACE_BARYCENTER_FREE_FLIGHT) {
        for (int i = 0; i < orbit->bodyCount; i++) {
            if (!SpaceBarycenterVectorIsFinite(
                    orbit->freeFlightOffsetGame[i]) ||
                !SpaceBarycenterVectorIsFinite(
                    orbit->freeFlightVelocityGame[i])) {
                return false;
            }
        }
        return true;
    }
    if (!(orbit->innerSeparationKm > 0.0) ||
        !isfinite(orbit->innerSeparationKm) ||
        orbit->innerEccentricity < 0.0 ||
        orbit->innerEccentricity >= 1.0 ||
        !isfinite(orbit->innerEccentricity) ||
        !isfinite(orbit->innerPhaseRad) ||
        !isfinite(orbit->innerArgumentPeriapsisRad) ||
        !isfinite(orbit->innerInclinationRad) ||
        !isfinite(orbit->innerNodeRad)) return false;
    double innerMass = orbit->massKg[0] + orbit->massKg[1];
    if (!(innerMass > 0.0) || !isfinite(innerMass)) return false;
    if (orbit->bodyCount == 3) {
        if (orbit->motion == SPACE_BARYCENTER_OUTER_FREE_FLIGHT) {
            return SpaceBarycenterVectorIsFinite(
                       orbit->outerFreeOffsetGame) &&
                   SpaceBarycenterVectorIsFinite(
                       orbit->outerFreeVelocityGame);
        }
        if (!(orbit->outerSeparationKm > 0.0) ||
            !isfinite(orbit->outerSeparationKm) ||
            orbit->outerEccentricity < 0.0 ||
            orbit->outerEccentricity >= 1.0 ||
            !isfinite(orbit->outerEccentricity) ||
            !isfinite(orbit->outerPhaseRad) ||
            !isfinite(orbit->outerArgumentPeriapsisRad) ||
            !isfinite(orbit->outerInclinationRad) ||
            !isfinite(orbit->outerNodeRad)) return false;
        double totalMass = innerMass + orbit->massKg[2];
        if (!(totalMass > 0.0) || !isfinite(totalMass)) return false;
    }
    return true;
}

static bool SpaceBarycenterRecenterStates(
    const SpaceBarycenterOrbit *orbit, SpaceBarycenterBodyState *states)
{
    double totalMass = 0.0;
    double position[3] = { 0.0, 0.0, 0.0 };
    double velocity[3] = { 0.0, 0.0, 0.0 };
    for (int i = 0; i < orbit->bodyCount; i++) {
        totalMass += orbit->massKg[i];
        position[0] += orbit->massKg[i] * states[i].offsetGame.x;
        position[1] += orbit->massKg[i] * states[i].offsetGame.y;
        position[2] += orbit->massKg[i] * states[i].offsetGame.z;
        velocity[0] += orbit->massKg[i] * states[i].velocityGame.x;
        velocity[1] += orbit->massKg[i] * states[i].velocityGame.y;
        velocity[2] += orbit->massKg[i] * states[i].velocityGame.z;
    }
    if (!(totalMass > 0.0) || !isfinite(totalMass)) return false;
    Vector3 center = {
        (float)(position[0] / totalMass),
        (float)(position[1] / totalMass),
        (float)(position[2] / totalMass)
    };
    Vector3 centerVelocity = {
        (float)(velocity[0] / totalMass),
        (float)(velocity[1] / totalMass),
        (float)(velocity[2] / totalMass)
    };
    if (!SpaceBarycenterVectorIsFinite(center) ||
        !SpaceBarycenterVectorIsFinite(centerVelocity)) {
        return false;
    }
    for (int i = 0; i < orbit->bodyCount; i++) {
        states[i].offsetGame.x -= center.x;
        states[i].offsetGame.y -= center.y;
        states[i].offsetGame.z -= center.z;
        states[i].velocityGame.x -= centerVelocity.x;
        states[i].velocityGame.y -= centerVelocity.y;
        states[i].velocityGame.z -= centerVelocity.z;
    }
    return SpaceBarycenterStatesAreFinite(states, orbit->bodyCount);
}

static bool SpaceBarycenterStatesAreFinite(
    const SpaceBarycenterBodyState *states, int count)
{
    if (!states || count < 0 || count > SPACE_BARYCENTER_MAX_BODIES) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (!SpaceBarycenterVectorIsFinite(states[i].offsetGame) ||
            !SpaceBarycenterVectorIsFinite(states[i].velocityGame)) {
            return false;
        }
    }
    return true;
}

int SpaceBarycenterSolve(const SpaceBarycenterOrbit *orbit,
                         double simulationTime,
                         SpaceBarycenterBodyState *out, int maxCount)
{
    if (!out || maxCount <= 0) return 0;
    int clearCount = maxCount < SPACE_BARYCENTER_MAX_BODIES
        ? maxCount : SPACE_BARYCENTER_MAX_BODIES;
    SpaceBarycenterClearStates(out, clearCount);
    if (!SpaceBarycenterOrbitIsValid(orbit, simulationTime, maxCount)) {
        return 0;
    }
    if (orbit->bodyCount == 1) return 1;

    if (orbit->motion == SPACE_BARYCENTER_FREE_FLIGHT) {
        for (int i = 0; i < orbit->bodyCount; i++) {
            out[i].offsetGame = SpaceBarycenterAdvance(
                orbit->freeFlightOffsetGame[i],
                orbit->freeFlightVelocityGame[i], simulationTime);
            out[i].velocityGame = orbit->freeFlightVelocityGame[i];
        }
        if (!SpaceBarycenterStatesAreFinite(out, orbit->bodyCount) ||
            !SpaceBarycenterRecenterStates(orbit, out)) {
            SpaceBarycenterClearStates(out, clearCount);
            return 0;
        }
        return orbit->bodyCount;
    }

    double innerMass = orbit->massKg[0] + orbit->massKg[1];
    SpaceBarycenterBodyState inner;
    if (!SpaceBarycenterRelativeOrbit(
        orbit->innerSeparationKm, innerMass, orbit->innerEccentricity,
        orbit->innerPhaseRad, orbit->innerArgumentPeriapsisRad,
        orbit->innerInclinationRad, orbit->innerNodeRad, simulationTime,
        &inner)) return 0;

    Vector3 innerCenterPosition = { 0 };
    Vector3 innerCenterVelocity = { 0 };
    if (orbit->bodyCount == 3) {
        double totalMass = innerMass + orbit->massKg[2];
        SpaceBarycenterBodyState outer;
        if (orbit->motion == SPACE_BARYCENTER_OUTER_FREE_FLIGHT) {
            outer.offsetGame = SpaceBarycenterAdvance(
                orbit->outerFreeOffsetGame,
                orbit->outerFreeVelocityGame, simulationTime);
            outer.velocityGame = orbit->outerFreeVelocityGame;
        } else if (!SpaceBarycenterRelativeOrbit(
                       orbit->outerSeparationKm, totalMass,
                       orbit->outerEccentricity, orbit->outerPhaseRad,
                       orbit->outerArgumentPeriapsisRad,
                       orbit->outerInclinationRad, orbit->outerNodeRad,
                       simulationTime, &outer)) {
            return 0;
        }
        innerCenterPosition = SpaceBarycenterScale(
            outer.offsetGame, -orbit->massKg[2] / totalMass);
        innerCenterVelocity = SpaceBarycenterScale(
            outer.velocityGame, -orbit->massKg[2] / totalMass);
        out[2].offsetGame = SpaceBarycenterScale(outer.offsetGame,
                                                innerMass / totalMass);
        out[2].velocityGame = SpaceBarycenterScale(outer.velocityGame,
                                                  innerMass / totalMass);
    }

    out[0].offsetGame = SpaceBarycenterAdd(
        innerCenterPosition,
        SpaceBarycenterScale(inner.offsetGame, -orbit->massKg[1] / innerMass));
    out[0].velocityGame = SpaceBarycenterAdd(
        innerCenterVelocity,
        SpaceBarycenterScale(inner.velocityGame, -orbit->massKg[1] / innerMass));
    out[1].offsetGame = SpaceBarycenterAdd(
        innerCenterPosition,
        SpaceBarycenterScale(inner.offsetGame, orbit->massKg[0] / innerMass));
    out[1].velocityGame = SpaceBarycenterAdd(
        innerCenterVelocity,
        SpaceBarycenterScale(inner.velocityGame, orbit->massKg[0] / innerMass));
    if (!SpaceBarycenterStatesAreFinite(out, orbit->bodyCount)) {
        SpaceBarycenterClearStates(out, clearCount);
        return 0;
    }
    if (orbit->motion == SPACE_BARYCENTER_OUTER_FREE_FLIGHT &&
        !SpaceBarycenterRecenterStates(orbit, out)) {
        SpaceBarycenterClearStates(out, clearCount);
        return 0;
    }
    return orbit->bodyCount;
}
