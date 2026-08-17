#include "gameplay/inventory.h"

#include <assert.h>
#include <stdio.h>

bool IsColorBlock(BlockType type)
{
    return type >= BLOCK_COLOR_START && type <= BLOCK_COLOR_END;
}

bool IsValidBlockType(BlockType type)
{
    return (type >= BLOCK_AIR && type <= BLOCK_NATURAL_END) ||
           IsColorBlock(type);
}

static void TestNaturalBlocks(void)
{
    assert(BLOCK_SPACESHIP_OCCUPIED == 63);
    assert(BLOCK_NATURAL_START == 64);
    assert(BLOCK_NATURAL_END == 71);
    assert(BLOCK_COLOR_START == 256);

    InventoryReset();
    for (int index = 0; index < 8; index++) {
        BlockType type = (BlockType)(BLOCK_NATURAL_START + index);
        assert(InventoryAdd(type, index + 2) == index + 2);
        assert(InventoryCount(type) == index + 2);
        assert(InventoryConsume(type, 1));
        assert(InventoryCount(type) == index + 1);
    }
    assert(InventoryAdd(BLOCK_SPACESHIP_CORE_NORTH, 1) == 0);
    assert(InventoryAdd(BLOCK_SPACESHIP_OCCUPIED, 1) == 0);
}

static void TestSaveLoadRoundTrip(void)
{
    FILE *file = tmpfile();
    assert(file);
    assert(InventorySave(file));
    rewind(file);

    InventoryReset();
    assert(InventoryCount(BLOCK_GRAVEL) == 0);
    assert(InventoryLoad(file));
    for (int index = 0; index < 8; index++) {
        BlockType type = (BlockType)(BLOCK_NATURAL_START + index);
        assert(InventoryCount(type) == index + 1);
    }
    fclose(file);
}

static void TestStarterKitDoesNotGrantNaturalBlocks(void)
{
    InventoryReset();
    InventoryGrantStarterKit();
    for (int index = 0; index < 8; index++) {
        assert(InventoryCount(
                   (BlockType)(BLOCK_NATURAL_START + index)) == 0);
    }
}

int main(void)
{
    TestNaturalBlocks();
    TestSaveLoadRoundTrip();
    TestStarterKitDoesNotGrantNaturalBlocks();
    puts("inventory tests passed");
    return 0;
}
