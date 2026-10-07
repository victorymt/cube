#ifndef VOXELCRAFT_LANDFORM_RUNTIME_H
#define VOXELCRAFT_LANDFORM_RUNTIME_H

#include "world/landform_model.h"
#include "world/planet_landform_strategy.h"
#include "world/terrain.h"

typedef struct LandformRuntimeInput {
    WorldSurfaceKind surfaceKind;
    uint32_t bodyId;
    uint32_t seed;
    uint32_t terrainVersion;
    bool enabled;
    PlanetLandformStrategy strategy;
} LandformRuntimeInput;

// Prepare an immutable atlas for the complete surface identity.
bool LandformRuntimePrepareSurface(const LandformRuntimeInput *input);
// Prepare on the main thread with workers drained; never builds during reads.
// Compatibility wrapper for the historical Homeworld-only callers.
bool LandformRuntimePrepare(uint32_t seed, uint32_t version, bool enabled);
// Returns the immutable atlas for the current surface, or NULL when unsupported.
const LandformAtlas *LandformRuntimeAtlas(void);
const PlanetLandformStrategy *LandformRuntimeStrategy(void);
uint32_t LandformRuntimeCapabilities(void);
Vector3 LandformRuntimeDirectionAt(int x, int z);
bool LandformRuntimePhysicsDiagnosticAt(int x, int z,
                                        LandformPhysicsDiagnostic *out);
void LandformRuntimeReset(void);
// sample must contain the current surface's own persistent baseline.
void LandformApplyTerrainSample(int x, int z, SurfaceTerrainSample *sample);

#endif
