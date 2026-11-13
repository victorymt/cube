#include "world/world_frame.h"

#include "world/actor_motion.h"
#include "world/world_environment.h"

#include "raylib.h"

#include <math.h>

bool WorldFrameIdEqual(WorldFrameId a, WorldFrameId b)
{
    return a.kind == b.kind && a.bodyId == b.bodyId;
}

bool WorldFrameCanTransform(WorldFrameId source, WorldFrameId target)
{
    if (WorldFrameIdEqual(source, target)) return true;
    if (source.bodyId != target.bodyId) return false;
    if (source.kind == WORLD_FRAME_LOCAL &&
        target.kind == WORLD_FRAME_BODY_CENTERED) return true;
    if (source.kind == WORLD_FRAME_BODY_CENTERED &&
        target.kind == WORLD_FRAME_LOCAL) return true;
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
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    if (!context.active || context.bodyId != source->frame.bodyId) return false;

    WorldFramePoint converted = { .frame = target };
    if (source->frame.kind == WORLD_FRAME_LOCAL) {
        SurfacePose pose;
        if (!WorldActorPoseFromLocal(
                (Vector3){ (float)source->x, (float)source->y,
                           (float)source->z }, &pose)) return false;
        converted.x = pose.position.x;
        converted.y = pose.position.y;
        converted.z = pose.position.z;
    } else {
        SurfacePose pose = {
            .bodyId = source->frame.bodyId,
            .position = { source->x, source->y, source->z },
            .velocity = { 0.0, 0.0, 0.0 },
            .heading = { 0.0, 0.0, 1.0 },
            .pitch = 0.0
        };
        Vector3 local;
        if (!WorldActorLocalFromPose(&pose, &local)) return false;
        converted.x = local.x;
        converted.y = local.y;
        converted.z = local.z;
    }
    *out = converted;
    return true;
}
