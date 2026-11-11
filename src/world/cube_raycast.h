#ifndef VOXELCRAFT_CUBE_RAYCAST_H
#define VOXELCRAFT_CUBE_RAYCAST_H

#include "world/surface_geometry.h"
#include "world/world_location.h"
#include "world/world_cube.h"

/*
 * Selection ray on a cube surface. The legacy DDA walks Cartesian cells, which
 * has no meaning on a sphere; this walks physical distance along the ray and
 * asks each visited cube cell for an exact triangle hit, so the block the
 * player selects is the same cell collision and rendering use.
 */

typedef struct CubeRaycastHit {
    WorldBlockAddress address;
    SurfaceGeometryHit geometry;
    /* Centre of the hit cell in planet-centred coordinates, so callers can
     * project it into whatever local space they render or edit in. */
    SurfaceVector cellCenter;
    /* Face the ray entered through, for placement. */
    WorldBlockSide enteringSide;
    bool valid;
} CubeRaycastHit;

typedef enum CubeRaycastMask {
    CUBE_RAYCAST_BLOCK_SOLID = 1u << 0,
    CUBE_RAYCAST_BLOCK_LIQUID = 1u << 1,
    CUBE_RAYCAST_BLOCK_ALL =
        CUBE_RAYCAST_BLOCK_SOLID | CUBE_RAYCAST_BLOCK_LIQUID
} CubeRaycastMask;

/* Marches from `origin` along `direction` for at most `maxDistance` physical
 * blocks. Returns false when nothing solid is hit within range or the input is
 * invalid. Cells of the given body id only. */
bool CubeRaycast(uint32_t bodyId, SurfaceVector origin, SurfaceVector direction,
                 double maxDistance, unsigned mask, CubeRaycastHit *outHit);

#endif
