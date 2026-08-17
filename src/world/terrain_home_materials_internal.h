#ifndef VOXELCRAFT_TERRAIN_HOME_MATERIALS_INTERNAL_H
#define VOXELCRAFT_TERRAIN_HOME_MATERIALS_INTERNAL_H

#include "world/terrain.h"

BlockType TerrainHomeBaseBlockFromSample(
    int worldX, int y, int worldZ, TerrainMode mode,
    const SurfaceTerrainSample *surface, int seaLevel);

#endif
