#include "gameplay/inventory.h"

#include <assert.h>
#include <stdint.h>
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
    assert(BLOCK_CRYSTAL == 71);
    assert(BLOCK_GRANITE == 72);
    assert(BLOCK_QUARTZ_ORE == 83);
    assert(BLOCK_LOAM == 84);
    assert(BLOCK_NICKEL_ORE == 95);
    assert(BLOCK_TALL_GRASS == 96);
    assert(BLOCK_CHEMO_MAT == 110);
    static const BlockType stage05[] = {
        BLOCK_ANDESITE, BLOCK_DIORITE, BLOCK_RHYOLITE, BLOCK_TUFF,
        BLOCK_SCHIST, BLOCK_SLATE, BLOCK_SERPENTINITE, BLOCK_DOLOMITE,
        BLOCK_GYPSUM, BLOCK_TRAVERTINE, BLOCK_BAUXITE,
        BLOCK_HEMATITE_ORE, BLOCK_MAGNETITE_ORE, BLOCK_PHOSPHATE_ROCK,
        BLOCK_CHERNOZEM, BLOCK_TERRA_ROSSA, BLOCK_ALLUVIUM,
        BLOCK_LEAF_LITTER, BLOCK_HUMUS, BLOCK_COMPOST, BLOCK_SHELL_BED,
        BLOCK_CORAL_LIMESTONE, BLOCK_GUANO, BLOCK_CHARRED_WOOD,
        BLOCK_CHARCOAL, BLOCK_FIRE_ASH
    };
    for (size_t index = 0; index < sizeof(stage05) / sizeof(stage05[0]);
         index++) {
        assert(stage05[index] == (BlockType)(111 + (int)index));
    }
    static const BlockType stage06[] = {
        BLOCK_OAK_LOG, BLOCK_OAK_LEAVES, BLOCK_BIRCH_LOG,
        BLOCK_BIRCH_LEAVES, BLOCK_ASPEN_LOG, BLOCK_ASPEN_LEAVES,
        BLOCK_SPRUCE_LOG, BLOCK_SPRUCE_NEEDLES, BLOCK_PINE_LOG,
        BLOCK_PINE_NEEDLES, BLOCK_WILLOW_LOG, BLOCK_WILLOW_LEAVES,
        BLOCK_BIG_BLUESTEM, BLOCK_BRACKEN, BLOCK_COMMON_REED,
        BLOCK_SPHAGNUM, BLOCK_HEATHER, BLOCK_FIREWEED, BLOCK_SAGUARO
    };
    for (size_t index = 0; index < sizeof(stage06) / sizeof(stage06[0]);
         index++) {
        assert(stage06[index] == (BlockType)(137 + (int)index));
    }
    assert(BLOCK_STAGE05_START == 111);
    assert(BLOCK_STAGE05_GEOLOGY_END == 124);
    assert(BLOCK_STAGE05_BIOGENIC_START == 125);
    assert(BLOCK_STAGE05_BIOGENIC_END == 133);
    assert(BLOCK_FIRE_RESIDUE_START == 134);
    assert(BLOCK_STAGE05_END == 136);
    assert(BLOCK_STAGE06_START == 137);
    assert(BLOCK_STAGE06_END == 155);
    assert(BLOCK_NATURAL_END == 155);
    assert(BLOCK_COLOR_START == 256);
    assert(BLOCK_COLOR_END == 511);
    assert(INVENTORY_BLOCK_TYPE_COUNT == 512);

    InventoryReset();
    int naturalCount = BLOCK_NATURAL_END - BLOCK_NATURAL_START + 1;
    for (int index = 0; index < naturalCount; index++) {
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
    assert(ftell(file) ==
           (long)(INVENTORY_BLOCK_TYPE_COUNT * sizeof(uint16_t)));
    rewind(file);

    InventoryReset();
    assert(InventoryCount(BLOCK_GRAVEL) == 0);
    assert(InventoryLoad(file));
    int naturalCount = BLOCK_NATURAL_END - BLOCK_NATURAL_START + 1;
    for (int index = 0; index < naturalCount; index++) {
        BlockType type = (BlockType)(BLOCK_NATURAL_START + index);
        assert(InventoryCount(type) == index + 1);
    }
    fclose(file);
}

static void TestLoadIsTransactional(void)
{
    InventoryReset();
    assert(InventoryAdd(BLOCK_FIRE_ASH, 17) == 17);

    FILE *file = tmpfile();
    assert(file);
    uint16_t loaded[INVENTORY_BLOCK_TYPE_COUNT] = { 0 };
    loaded[BLOCK_ANDESITE] = 9;
    loaded[BLOCK_CHARCOAL] = INVENTORY_MAX_PER_BLOCK + 1;
    assert(fwrite(loaded, sizeof(loaded), 1, file) == 1);
    rewind(file);
    assert(!InventoryLoad(file));
    assert(InventoryCount(BLOCK_FIRE_ASH) == 17);
    assert(InventoryCount(BLOCK_ANDESITE) == 0);
    fclose(file);
}

static void TestStarterKitDoesNotGrantNaturalBlocks(void)
{
    InventoryReset();
    InventoryGrantStarterKit();
    int naturalCount = BLOCK_NATURAL_END - BLOCK_NATURAL_START + 1;
    for (int index = 0; index < naturalCount; index++) {
        assert(InventoryCount(
                   (BlockType)(BLOCK_NATURAL_START + index)) == 0);
    }
}

int main(void)
{
    TestNaturalBlocks();
    TestSaveLoadRoundTrip();
    TestLoadIsTransactional();
    TestStarterKitDoesNotGrantNaturalBlocks();
    puts("inventory tests passed");
    return 0;
}
