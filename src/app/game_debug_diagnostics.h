#ifndef VOXELCRAFT_GAME_DEBUG_DIAGNOSTICS_H
#define VOXELCRAFT_GAME_DEBUG_DIAGNOSTICS_H

#include "world/chunks.h"

struct GameRuntime;

/* Main-thread caches; detailed observations never replace base observations. */
const ChunkDiagnostics *GameDebugChunkDiagnostics(struct GameRuntime *game,
                                                  bool includeLod);
void GameDebugInvalidateDiagnostics(struct GameRuntime *game);

#endif
