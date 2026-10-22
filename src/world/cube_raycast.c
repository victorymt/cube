#include "world/cube_raycast.h"

#include <math.h>

/* Step size in blocks. A cell near a pole can be far smaller than one block in
 * longitude, so the march must be finer than a cell to avoid stepping over a
 * thin sliver; 0.25 blocks bounds the miss to a quarter of the smallest
 * reliably-visible feature while keeping the march cheap. */
#define CUBE_RAYCAST_STEP 0.25
#define CUBE_RAYCAST_MAX_STEPS 4096u

static bool IsSolid(BlockType type)
{
    return type != BLOCK_AIR && type != BLOCK_WATER;
}

bool CubeRaycast(uint32_t bodyId, SurfaceVector origin, SurfaceVector direction,
                 double maxDistance, CubeRaycastHit *outHit)
{
    if (!outHit) return false;
    *outHit = (CubeRaycastHit){0};
    if (!SurfaceVectorIsFinite(origin) || !SurfaceVectorIsFinite(direction) ||
        !isfinite(maxDistance) || maxDistance <= 0.0) return false;
    double length = SurfaceVectorLength(direction);
    if (!isfinite(length) || length <= 0.0) return false;
    SurfaceVector unit = SurfaceVectorScale(direction, 1.0 / length);

    SurfaceAddress previous;
    bool havePrevious = false;
    for (unsigned step = 0; step < CUBE_RAYCAST_MAX_STEPS; step++) {
        double travelled = (double)step * CUBE_RAYCAST_STEP;
        if (travelled > maxDistance) break;
        SurfaceVector point = SurfaceVectorAdd(origin,
            SurfaceVectorScale(unit, travelled));
        SurfaceAddress cell;
        if (!SurfaceCellFromPoint(bodyId, point, &cell)) break;
        if (havePrevious && SurfaceAddressEqual(previous, cell)) continue;

        /* Entering side: the previous cell's neighbour relationship, or the
         * ray direction for the first cell. */
        WorldBlockSide entering = WORLD_BLOCK_UP;
        bool haveEntering = false;
        if (havePrevious) {
            for (int side = 0; side < 6; side++) {
                WorldBlockAddress from;
                from.dimension = bodyId == 0 ? WORLD_DIMENSION_HOME
                                             : WORLD_DIMENSION_PLANET;
                from.at.surface = previous;
                WorldBlockAddress to;
                WorldBlockSide back;
                if (WorldBlockNeighbor(from, (WorldBlockSide)side, &to, &back) &&
                    WorldBlockAddressEqual(to, (WorldBlockAddress){
                        .dimension = from.dimension,
                        .at.surface = cell })) {
                    entering = (WorldBlockSide)side;
                    haveEntering = true;
                    break;
                }
            }
        }

        BlockType type = BLOCK_AIR;
        if (WorldCubeBlockAt((WorldBlockAddress){
                .dimension = bodyId == 0 ? WORLD_DIMENSION_HOME
                                         : WORLD_DIMENSION_PLANET,
                .at.surface = cell }, &type) && IsSolid(type)) {
            outHit->address = (WorldBlockAddress){
                .dimension = bodyId == 0 ? WORLD_DIMENSION_HOME
                                         : WORLD_DIMENSION_PLANET,
                .at.surface = cell };
            outHit->geometry.point = point;
            outHit->geometry.distance = travelled;
            outHit->enteringSide = haveEntering ? entering : WORLD_BLOCK_DOWN;
            outHit->valid = true;
            return true;
        }
        previous = cell;
        havePrevious = true;
    }
    return false;
}
