#ifndef VOXELCRAFT_TERRAIN_H
#define VOXELCRAFT_TERRAIN_H

#include "planet_surface.h"
#include "types.h"

#define HOME_SEA_LEVEL 80
#define BATHYMETRY_MIN_SEABED_Y 8
#define BATHYMETRY_MAX_WATER_DEPTH 72

typedef enum BathymetryZone {
    BATHYMETRY_ZONE_LAND = 0,
    BATHYMETRY_ZONE_COAST,
    BATHYMETRY_ZONE_SHELF,
    BATHYMETRY_ZONE_SLOPE,
    BATHYMETRY_ZONE_ABYSSAL_PLAIN,
    BATHYMETRY_ZONE_TRENCH,
    BATHYMETRY_ZONE_SEAMOUNT
} BathymetryZone;

typedef enum BathymetryMaterial {
    BATHYMETRY_MATERIAL_SAND = 0,
    BATHYMETRY_MATERIAL_SEDIMENT,
    BATHYMETRY_MATERIAL_ROCK
} BathymetryMaterial;

typedef struct BathymetrySample {
    int seaLevel;
    int seabedY;
    int waterDepth;
    BathymetryZone zone;
    BathymetryMaterial material;
} BathymetrySample;

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
    BathymetrySample bathymetry;
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
BathymetrySample TerrainBathymetryAt(int x, int z, TerrainMode mode);
int TerrainSeaLevel(TerrainMode mode);
int PlanetTerrainHeight(int x, int z);
int PlanetTerrainSeaLevel(void);
BathymetrySample PlanetBathymetryAt(int x, int z);
const char *BathymetryZoneName(BathymetryZone zone);
const char *BathymetryMaterialName(BathymetryMaterial material);
BlockType BathymetryMaterialBlock(BathymetryMaterial material);
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
BlockType TerrainBaseBlockAt(int x, int y, int z, TerrainMode mode);
bool ShouldPlacePond(int x, int z, int height);
void SetChunkLocalBlock(Chunk *chunk, int worldX, int y, int worldZ, BlockType type);
// Generates only the procedural base layer for one absent vertical section.
// Decorations, structures, edits, and runtime fluid state are applied later.
bool GenerateChunkTerrainSectionBase(Chunk *chunk, int cx, int cz,
                                     int sectionY, TerrainMode mode);
void GenerateChunkTerrain(Chunk *chunk, int cx, int cz, TerrainMode mode);
void ApplyEditsToChunkSection(Chunk *chunk, int sectionY);
void ApplyEditsToChunk(Chunk *chunk);

#ifdef TERRAIN_TESTING
int TerrainTestHomeTreeVariantAt(int treeX, int treeZ, bool conifer);
int TerrainTestHomeTreeCrownRadiusAt(int treeX, int treeZ);
void TerrainTestPlaceHomeTree(Chunk *chunk, int treeX, int base, int treeZ,
                              bool conifer, int variant);
#endif

#endif
