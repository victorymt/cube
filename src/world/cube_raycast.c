#include "world/cube_raycast.h"

#include "world/block_types.h"
#include "world/surface_cube.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>

/* Cell-to-cell traversal uses exact boundary intersections, so the step count
 * is a safety limit rather than a spatial resolution. */
#define CUBE_RAYCAST_MAX_STEPS 4096u

static bool CellAccepted(WorldCell cell, unsigned mask)
{
    bool liquid = IsLiquidBlock(cell.block) || cell.waterVolume > 0u;
    if (cell.block == BLOCK_AIR && cell.waterVolume == 0u) return false;
    unsigned bit = liquid ? CUBE_RAYCAST_BLOCK_LIQUID
                          : CUBE_RAYCAST_BLOCK_SOLID;
    return (mask & bit) != 0u;
}

static bool NextCubeCell(SurfaceAddress cell, WorldBlockSide side,
                         SurfaceAddress *out)
{
    if (!out || !SurfaceAddressIsValid(cell) || (unsigned)side >= 6) return false;
    SurfaceAddress next = cell;
    if (side <= WORLD_BLOCK_V_PLUS) {
        return SurfaceCubeCellNeighbor(cell, (SurfaceCubeEdge)side, out, NULL);
    }
    if (side == WORLD_BLOCK_DOWN) {
        if (cell.radial == INT_MIN) return false;
        next.radial--;
    } else {
        if (cell.radial == INT_MAX) return false;
        next.radial++;
    }
    if (!SurfaceAddressIsValid(next)) return false;
    *out = next;
    return true;
}

bool CubeRaycast(uint32_t bodyId, SurfaceVector origin, SurfaceVector direction,
                 double maxDistance, unsigned mask, CubeRaycastHit *outHit)
{
    if (!outHit) return false;
    *outHit = (CubeRaycastHit){0};
    if (!SurfaceVectorIsFinite(origin) || !SurfaceVectorIsFinite(direction) ||
        !isfinite(maxDistance) || maxDistance <= 0.0) return false;
    double length = SurfaceVectorLength(direction);
    if (!isfinite(length) || length <= 0.0) return false;
    SurfaceVector unit = SurfaceVectorScale(direction, 1.0 / length);
    SurfaceAddress cell;
    if (!SurfaceCellFromPoint(bodyId, origin, &cell)) return false;
    WorldBlockAddress address = {
        .dimension = bodyId == 0 ? WORLD_DIMENSION_HOME
                                 : WORLD_DIMENSION_PLANET,
        .at.surface = cell
    };
    double travelled = 0.0;
    for (unsigned step = 0; step < CUBE_RAYCAST_MAX_STEPS; step++) {
        WorldCell cellState;
        if (WorldCubeCellAt(address, &cellState) &&
            CellAccepted(cellState, mask)) {
            SurfaceGeometryHit geometry = { 0 };
            if (step == 0) {
                geometry = (SurfaceGeometryHit){
                    .distance = travelled,
                    .point = SurfaceVectorAdd(origin,
                        SurfaceVectorScale(unit, travelled)),
                    .normal = SurfaceVectorScale(unit, -1.0),
                    .side = WORLD_BLOCK_DOWN
                };
            } else if (!SurfaceRayCellBox(cell, SURFACE_CELL_BOX_FULL, origin,
                                          unit, maxDistance, &geometry)) {
                break;
            }
            outHit->address = address;
            outHit->geometry = geometry;
            SurfaceVector center;
            if (SurfaceCellCenter(cell, &center)) outHit->cellCenter = center;
            outHit->enteringSide = geometry.side;
            outHit->valid = true;
            return true;
        }

        SurfaceGeometryHit exit;
        if (!SurfaceRayCellBoxExit(cell, SURFACE_CELL_BOX_FULL, origin, unit,
                                   maxDistance, &exit)) break;
        SurfaceAddress nextCell;
        if (!NextCubeCell(cell, exit.side, &nextCell)) break;
        WorldBlockAddress next = {
            .dimension = address.dimension,
            .at.surface = nextCell
        };
        double nextDistance = nextafter(exit.distance, INFINITY);
        if (!isfinite(nextDistance) || nextDistance <= travelled ||
            nextDistance > maxDistance) break;
        travelled = nextDistance;
        address = next;
        cell = next.at.surface;
    }
    return false;
}
