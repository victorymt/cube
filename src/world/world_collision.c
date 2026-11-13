#include "world/world_collision.h"

#include "world/block_edits.h"
#include "world/block_types.h"
#include "world/surface_contract.h"
#include "world/surface_pose.h"
#include "world/surface_topology.h"
#include "world/world_cube.h"
#include "world/world_environment.h"
#include "world/world_surface_snapshot.h"

#include <limits.h>
#include <math.h>

#define CAMERA_POINT_MARGIN 0.025f

static bool PointIsFinite(Vector3 position)
{
    return isfinite(position.x) && isfinite(position.y) && isfinite(position.z) &&
        position.x >= (float)INT_MIN && position.x <= (float)INT_MAX &&
        position.y >= (float)INT_MIN && position.y <= (float)INT_MAX &&
        position.z >= (float)INT_MIN && position.z <= (float)INT_MAX;
}

static bool CubePointInsideSolid(Vector3 position)
{
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    SurfaceMapProjection projection = SurfaceProjectMapCoordinates(
        (float)context.originX + position.x,
        (float)context.originZ + position.z);
    SurfacePose pose;
    if (!SurfacePoseFromGeographic(
            context.bodyId, (double)projection.longitude,
            (double)projection.latitude, (double)position.y, 0.0, 0.0,
            &pose)) {
        return true;
    }

    BlockType type = BLOCK_AIR;
    if (!WorldCubeBlockAtPoint(context.bodyId, pose.position, &type)) return true;
    float height = BlockCollisionHeight(type);
    if (height <= 0.0f) return false;
    float base = floorf(position.y);
    return position.y > base + CAMERA_POINT_MARGIN &&
           position.y < base + height - CAMERA_POINT_MARGIN;
}

static bool LegacyPointInsideSolid(Vector3 position)
{
    if (!PointIsFinite(position)) return true;
    int x = (int)floorf(position.x);
    int y = (int)floorf(position.y);
    int z = (int)floorf(position.z);
    BlockType type = GetBlockAt(x, y, z);
    float top;
    if (type == BLOCK_STONE_STAIRS || type == BLOCK_WOOD_STAIRS) {
        float zFraction = position.z - (float)z;
        int step = (int)floorf(zFraction * 3.0f);
        if (step < 0) step = 0;
        if (step > 2) step = 2;
        top = (float)y + (float)(step + 1) / 3.0f;
    } else {
        float height = BlockCollisionHeightAt(x, y, z);
        if (height <= 0.0f) return false;
        top = (float)y + height;
    }
    return position.y > (float)y + CAMERA_POINT_MARGIN &&
           position.y < top - CAMERA_POINT_MARGIN;
}

bool WorldPointInsideSolid(Vector3 position)
{
    if (WorldIsSurfaceActive() && WorldCubeRuntimeEnabled() &&
        SurfaceContractSupportsPhysicalPose(WorldCurrentSurfaceContract())) {
        return CubePointInsideSolid(position);
    }
    return LegacyPointInsideSolid(position);
}
