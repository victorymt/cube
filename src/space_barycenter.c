#include "space_barycenter.h"

#include "space_units.h"

#include <math.h>

static bool SpaceBarycenterVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static bool SpaceBarycenterRelativeOrbit(
    double separationKm, double centralMassKg, double phaseAtEpoch,
    double inclination, double node, double simulationTime,
    SpaceBarycenterBodyState *out)
{
    if (!out) return false;
    *out = (SpaceBarycenterBodyState){ 0 };
    double radius = SpaceUnitsKilometersToGameDistance(separationKm);
    double meanMotion = SpaceUnitsKeplerMeanMotionGame(separationKm,
                                                       centralMassKg);
    if (!(separationKm > 0.0) || !isfinite(separationKm) ||
        !(centralMassKg > 0.0) || !isfinite(centralMassKg) ||
        !isfinite(phaseAtEpoch) || !isfinite(inclination) ||
        !isfinite(node) || !isfinite(radius) || !(radius > 0.0) ||
        !(meanMotion > 0.0) || !isfinite(meanMotion)) {
        return false;
    }
    double phase = 0.0;
    if (!SpaceUnitsMeanAnomalyAtTime(phaseAtEpoch, meanMotion,
                                     simulationTime, &phase)) {
        return false;
    }
    double cosine = cos(phase);
    double sine = sin(phase);
    double nodeCosine = cos(node);
    double nodeSine = sin(node);
    double inclinationCosine = cos(inclination);
    double inclinationSine = sin(inclination);

    SpaceBarycenterBodyState state = {
        .offsetGame = {
            (float)(radius * (nodeCosine * cosine -
                              nodeSine * sine * inclinationCosine)),
            (float)(radius * sine * inclinationSine),
            (float)(radius * (nodeSine * cosine +
                              nodeCosine * sine * inclinationCosine))
        },
        .velocityGame = {
            (float)(radius * meanMotion *
                    (-nodeCosine * sine -
                     nodeSine * cosine * inclinationCosine)),
            (float)(radius * meanMotion * cosine * inclinationSine),
            (float)(radius * meanMotion *
                    (-nodeSine * sine +
                     nodeCosine * cosine * inclinationCosine))
        }
    };
    if (!SpaceBarycenterVectorIsFinite(state.offsetGame) ||
        !SpaceBarycenterVectorIsFinite(state.velocityGame)) {
        return false;
    }
    *out = state;
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

static void SpaceBarycenterClearStates(SpaceBarycenterBodyState *out,
                                       int count)
{
    for (int i = 0; i < count; i++) {
        out[i] = (SpaceBarycenterBodyState){ 0 };
    }
}

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
    if (orbit->bodyCount == 1) return true;
    if (!(orbit->innerSeparationKm > 0.0) ||
        !isfinite(orbit->innerSeparationKm) ||
        !isfinite(orbit->innerPhaseRad) ||
        !isfinite(orbit->innerInclinationRad) ||
        !isfinite(orbit->innerNodeRad)) return false;
    double innerMass = orbit->massKg[0] + orbit->massKg[1];
    if (!(innerMass > 0.0) || !isfinite(innerMass)) return false;
    if (orbit->bodyCount == 3) {
        if (!(orbit->outerSeparationKm > 0.0) ||
            !isfinite(orbit->outerSeparationKm) ||
            !isfinite(orbit->outerPhaseRad) ||
            !isfinite(orbit->outerInclinationRad) ||
            !isfinite(orbit->outerNodeRad)) return false;
        double totalMass = innerMass + orbit->massKg[2];
        if (!(totalMass > 0.0) || !isfinite(totalMass)) return false;
    }
    return true;
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

    double innerMass = orbit->massKg[0] + orbit->massKg[1];
    SpaceBarycenterBodyState inner;
    if (!SpaceBarycenterRelativeOrbit(
        orbit->innerSeparationKm, innerMass, orbit->innerPhaseRad,
        orbit->innerInclinationRad, orbit->innerNodeRad, simulationTime,
        &inner)) return 0;

    Vector3 innerCenterPosition = { 0 };
    Vector3 innerCenterVelocity = { 0 };
    if (orbit->bodyCount == 3) {
        double totalMass = innerMass + orbit->massKg[2];
        SpaceBarycenterBodyState outer;
        if (!SpaceBarycenterRelativeOrbit(
            orbit->outerSeparationKm, totalMass, orbit->outerPhaseRad,
            orbit->outerInclinationRad, orbit->outerNodeRad, simulationTime,
            &outer)) return 0;
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
    return orbit->bodyCount;
}
