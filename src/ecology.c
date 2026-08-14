#include "ecology_internal.h"

#include "space.h"
#include "world.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define ECOLOGY_LOCAL_CACHE_SIZE 256u

#if defined(__GNUC__) || defined(__clang__)
#define ECOLOGY_THREAD_LOCAL __thread
#else
#define ECOLOGY_THREAD_LOCAL
#endif

typedef struct EcologyLocalCacheEntry {
    bool valid;
    int x;
    int z;
    int originX;
    int originZ;
    double simulationTime;
    uint32_t daylightBits;
    uint32_t profileGeneration;
    uint32_t populationEpoch;
    uint64_t editRevision;
    PlanetLocalEcology ecology;
} EcologyLocalCacheEntry;

static ECOLOGY_THREAD_LOCAL EcologyLocalCacheEntry
    ecologyLocalCache[ECOLOGY_LOCAL_CACHE_SIZE] = { 0 };

uint32_t EcologyMix(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

bool EcologyWorldIsActive(void)
{
    return PlanetWorldIsActive() || HomeWorldSurfaceIsActive();
}

uint32_t EcologyWorldSurfaceId(void)
{
    return PlanetWorldIsActive() ? PlanetWorldSeed() :
                                   ECOLOGY_HOMEWORLD_SURFACE_ID;
}

uint32_t EcologyWorldSeed(void)
{
    return PlanetWorldIsActive() ? PlanetWorldSeed() : WorldGetSeed();
}

int EcologyWorldOriginX(void)
{
    return PlanetWorldIsActive() ? PlanetWorldOriginX() : 0;
}

int EcologyWorldOriginZ(void)
{
    return PlanetWorldIsActive() ? PlanetWorldOriginZ() : 0;
}

static uint32_t EcologyFloatBits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint64_t EcologyDoubleBits(double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static unsigned EcologyLocalCacheIndex(int x, int z, double simulationTime,
                                       float daylight,
                                       int originX, int originZ,
                                       uint64_t editRevision)
{
    // Runtime-only generations validate entries below, but must not change the
    // slot chosen for the same physical query after a save/load cycle.
    uint64_t timeBits = EcologyDoubleBits(simulationTime);
    uint32_t hash = (uint32_t)x * 0x9e3779b9u;
    hash ^= (uint32_t)z * 0x85ebca6bu;
    hash ^= (uint32_t)timeBits;
    hash ^= (uint32_t)(timeBits >> 32);
    hash ^= EcologyFloatBits(daylight);
    hash ^= (uint32_t)originX * 0x27d4eb2fu;
    hash ^= (uint32_t)originZ * 0x165667b1u;
    hash ^= (uint32_t)editRevision * 0x369dea0fu;
    hash ^= (uint32_t)(editRevision >> 32) * 0xa24baed5u;
    return EcologyMix(hash) & (ECOLOGY_LOCAL_CACHE_SIZE - 1u);
}

int EcologyFloorDivide(int value, int divisor)
{
    int quotient = value / divisor;
    int remainder = value % divisor;
    if (remainder < 0) quotient--;
    return quotient;
}

uint32_t EcologyHash(int x, int z, uint32_t salt)
{
    uint32_t hash = EcologyWorldSeed() ^ salt;
    hash ^= (uint32_t)x * 0x9e3779b9u;
    hash ^= (uint32_t)z * 0x85ebca6bu;
    return EcologyMix(hash);
}

float EcologyClamp(float value)
{
    if (!isfinite(value)) return 0.0f;
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

void PlanetEcologyResetState(void)
{
    EcologyPopulationResetState();
    memset(ecologyLocalCache, 0, sizeof(ecologyLocalCache));
}

bool PlanetEcologySaveState(FILE *file)
{
    if (!EcologyPopulationSaveState(file)) return false;
    // A loaded simulation starts with a cold local cache. Invalidate the live
    // cache as well so continuing after a save follows the same access order.
    memset(ecologyLocalCache, 0, sizeof(ecologyLocalCache));
    return true;
}

bool PlanetEcologyLoadState(FILE *file)
{
    if (!EcologyPopulationLoadState(file)) return false;
    memset(ecologyLocalCache, 0, sizeof(ecologyLocalCache));
    return true;
}

bool PlanetEcologyRecordFaunaHarvest(int x, int z, float daylight,
                                     float organismScale,
                                     float ecologyCapacity)
{
    if (!EcologyWorldIsActive()) return false;
    PlanetEcologyProfile profile = PlanetEcologyCurrent();
    return EcologyPopulationRecordFaunaHarvest(
        x, z, SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()),
        daylight, &profile,
        organismScale, ecologyCapacity);
}

bool PlanetEcologyEvolutionRegionAt(int x, int z, float daylight,
                                    PlanetEvolutionRegion *out)
{
    if (!EcologyWorldIsActive() || !out) return false;
    PlanetEcologyProfile profile = PlanetEcologyCurrent();
    return EcologyEvolutionRegionAt(
        x, z, SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()),
        EcologyClamp(daylight), &profile, out);
}

bool PlanetEcologySampleGenome(int x, int z, float daylight,
                               uint32_t sampleSeed, CreatureGenome *outGenome,
                               uint32_t *outLineageId,
                               uint32_t *outSpeciesId)
{
    if (!EcologyWorldIsActive()) return false;
    PlanetEcologyProfile profile = PlanetEcologyCurrent();
    return EcologyEvolutionSampleGenome(
        x, z, SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()),
        EcologyClamp(daylight), &profile, sampleSeed, outGenome,
        outLineageId, outSpeciesId);
}

bool PlanetEcologyRecordEvolutionEvent(
    int x, int z, float daylight, uint32_t lineageId,
    PlanetEvolutionEvent event, float biomass)
{
    if (!EcologyWorldIsActive()) return false;
    PlanetEcologyProfile profile = PlanetEcologyCurrent();
    return EcologyEvolutionRecordEvent(
        x, z, SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()),
        EcologyClamp(daylight), &profile, lineageId, event, biomass);
}

PlanetLocalEcology PlanetEcologyLocalAt(int x, int z, float daylight)
{
    PlanetLocalEcology local = { 0 };
    if (!EcologyWorldIsActive()) return local;

    // Invalid daylight is the same as a dark cell; normalize before hashing so
    // it cannot create a distinct cache entry or poison a later replay.
    daylight = EcologyClamp(daylight);
    PlanetEcologyProfile profile = PlanetEcologyCurrent();
    uint32_t profileGeneration = EcologyProfileGeneration();
    uint32_t populationEpoch = EcologyPopulationEpoch();
    double simulationTime = SpacePeriodicSimulationTime(
        SpaceElapsedSimulationTime());
    int originX = EcologyWorldOriginX();
    int originZ = EcologyWorldOriginZ();
    uint64_t editRevision = WorldGetEditRevision();
    uint32_t daylightBits = EcologyFloatBits(daylight);
    unsigned cacheIndex = EcologyLocalCacheIndex(
        x, z, simulationTime, daylight, originX, originZ, editRevision);
    EcologyLocalCacheEntry *cached = &ecologyLocalCache[cacheIndex];
    if (cached->valid && cached->x == x && cached->z == z &&
        cached->originX == originX && cached->originZ == originZ &&
        cached->simulationTime == simulationTime &&
        cached->daylightBits == daylightBits &&
        cached->profileGeneration == profileGeneration &&
        cached->populationEpoch == populationEpoch &&
        cached->editRevision == editRevision) {
        return cached->ecology;
    }

    local = EcologyDynamicLocalAt(x, z, simulationTime, daylight, &profile);
    PlanetPopulationMigrationState migration = { 0 };
    PlanetRegionalPopulation population = EcologyRegionalPopulationAt(
        x, z, simulationTime, daylight, &profile, &migration);
    local.population = population;
    local.migration = migration;
    int regionX = EcologyFloorDivide(
        originX + x, ECOLOGY_POPULATION_REGION_SIZE);
    int regionZ = EcologyFloorDivide(
        originZ + z, ECOLOGY_POPULATION_REGION_SIZE);
    local.environment.disturbance = EcologyRegionalDisturbance(
        EcologyWorldSurfaceId(), regionX, regionZ, originX, originZ);
    local.diagnostics.regionX = regionX;
    local.diagnostics.regionZ = regionZ;
    local.diagnostics.habitatStress = EcologyClamp(
        local.environment.disturbance * 0.94f);
    local.diagnostics.harvestStress = EcologyClamp(
        population.faunaHarvestPressure * 0.88f);
    local.diagnostics.faunaStress = EcologyClamp(
        1.0f - (1.0f - local.diagnostics.habitatStress) *
        (1.0f - local.diagnostics.harvestStress));
    PlanetPopulationInput populationInput = {
        .floraCapacity = local.suitability.floraCapacity,
        .faunaCapacity = local.suitability.faunaCapacity,
        .floraActivity = local.suitability.floraActivity,
        .faunaActivity = local.suitability.faunaActivity,
        .radiationExposure = local.environment.radiationExposure,
        .ejectaExposure = local.environment.ejectaExposure
    };
    local.diagnostics.faunaNetRecoveryRate = PlanetPopulationFaunaNetRate(
        &population, &populationInput, local.diagnostics.faunaStress);
    local.diagnostics.radiationMemory = EcologyClamp(
        population.radiationMemory);
    float floraPresence = PlanetPopulationFloraPresence(&population);
    float faunaPresence = PlanetPopulationFaunaPresence(&population);
    local.suitability.floraActivity = EcologyClamp(
        local.suitability.floraActivity * (0.08f + floraPresence * 0.92f));
    local.suitability.faunaActivity = EcologyClamp(
        local.suitability.faunaActivity * (0.04f + faunaPresence * 0.96f));

    *cached = (EcologyLocalCacheEntry){
        .valid = true,
        .x = x,
        .z = z,
        .originX = originX,
        .originZ = originZ,
        .simulationTime = simulationTime,
        .daylightBits = daylightBits,
        .profileGeneration = profileGeneration,
        .populationEpoch = populationEpoch,
        .editRevision = editRevision,
        .ecology = local
    };
    return local;
}

float PlanetEcologyFaunaDensityAt(int x, int z, float daylight)
{
    return PlanetEcologyLocalAt(x, z, daylight).suitability.faunaActivity;
}
