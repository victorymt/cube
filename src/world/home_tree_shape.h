#ifndef VOXELCRAFT_HOME_TREE_SHAPE_H
#define VOXELCRAFT_HOME_TREE_SHAPE_H

#include "ecology/flora_taxa.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct HomeTreeShapeSpec {
    int rootX;
    int baseY;
    int rootZ;
    int taxonId;
    uint32_t shapeHash;
    BlockType primaryBlock;
    BlockType accentBlock;
} HomeTreeShapeSpec;

typedef struct HomeTreeShapeBounds {
    int minX;
    int minY;
    int minZ;
    int maxX;
    int maxY;
    int maxZ;
} HomeTreeShapeBounds;

typedef bool (*HomeTreeShapeEmitter)(void *context, int x, int y, int z,
                                     BlockType block, bool replace);

// Emits the final deterministic tree recipe in placement order. replace is
// true for trunk/branch blocks and false for foliage that only fills air.
bool HomeTreeShapeEmit(const HomeTreeShapeSpec *spec,
                       HomeTreeShapeEmitter emitter, void *context);
bool HomeTreeShapeBoundsAt(const HomeTreeShapeSpec *spec,
                           HomeTreeShapeBounds *outBounds);
bool HomeTreeShapeBlockAt(const HomeTreeShapeSpec *spec,
                          int worldX, int y, int worldZ,
                          BlockType *outBlock);

#endif
