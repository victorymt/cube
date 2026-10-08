#ifndef VOXELCRAFT_VOLCANO_TERRAIN_H
#define VOXELCRAFT_VOLCANO_TERRAIN_H

#include "world/surface_topology.h"

/* Centers coincide with a subset of the 128-cell event candidate lattice. */
#define VOLCANO_TERRAIN_SPACING 512
#define VOLCANO_TERRAIN_OFFSET 320

typedef struct VolcanoTerrainDefinition {
    SurfaceAddress center;
    uint32_t seed;
    float radius, height, craterRadius, craterDepth, potential;
} VolcanoTerrainDefinition;

typedef struct VolcanoTerrainSample {
    VolcanoTerrainDefinition definition;
    float height;       /* Added relief in blocks. */
    float foundation;   /* Blend underlying terrain toward the center's base. */
    float crater;      /* Interior mask, 0..1. */
} VolcanoTerrainSample;

/* Pure, bounded, thread-safe queries; never discover runtime records. */
bool VolcanoTerrainDefine(uint32_t seed, float activity, SurfaceAddress center,
                           VolcanoTerrainDefinition *out);
VolcanoTerrainSample VolcanoTerrainShape(const VolcanoTerrainDefinition *definition,
                                         Vector3 direction);
VolcanoTerrainSample VolcanoTerrainAt(uint32_t seed, float activity, Vector3 direction);

#endif
