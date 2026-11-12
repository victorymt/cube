#ifndef VOXELCRAFT_PLAYER_COLLISION_INTERNAL_H
#define VOXELCRAFT_PLAYER_COLLISION_INTERNAL_H

#include "world/block_type.h"

#include "raylib.h"

#include <math.h>

static inline bool PlayerInternalVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static inline bool PlayerInternalIsStairs(BlockType type)
{
    return type == BLOCK_STONE_STAIRS || type == BLOCK_WOOD_STAIRS;
}

static inline float PlayerInternalStairsTop(int x, int y, int z,
                                            float playerZ)
{
    (void)x;
    float zFrac = playerZ - (float)z;
    int step = (int)floorf(zFrac * 3.0f);
    if (step < 0) step = 0;
    if (step > 2) step = 2;
    return (float)y + (float)(step + 1) / 3.0f;
}

#endif
