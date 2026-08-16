#include "space/space_coordinates.h"

#include "space/space_units.h"

#include <math.h>

bool CelestialVectorIsFinite(CelestialVector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

CelestialVector3 CelestialVectorAdd(CelestialVector3 left,
                                    CelestialVector3 right)
{
    return (CelestialVector3){ left.x + right.x, left.y + right.y,
                               left.z + right.z };
}

CelestialVector3 CelestialVectorSubtract(CelestialVector3 left,
                                         CelestialVector3 right)
{
    return (CelestialVector3){ left.x - right.x, left.y - right.y,
                               left.z - right.z };
}

CelestialVector3 CelestialVectorScale(CelestialVector3 value, double scale)
{
    return (CelestialVector3){ value.x * scale, value.y * scale,
                               value.z * scale };
}

double CelestialVectorLength(CelestialVector3 value)
{
    return sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

CelestialVector3 CelestialVectorFromGameDistance(Vector3 value)
{
    return (CelestialVector3){
        SpaceUnitsGameDistanceToKilometers(value.x),
        SpaceUnitsGameDistanceToKilometers(value.y),
        SpaceUnitsGameDistanceToKilometers(value.z)
    };
}

Vector3 CelestialVectorToGameDistance(CelestialVector3 value)
{
    return (Vector3){
        (float)SpaceUnitsKilometersToGameDistance(value.x),
        (float)SpaceUnitsKilometersToGameDistance(value.y),
        (float)SpaceUnitsKilometersToGameDistance(value.z)
    };
}

CelestialVector3 CelestialVelocityFromGame(Vector3 value)
{
    return (CelestialVector3){
        SpaceUnitsGameVelocityToKilometersPerSecond(value.x),
        SpaceUnitsGameVelocityToKilometersPerSecond(value.y),
        SpaceUnitsGameVelocityToKilometersPerSecond(value.z)
    };
}

Vector3 CelestialVelocityToGame(CelestialVector3 value)
{
    return (Vector3){
        (float)SpaceUnitsKilometersPerSecondToGameVelocity(value.x),
        (float)SpaceUnitsKilometersPerSecondToGameVelocity(value.y),
        (float)SpaceUnitsKilometersPerSecondToGameVelocity(value.z)
    };
}

bool CelestialProjectRelative(CelestialPosition position,
                              CelestialPosition camera,
                              Vector3 *outLocal)
{
    if (!outLocal) return false;
    *outLocal = (Vector3){ 0 };
    if (position.systemAnchorX != camera.systemAnchorX ||
        position.systemAnchorZ != camera.systemAnchorZ ||
        !CelestialVectorIsFinite(position.offsetKm) ||
        !CelestialVectorIsFinite(camera.offsetKm)) {
        return false;
    }
    CelestialVector3 relative = CelestialVectorSubtract(position.offsetKm,
                                                         camera.offsetKm);
    Vector3 projected = CelestialVectorToGameDistance(relative);
    if (!isfinite(projected.x) || !isfinite(projected.y) ||
        !isfinite(projected.z)) {
        return false;
    }
    *outLocal = projected;
    return true;
}
