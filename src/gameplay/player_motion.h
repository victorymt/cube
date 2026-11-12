#ifndef VOXELCRAFT_PLAYER_MOTION_H
#define VOXELCRAFT_PLAYER_MOTION_H

#include "gameplay/player_types.h"
#include "world/surface_pose.h"

bool IsSolidBlockAt(int x, int y, int z);
bool PlayerOverlapsWorld(Vector3 position);
bool PlayerOverlapsWorldWithOptions(Vector3 position, bool allowUnready);
/* Converts a body-centred surface pose into the active surface's local player
 * coordinates. Used by debug pose placement and the movement path. */
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
