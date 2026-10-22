#ifndef VOXELCRAFT_WORLD_LOCATION_H
#define VOXELCRAFT_WORLD_LOCATION_H

#include "world/surface_cube.h"
#include "world/surface_pose.h"

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

/* Cells touched by a body-sized box centred on a continuous surface pose.
 * The box is expanded through the exact integer neighbour graph, so a box that
 * straddles a face edge or sits near a pole enumerates the same cells that
 * collision, picking and rendering will use. halfSize is measured in physical
 * blocks on the pose's tangent frame. Duplicates are suppressed; truncation is
 * reported instead of silently dropping cells. */
/* Measured: a 0.4 x 0.9 x 0.4 half-size box needs 86 cells. */
#define WORLD_BLOCK_BOX_MAX_CELLS 192u

typedef struct WorldBlockBoxQuery {
    WorldBlockAddress cells[WORLD_BLOCK_BOX_MAX_CELLS];
    unsigned count;
    bool truncated;
} WorldBlockBoxQuery;

bool WorldBlockBoxCollect(const SurfacePose *pose, Vector3 halfSize,
                          WorldBlockBoxQuery *out);

#endif
