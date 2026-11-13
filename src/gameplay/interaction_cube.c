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
    /* Local Y is a radial height, matching PlayerSurfacePoseFromLocal. Subtracting
     * the sphere radius here put the ray thousands of blocks below the surface,
     * so every cube pick missed. */
    if (!SurfacePoseFromGeographic(context.bodyId,
            (double)projection.longitude, (double)projection.latitude,
            (double)origin.y, 0.0, 0.0, &pose)) return result;

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
    unsigned cubeMask = 0u;
    if (mask & RAYCAST_BLOCK_SOLID) cubeMask |= CUBE_RAYCAST_BLOCK_SOLID;
    if (mask & RAYCAST_BLOCK_LIQUID) cubeMask |= CUBE_RAYCAST_BLOCK_LIQUID;
    if (!CubeRaycast(context.bodyId, pose.position, worldDirection,
                     (double)maxDistance, cubeMask, &hit) || !hit.valid)
        return result;

    /* Keep the authoritative cube address. Local map coordinates remain a
     * presentation/debug view because some cube cells have no integer map
     * representative. */
    result.hit = true;
    result.distance = (float)hit.geometry.distance;
    result.hasAddress = true;
    result.address = hit.address;
    result.enteringSide = hit.enteringSide;
    SurfaceMapCell canonical;
    if (SurfaceAddressCanonicalMapCell(hit.address.at.surface, &canonical)) {
        result.x = canonical.x - context.originX;
        result.z = canonical.z - context.originZ;
    } else {
        Vector2 map;
        if (SurfaceMapCoordinatesFromDirection(
                (Vector3){ (float)hit.cellCenter.x, (float)hit.cellCenter.y,
                           (float)hit.cellCenter.z }, &map)) {
            result.x = (int)floorf(map.x) - context.originX;
            result.z = (int)floorf(map.y) - context.originZ;
        }
    }
    /* Report a radial height, not a distance from the planet centre. */
    result.y = (int)floorf((float)SurfaceVectorLength(hit.cellCenter) -
                           SURFACE_RADIUS_BLOCKS);
    /* Entry face as a local axis normal, matching the legacy convention. The
     * frame is orthonormal, so projecting the world normal onto each basis
     * axis gives its local components. */
    SurfaceFrame hitFrame = SurfaceFrameAt(hit.address.at.surface);
    Vector3 localNormal = {
        (float)SurfaceVectorDot(hit.geometry.normal,
            (SurfaceVector){ hitFrame.east.x, hitFrame.east.y,
                             hitFrame.east.z }),
        (float)SurfaceVectorDot(hit.geometry.normal,
            (SurfaceVector){ hitFrame.up.x, hitFrame.up.y, hitFrame.up.z }),
        (float)SurfaceVectorDot(hit.geometry.normal,
            (SurfaceVector){ hitFrame.north.x, hitFrame.north.y,
                             hitFrame.north.z })
    };
    result.nx = (int)(localNormal.x > 0.5f) - (int)(localNormal.x < -0.5f);
    result.ny = (int)(localNormal.y > 0.5f) - (int)(localNormal.y < -0.5f);
    result.nz = (int)(localNormal.z > 0.5f) - (int)(localNormal.z < -0.5f);
    return result;
}
