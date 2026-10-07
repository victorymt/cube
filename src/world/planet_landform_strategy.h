#ifndef VOXELCRAFT_PLANET_LANDFORM_STRATEGY_H
#define VOXELCRAFT_PLANET_LANDFORM_STRATEGY_H

#include "space/planet_profile.h"
#include "world/landform_model.h"
#include "world/world_environment.h"

typedef enum LandformStrategyId {
    LANDFORM_STRATEGY_NONE, LANDFORM_STRATEGY_HOMEWORLD, LANDFORM_STRATEGY_WATER_WORLD
} LandformStrategyId;

typedef enum LandformCapability {
    LANDFORM_CAPABILITY_NONE = 0,
    LANDFORM_HAS_DRAINAGE = 1 << 0,
    LANDFORM_HAS_LIQUID_WATER = 1 << 1,
    LANDFORM_HAS_SEDIMENT = 1 << 2
} LandformCapability;

// Value object: owns its seed/profile, so selection and sampling need no globals.
typedef struct PlanetLandformStrategy {
    LandformStrategyId id;
    uint32_t capabilities, seed;
    float seaLevel;
    LandformPhysics physics;
    PlanetProfile profile;
} PlanetLandformStrategy;

PlanetLandformStrategy HomeworldLandformStrategy(uint32_t seed);
PlanetLandformStrategy WaterWorldLandformStrategy(uint32_t seed, const PlanetProfile *profile);
PlanetLandformStrategy PlanetLandformStrategyForSurface(
    WorldSurfaceKind kind, uint32_t seed, const PlanetProfile *profile);
const char *LandformStrategyName(LandformStrategyId id);
LandformBaseline PlanetLandformBaseline(Vector3 direction, void *strategy);
LandformBuildInput PlanetLandformBuildInput(PlanetLandformStrategy *strategy);
uint64_t PlanetLandformProfileFingerprint(const PlanetProfile *profile);
uint64_t PlanetLandformStrategyFingerprint(const PlanetLandformStrategy *strategy);

#endif
