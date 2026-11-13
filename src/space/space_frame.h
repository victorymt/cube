#ifndef VOXELCRAFT_SPACE_FRAME_H
#define VOXELCRAFT_SPACE_FRAME_H

#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

/* Game-distance offset inside one star-system frame. */
typedef struct SpaceSystemPoint {
    int64_t systemAnchorX;
    int64_t systemAnchorZ;
    double x;
    double y;
    double z;
} SpaceSystemPoint;

bool SpaceSystemPointFromLocal(Vector3 local, SpaceSystemPoint *out);
bool SpaceSystemPointToLocal(const SpaceSystemPoint *systemPoint,
                             Vector3 *outLocal);

#endif
