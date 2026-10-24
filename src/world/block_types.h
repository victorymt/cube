#ifndef VOXELCRAFT_BLOCK_TYPES_H
#define VOXELCRAFT_BLOCK_TYPES_H

#include "world/world_types.h"

#include "raylib.h"

#include <stdbool.h>

/*
 * Static properties of a block type: name, category predicates, collision and
 * liquid height, palette and material response. Everything here is a pure
 * function of the block type except the two coordinate queries, which consult
 * the block at that position for its overlay height.
 *
 * A consumer that only classifies or draws block types needs this header and
 * nothing else from world.
 */

typedef struct BlockMaterialResponse {
    float windResistance;
    float impactResistance;
    float flammability;
    float waterErodibility;
} BlockMaterialResponse;

const char *BlockName(BlockType type);
bool IsColorBlock(BlockType type);
bool IsValidBlockType(BlockType type);
bool IsWaterBlock(BlockType type);
bool IsLiquidBlock(BlockType type);
BlockRenderShape BlockRenderShapeFor(BlockType type);
bool IsPlantBlock(BlockType type);
bool IsEcologyBlock(BlockType type);
bool IsStage05Block(BlockType type);
bool IsStage06Block(BlockType type);
bool IsStage05GeologyBlock(BlockType type);
bool IsBiogenicBlock(BlockType type);
bool IsFireResidueBlock(BlockType type);
float BlockCollisionHeight(BlockType type);
float BlockCollisionHeightAt(int x, int y, int z);
float BlockLiquidHeightAt(int x, int y, int z);
bool IsTranslucentBlock(BlockType type);
int ColorBlockIndex(BlockType type);
BlockType ColorBlockFromIndex(int index);
Color ColorPalette256(int index);
Color BlockBaseColor(BlockType type);
BlockMaterialResponse BlockMaterialResponseFor(BlockType type);
BlockType NearestImageBlock(Color color);

#endif
