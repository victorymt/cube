#ifndef VOXELCRAFT_ECOLOGY_INTERNAL_H
#define VOXELCRAFT_ECOLOGY_INTERNAL_H

#include "ecology.h"

#include <stdint.h>

#define ECOLOGY_POPULATION_REGION_SIZE 64
#define ECOLOGY_POPULATION_SET_COUNT 256u
#define ECOLOGY_POPULATION_SET_WAYS 4u
#define ECOLOGY_POPULATION_STEP_DAYS 4.0
#define ECOLOGY_POPULATION_MAX_REGIONS \
    (ECOLOGY_POPULATION_SET_COUNT * ECOLOGY_POPULATION_SET_WAYS)
#define ECOLOGY_POPULATION_STATE_VERSION 5u
#define ECOLOGY_POPULATION_MIGRATION_STATE_VERSION 2u
#define ECOLOGY_POPULATION_HARVEST_STATE_VERSION 3u
#define ECOLOGY_POPULATION_RADIATION_STATE_VERSION 4u
#define ECOLOGY_POPULATION_EVOLUTION_STATE_VERSION 5u
#define ECOLOGY_POPULATION_LEGACY_STATE_VERSION 1u
#define ECOLOGY_HOMEWORLD_SURFACE_ID 0x484f4d45u

uint32_t EcologyMix(uint32_t value);
int EcologyFloorDivide(int value, int divisor);
uint32_t EcologyHash(int x, int z, uint32_t salt);
float EcologyClamp(float value);
bool EcologyWorldIsActive(void);
uint32_t EcologyWorldSurfaceId(void);
uint32_t EcologyWorldSeed(void);
int EcologyWorldOriginX(void);
int EcologyWorldOriginZ(void);

uint32_t EcologyProfileGeneration(void);
PlanetEcologyTraits EcologyTraitsForProfile(
    const PlanetEcologyProfile *profile);
PlanetLocalEnvironment EcologyEnvironmentAt(
    int x, int z, double simulationTime, float daylight,
    float precipitationRate, float currentStorm, bool dynamic,
    const PlanetEcologyProfile *ecology);
PlanetEcologySuitability EcologyStaticSuitabilityForProfile(
    int x, int z, const PlanetEcologyProfile *profile);
PlanetLocalEcology EcologyDynamicLocalAt(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile);

uint32_t EcologyPopulationEpoch(void);
void EcologyPopulationResetState(void);
bool EcologyPopulationSaveState(FILE *file);
bool EcologyPopulationLoadState(FILE *file);
float EcologyRegionalDisturbance(
    uint32_t surfaceId, int regionX, int regionZ, int originX, int originZ);
PlanetRegionalPopulation EcologyRegionalPopulationAt(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile,
    PlanetPopulationMigrationState *outMigration);
bool EcologyPopulationRecordFaunaHarvest(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, float organismScale,
    float ecologyCapacity);
bool EcologyEvolutionRegionAt(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, PlanetEvolutionRegion *out);
bool EcologyEvolutionSampleGenome(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, uint32_t sampleSeed,
    CreatureGenome *outGenome, uint32_t *outLineageId,
    uint32_t *outSpeciesId);
bool EcologyEvolutionRecordEvent(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, uint32_t lineageId,
    PlanetEvolutionEvent event, float biomass);

#endif
