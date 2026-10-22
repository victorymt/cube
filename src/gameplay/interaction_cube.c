#include "gameplay/interaction.h"

#include "raymath.h"
#include "world/cube_raycast.h"
#include "world/surface_pose.h"
#include "world/surface_topology.h"
#include "world/world_environment.h"

#include <math.h>

/* Cube selection: the ray is traced in planet-centred space and the hit cell
 * is projected back into the local cell coordinates every consumer of
 * HitResult already understands. */
HitResult InteractionCubeRaycast(Vector3 origin, Vector3 direction,
                                   float maxDistance, unsigned mask)
{
    HitResult result = { 0 };
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    if (!context.active) return result;
    /* Direction normalisation is local: the shared helper is private to the
     * Cartesian walker. */
    if (!isfinite(origin.x) || !isfinite(origin.y) || !isfinite(origin.z) ||
        !isfinite(direction.x) || !isfinite(direction.y) ||
        !isfinite(direction.z) || !isfinite(maxDistance) || maxDistance < 0.0f)
        return result;
    double length = sqrt((double)direction.x * (double)direction.x +
                         (double)direction.y * (double)direction.y +
                         (double)direction.z * (double)direction.z);
    if (!isfinite(length) || length <= 1e-6) return result;
    direction.x = (float)((double)direction.x / length);
    direction.y = (float)((double)direction.y / length);
    direction.z = (float)((double)direction.z / length);

    /* Local cell origin -> planet-centred origin. */
    SurfaceMapProjection projection = SurfaceProjectMapCoordinates(
        (float)context.originX + origin.x, (float)context.originZ + origin.z);
    SurfacePose pose;
    if (!SurfacePoseFromGeographic(context.bodyId,
            (double)projection.longitude, (double)projection.latitude,
            (double)origin.y - (double)SURFACE_RADIUS_BLOCKS, 0.0, 0.0,
            &pose)) return result;

    /* The direction is a local vector; rotate it into the surface frame. */
    SurfaceBasis basis;
    if (!SurfacePoseBasis(&pose, &basis)) return result;
    SurfaceVector worldDirection = SurfaceVectorAdd(
        SurfaceVectorAdd(
            SurfaceVectorScale((SurfaceVector){ basis.right.x, basis.right.y,
                                                basis.right.z },
                               (double)direction.x),
            SurfaceVectorScale((SurfaceVector){ basis.forward.x, basis.forward.y,
                                                basis.forward.z },
                               (double)direction.z)),
        SurfaceVectorScale((SurfaceVector){ basis.up.x, basis.up.y, basis.up.z },
                           (double)direction.y));

    CubeRaycastHit hit;
    if (!CubeRaycast(context.bodyId, pose.position, worldDirection,
                     (double)maxDistance, &hit) || !hit.valid)
        return result;

    /* Project the hit cell centre back into local cell coordinates. */
    Vector2 map;
    if (!SurfaceMapCoordinatesFromDirection(
            (Vector3){ (float)hit.cellCenter.x, (float)hit.cellCenter.y,
                       (float)hit.cellCenter.z }, &map)) return result;
    result.hit = true;
    result.x = (int)floorf(map.x) - context.originX;
    result.z = (int)floorf(map.y) - context.originZ;
    result.y = (int)floorf((float)SurfaceVectorLength(hit.cellCenter));
    /* Entry face as a local axis normal, matching the legacy convention. The
     * frame is orthonormal, so projecting the world normal onto each basis
     * axis gives its local components. */
    Vector3 localNormal = {
        (float)SurfaceVectorDot(hit.geometry.normal,
            (SurfaceVector){ basis.right.x, basis.right.y, basis.right.z }),
        (float)SurfaceVectorDot(hit.geometry.normal,
            (SurfaceVector){ basis.up.x, basis.up.y, basis.up.z }),
        (float)SurfaceVectorDot(hit.geometry.normal,
            (SurfaceVector){ basis.forward.x, basis.forward.y, basis.forward.z })
    };
    result.nx = (int)(localNormal.x > 0.5f) - (int)(localNormal.x < -0.5f);
    result.ny = (int)(localNormal.y > 0.5f) - (int)(localNormal.y < -0.5f);
    result.nz = (int)(localNormal.z > 0.5f) - (int)(localNormal.z < -0.5f);
    (void)mask;
    return result;
}
