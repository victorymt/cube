#include "world/terrain_structures_internal.h"

#include "world/cga_voxel.h"

void TerrainStructuresGenerate(Chunk *chunk, int cx, int cz,
                               TerrainMode mode)
{
    CgaGenerateStructureFeatures(chunk, cx, cz, mode, NULL);
}

#ifdef TERRAIN_TESTING
void TerrainTestGenerateMineshaft(Chunk *chunk, int cx, int cz,
                                  TerrainMode mode)
{
    CgaGenerateMineshaftFeatures(chunk, cx, cz, mode, NULL);
}

void TerrainTestGenerateStructures(Chunk *chunk, int cx, int cz,
                                   TerrainMode mode)
{
    TerrainStructuresGenerate(chunk, cx, cz, mode);
}
#endif
