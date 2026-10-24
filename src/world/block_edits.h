#ifndef VOXELCRAFT_BLOCK_EDITS_H
#define VOXELCRAFT_BLOCK_EDITS_H

#include "world/block_type.h"
#include "world/block_edit_types.h"
#include "world/surface_topology.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * The mutable block surface: reading and writing cells, the persisted edit
 * store that backs player changes, and the undo/redo history over them.
 *
 * These live together because they are one mechanism: a write appends an edit,
 * advances the edit revision and pushes an undo record. Splitting them further
 * would put one mechanism behind several headers without decoupling anything.
 */

/* Who is asking for a write. Replay and environment systems use this to tell
 * their own writes apart from player edits. */
typedef enum WorldMutationSource {
    WORLD_MUTATION_PLAYER = 0,
    WORLD_MUTATION_SYSTEM,
    WORLD_MUTATION_ENVIRONMENT
} WorldMutationSource;

/* Block access. `Base` bypasses generated overlays; the plain form composes
 * them. SurfaceBlockReadyAt reports whether the cell can be answered yet. */
BlockType GetBlockAt(int x, int y, int z);
BlockType GetBlockBaseAt(int x, int y, int z);
bool SurfaceBlockReadyAt(int x, int y, int z);

/* Writes. SetBlock records undo history; the NoUndo forms are for replay and
 * internal callers that own their own history. */
bool SetBlock(int x, int y, int z, BlockType type);
bool SetBlockNoUndo(int x, int y, int z, BlockType type);
bool SetBlockNoUndoFromSource(int x, int y, int z, BlockType type,
                              WorldMutationSource source);
WorldMutationSource WorldCurrentMutationSource(void);
bool SetBlockForImport(int x, int y, int z, BlockType type);

/* The persisted edit store, indexed for save/load and diagnostics. */
int WorldGetEditCount(void);
uint64_t WorldGetEditRevision(void);
const BlockEdit *WorldGetEditAt(int index);
uint32_t WorldGetEditDimensionAt(int index);
bool WorldGetEditSurfaceAddressAt(int index, SurfaceAddress *outAddress);
bool WorldGetEditSurfaceMapCellAt(int index, SurfaceMapCell *outCell);
bool WorldGetEditForCurrentDimension(int index, BlockEdit *outEdit);
bool WorldGetBlockEditForCurrentDimensionAt(int x, int y, int z,
                                            BlockType *outType);

/* Undo history. Groups exist so one user action can span several writes. */
void WorldBeginUndoGroup(void);
void WorldEndUndoGroup(void);
bool UndoBlockEdit(void);
bool RedoBlockEdit(void);
void ClearUndoHistory(void);

#endif
