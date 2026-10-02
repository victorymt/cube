#include "ecology/entity_internal.h"

#include "world/surface_topology.h"
#include "world/world_environment.h"
#define RAYMATH_STATIC_INLINE
#include "raymath.h"

#include <math.h>
#include <string.h>

typedef struct EntityPreviousPose {
    EntityRenderPose pose;
    EntityType type;
    uint32_t organismId;
    bool valid;
    bool corpse;
} EntityPreviousPose;

static EntityPreviousPose previousPoses[MAX_ENTITIES];
static uint32_t previousSurfaceId;
static int previousOriginX;
static int previousOriginZ;
static bool previousSurfaceActive;

void EntitiesResetRenderPoses(void)
{
    memset(previousPoses, 0, sizeof(previousPoses));
}

void EntityInvalidateRenderPose(int index)
{
    if (index >= 0 && index < MAX_ENTITIES) previousPoses[index].valid = false;
}

void EntitiesCaptureRenderPoses(void)
{
    previousSurfaceId = WorldCurrentSurfaceId();
    previousOriginX = WorldSurfaceMapOriginX();
    previousOriginZ = WorldSurfaceMapOriginZ();
    previousSurfaceActive = WorldIsSurfaceActive();
    for (int index = 0; index < MAX_ENTITIES; index++) {
        const Entity *entity = &entityStore[index];
        previousPoses[index] = (EntityPreviousPose){
            .pose = { entity->position, entity->yaw, entity->phase, entity->pose },
            .type = entity->type,
            .organismId = entity->organismId,
            .valid = entity->active,
            .corpse = entity->corpse
        };
    }
}

static float InterpolateYaw(float previous, float current, float alpha)
{
    float delta = atan2f(sinf(current - previous), cosf(current - previous));
    return previous + delta * alpha;
}

static Vector3 PoseFacing(EntityRenderPose pose, const SurfaceFrame *frame)
{
    return Vector3Add(Vector3Scale(frame->east, sinf(pose.yaw)),
                       Vector3Scale(frame->north, cosf(pose.yaw)));
}

static void InterpolateSurfacePose(EntityRenderPose previous,
                                    EntityRenderPose current, float alpha,
                                    EntityRenderPose *outPose)
{
    Vector3 from = SurfaceDirectionFromMapCoordinates(
        previous.position.x, previous.position.z);
    Vector3 to = SurfaceDirectionFromMapCoordinates(
        current.position.x, current.position.z);
    Vector3 direction = Vector3Lerp(from, to, alpha);
    float longitude = atan2f(direction.z, direction.x);
    float latitude = atan2f(direction.y, hypotf(direction.x, direction.z));
    outPose->position.x = longitude *
        ((float)SURFACE_EQUATOR_BLOCKS / (2.0f * PI));
    outPose->position.z = latitude *
        ((float)SURFACE_POLE_TO_POLE_BLOCKS / PI);
    Vector2 canonical = SurfaceCanonicalMapPosition(
        outPose->position.x, outPose->position.z, NULL);
    outPose->position.x = canonical.x;
    outPose->position.z = canonical.y;

    /* Project both headings into the interpolated point's local axes. This
     * preserves orientation when crossing a pole reverses the north axis. */
    SurfaceFrame fromFrame = SurfaceFrameAtMapCoordinates(
        previousSurfaceId, previous.position.x, previous.position.z, 0);
    SurfaceFrame toFrame = SurfaceFrameAtMapCoordinates(
        previousSurfaceId, current.position.x, current.position.z, 0);
    SurfaceFrame poseFrame = SurfaceFrameAtMapCoordinates(
        previousSurfaceId, outPose->position.x, outPose->position.z, 0);
    Vector3 fromFacing = PoseFacing(previous, &fromFrame);
    Vector3 toFacing = PoseFacing(current, &toFrame);
    float fromYaw = atan2f(Vector3DotProduct(fromFacing, poseFrame.east),
                            Vector3DotProduct(fromFacing, poseFrame.north));
    float toYaw = atan2f(Vector3DotProduct(toFacing, poseFrame.east),
                          Vector3DotProduct(toFacing, poseFrame.north));
    outPose->yaw = InterpolateYaw(fromYaw, toYaw, alpha);
}

bool EntitiesRenderPoseAt(int index, float alpha, EntityRenderPose *outPose)
{
    if (!outPose || index < 0 || index >= MAX_ENTITIES) return false;
    const Entity *current = &entityStore[index];
    if (!current->active) return false;
    *outPose = (EntityRenderPose){ current->position, current->yaw, current->phase,
                                 current->pose };
    const EntityPreviousPose *previous = &previousPoses[index];
    if (!previous->valid || previous->type != current->type ||
        previous->organismId != current->organismId ||
        previous->corpse != current->corpse ||
        previousSurfaceId != WorldCurrentSurfaceId() ||
        previousOriginX != WorldSurfaceMapOriginX() ||
        previousOriginZ != WorldSurfaceMapOriginZ() ||
        previousSurfaceActive != WorldIsSurfaceActive()) return true;

    if (!isfinite(alpha) || alpha >= 1.0f) return true;
    if (alpha < 0.0f) alpha = 0.0f;
    SurfaceMapOffset surfaceDelta = SurfaceShortestMapOffset(
        previous->pose.position.x, previous->pose.position.z,
        current->position.x, current->position.z);
    Vector3 delta = { surfaceDelta.x,
        current->position.y - previous->pose.position.y, surfaceDelta.z };
    float distanceSquared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    if (!isfinite(distanceSquared) || distanceSquared >
        ENTITY_DESPAWN_DISTANCE * ENTITY_DESPAWN_DISTANCE) return true;

    if (alpha == 0.0f) {
        *outPose = previous->pose;
        return true;
    }
    outPose->position = Vector3Lerp(previous->pose.position, current->position, alpha);
    outPose->yaw = InterpolateYaw(previous->pose.yaw, current->yaw, alpha);
    outPose->phase = previous->pose.phase +
        (current->phase - previous->pose.phase) * alpha;
    outPose->pose = FaunaPoseInterpolate(previous->pose.pose, current->pose, alpha);
    if (WorldIsSurfaceActive()) {
        EntityRenderPose currentPose = {
            current->position, current->yaw, current->phase, current->pose
        };
        InterpolateSurfacePose(previous->pose, currentPose, alpha, outPose);
    }
    return true;
}
