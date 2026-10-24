#ifndef VOXELCRAFT_TERRAIN_TYPES_H
#define VOXELCRAFT_TERRAIN_TYPES_H

/* Terrain selection and biome identity. Kept separate from chunk storage: these
 * two enums are read by terrain generation, weather, the HUD and the debug DSL,
 * none of which should pay for `chunk_types.h`. */
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

#endif
