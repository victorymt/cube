#ifndef VOXELCRAFT_TERRAIN_H
#define VOXELCRAFT_TERRAIN_H

#include "types.h"

typedef enum PlanetBiome {
    PLANET_BIOME_BASALT_PLAINS = 0,
    PLANET_BIOME_LAVA_SEA,
    PLANET_BIOME_VOLCANIC_RIDGE,
    PLANET_BIOME_ICE_SHEET,
    PLANET_BIOME_GLACIER,
    PLANET_BIOME_DUNES,
    PLANET_BIOME_BADLANDS,
    PLANET_BIOME_OASIS,
    PLANET_BIOME_IMPACT_BASIN,
    PLANET_BIOME_CRATER_HIGHLANDS,
    PLANET_BIOME_OCEAN,
    PLANET_BIOME_COAST,
    PLANET_BIOME_PLAINS,
    PLANET_BIOME_FOREST,
    PLANET_BIOME_ALPINE,
    PLANET_BIOME_STORM_BANDS
} PlanetBiome;

unsigned int Hash2D(int x, int z);
unsigned int WorldHash2D(int x, int z);
unsigned int WorldHash3D(int x, int y, int z);
float TerrainNoise(float x, float z);
float BiomeNoise(int x, int z);
Biome BiomeAt(int x, int z);
int TerrainHeight(int x, int z, TerrainMode mode);
int PlanetTerrainHeight(int x, int z);
PlanetBiome PlanetBiomeAt(int x, int z);
const char *PlanetBiomeName(PlanetBiome biome);
bool ShouldPlaceTree(int x, int z, TerrainMode mode);
bool CaveAt(int x, int y, int z, int height);
bool CaveWaterAt(int x, int y, int z, int height);
BlockType OreAt(int x, int y, int z);
BlockType StoneOrCaveBlock(int x, int y, int z, int height);
bool ShouldPlacePond(int x, int z, int height);
void SetChunkLocalBlock(Chunk *chunk, int worldX, int y, int worldZ, BlockType type);
void GenerateChunkTerrain(Chunk *chunk, int cx, int cz, TerrainMode mode);
void ApplyEditsToChunk(Chunk *chunk);

#endif
