#ifndef VOXELCRAFT_LANDFORM_MODEL_H
#define VOXELCRAFT_LANDFORM_MODEL_H

#include "world/surface_topology.h"
#include <stddef.h>

#define LANDFORM_RESOLUTION 128
#define LANDFORM_CELL_COUNT (6 * LANDFORM_RESOLUTION * LANDFORM_RESOLUTION)
#define LANDFORM_NONE UINT32_MAX

typedef struct LandformBaseline {
    float elevation;
    float runoff; // Long-term effective precipitation, independent of weather.
    float hardness;
} LandformBaseline;
typedef LandformBaseline (*LandformBaselineFn)(Vector3 direction, void *user);

typedef struct LandformCell {
    Vector3 direction;
    float elevation, spill, area, discharge;
    float runoff, hardness, slope, width;
    uint32_t downstream, watershed, order;
} LandformCell;

typedef struct LandformAtlas {
    LandformCell *cells;
    float seaLevel;
    size_t bytes;
    uint32_t rivers, lakes, closedBasins;
} LandformAtlas;

// Pure deterministic build. Caller owns the returned atlas; reads are immutable.
LandformAtlas *LandformBuild(float seaLevel, LandformBaselineFn baseline, void *user);
void LandformDestroy(LandformAtlas *atlas);
uint32_t LandformCellAtDirection(Vector3 direction);
Vector3 LandformCellDirection(uint32_t cell);
uint32_t LandformNeighbor(uint32_t cell, int edge);
float LandformCellArea(uint32_t cell);

#endif
