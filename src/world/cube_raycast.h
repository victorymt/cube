#ifndef VOXELCRAFT_CUBE_RAYCAST_H
#define VOXELCRAFT_CUBE_RAYCAST_H

#include "world/surface_geometry.h"
#include "world/world_location.h"
#include "world/world_cube.h"
#include "world/world_types.h"

/*
 * Selection ray on a cube surface. The legacy DDA walks Cartesian cells, which
 * has no meaning on a sphere; this walks physical distance along the ray and
 * asks each visited cube cell for an exact triangle hit, so the block the
 * player selects is the same cell collision and rendering use.
 */

typedef struct CubeRaycastHit {
    WorldBlockAddress address;
    SurfaceGeometryHit geometry;
    /* Face the ray entered through, for placement. */
    WorldBlockSide enteringSide;
    bool valid;
} CubeRaycastHit;

/* Marches from `origin` along `direction` for at most `maxDistance` physical
 * blocks. Returns false when nothing solid is hit within range or the input is
 * invalid. Cells of the given body id only. */
bool CubeRaycast(uint32_t bodyId, SurfaceVector origin, SurfaceVector direction,
                 double maxDistance, CubeRaycastHit *outHit);

#endif
