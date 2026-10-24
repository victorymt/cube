#ifndef VOXELCRAFT_BLOCK_EDIT_TYPES_H
#define VOXELCRAFT_BLOCK_EDIT_TYPES_H

#include "world/block_type.h"

#include <stdbool.h>
#include <stdint.h>

/* Stored block edits. Split from block bookkeeping so a consumer that only
 * reads world state does not link against edit storage. */
typedef struct BlockEdit {
    int x;
    int y;
    int z;
    BlockType type;
} BlockEdit;

typedef struct BlockEditIndex {
    uint32_t bodyId;
    int mapX;
    int mapZ;
    int radial;
    int editIndex;
    bool used;
} BlockEditIndex;

#endif
