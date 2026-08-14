#ifndef VOXELCRAFT_TERRAIN_H
#define VOXELCRAFT_TERRAIN_H

#include "planet_surface.h"
#include "types.h"

#define HOME_SEA_LEVEL 80

typedef struct SurfaceTerrainSample {
    float elevation;
    float seaLevel;
    float continentalness;
    float erosion;
    float ridge;
    float peak;
    float trench;
    float slope;
    Biome biome;
} SurfaceTerrainSample;

unsigned int Hash2D(int x, int z);
unsigned int WorldHash2D(int x, int z);
// Accepts already-wrapped coordinate bit patterns for overflow-safe mixing.
unsigned int WorldHash2DBits(unsigned int xBits, unsigned int zBits);
unsigned int WorldHash3D(int x, int y, int z);
float TerrainNoise(float x, float z);
float BiomeNoise(int x, int z);
Biome BiomeAt(int x, int z);
int TerrainHeight(int x, int z, TerrainMode mode);
SurfaceTerrainSample SurfaceTerrainAt(int x, int z, TerrainMode mode);
int TerrainSeaLevel(TerrainMode mode);
int PlanetTerrainHeight(int x, int z);
int PlanetTerrainSeaLevel(void);
bool FindSafeSurfaceLanding(int preferredX, int preferredZ, int maxRadius,
                            int footprintRadius, int *outX, int *outZ,
                            int *outGroundY);
PlanetBiome PlanetBiomeAt(int x, int z);
void PlanetSurfaceLatLonAt(int x, int z, float *outLongitude, float *outLatitude);
PlanetSurfaceSample PlanetSurfaceBaselineAt(int x, int z);
PlanetSurfaceSample PlanetSurfaceAtTime(int x, int z, double simulationTime);
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
