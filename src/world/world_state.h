#ifndef VOXELCRAFT_WORLD_STATE_H
#define VOXELCRAFT_WORLD_STATE_H

#include "world/world_types.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * World-level identity and lifecycle: which world this is, how it was
 * generated, and how to start over. This is the smallest stable surface a
 * consumer needs to ask "which world am I looking at", and it deliberately
 * carries no block access, edit storage or lighting.
 */

uint32_t WorldGetSeed(void);
void WorldSetSeed(uint32_t seed);
void WorldReset(uint32_t seed);
TerrainMode WorldTerrainMode(void);
void WorldSetTerrainMode(TerrainMode mode);
void WorldCleanup(void);

#endif
