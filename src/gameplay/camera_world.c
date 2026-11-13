#include "gameplay/camera_world.h"

#include "gameplay/interaction.h"
#include "world/surface_contract.h"
#include "world/world_collision.h"
#include "world/world_environment.h"
#include "world/world_surface_snapshot.h"

#include <stddef.h>

static bool CameraPointInsideSolid(void *user, Vector3 position)
{
    (void)user;
    return WorldPointInsideSolid(position);
}

static bool CameraRaycastSolid(void *user, Vector3 origin, Vector3 direction,
                               float maxDistance, float *outDistance)
{
    (void)user;
    if (!outDistance) return false;
    if (WorldIsSurfaceActive() &&
        SurfaceContractSupportsPhysicalRaycast(WorldCurrentSurfaceContract())) {
        HitResult hit = RaycastBlocksFiltered(origin, direction, maxDistance,
                                              RAYCAST_BLOCK_SOLID);
        if (!hit.hit || hit.distance < 0.0f) return false;
        *outDistance = hit.distance;
        return true;
    }
    float distance = RaycastCameraOcclusion(origin, direction, maxDistance);
    if (distance < 0.0f) return false;
    *outDistance = distance;
    return true;
}

CameraWorldOps CameraWorldOpsDefault(void)
{
    return (CameraWorldOps){
        .pointInsideSolid = CameraPointInsideSolid,
        .raycastOcclusion = CameraRaycastSolid
    };
}

bool PlayerCameraPositionInsideSolid(Vector3 position)
{
    return CameraPointInsideSolid(NULL, position);
}
