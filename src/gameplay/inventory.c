#include "gameplay/inventory.h"

#include "world/world.h"

#include <stdint.h>
#include <string.h>

static uint16_t inventoryCounts[INVENTORY_BLOCK_TYPE_COUNT];

static bool InventoryTypeIndex(BlockType type, int *outIndex)
{
    int index = (int)type;
    if (index < 0 || index >= INVENTORY_BLOCK_TYPE_COUNT ||
        (type > BLOCK_NETHER_PORTAL && !IsColorBlock(type)) ||
        !IsValidBlockType(type)) return false;
    *outIndex = index;
    return true;
}

void InventoryReset(void)
{
    memset(inventoryCounts, 0, sizeof(inventoryCounts));
}

void InventoryGrantStarterKit(void)
{
    static const BlockType starterBlocks[] = {
        BLOCK_GRASS, BLOCK_DIRT, BLOCK_STONE, BLOCK_WOOD, BLOCK_PLANK,
        BLOCK_SAND, BLOCK_SNOW, BLOCK_GLASS, BLOCK_WATER
    };

    for (int i = 0; i < (int)(sizeof(starterBlocks) / sizeof(starterBlocks[0])); i++) {
        InventoryAdd(starterBlocks[i], 32);
    }
    InventoryAdd(BLOCK_SPACESHIP, 1);
    InventoryAdd(BLOCK_COAL_ORE, 3);
}

int InventoryCount(BlockType type)
{
    int index = 0;
    if (!InventoryTypeIndex(type, &index)) return 0;
    return inventoryCounts[index];
}

int InventoryAdd(BlockType type, int amount)
{
    int index = 0;
    if (amount <= 0 || !InventoryTypeIndex(type, &index)) return 0;

    int space = INVENTORY_MAX_PER_BLOCK - inventoryCounts[index];
    int added = amount < space ? amount : space;
    inventoryCounts[index] = (uint16_t)(inventoryCounts[index] + added);
    return added;
}

bool InventoryConsume(BlockType type, int amount)
{
    int index = 0;
    if (amount <= 0 || !InventoryTypeIndex(type, &index) || inventoryCounts[index] < amount) return false;
    inventoryCounts[index] = (uint16_t)(inventoryCounts[index] - amount);
    return true;
}

bool InventorySave(FILE *file)
{
    return fwrite(inventoryCounts, sizeof(inventoryCounts), 1, file) == 1;
}

bool InventoryLoad(FILE *file)
{
    uint16_t loaded[INVENTORY_BLOCK_TYPE_COUNT];
    if (fread(loaded, sizeof(loaded), 1, file) != 1) return false;

    for (int i = 0; i < INVENTORY_BLOCK_TYPE_COUNT; i++) {
        if (loaded[i] > INVENTORY_MAX_PER_BLOCK) return false;
    }
    memcpy(inventoryCounts, loaded, sizeof(inventoryCounts));
    return true;
}
