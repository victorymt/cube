#ifndef VOXELCRAFT_WORLD_ENVIRONMENT_H
#define VOXELCRAFT_WORLD_ENVIRONMENT_H

#include "types.h"

#include <stdint.h>

// The active environment describes where the player currently lives. The
// block region is kept separate because the space layer overlaps the ascent
// corridor in the local coordinate system.
typedef enum WorldDimension {
    WORLD_DIMENSION_HOME = 0,
    WORLD_DIMENSION_PLANET,
    WORLD_DIMENSION_SPACE,
    WORLD_DIMENSION_NETHER
} WorldDimension;

typedef enum WorldBlockRegion {
    WORLD_BLOCK_REGION_NONE = 0,
    WORLD_BLOCK_REGION_SURFACE,
    WORLD_BLOCK_REGION_NETHER,
    WORLD_BLOCK_REGION_SPACE
} WorldBlockRegion;

// Dimension selection is explicit; the position-aware form remains for
// callers that also use altitude for presentation decisions.
WorldDimension WorldCurrentDimension(void);
WorldDimension WorldCurrentDimensionAt(float y);
bool WorldNetherIsActive(void);
void WorldSetNetherActive(bool active);
const char *WorldDimensionName(WorldDimension dimension);
WorldBlockRegion WorldBlockRegionAt(int y);

bool WorldIsSurfaceActive(void);
bool WorldIsSpaceActive(void);
bool WorldIsSurfaceDimension(WorldDimension dimension);
int WorldSurfaceMinY(void);
int WorldSurfaceMaxYExclusive(void);
bool WorldCanAccessBlockY(int y);
float WorldGravityScale(void);
int WorldSurfaceHeightAt(int x, int z);
bool WorldProceduralWaterSurfaceAt(int x, int z, float *outSurfaceY);
uint32_t WorldCurrentSurfaceId(void);

#endif
