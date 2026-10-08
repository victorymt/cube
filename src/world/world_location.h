#ifndef VOXELCRAFT_WORLD_LOCATION_H
#define VOXELCRAFT_WORLD_LOCATION_H

#include "world/surface_cube.h"

typedef enum WorldDimension {
    WORLD_DIMENSION_HOME = 0,
    WORLD_DIMENSION_PLANET,
    WORLD_DIMENSION_SPACE,
    WORLD_DIMENSION_NETHER
} WorldDimension;

// A Cartesian position is never a serialized/encoded cube face. The tag
// selects which member is meaningful; unused union bytes are not identity.
typedef struct WorldBlockAddress {
    WorldDimension dimension;
    union {
        SurfaceAddress surface;
        struct { int x, y, z; } cartesian;
    } at;
} WorldBlockAddress;

typedef enum WorldBlockSide {
    WORLD_BLOCK_U_MINUS = 0, WORLD_BLOCK_U_PLUS,
    WORLD_BLOCK_V_MINUS, WORLD_BLOCK_V_PLUS,
    WORLD_BLOCK_DOWN, WORLD_BLOCK_UP
} WorldBlockSide;

bool WorldBlockAddressIsValid(WorldBlockAddress address);
bool WorldBlockAddressEqual(WorldBlockAddress a, WorldBlockAddress b);
uint32_t WorldBlockAddressHash(WorldBlockAddress address);
bool WorldBlockNeighbor(WorldBlockAddress address, WorldBlockSide side,
                         WorldBlockAddress *outAddress, WorldBlockSide *outReturnSide);

#endif
