#ifndef VOXELCRAFT_GAME_DEBUG_DIAGNOSTICS_H
#define VOXELCRAFT_GAME_DEBUG_DIAGNOSTICS_H

#include "world/chunks.h"
#include "core/debug_dsl.h"

struct GameRuntime;

/* Main-thread caches; detailed observations never replace base observations. */
const ChunkDiagnostics *GameDebugChunkDiagnostics(struct GameRuntime *game,
                                                  bool includeLod);
void GameDebugInvalidateDiagnostics(struct GameRuntime *game);
bool GameDebugDiagnosticsDslResolve(struct GameRuntime *game, const char *name,
                                    DebugDslValue *outValue);

#endif
