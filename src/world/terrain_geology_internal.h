#ifndef VOXELCRAFT_TERRAIN_GEOLOGY_INTERNAL_H
#define VOXELCRAFT_TERRAIN_GEOLOGY_INTERNAL_H

#include "space/planet_surface.h"
#include "world/world_types.h"

BlockType TerrainGeologyHomeStoneBlock(Biome biome, int depth, float region,
                                       float strata);
BlockType TerrainGeologyPlanetSubsurfaceBlock(SolarBodyStyle style,
                                               PlanetBiome biome, int depth,
                                               unsigned int hash);

#endif
