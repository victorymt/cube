#ifndef VOXELCRAFT_GAME_DEBUG_LOD_H
#define VOXELCRAFT_GAME_DEBUG_LOD_H

#include "core/debug_dsl.h"
#include "world/chunks.h"

#include <stdbool.h>

bool GameDebugLodDslResolve(const char *name, DebugDslValue *outValue);
bool GameDebugLodDslResolveSnapshot(const ChunkDiagnostics *diagnostics,
                                    const char *name,
                                    DebugDslValue *outValue);

#endif
