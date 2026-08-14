#ifndef VOXELCRAFT_PLANET_SURFACE_H
#define VOXELCRAFT_PLANET_SURFACE_H

#include "space.h"

#define PLANET_GLOBAL_CIRCUMFERENCE_BLOCKS 16384.0f
#define PLANET_GLOBAL_POLE_TO_POLE_BLOCKS 8192.0f

typedef enum PlanetBiome {
    PLANET_BIOME_BASALT_PLAINS = 0,
    PLANET_BIOME_LAVA_SEA,
    PLANET_BIOME_VOLCANIC_RIDGE,
    PLANET_BIOME_ICE_SHEET,
    PLANET_BIOME_GLACIER,
    PLANET_BIOME_DUNES,
    PLANET_BIOME_BADLANDS,
    PLANET_BIOME_OASIS,
    PLANET_BIOME_IMPACT_BASIN,
    PLANET_BIOME_CRATER_HIGHLANDS,
    PLANET_BIOME_OCEAN,
    PLANET_BIOME_COAST,
    PLANET_BIOME_PLAINS,
    PLANET_BIOME_FOREST,
    PLANET_BIOME_ALPINE,
    PLANET_BIOME_STORM_BANDS,
    PLANET_BIOME_COUNT
} PlanetBiome;

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
const char *PlanetBiomeName(PlanetBiome biome);

#endif
