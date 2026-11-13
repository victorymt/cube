#include "space/space_frame.h"

#include "space/space_internal.h"

#include <math.h>

bool SpaceSystemPointFromLocal(Vector3 local, SpaceSystemPoint *out)
{
    if (!out || !isfinite(local.x) || !isfinite(local.y) ||
        !isfinite(local.z)) return false;
    int anchorX = SpaceAnchorForLocalCoordinate(local.x, SpaceOriginX());
    int anchorZ = SpaceAnchorForLocalCoordinate(local.z, SpaceOriginZ());
    double globalX = (double)local.x + (double)SpaceOriginX();
    double globalZ = (double)local.z + (double)SpaceOriginZ();
    *out = (SpaceSystemPoint){
        .systemAnchorX = anchorX,
        .systemAnchorZ = anchorZ,
        .x = globalX - (double)SpaceSystemGlobalCoordinate(anchorX),
        .y = (double)local.y,
        .z = globalZ - (double)SpaceSystemGlobalCoordinate(anchorZ)
    };
    return true;
}

bool SpaceSystemPointToLocal(const SpaceSystemPoint *systemPoint,
                             Vector3 *outLocal)
{
    if (!systemPoint || !outLocal || !isfinite(systemPoint->x) ||
        !isfinite(systemPoint->y) || !isfinite(systemPoint->z))
        return false;
    double globalX = (double)SpaceSystemGlobalCoordinate(
        (int)systemPoint->systemAnchorX) + systemPoint->x;
    double globalZ = (double)SpaceSystemGlobalCoordinate(
        (int)systemPoint->systemAnchorZ) + systemPoint->z;
    Vector3 local = {
        (float)(globalX - (double)SpaceOriginX()),
        (float)systemPoint->y,
        (float)(globalZ - (double)SpaceOriginZ())
    };
    if (!isfinite(local.x) || !isfinite(local.y) || !isfinite(local.z))
        return false;
    *outLocal = local;
    return true;
}
