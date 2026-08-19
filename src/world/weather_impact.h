#ifndef VOXELCRAFT_WEATHER_IMPACT_H
#define VOXELCRAFT_WEATHER_IMPACT_H

#include "world/weather_model.h"
#include "world/wildfire_model.h"
#include "world/world_types.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define WEATHER_IMPACT_MAX_SURFACES 1024u
#define WEATHER_IMPACT_MAX_FIRES 256u
#define WEATHER_IMPACT_MAX_BURN_SITES 512u
#define WEATHER_IMPACT_TICK_RATE 2.0f

typedef enum WeatherSurfaceFlags {
    WEATHER_SURFACE_WET = 1u << 0,
    WEATHER_SURFACE_MUD = 1u << 1,
    WEATHER_SURFACE_SNOW = 1u << 2,
    WEATHER_SURFACE_FROST = 1u << 3,
    WEATHER_SURFACE_ICE = 1u << 4
} WeatherSurfaceFlags;

typedef struct WeatherSurfaceState {
    uint32_t surfaceId;
    int x;
    int y;
    int z;
    WeatherSurfaceFlags flags;
    float wetness;
    float snowDepth;
    float iceAmount;
    float erosionExposure;
    float windExposure;
    float impactExposure;
} WeatherSurfaceState;

typedef struct WeatherImpactStats {
    uint64_t ticks;
    uint64_t processedSurfaces;
    uint64_t depositedWater;
    uint32_t surfaceCount;
    uint32_t activeFires;
    uint32_t blockDamageEvents;
    uint32_t burnedBlocks;
    uint32_t ignitions;
    uint32_t spreadIgnitions;
    uint32_t extinctions;
    uint32_t suppressions;
    uint32_t burnSiteCount;
    uint32_t recoveredBurnSites;
    uint32_t droppedSurfaceUpdates;
    uint32_t droppedIgnitions;
    uint32_t droppedBurnSites;
} WeatherImpactStats;

typedef struct WeatherImpactFireSnapshot {
    uint32_t surfaceId;
    int x;
    int y;
    int z;
    BlockType fuelBlock;
    WildfireState state;
} WeatherImpactFireSnapshot;

typedef struct WeatherBurnSiteState {
    uint32_t surfaceId;
    int x;
    int y;
    int z;
    float severity;
    float recovery;
    float ageSeconds;
} WeatherBurnSiteState;

typedef struct WeatherImpactExposure {
    float heat;
    float smoke;
    float nearestDistance;
} WeatherImpactExposure;

void WeatherImpactInit(bool damageEnabled);
void WeatherImpactReset(void);
void WeatherImpactSetEnabled(bool enabled);
bool WeatherImpactEnabled(void);
void WeatherImpactUpdate(float dt, Vector3 playerPosition,
                         WeatherFieldSample weather);
void WeatherImpactStepTicks(unsigned ticks, Vector3 playerPosition,
                            WeatherFieldSample weather);
bool WeatherImpactSurfaceAt(int x, int y, int z,
                            WeatherSurfaceState *outState);
bool WeatherImpactFireAt(int x, int y, int z, float *outIntensity);
bool WeatherImpactFireStateAt(int x, int y, int z,
                              WeatherImpactFireSnapshot *outFire);
bool WeatherImpactIgniteAt(int x, int y, int z, float intensity);
unsigned WeatherImpactSuppressAt(int x, int y, int z, float radius,
                                 float amount);
unsigned WeatherImpactClearFires(void);
unsigned WeatherImpactCollectFires(Vector3 origin, float radius,
                                   WeatherImpactFireSnapshot *outFires,
                                   unsigned capacity);
bool WeatherImpactNearestFire(Vector3 position,
                              WeatherImpactFireSnapshot *outFire,
                              float *outDistance);
WeatherImpactExposure WeatherImpactExposureAt(Vector3 position,
                                              float shelter,
                                              float immersion);
bool WeatherImpactBurnSiteAt(int x, int y, int z,
                             WeatherBurnSiteState *outSite);
float WeatherImpactBurnSeverityAt(int x, int y, int z);
WeatherImpactStats WeatherImpactGetStats(void);
void WeatherImpactOnBlockChanged(int x, int y, int z);
BlockType WeatherImpactResidueForFuel(BlockType fuel, float severity,
                                      float moisture);

bool WeatherImpactSaveState(FILE *file);
bool WeatherImpactLoadState(FILE *file);

#endif
