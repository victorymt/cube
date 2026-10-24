#ifndef VOXELCRAFT_FLORA_TYPES_H
#define VOXELCRAFT_FLORA_TYPES_H

#include "world/block_type.h"

#include "raylib.h"

#include <stdint.h>

/* Instance data for placed vegetation. Depends on the block taxonomy because a
 * tree instance names the block it is built from. */
typedef struct FloraVisualInstance {
    int firstVertex;
    int vertexCount;
    int taxonId;
    Vector3 anchor;
    float height;
    float windResponse;
} FloraVisualInstance;

typedef enum FloraStructureKind {
    FLORA_STRUCTURE_ALIEN_CANOPY = 0,
    FLORA_STRUCTURE_CRYSTAL,
    FLORA_STRUCTURE_SPORE,
    FLORA_STRUCTURE_THERMAL_VENT,
    FLORA_STRUCTURE_HOME_TREE
} FloraStructureKind;

typedef struct FloraStructureInstance {
    FloraStructureKind kind;
    int taxonId;
    uint32_t shapeHash;
    int rootX;
    int groundY;
    int rootZ;
    int minX;
    int minY;
    int minZ;
    int maxX;
    int maxY;
    int maxZ;
    BlockType primaryBlock;
    BlockType accentBlock;
    float windResponse;
} FloraStructureInstance;

#endif
