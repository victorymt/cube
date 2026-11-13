#ifndef VOXELCRAFT_ACTOR_MOTION_H
#define VOXELCRAFT_ACTOR_MOTION_H

#include "world/actor_body.h"
#include "world/surface_pose.h"

#include "raylib.h"

#include <stdbool.h>

/* Physical body shape used by the topology-neutral motion solver. */
typedef struct ActorCollider {
    float radius;
    float height;
    float stepHeight;
} ActorCollider;

typedef struct ActorMotionRequest {
    Vector3 delta;
    bool allowUnready;
    bool canStepUp;
} ActorMotionRequest;

typedef struct ActorMotionResult {
    ActorBody body;
    bool moved;
} ActorMotionResult;

/* Resolve a desired displacement against the active surface backend. The input
 * body is not mutated; the final kinematic state is returned in outResult. */
bool WorldActorResolveMotion(const ActorBody *body,
                             const ActorCollider *collider,
                             const ActorMotionRequest *request,
                             ActorMotionResult *outResult);
bool WorldActorOverlaps(const ActorCollider *collider, Vector3 position,
                        bool allowUnready);
int WorldActorMissingSurfaceChunkCount(const ActorCollider *collider,
                                       Vector3 position);
bool WorldActorFindLandingSpot(const ActorCollider *collider, Vector3 start,
                               int minY, int maxY, bool allowUnready,
                               Vector3 *out);

bool WorldActorPoseFromLocal(Vector3 position, SurfacePose *outPose);
bool WorldActorLocalFromPose(const SurfacePose *pose, Vector3 *outPosition);

#endif
