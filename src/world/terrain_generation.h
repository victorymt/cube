#ifndef VOXELCRAFT_TERRAIN_GENERATION_H
#define VOXELCRAFT_TERRAIN_GENERATION_H

#include <stdbool.h>
#include <stdint.h>

#define TERRAIN_GENERATION_CURRENT 7u
#define TERRAIN_GENERATION_LEGACY 6u
#define TERRAIN_GENERATION_MIN 2u

// Retained across body changes; setters require drained chunk workers.
// Versions 2–5 keep the pre-existing v6 compatibility path.
uint32_t TerrainGenerationVersion(void);
bool TerrainGenerationSetVersion(uint32_t version);

#endif
