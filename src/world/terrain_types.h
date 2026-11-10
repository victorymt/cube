#ifndef VOXELCRAFT_TERRAIN_TYPES_H
#define VOXELCRAFT_TERRAIN_TYPES_H

/* Terrain and surface classification types. Kept separate from chunk storage
 * so generation, sampling, weather, HUD and debug consumers do not pay for the
 * chunk graph merely to name a biome or water column. */
typedef enum TerrainMode {
    TERRAIN_VARIED = 0,
    TERRAIN_FLAT
} TerrainMode;

typedef enum Biome {
    BIOME_PLAINS = 0,
    BIOME_FOREST,
    BIOME_DESERT,
    BIOME_SNOW,
    BIOME_MOUNTAIN,
    BIOME_SWAMP
} Biome;

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
    PLANET_BIOME_STORM_BANDS,
    PLANET_BIOME_TEMPERATE_MARSH,
    PLANET_BIOME_SALT_MARSH,
    PLANET_BIOME_FROZEN_MIRE,
    PLANET_BIOME_MAGMA_MIRE,
    PLANET_BIOME_CRATER_BOG,
    PLANET_BIOME_COUNT
} PlanetBiome;

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

#endif
