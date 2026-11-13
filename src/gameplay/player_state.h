#ifndef VOXELCRAFT_PLAYER_STATE_H
#define VOXELCRAFT_PLAYER_STATE_H

#include "gameplay/player_types.h"
#include "world/actor_motion.h"

#include <stdbool.h>

/* Explicit transition/commit helpers for code outside the locomotion policy.
 * These keep special gameplay actions from scattering raw Player field writes. */
void PlayerHaltMotion(Player *player);
void PlayerSetLook(Player *player, float yaw, float pitch);
void PlayerSetFloating(Player *player, bool floating);
bool PlayerPlaceAt(Player *player, Vector3 position, Vector3 velocity,
                   float yaw, float pitch, bool onGround, bool floating);
void PlayerApplyMotionResult(Player *player, const ActorMotionResult *result);
void PlayerRestoreState(Player *player, const Player *restored);

#endif
