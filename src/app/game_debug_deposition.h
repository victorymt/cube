#ifndef VOXELCRAFT_GAME_DEBUG_DEPOSITION_H
#define VOXELCRAFT_GAME_DEBUG_DEPOSITION_H
#include "core/debug_control.h"
#include "core/debug_dsl.h"
/* Runs an isolated, bounded H0 fixture; never reads or changes the world. */
bool GameDebugDepositionRun(DebugControl *control);
bool GameDebugDepositionResolve(const DebugControl *control, const char *field, DebugDslValue *out);
#endif
