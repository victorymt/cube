#ifndef VOXELCRAFT_INVENTORY_H
#define VOXELCRAFT_INVENTORY_H

#include "world/world_types.h"

#include <stdbool.h>
#include <stdio.h>

#define INVENTORY_MAX_PER_BLOCK 999
#define INVENTORY_BLOCK_TYPE_COUNT (BLOCK_COLOR_END + 1)
#define INVENTORY_VOLUME_PER_BLOCK 1024u

/* A type occupies one browser slot; quantities remain in the inventory ledger.
 * Moving a browser slot or hotbar binding never creates or consumes items. */
#define INVENTORY_BROWSER_CAPACITY (BLOCK_BUILTIN_END + 1 + COLOR_BLOCK_COUNT)
typedef struct InventoryBrowser {
    BlockType order[INVENTORY_BROWSER_CAPACITY];
    int count;
    int page;
    int drag; /* absolute order index; count + hotbar index for hotbar sources */
    bool ownedOnly;
} InventoryBrowser;
void InventoryBrowserInit(InventoryBrowser *browser);
void InventoryBrowserSort(InventoryBrowser *browser);
int InventoryBrowserList(const InventoryBrowser *browser, int *indices);
bool InventoryBrowserDrop(InventoryBrowser *browser, BlockType *hotbar,
                          int source, int target);

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
