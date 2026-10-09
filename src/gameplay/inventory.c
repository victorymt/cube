#include "gameplay/inventory.h"

#include "world/world.h"

#include <stdint.h>
#include <string.h>

static uint16_t inventoryCounts[INVENTORY_BLOCK_TYPE_COUNT];
/* Stable INVFR001 order; independent of enum adjacency and struct layout. */
static const BlockType fractionTypes[] = { BLOCK_VOLCANIC_ASH, BLOCK_BASALT,
    BLOCK_OBSIDIAN, BLOCK_SCORIA, BLOCK_PUMICE, BLOCK_RHYOLITE };
static uint16_t remainders[6];

static int FractionIndex(BlockType type)
{
    for (unsigned i = 0; i < 6; i++) if (fractionTypes[i] == type) return (int)i;
    return -1;
}
unsigned InventoryRemainder(BlockType type)
{
    int index = FractionIndex(type);
    return index < 0 ? 0 : remainders[index];
}
void InventoryResetRemainders(void) { memset(remainders, 0, sizeof(remainders)); }

bool InventoryCollectVolume(BlockType type, unsigned units,
                            bool (*removeSource)(void *user), void *user)
{
    int index = FractionIndex(type);
    if (index < 0 || !units || units > INVENTORY_VOLUME_PER_BLOCK || !removeSource ||
        InventoryCount(type) >= INVENTORY_MAX_PER_BLOCK) return false;
    unsigned total = remainders[index] + units;
    if (!removeSource(user)) return false;
    /* No fallible operations or world callbacks after the source commits. */
    inventoryCounts[(int)type] += (uint16_t)(total / INVENTORY_VOLUME_PER_BLOCK);
    remainders[index] = (uint16_t)(total % INVENTORY_VOLUME_PER_BLOCK);
    return true;
}
bool InventorySaveRemainders(FILE *file)
{
    if (!file || fwrite("INVFR001", 1, 8, file) != 8) return false;
    for (unsigned i = 0; i < 6; i++) {
        unsigned char bytes[2] = { (unsigned char)remainders[i], (unsigned char)(remainders[i] >> 8) };
        if (fwrite(bytes, 1, 2, file) != 2) return false;
    }
    return !ferror(file);
}
bool InventoryLoadRemainders(FILE *file)
{
    char magic[8]; uint16_t loaded[6];
    if (!file || fread(magic, 1, 8, file) != 8 || memcmp(magic, "INVFR001", 8)) return false;
    for (unsigned i = 0; i < 6; i++) {
        unsigned char bytes[2];
        if (fread(bytes, 1, 2, file) != 2) return false;
        loaded[i] = (uint16_t)((unsigned)bytes[0] | ((unsigned)bytes[1] << 8));
        if (loaded[i] >= INVENTORY_VOLUME_PER_BLOCK) return false;
    }
    memcpy(remainders, loaded, sizeof(loaded));
    return true;
}

static bool InventoryTypeIndex(BlockType type, int *outIndex)
{
    int index = (int)type;
    if (index < 0 || index >= INVENTORY_BLOCK_TYPE_COUNT ||
        (type > BLOCK_NETHER_PORTAL &&
         !(type >= BLOCK_NATURAL_START && type <= BLOCK_BUILTIN_END) &&
         !IsColorBlock(type)) ||
        !IsValidBlockType(type)) return false;
    *outIndex = index;
    return true;
}

void InventoryReset(void)
{
    memset(inventoryCounts, 0, sizeof(inventoryCounts));
    InventoryResetRemainders();
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
    for (int type = BLOCK_BUILDING_START; type <= BLOCK_BUILDING_END; type++) {
        InventoryAdd((BlockType)type, 64);
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
    InventoryResetRemainders();
    return true;
}
