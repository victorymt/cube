#include "gameplay/inventory.h"
#include "world/world.h"
#include "world/world_environment.h"
#include "world/ash_runtime.h"
#include "world/lava_runtime.h"
#include "app/game_geology.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

extern bool GameInteractionTestCollectBlockAt(int x, int y, int z, BlockType type);
static unsigned mutations, waterWakes;
static bool allowed, ready = true, nether;
static BlockType base = BLOCK_AIR;
static WorldSurfaceContext surface = { .active = true, .bodyId = 7 };
bool IsColorBlock(BlockType type) { return type >= BLOCK_COLOR_START && type <= BLOCK_COLOR_END; }
bool IsValidBlockType(BlockType type) { return (type >= BLOCK_AIR && type <= BLOCK_NATURAL_END) || IsColorBlock(type); }
WorldSurfaceContext WorldCurrentSurfaceContext(void) { return surface; }
bool WorldNetherIsActive(void) { return nether; }
WorldBlockRegion WorldBlockRegionAt(int y) { (void)y; return WORLD_BLOCK_REGION_SURFACE; }
bool SurfaceBlockReadyAt(int x, int y, int z) { (void)x; (void)y; (void)z; return ready; }
BlockType GetBlockBaseAt(int x, int y, int z) { (void)x; (void)y; (void)z; return base; }
void FluidOnBlockChanged(int x, int y, int z, BlockType previous, BlockType next)
{
    (void)x; (void)y; (void)z;
    assert(previous != BLOCK_AIR && next == BLOCK_AIR); waterWakes++;
}
bool SetBlock(int x, int y, int z, BlockType type)
{
    assert(x == 1 && y == 2 && z == 3 && type == BLOCK_AIR);
    mutations++;
    return allowed;
}
static AshBoundary AshOpen(int x, int y, int z, void *user)
{
    (void)x; (void)y; (void)z; (void)user; return ASH_OPEN;
}
static LavaBoundary Basin(int x, int y, int z, void *user)
{
    (void)user; return x == 1 && y == 2 && z == 3 ? LAVA_OPEN : LAVA_BLOCKED;
}
static void TestWholeBlocks(void)
{
    InventoryReset(); allowed = false;
    for (unsigned i = 0; i < 100; i++) assert(!GameInteractionTestCollectBlockAt(1, 2, 3, BLOCK_BASALT));
    assert(InventoryCount(BLOCK_BASALT) == 0 && mutations == 100);
    allowed = true;
    assert(GameInteractionTestCollectBlockAt(1, 2, 3, BLOCK_BASALT));
    assert(InventoryCount(BLOCK_BASALT) == 1 && mutations == 101);
    InventoryAdd(BLOCK_BASALT, 998);
    assert(!GameInteractionTestCollectBlockAt(1, 2, 3, BLOCK_BASALT) && mutations == 101);
}
static void TestAshCollection(void)
{
    InventoryReset(); allowed = false; mutations = 0;
    AshContext ctx = { .valid = true, .bodyId = 7, .boundary = AshOpen };
    assert(AshRuntimeDeposit(&ctx, 1, 2, 3, 512) == 512);
    surface.bodyId = 8;
    assert(GameGeologyCollectAt(1, 2, 3, BLOCK_VOLCANIC_ASH) == GAME_GEOLOGY_NO_DEPOSIT);
    surface.bodyId = 7; nether = true;
    assert(GameGeologyCollectAt(1, 2, 3, BLOCK_VOLCANIC_ASH) == GAME_GEOLOGY_NO_DEPOSIT);
    nether = false;
    ready = false;
    assert(!GameInteractionTestCollectBlockAt(1, 2, 3, BLOCK_VOLCANIC_ASH));
    ready = true; base = BLOCK_STONE;
    assert(!GameInteractionTestCollectBlockAt(1, 2, 3, BLOCK_VOLCANIC_ASH));
    base = BLOCK_AIR;
    assert(!GameInteractionTestCollectBlockAt(1, 2, 3, BLOCK_BASALT));
    InventoryAdd(BLOCK_VOLCANIC_ASH, INVENTORY_MAX_PER_BLOCK);
    assert(!GameInteractionTestCollectBlockAt(1, 2, 3, BLOCK_VOLCANIC_ASH));
    assert(AshRuntimeStats().volume == 512 && AshRuntimeStats().collected == 0 && waterWakes == 0);
    InventoryConsume(BLOCK_VOLCANIC_ASH, INVENTORY_MAX_PER_BLOCK);
    assert(GameInteractionTestCollectBlockAt(1, 2, 3, BLOCK_VOLCANIC_ASH));
    assert(InventoryCount(BLOCK_VOLCANIC_ASH) == 0 && InventoryRemainder(BLOCK_VOLCANIC_ASH) == 512);
    assert(waterWakes == 1 && mutations == 0);
    /* Repeated collection of the absent overlay cannot create inventory. */
    assert(!GameInteractionTestCollectBlockAt(1, 2, 3, BLOCK_VOLCANIC_ASH));
    assert(InventoryRemainder(BLOCK_VOLCANIC_ASH) == 512);
    /* Canonical addressing after a local origin rebase. */
    assert(AshRuntimeDeposit(&ctx, 101, 2, 3, 513) == 513); surface.originX = 100;
    assert(GameInteractionTestCollectBlockAt(1, 2, 3, BLOCK_VOLCANIC_ASH)); surface.originX = 0;
    assert(InventoryCount(BLOCK_VOLCANIC_ASH) == 1 && InventoryRemainder(BLOCK_VOLCANIC_ASH) == 1);
    assert(AshRuntimeStats().collected == 1025 && AshRuntimeStats().volume == 0);
}
static void TestRockCollection(void)
{
    LavaContext ctx = { .valid = true, .bodyId = 7, .boundary = Basin };
    assert(LavaRuntimeDepositMaterial(&ctx, 1, 2, 3, 512, 1450, 1, 1) == 512);
    assert(!GameInteractionTestCollectBlockAt(1, 2, 3, BLOCK_LAVA));
    assert(!InventoryCount(BLOCK_LAVA) && LavaRuntimeStats().removed == 0);
    for (unsigned i = 0; i < 32000 && LavaRuntimeStats().hotCells; i++) LavaRuntimeStep(&ctx);
    assert(LavaRuntimeStats().hotCells == 0);
    assert(GameInteractionTestCollectBlockAt(1, 2, 3, BLOCK_PUMICE));
    assert(InventoryRemainder(BLOCK_PUMICE) == 512 && !InventoryRemainder(BLOCK_BASALT));
    LavaStats s = LavaRuntimeStats();
    assert(s.removed == 512 && !s.volume && fabs(s.suppliedEnergy - s.cooledEnergy - s.exportedEnergy) < 0.01);
}
int main(void)
{
    TestWholeBlocks(); TestAshCollection(); TestRockCollection();
    puts("whole and fractional collection transactions passed");
}
