#ifndef VOXELCRAFT_LANDFORM_MODEL_H
#define VOXELCRAFT_LANDFORM_MODEL_H

#include "world/surface_topology.h"
#include "world/landform_physics.h"
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

// Callback supplies elevation in blocks, normalized long-term runoff and hardness.
// user is borrowed only for the synchronous build; atlas never retains it.
typedef struct LandformBuildInput {
    float seaLevel;
    LandformBaselineFn baseline;
    void *user;
    LandformPhysics physics;
    uint32_t strategyId;
} LandformBuildInput;

typedef struct LandformCell {
    Vector3 direction;
    float elevation, spill, area, discharge;
    float runoff, hardness, slope, width, localArea;
    uint32_t downstream, watershed, order;
} LandformCell;

typedef struct LandformAtlas {
    LandformCell *cells;
    float seaLevel;
    size_t bytes;
    uint32_t rivers, lakes, closedBasins;
    LandformPhysics physics; // Build metadata, never copied into voxel samples.
    uint32_t strategyId;
    bool terraces; // Version 8 static river history; disabled for version 7.
} LandformAtlas;

typedef enum LandformKind {
    LANDFORM_UPLAND, LANDFORM_RIVER, LANDFORM_DRY_CHANNEL, LANDFORM_LAKE,
    LANDFORM_WETLAND, LANDFORM_FLOODPLAIN, LANDFORM_FAN, LANDFORM_TERRACE
} LandformKind;

typedef struct LandformSample {
    float elevation, waterLevel, area, slope, wetness;
    float gravel, sand, fines, sedimentDepth, channelDistance;
    uint32_t watershed, reach;
    LandformKind kind;
    bool hasWater;
} LandformSample;

LandformSample LandformSampleAt(const LandformAtlas *atlas, Vector3 direction,
                               float baselineElevation);
const char *LandformKindName(LandformKind kind);

// Pure deterministic build. Caller owns the returned atlas; reads are immutable.
LandformAtlas *LandformBuild(float seaLevel, LandformBaselineFn baseline, void *user);
// Compatibility defaults preserve the historical geometry and normalized runoff.
LandformBuildInput LandformLegacyBuildInput(float seaLevel, LandformBaselineFn baseline, void *user);
LandformAtlas *LandformBuildWithInput(const LandformBuildInput *input);
void LandformDestroy(LandformAtlas *atlas);
uint32_t LandformCellAtDirection(Vector3 direction);
Vector3 LandformCellDirection(uint32_t cell);
uint32_t LandformNeighbor(uint32_t cell, int edge);
float LandformCellArea(uint32_t cell);

#endif
