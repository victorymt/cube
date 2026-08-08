#ifndef VOXELCRAFT_INVENTORY_H
#define VOXELCRAFT_INVENTORY_H

#include "types.h"

#include <stdbool.h>
#include <stdio.h>

#define INVENTORY_MAX_PER_BLOCK 999
#define INVENTORY_BLOCK_TYPE_COUNT (BLOCK_COLOR_END + 1)

void InventoryReset(void);
void InventoryGrantStarterKit(void);
int InventoryCount(BlockType type);
int InventoryAdd(BlockType type, int amount);
bool InventoryConsume(BlockType type, int amount);
bool InventorySave(FILE *file);
bool InventoryLoad(FILE *file);

#endif
