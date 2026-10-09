#ifndef VOXELCRAFT_GAME_DEBUG_VOLCANO_HISTORY_H
#define VOXELCRAFT_GAME_DEBUG_VOLCANO_HISTORY_H
#include "core/debug_control.h"
#include "core/debug_dsl.h"
bool GameDebugVolcanoHistory(DebugControl *control);
bool GameDebugVolcanoHistoryWorld(DebugControl *control);
bool GameDebugVolcanoHistoryResolve(const DebugControl *control, const char *field, DebugDslValue *out);
#endif
