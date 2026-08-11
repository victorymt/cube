#include "space_barycenter.h"

#include "space_units.h"

#include <math.h>

#define SPACE_BARYCENTER_PI 3.14159265358979323846

static bool SpaceBarycenterVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static bool SpaceBarycenterPhaseAtTime(double phaseAtEpoch,
                                       double meanMotion,
                                       double simulationTime,
                                       double *out)
{
    if (!out || !isfinite(phaseAtEpoch) || !(meanMotion > 0.0) ||
        !isfinite(meanMotion) || !isfinite(simulationTime)) {
        return false;
    }
    double period = 2.0 * SPACE_BARYCENTER_PI / meanMotion;
    double reducedTime = isfinite(period) && period > 0.0
        ? fmod(simulationTime, period) : simulationTime;
    double phase = fmod(phaseAtEpoch + reducedTime * meanMotion,
                        2.0 * SPACE_BARYCENTER_PI);
    if (!isfinite(phase)) return false;
    *out = phase;
    return true;
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
    if (!SpaceBarycenterPhaseAtTime(phaseAtEpoch, meanMotion,
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

int SpaceBarycenterSolve(const SpaceBarycenterOrbit *orbit,
                         double simulationTime,
                         SpaceBarycenterBodyState *out, int maxCount)
{
    if (!out || maxCount <= 0) return 0;
    int clearCount = maxCount < SPACE_BARYCENTER_MAX_BODIES
        ? maxCount : SPACE_BARYCENTER_MAX_BODIES;
    for (int i = 0; i < clearCount; i++) {
        out[i] = (SpaceBarycenterBodyState){ 0 };
    }
    if (!orbit || !isfinite(simulationTime) || orbit->bodyCount < 1 ||
        orbit->bodyCount > SPACE_BARYCENTER_MAX_BODIES ||
        maxCount < orbit->bodyCount) return 0;

    for (int i = 0; i < orbit->bodyCount; i++) {
        if (!(orbit->massKg[i] > 0.0) || !isfinite(orbit->massKg[i])) {
            return 0;
        }
    }
    if (orbit->bodyCount == 1) return 1;
    if (!(orbit->innerSeparationKm > 0.0) ||
        !isfinite(orbit->innerSeparationKm) ||
        !isfinite(orbit->innerPhaseRad) ||
        !isfinite(orbit->innerInclinationRad) ||
        !isfinite(orbit->innerNodeRad)) return 0;

    double innerMass = orbit->massKg[0] + orbit->massKg[1];
    if (!(innerMass > 0.0) || !isfinite(innerMass)) return 0;
    SpaceBarycenterBodyState inner;
    if (!SpaceBarycenterRelativeOrbit(
        orbit->innerSeparationKm, innerMass, orbit->innerPhaseRad,
        orbit->innerInclinationRad, orbit->innerNodeRad, simulationTime,
        &inner)) return 0;

    Vector3 innerCenterPosition = { 0 };
    Vector3 innerCenterVelocity = { 0 };
    if (orbit->bodyCount == 3) {
        if (!(orbit->outerSeparationKm > 0.0) ||
            !isfinite(orbit->outerSeparationKm) ||
            !isfinite(orbit->outerPhaseRad) ||
            !isfinite(orbit->outerInclinationRad) ||
            !isfinite(orbit->outerNodeRad)) return 0;
        double totalMass = innerMass + orbit->massKg[2];
        if (!(totalMass > 0.0) || !isfinite(totalMass)) return 0;
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
    for (int i = 0; i < orbit->bodyCount; i++) {
        if (!SpaceBarycenterVectorIsFinite(out[i].offsetGame) ||
            !SpaceBarycenterVectorIsFinite(out[i].velocityGame)) {
            for (int clear = 0; clear < clearCount; clear++) {
                out[clear] = (SpaceBarycenterBodyState){ 0 };
            }
            return 0;
        }
    }
    return orbit->bodyCount;
}
