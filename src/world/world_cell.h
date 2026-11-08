#ifndef VOXELCRAFT_WORLD_CELL_H
#define VOXELCRAFT_WORLD_CELL_H

#include "world/block_type.h"

#include <stdbool.h>
#include <stdint.h>

#define WORLD_CELL_WATER_FULL 255u

/*
 * Canonical block state for the voxel world. Water is independent of the
 * occupying block so air and plants can share a cell with a partial volume.
 * BLOCK_WATER is a legacy encoding and is converted by the helpers below.
 */
typedef struct WorldCell {
    BlockType block;
    uint8_t waterVolume;
} WorldCell;

static inline bool WorldCellBlockIsValid(BlockType block)
{
    return (block >= BLOCK_AIR && block <= BLOCK_BUILTIN_END) ||
           (block >= BLOCK_COLOR_START && block <= BLOCK_COLOR_END);
}

static inline bool WorldCellBlockIsPlant(BlockType block)
{
    switch (block) {
    case BLOCK_FLOWER:
    case BLOCK_MUSHROOM:
    case BLOCK_TALL_GRASS:
    case BLOCK_FERN:
    case BLOCK_REED:
    case BLOCK_MOSS_CARPET:
    case BLOCK_LICHEN:
    case BLOCK_MICROBIAL_MAT:
    case BLOCK_MYCELIUM:
    case BLOCK_CHEMO_MAT:
    case BLOCK_LEAF_LITTER:
    case BLOCK_BIG_BLUESTEM:
    case BLOCK_BRACKEN:
    case BLOCK_COMMON_REED:
    case BLOCK_SPHAGNUM:
    case BLOCK_HEATHER:
    case BLOCK_FIREWEED:
        return true;
    default:
        return false;
    }
}

static inline bool WorldCellCanContainWater(BlockType block)
{
    return block == BLOCK_AIR || WorldCellBlockIsPlant(block);
}

static inline bool WorldCellIsValid(WorldCell cell)
{
    return WorldCellBlockIsValid(cell.block) && cell.block != BLOCK_WATER &&
           (cell.waterVolume == 0u || WorldCellCanContainWater(cell.block));
}

static inline bool WorldCellEqual(WorldCell a, WorldCell b)
{
    return a.block == b.block && a.waterVolume == b.waterVolume;
}

static inline WorldCell WorldCellFromLegacyBlock(BlockType block)
{
    if (block == BLOCK_WATER) {
        return (WorldCell){ BLOCK_AIR, WORLD_CELL_WATER_FULL };
    }
    return (WorldCell){ block, 0u };
}

static inline WorldCell WorldCellWithBlock(WorldCell cell, BlockType block)
{
    if (block == BLOCK_WATER) return WorldCellFromLegacyBlock(block);
    cell.block = block;
    if (!WorldCellCanContainWater(block)) cell.waterVolume = 0u;
    return cell;
}

#endif
