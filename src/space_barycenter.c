#include "space_barycenter.h"

#include "space_units.h"

#include <math.h>

#define SPACE_BARYCENTER_PI 3.14159265358979323846

static SpaceBarycenterBodyState SpaceBarycenterRelativeOrbit(
    double separationKm, double centralMassKg, double phaseAtEpoch,
    double inclination, double node, double simulationTime)
{
    double radius = SpaceUnitsKilometersToGameDistance(separationKm);
    double meanMotion = SpaceUnitsKeplerMeanMotionGame(separationKm,
                                                       centralMassKg);
    double phase = phaseAtEpoch + fmod(simulationTime * meanMotion,
                                       2.0 * SPACE_BARYCENTER_PI);
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
    return state;
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
    if (!orbit || !out || maxCount <= 0 || orbit->bodyCount < 1 ||
        orbit->bodyCount > SPACE_BARYCENTER_MAX_BODIES ||
        maxCount < orbit->bodyCount || orbit->massKg[0] <= 0.0) return 0;

    for (int i = 0; i < orbit->bodyCount; i++) {
        if (orbit->massKg[i] <= 0.0) return 0;
        out[i] = (SpaceBarycenterBodyState){ 0 };
    }
    if (orbit->bodyCount == 1) return 1;
    if (orbit->innerSeparationKm <= 0.0) return 0;

    double innerMass = orbit->massKg[0] + orbit->massKg[1];
    SpaceBarycenterBodyState inner = SpaceBarycenterRelativeOrbit(
        orbit->innerSeparationKm, innerMass, orbit->innerPhaseRad,
        orbit->innerInclinationRad, orbit->innerNodeRad, simulationTime);

    Vector3 innerCenterPosition = { 0 };
    Vector3 innerCenterVelocity = { 0 };
    if (orbit->bodyCount == 3) {
        if (orbit->outerSeparationKm <= 0.0) return 0;
        double totalMass = innerMass + orbit->massKg[2];
        SpaceBarycenterBodyState outer = SpaceBarycenterRelativeOrbit(
            orbit->outerSeparationKm, totalMass, orbit->outerPhaseRad,
            orbit->outerInclinationRad, orbit->outerNodeRad, simulationTime);
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
    return orbit->bodyCount;
}
