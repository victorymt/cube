#ifndef VOXELCRAFT_LANDFORM_RUNTIME_H
#define VOXELCRAFT_LANDFORM_RUNTIME_H

#include "world/landform_model.h"
#include "world/terrain.h"

// Prepare on the main thread with workers drained; never builds during reads.
bool LandformRuntimePrepare(uint32_t seed, uint32_t version, bool enabled);
// Returns the immutable atlas for the active Homeworld generation.
const LandformAtlas *LandformRuntimeAtlas(void);
void LandformRuntimeReset(void);
void LandformApplyTerrainSample(int x, int z, SurfaceTerrainSample *sample);

#endif
