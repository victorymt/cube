#ifndef VOXELCRAFT_INTERACTION_TYPES_H
#define VOXELCRAFT_INTERACTION_TYPES_H

#include "world/world_location.h"

#include <stdbool.h>

typedef struct HitResult {
    bool hit;
    int x;
    int y;
    int z;
    int nx;
    int ny;
    int nz;
    bool hasAddress;
    WorldBlockAddress address;
    WorldBlockSide enteringSide;
} HitResult;

#endif
