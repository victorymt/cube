#ifndef VOXELCRAFT_SUBSURFACE_H
#define VOXELCRAFT_SUBSURFACE_H

#include <stdbool.h>
#include <stdint.h>

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

#endif
