#include "gameplay/inventory.h"
#include "world/world.h"
#include <assert.h>
#include <stdio.h>

extern bool GameInteractionTestCollectBlockAt(int x, int y, int z, BlockType type);
static int inventory;
static unsigned mutations;
static bool allowed;

int InventoryCount(BlockType type) { assert(type == BLOCK_BASALT); return inventory; }
int InventoryAdd(BlockType type, int amount)
{
    assert(type == BLOCK_BASALT && amount == 1 && allowed && mutations > 0u);
    inventory += amount;
    return amount;
}
bool SetBlock(int x, int y, int z, BlockType type)
{
    assert(x == 1 && y == 2 && z == 3 && type == BLOCK_AIR);
    mutations++;
    return allowed;
}
int main(void)
{
    allowed = false;
    for (unsigned i = 0u; i < 100u; i++) {
        assert(!GameInteractionTestCollectBlockAt(1, 2, 3, BLOCK_BASALT));
    }
    assert(inventory == 0 && mutations == 100u);
    allowed = true;
    assert(GameInteractionTestCollectBlockAt(1, 2, 3, BLOCK_BASALT));
    assert(inventory == 1 && mutations == 101u);
    inventory = INVENTORY_MAX_PER_BLOCK;
    assert(!GameInteractionTestCollectBlockAt(1, 2, 3, BLOCK_BASALT));
    assert(mutations == 101u);
    puts("block collection commits before inventory tests passed");
}
