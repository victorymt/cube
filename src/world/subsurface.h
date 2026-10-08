#ifndef VOXELCRAFT_SUBSURFACE_H
#define VOXELCRAFT_SUBSURFACE_H

#include <stdbool.h>
#include <stdint.h>
#include "raylib.h"

typedef struct SubsurfaceParams {
    uint32_t seed;
    float activity;
    int minY;
    int surfaceClearance;
    int aquiferLevel;
    float aquiferChance;
} SubsurfaceParams;

typedef struct SubsurfaceSample {
    float tunnel;
    float chamber;
    float shaft;
    float aquifer;
    float openness;
    bool cave;
    bool flooded;
} SubsurfaceSample;

SubsurfaceSample SubsurfaceSampleAt(const SubsurfaceParams *params,
                                    int x, int y, int z,
                                    int surfaceHeight);
// Pure cube-generation entry point; direction is a unit vector. The
// per-thread column cache keys the full direction and seed, never map cells.
SubsurfaceSample SubsurfaceSampleAtDirection(const SubsurfaceParams *params,
                                             Vector3 direction, int y,
                                             int surfaceHeight);

#endif
