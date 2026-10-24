#ifndef VOXELCRAFT_WORLD_H
#define VOXELCRAFT_WORLD_H

/*
 * Chunk lifecycle notifications from the streaming layer into the world.
 *
 * This is the only thing world.h still owns. It used to also carry block
 * properties, block access, the edit store, undo history, torch lighting and
 * world identity; those now live in world_state.h, block_types.h,
 * block_edits.h and block_lighting.h, so a consumer pays only for what it uses.
 *
 * This aggregate include keeps existing callers compiling. New code must
 * include the specific header instead; make test-architecture enforces that.
 */

#include "world/block_edits.h"
#include "world/block_lighting.h"
#include "world/block_types.h"
#include "world/world_state.h"

#include "world/chunk_types.h"

#include <stdbool.h>

void WorldNotifyChunkLoaded(Chunk *chunk);
void WorldNotifyChunkSectionLoaded(Chunk *chunk, int sectionY);
bool WorldPrepareChunkSectionUnload(Chunk *chunk, int sectionY);

#endif
