#ifndef VOXELCRAFT_PLANET_SURFACE_H
#define VOXELCRAFT_PLANET_SURFACE_H

#include "space/planet_profile.h"
#include "world/terrain_types.h"
#include "world/volcano_terrain.h"

#include <stdint.h>

#define PLANET_GLOBAL_CIRCUMFERENCE_BLOCKS 16384.0f
#define PLANET_GLOBAL_POLE_TO_POLE_BLOCKS 8192.0f

typedef struct PlanetSurfaceSample {
    float continentalness;
    float regionalness;
    float erosion;
    float ridge;
    float peak;
    float trench;
    float climate;
    float detail;
    float temperature;
    float meanTemperature;
    float seasonalAmplitude;
    float moisture;
    float iceCoverage;
    float impactDepth;
    float impactRim;
    float ejecta;
    float volcanicActivity;
    float volcanicCone;
    float caldera;
    float lavaFlow;
    VolcanoTerrainSample volcano;
    float duneBand;
    float glacierFlow;
    float glacierCracks;
    PlanetBiome biome;
} PlanetSurfaceSample;

PlanetSurfaceSample PlanetSampleGlobalSurface(uint32_t seed, const PlanetProfile *profile,
                                              float longitude, float latitude);
PlanetSurfaceSample PlanetSampleGlobalSurfaceAtTime(uint32_t seed,
                                                    const PlanetProfile *profile,
                                                    float longitude, float latitude,
                                                    double simulationTime);
PlanetSurfaceSample PlanetSampleGlobalSurfaceBaseline(uint32_t seed,
                                                      const PlanetProfile *profile,
                                                      float longitude, float latitude);
float PlanetSurfaceFractalNoiseAt(uint32_t seed, float longitude,
                                  float latitude, float frequency,
                                  unsigned int lane);
const char *PlanetBiomeName(PlanetBiome biome);

#endif
