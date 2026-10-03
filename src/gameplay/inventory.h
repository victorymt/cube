#ifndef VOXELCRAFT_INVENTORY_H
#define VOXELCRAFT_INVENTORY_H

#include "world/world_types.h"

#include <stdbool.h>
#include <stdio.h>

#define INVENTORY_MAX_PER_BLOCK 999
#define INVENTORY_BLOCK_TYPE_COUNT (BLOCK_COLOR_END + 1)
#define INVENTORY_VOLUME_PER_BLOCK 1024u

void InventoryReset(void);
void InventoryGrantStarterKit(void);
int InventoryCount(BlockType type);
int InventoryAdd(BlockType type, int amount);
bool InventoryConsume(BlockType type, int amount);
bool InventorySave(FILE *file);
bool InventoryLoad(FILE *file);
/* Main-thread transaction: check capacity, remove the source, then credit
   whole blocks and the material's remainder. Failed removal changes nothing.
   The removal callback must not modify inventory or reenter collection. */
bool InventoryCollectVolume(BlockType type, unsigned units,
                            bool (*removeSource)(void *user), void *user);
unsigned InventoryRemainder(BlockType type);
void InventoryResetRemainders(void);
bool InventorySaveRemainders(FILE *file);
bool InventoryLoadRemainders(FILE *file);

#endif
