#include "world/world_frame.h"

#include "world/actor_motion.h"
#include "space/space_frame.h"
#include "world/world_environment.h"

#include "raylib.h"

#include <math.h>

WorldFrameId WorldFrameSystemId(int64_t systemAnchorX, int64_t systemAnchorZ)
{
    return (WorldFrameId){
        .kind = WORLD_FRAME_SYSTEM,
        .systemAnchorX = systemAnchorX,
        .systemAnchorZ = systemAnchorZ
    };
}

WorldFrameId WorldFrameSaveId(uint32_t bodyId)
{
    return (WorldFrameId){ .kind = WORLD_FRAME_LOCAL, .bodyId = bodyId };
}

bool WorldFrameIdEqual(WorldFrameId a, WorldFrameId b)
{
    return a.kind == b.kind && a.bodyId == b.bodyId &&
        a.systemAnchorX == b.systemAnchorX &&
        a.systemAnchorZ == b.systemAnchorZ;
}

bool WorldFrameCanTransform(WorldFrameId source, WorldFrameId target)
{
    if (WorldFrameIdEqual(source, target)) return true;
    if (source.bodyId != target.bodyId) return false;
    if (source.kind == WORLD_FRAME_LOCAL &&
        target.kind == WORLD_FRAME_BODY_CENTERED) return true;
    if (source.kind == WORLD_FRAME_BODY_CENTERED &&
        target.kind == WORLD_FRAME_LOCAL) return true;
    if (source.kind == WORLD_FRAME_SYSTEM &&
        (target.kind == WORLD_FRAME_LOCAL ||
         target.kind == WORLD_FRAME_BODY_CENTERED)) return true;
    if (target.kind == WORLD_FRAME_SYSTEM &&
        (source.kind == WORLD_FRAME_LOCAL ||
         source.kind == WORLD_FRAME_BODY_CENTERED)) return true;
    return false;
}

static bool WorldFramePointToLocal(const WorldFramePoint *point,
                                   Vector3 *outLocal)
{
    if (!point || !outLocal) return false;
    if (point->frame.kind == WORLD_FRAME_LOCAL) {
        *outLocal = (Vector3){
            (float)point->x, (float)point->y, (float)point->z
        };
        return isfinite(outLocal->x) && isfinite(outLocal->y) &&
            isfinite(outLocal->z);
    }
    if (point->frame.kind == WORLD_FRAME_BODY_CENTERED) {
        SurfacePose pose = {
            .bodyId = point->frame.bodyId,
            .position = { point->x, point->y, point->z },
            .velocity = { 0.0, 0.0, 0.0 },
            .heading = { 0.0, 0.0, 1.0 },
            .pitch = 0.0
        };
        return WorldActorLocalFromPose(&pose, outLocal);
    }
    if (point->frame.kind == WORLD_FRAME_SYSTEM) {
        SpaceSystemPoint systemPoint = {
            .systemAnchorX = point->frame.systemAnchorX,
            .systemAnchorZ = point->frame.systemAnchorZ,
            .x = point->x,
            .y = point->y,
            .z = point->z
        };
        return SpaceSystemPointToLocal(&systemPoint, outLocal);
    }
    return false;
}

bool WorldFrameTransformPoint(const WorldFramePoint *source,
                              WorldFrameId target, double simulationTime,
                              WorldFramePoint *out)
{
    if (!source || !out || !isfinite(source->x) || !isfinite(source->y) ||
        !isfinite(source->z) || !isfinite(simulationTime) ||
        !WorldFrameCanTransform(source->frame, target)) {
        return false;
    }
    if (WorldFrameIdEqual(source->frame, target)) {
        *out = *source;
        return true;
    }
    Vector3 local;
    if (!WorldFramePointToLocal(source, &local)) return false;
    WorldFramePoint converted = { .frame = target };
    if (target.kind == WORLD_FRAME_LOCAL) {
        converted.x = local.x;
        converted.y = local.y;
        converted.z = local.z;
    } else if (target.kind == WORLD_FRAME_BODY_CENTERED) {
        WorldSurfaceContext context = WorldCurrentSurfaceContext();
        if (!context.active || context.bodyId != target.bodyId) return false;
        SurfacePose pose = {
            .bodyId = target.bodyId,
            .position = { 0.0, 0.0, 0.0 },
            .velocity = { 0.0, 0.0, 0.0 },
            .heading = { 0.0, 0.0, 1.0 },
            .pitch = 0.0
        };
        if (!WorldActorPoseFromLocal(local, &pose)) return false;
        converted.x = pose.position.x;
        converted.y = pose.position.y;
        converted.z = pose.position.z;
    } else if (target.kind == WORLD_FRAME_SYSTEM) {
        SpaceSystemPoint systemPoint;
        if (!SpaceSystemPointFromLocal(local, &systemPoint)) return false;
        if (systemPoint.systemAnchorX != target.systemAnchorX ||
            systemPoint.systemAnchorZ != target.systemAnchorZ) return false;
        converted.x = systemPoint.x;
        converted.y = systemPoint.y;
        converted.z = systemPoint.z;
    } else {
        return false;
    }
    *out = converted;
    return true;
}

bool WorldFrameTransformPointToCamera(const WorldFramePoint *source,
                                      Vector3 cameraLocalPosition,
                                      double simulationTime,
                                      WorldFramePoint *out)
{
    if (!source || !out || !isfinite(simulationTime) ||
        !isfinite(cameraLocalPosition.x) || !isfinite(cameraLocalPosition.y) ||
        !isfinite(cameraLocalPosition.z) ||
        source->frame.kind == WORLD_FRAME_RENDER_CAMERA) return false;
    Vector3 local;
    if (!WorldFramePointToLocal(source, &local)) return false;
    *out = (WorldFramePoint){
        .frame = { .kind = WORLD_FRAME_RENDER_CAMERA, .bodyId = source->frame.bodyId },
        .x = (double)local.x - (double)cameraLocalPosition.x,
        .y = (double)local.y - (double)cameraLocalPosition.y,
        .z = (double)local.z - (double)cameraLocalPosition.z
    };
    return true;
}

bool WorldFrameTransformPointFromCamera(const WorldFramePoint *source,
                                        Vector3 cameraLocalPosition,
                                        double simulationTime,
                                        WorldFramePoint *out)
{
    if (!source || !out || source->frame.kind != WORLD_FRAME_RENDER_CAMERA ||
        !isfinite(simulationTime) || !isfinite(cameraLocalPosition.x) ||
        !isfinite(cameraLocalPosition.y) || !isfinite(cameraLocalPosition.z)) {
        return false;
    }
    WorldFramePoint local = {
        .frame = { .kind = WORLD_FRAME_LOCAL, .bodyId = source->frame.bodyId },
        .x = source->x + (double)cameraLocalPosition.x,
        .y = source->y + (double)cameraLocalPosition.y,
        .z = source->z + (double)cameraLocalPosition.z
    };
    *out = local;
    return true;
}
