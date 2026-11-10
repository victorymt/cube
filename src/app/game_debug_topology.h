#ifndef VOXELCRAFT_GAME_DEBUG_TOPOLOGY_H
#define VOXELCRAFT_GAME_DEBUG_TOPOLOGY_H

#include "core/debug_dsl.h"
#include "space/planet_profile.h"

#include <stdbool.h>

struct GameRuntime;

void GameDebugTopologyReply(struct GameRuntime *game);
/* Enters the cube-grid home world. Opens the runtime gate, installs a cube
 * policy and rebuilds the surface; on failure the gate and policy are restored
 * so the legacy world keeps working. */
bool GameDebugEnterCubeWorld(struct GameRuntime *game);
/* Selects the explicit debug topology for a direct Home/planet transition.
 * Home selects the legacy baseline; planets remain legacy while the current
 * Home policy is preserved. */
bool GameDebugTransitionSurface(struct GameRuntime *game, bool homeWorld,
                                SolarBodyStyle style, uint32_t seed);
bool GameDebugSetSurfacePose(struct GameRuntime *game);
bool GameDebugSetSurfaceAddress(struct GameRuntime *game);
bool GameDebugTopologyDslResolve(const struct GameRuntime *game,
                                 const char *name,
                                 DebugDslValue *outValue);

#endif
