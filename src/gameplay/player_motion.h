#ifndef VOXELCRAFT_PLAYER_MOTION_H
#define VOXELCRAFT_PLAYER_MOTION_H

#include "gameplay/player_types.h"
#include "world/actor_motion.h"

#include <stdbool.h>

/* Player-specific compatibility facade over the topology-neutral world motion
 * solver. New geometry implementations live in world/actor_motion.c. */
const ActorCollider *PlayerMotionCollider(void);
bool PlayerOverlapsWorld(Vector3 position);
bool PlayerOverlapsWorldWithOptions(Vector3 position, bool allowUnready);
bool PlayerLocalFromSurfacePose(const SurfacePose *pose, Vector3 *outPosition);
bool PlayerSurfacePoseFromLocal(Vector3 position, SurfacePose *outPose);
int PlayerMissingSurfaceChunkCount(Vector3 position);
bool PlayerFindLandingSpot(Vector3 start, int minY, int maxY, Vector3 *out);
void MovePlayer(Player *player, Vector3 delta);
void PlayerMoveWithOptions(Player *player, Vector3 delta, bool allowUnready,
                           bool canStepUp);

Vector3 ForwardFromAngles(float yaw, float pitch);
Vector3 FlatForward(float yaw);
Vector3 RightFromYaw(float yaw);

#endif
