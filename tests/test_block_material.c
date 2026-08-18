#include "world/block_catalog.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    const BlockCatalogEntry *stone = BlockCatalogGet(BLOCK_STONE);
    const BlockCatalogEntry *sand = BlockCatalogGet(BLOCK_SAND);
    const BlockCatalogEntry *wood = BlockCatalogGet(BLOCK_WOOD);
    const BlockCatalogEntry *leaves = BlockCatalogGet(BLOCK_LEAVES);
    const BlockCatalogEntry *bedrock = BlockCatalogGet(BLOCK_BEDROCK);
    assert(stone->impactResistance > sand->impactResistance);
    assert(stone->waterErodibility < sand->waterErodibility);
    assert(wood->flammability > stone->flammability);
    assert(leaves->windResistance < wood->windResistance);
    assert(bedrock->windResistance == 1.0f);
    assert(bedrock->impactResistance == 1.0f);
    puts("block material tests passed");
    return 0;
}
