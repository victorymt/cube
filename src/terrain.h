#ifndef VOXELCRAFT_TERRAIN_H
#define VOXELCRAFT_TERRAIN_H

#include "types.h"

unsigned int Hash2D(int x, int z);
float TerrainNoise(float x, float z);
float BiomeNoise(int x, int z);
Biome BiomeAt(int x, int z);
int TerrainHeight(int x, int z, TerrainMode mode);
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
