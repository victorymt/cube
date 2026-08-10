#include "ecology.h"

#include "chunks.h"
#include "space.h"
#include "terrain.h"
#include "weather.h"
#include "world.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PLANET_FLORA_STRUCTURE_RADIUS 3
#define ECOLOGY_LOCAL_CACHE_SIZE 256u
#define ECOLOGY_DISTURBANCE_CACHE_SIZE 256u
#define ECOLOGY_POPULATION_REGION_SIZE 64
#define ECOLOGY_POPULATION_SET_COUNT 256u
#define ECOLOGY_POPULATION_SET_WAYS 4u
#define ECOLOGY_POPULATION_STEP_DAYS 4.0
#define ECOLOGY_POPULATION_MAX_REGIONS \
    (ECOLOGY_POPULATION_SET_COUNT * ECOLOGY_POPULATION_SET_WAYS)
#define ECOLOGY_POPULATION_STATE_VERSION 2u
#define ECOLOGY_POPULATION_LEGACY_STATE_VERSION 1u

#if defined(__GNUC__) || defined(__clang__)
#define ECOLOGY_THREAD_LOCAL __thread
#else
#define ECOLOGY_THREAD_LOCAL
#endif

typedef struct EcologyProfileCache {
    bool valid;
    bool darkSide;
    uint32_t worldSeed;
    uint32_t generation;
    PlanetProfile planet;
    PlanetEcologyProfile ecology;
} EcologyProfileCache;

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

typedef struct EcologyDisturbanceCacheEntry {
    bool valid;
    uint32_t surfaceId;
    uint64_t editRevision;
    int regionX;
    int regionZ;
    int originX;
    int originZ;
    float disturbance;
} EcologyDisturbanceCacheEntry;

typedef struct EcologyPopulationRecord {
    bool valid;
    uint32_t surfaceId;
    int regionX;
    int regionZ;
    double lastUpdateTime;
    uint64_t lastAccess;
    PlanetRegionalPopulation population;
    PlanetPopulationMigrationState migration;
} EcologyPopulationRecord;

typedef struct EcologyPopulationStepState {
    bool active;
    PlanetRegionalPopulation population;
    PlanetPopulationInput input;
    PlanetMigrationHabitat habitat;
    float windX;
    float windZ;
    float floraStress;
    float faunaStress;
} EcologyPopulationStepState;

static ECOLOGY_THREAD_LOCAL EcologyProfileCache ecologyProfileCache = { 0 };
static ECOLOGY_THREAD_LOCAL EcologyLocalCacheEntry
    ecologyLocalCache[ECOLOGY_LOCAL_CACHE_SIZE] = { 0 };
static ECOLOGY_THREAD_LOCAL EcologyDisturbanceCacheEntry
    ecologyDisturbanceCache[ECOLOGY_DISTURBANCE_CACHE_SIZE] = { 0 };
static EcologyPopulationRecord
    ecologyPopulationRecords[ECOLOGY_POPULATION_MAX_REGIONS] = { 0 };
static uint64_t ecologyPopulationAccessSerial = 0u;
static uint32_t ecologyPopulationEpoch = 1u;

static uint32_t EcologyMix(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
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
                                       uint32_t profileGeneration,
                                       uint32_t populationEpoch,
                                       int originX, int originZ,
                                       uint64_t editRevision)
{
    uint64_t timeBits = EcologyDoubleBits(simulationTime);
    uint32_t hash = (uint32_t)x * 0x9e3779b9u;
    hash ^= (uint32_t)z * 0x85ebca6bu;
    hash ^= (uint32_t)timeBits;
    hash ^= (uint32_t)(timeBits >> 32);
    hash ^= EcologyFloatBits(daylight);
    hash ^= profileGeneration * 0xc2b2ae35u;
    hash ^= populationEpoch * 0x7feb352du;
    hash ^= (uint32_t)originX * 0x27d4eb2fu;
    hash ^= (uint32_t)originZ * 0x165667b1u;
    hash ^= (uint32_t)editRevision * 0x369dea0fu;
    hash ^= (uint32_t)(editRevision >> 32) * 0xa24baed5u;
    return EcologyMix(hash) & (ECOLOGY_LOCAL_CACHE_SIZE - 1u);
}

static int EcologyFloorDivide(int value, int divisor)
{
    int quotient = value / divisor;
    int remainder = value % divisor;
    if (remainder < 0) quotient--;
    return quotient;
}

static unsigned EcologyPopulationSetIndex(uint32_t surfaceId,
                                          int regionX, int regionZ)
{
    uint32_t hash = surfaceId;
    hash ^= (uint32_t)regionX * 0x9e3779b9u;
    hash ^= (uint32_t)regionZ * 0x85ebca6bu;
    return EcologyMix(hash) & (ECOLOGY_POPULATION_SET_COUNT - 1u);
}

static float EcologyPopulationOccupancy(uint32_t surfaceId, int regionX,
                                        int regionZ, uint32_t lane,
                                        float minimum, float range)
{
    uint32_t hash = surfaceId ^ lane;
    hash ^= (uint32_t)regionX * 0xc2b2ae35u;
    hash ^= (uint32_t)regionZ * 0x27d4eb2fu;
    hash = EcologyMix(hash);
    float unit = (float)(hash & 0x00ffffffu) / 16777215.0f;
    return minimum + unit * range;
}

static uint32_t EcologyHash(int x, int z, uint32_t salt)
{
    uint32_t hash = PlanetWorldSeed() ^ salt;
    hash ^= (uint32_t)x * 0x9e3779b9u;
    hash ^= (uint32_t)z * 0x85ebca6bu;
    return EcologyMix(hash);
}

static float EcologyClamp(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float EcologyBiomeSupport(PlanetBiome biome,
                                 const PlanetEcologyProfile *profile)
{
    float support = 0.0f;
    switch (biome) {
    case PLANET_BIOME_FOREST:             support = 1.00f; break;
    case PLANET_BIOME_OASIS:              support = 0.95f; break;
    case PLANET_BIOME_PLAINS:             support = 0.78f; break;
    case PLANET_BIOME_COAST:              support = 0.68f; break;
    case PLANET_BIOME_VOLCANIC_RIDGE:     support = 0.35f; break;
    case PLANET_BIOME_BASALT_PLAINS:      support = 0.28f; break;
    case PLANET_BIOME_ALPINE:             support = 0.40f; break;
    case PLANET_BIOME_BADLANDS:           support = 0.28f; break;
    case PLANET_BIOME_DUNES:              support = 0.16f; break;
    case PLANET_BIOME_GLACIER:            support = 0.18f; break;
    case PLANET_BIOME_ICE_SHEET:          support = 0.10f; break;
    case PLANET_BIOME_IMPACT_BASIN:       support = 0.22f; break;
    case PLANET_BIOME_CRATER_HIGHLANDS:   support = 0.14f; break;
    case PLANET_BIOME_OCEAN:
    case PLANET_BIOME_LAVA_SEA:
    case PLANET_BIOME_STORM_BANDS:
    default:                              support = 0.0f; break;
    }

    if (!profile) return support;
    if (profile->flora == PLANET_FLORA_CRYSTAL) {
        if (biome == PLANET_BIOME_GLACIER) support = 0.65f;
        if (biome == PLANET_BIOME_ICE_SHEET) support = 0.55f;
        if (biome == PLANET_BIOME_ALPINE) support = 0.72f;
        if (biome == PLANET_BIOME_BADLANDS) support = 0.55f;
        if (biome == PLANET_BIOME_IMPACT_BASIN) support = 0.58f;
        if (biome == PLANET_BIOME_CRATER_HIGHLANDS) support = 0.52f;
        if (biome == PLANET_BIOME_BASALT_PLAINS) support = 0.55f;
        if (biome == PLANET_BIOME_VOLCANIC_RIDGE) support = 0.68f;
    } else if (profile->flora == PLANET_FLORA_THERMAL_VENT) {
        if (biome == PLANET_BIOME_VOLCANIC_RIDGE) support = 0.88f;
        if (biome == PLANET_BIOME_BASALT_PLAINS) support = 0.68f;
    } else if (profile->flora == PLANET_FLORA_SPORE) {
        if (biome == PLANET_BIOME_IMPACT_BASIN) support = 0.42f;
        if (biome == PLANET_BIOME_CRATER_HIGHLANDS) support = 0.34f;
    }
    return support;
}

static PlanetEcologyTraits EcologyTraitsForProfile(
    const PlanetEcologyProfile *profile)
{
    PlanetEcologyTraits traits = { 0 };
    traits.preferredTemperatureK = 288.0f;
    traits.temperatureToleranceK = 42.0f;
    traits.waterDependence = 0.92f;
    traits.lightDependence = 0.78f;
    traits.stormResistance = 0.28f;
    traits.altitudeTolerance = 0.25f;
    traits.slopeTolerance = 0.22f;
    traits.foodWebDependence = 0.86f;
    traits.nocturnalFraction = 0.18f;
    if (!profile) return traits;

    if (profile->chemistry == PLANET_CHEMISTRY_SILICON) {
        traits.preferredTemperatureK = 326.0f;
        traits.temperatureToleranceK = 76.0f;
        traits.waterDependence = 0.28f;
        traits.lightDependence = 0.52f;
        traits.stormResistance = 0.72f;
        traits.altitudeTolerance = 0.62f;
        traits.slopeTolerance = 0.68f;
        traits.foodWebDependence = 0.45f;
        traits.nocturnalFraction = 0.35f;
    } else if (profile->chemistry == PLANET_CHEMISTRY_SULFUR) {
        traits.preferredTemperatureK = 348.0f;
        traits.temperatureToleranceK = 68.0f;
        traits.waterDependence = 0.22f;
        traits.lightDependence = 0.40f;
        traits.stormResistance = 0.68f;
        traits.altitudeTolerance = 0.55f;
        traits.slopeTolerance = 0.55f;
        traits.foodWebDependence = 0.55f;
        traits.nocturnalFraction = 0.25f;
    }

    switch (profile->niche) {
    case PLANET_NICHE_MICROBIAL:
        traits.foodWebDependence = 0.0f;
        traits.lightDependence *= 0.45f;
        traits.stormResistance = fmaxf(traits.stormResistance, 0.78f);
        break;
    case PLANET_NICHE_DECOMPOSER:
        traits.lightDependence = 0.12f;
        traits.foodWebDependence = 0.52f;
        traits.nocturnalFraction = 0.78f;
        break;
    case PLANET_NICHE_CRYSTAL_GRAZER:
        traits.waterDependence = 0.12f;
        traits.lightDependence = 0.32f;
        traits.slopeTolerance = 0.78f;
        traits.foodWebDependence = 0.24f;
        break;
    case PLANET_NICHE_FILTER_FEEDER:
        traits.stormResistance = 0.82f;
        traits.altitudeTolerance = 1.0f;
        traits.slopeTolerance = 1.0f;
        traits.foodWebDependence = 0.58f;
        break;
    case PLANET_NICHE_BIOLUMINESCENT_COLONY:
        traits.lightDependence = 0.05f;
        traits.foodWebDependence = 0.25f;
        traits.nocturnalFraction = 1.0f;
        break;
    case PLANET_NICHE_GRAZER:
    default:
        break;
    }
    return traits;
}

static PlanetLocalEnvironment EcologyEnvironmentAt(
    int x, int z, double simulationTime, float daylight,
    float precipitationRate, float currentStorm, bool dynamic,
    const PlanetEcologyProfile *ecology)
{
    PlanetLocalEnvironment environment = { 0 };
    if (!PlanetWorldIsActive()) return environment;

    const PlanetProfile *planet = PlanetWorldProfile();
    PlanetSurfaceSample surface = dynamic
        ? PlanetSurfaceAtTime(x, z, simulationTime)
        : PlanetSurfaceBaselineAt(x, z);
    int height = PlanetTerrainHeight(x, z);
    int east = PlanetTerrainHeight(x + 2, z);
    int west = PlanetTerrainHeight(x - 2, z);
    int south = PlanetTerrainHeight(x, z + 2);
    int north = PlanetTerrainHeight(x, z - 2);
    float maxSlope = fmaxf(fabsf((float)(east - west)),
                           fabsf((float)(south - north))) * 0.25f;
    environment.slope = EcologyClamp(maxSlope / 3.5f);
    environment.elevation = EcologyClamp(((float)height - 5.0f) / 25.0f);
    float surrounding = ((float)east + (float)west + (float)south + (float)north) * 0.25f;
    environment.shelter = EcologyClamp(0.48f + (surrounding - (float)height) / 7.0f);

    float lapseCooling = fmaxf((float)height - 12.0f, 0.0f) * 0.68f;
    environment.meanTemperatureK = surface.meanTemperature - lapseCooling;
    environment.currentTemperatureK = surface.temperature - lapseCooling;
    environment.seasonalAmplitudeK = surface.seasonalAmplitude;

    float drainage = 1.0f - environment.slope * 0.62f;
    environment.soilMoisture = EcologyClamp(
        surface.moisture * (1.0f - surface.iceCoverage * 0.76f) * drainage);
    float windAngle = planet->prevailingWindAngle;
    int upwindX = x - (int)lroundf(cosf(windAngle) * 8.0f);
    int upwindZ = z - (int)lroundf(sinf(windAngle) * 8.0f);
    int upwindHeight = PlanetTerrainHeight(upwindX, upwindZ);
    float orographicLift = EcologyClamp(
        0.50f + ((float)height - (float)upwindHeight) / 10.0f);
    environment.meanPrecipitation = EcologyClamp(
        surface.moisture * (0.36f + EcologyClamp(planet->cloudCoverage) * 0.64f) *
        (0.75f + orographicLift * 0.42f) *
        (1.0f - environment.elevation * 0.18f));

    float liquidThermal = EcologyClamp(
        (environment.meanTemperatureK - 238.0f) / 42.0f);
    liquidThermal *= 1.0f - EcologyClamp(
        (environment.meanTemperatureK - 365.0f) / 115.0f);
    float nearbyWater = 0.0f;
    if (surface.biome == PLANET_BIOME_COAST) nearbyWater = 0.62f;
    else if (surface.biome == PLANET_BIOME_OASIS) nearbyWater = 0.86f;
    else if (surface.biome == PLANET_BIOME_OCEAN) nearbyWater = 1.0f;
    environment.liquidWaterAccess = EcologyClamp(
        environment.soilMoisture * 0.58f +
        environment.meanPrecipitation * 0.22f + nearbyWater) * liquidThermal;

    float longitude = 0.0f;
    float latitude = 0.0f;
    PlanetSurfaceLatLonAt(x, z, &longitude, &latitude);
    (void)longitude;
    float irradiance = EcologyClamp((float)planet->receivedIrradiance / 1.4f);
    float annualSun = 0.55f + 0.45f * cosf(latitude);
    float cloudTransmission = 1.0f - EcologyClamp(planet->cloudCoverage) * 0.55f;
    environment.meanUsableLight = EcologyClamp(
        irradiance * annualSun * cloudTransmission);
    environment.currentUsableLight = dynamic
        ? EcologyClamp(daylight * EcologyClamp((float)planet->receivedIrradiance))
        : environment.meanUsableLight;

    float roughness = EcologyClamp((planet->terrainRoughness - 0.35f) / 1.20f);
    environment.stormExposure = EcologyClamp(
        planet->windStrength * 0.55f + planet->cloudCoverage * 0.20f +
        planet->oceanCoverage * 0.15f + roughness * 0.10f);
    environment.stormExposure *= 1.0f - environment.shelter * 0.35f;
    environment.precipitationRate = dynamic
        ? EcologyClamp(precipitationRate) : environment.meanPrecipitation;
    environment.currentStorm = dynamic ? EcologyClamp(currentStorm) : 0.0f;
    environment.biomeSupport = EcologyBiomeSupport(surface.biome, ecology);
    return environment;
}

static int EcologyPaletteIndex(PlanetChemistry chemistry, uint32_t hash, bool accent)
{
    unsigned r;
    unsigned g;
    unsigned b;
    unsigned shift = accent ? 11u : 0u;
    switch (chemistry) {
    case PLANET_CHEMISTRY_SILICON:
        r = 3u + ((hash >> shift) & 1u);
        g = 2u + ((hash >> (shift + 3u)) & 2u);
        b = 2u + ((hash >> (shift + 5u)) & 1u);
        break;
    case PLANET_CHEMISTRY_SULFUR:
        r = 6u + ((hash >> shift) & 1u);
        g = 4u + ((hash >> (shift + 3u)) & 3u);
        b = (hash >> (shift + 5u)) & 1u;
        break;
    case PLANET_CHEMISTRY_CARBON:
    default:
        r = 1u + ((hash >> shift) & 2u);
        g = 4u + ((hash >> (shift + 3u)) & 3u);
        b = (hash >> (shift + 5u)) & 2u;
        break;
    }
    return (int)((r << 5u) | (g << 2u) | b);
}

PlanetEcologyProfile PlanetEcologyCurrent(void)
{
    PlanetEcologyProfile result = { 0 };
    if (!PlanetWorldIsActive()) return result;

    const PlanetProfile *planet = PlanetWorldProfile();
    uint32_t worldSeed = PlanetWorldSeed();
    bool darkSide = PlanetWorldIsDarkSide();
    if (ecologyProfileCache.valid &&
        ecologyProfileCache.worldSeed == worldSeed &&
        ecologyProfileCache.darkSide == darkSide &&
        memcmp(&ecologyProfileCache.planet, planet, sizeof(*planet)) == 0) {
        return ecologyProfileCache.ecology;
    }

    float temperature = planet->equilibriumTempK;
    float temperatureComfort = 1.0f - EcologyClamp(fabsf(temperature - 288.0f) / 150.0f);
    float pressure = fmaxf(planet->surfacePressureAtm, 0.0f);
    float atmosphere = EcologyClamp(planet->atmosphereDensity);
    float water = EcologyClamp(planet->oceanCoverage);
    float ice = EcologyClamp(planet->iceCoverage);
    float wind = EcologyClamp(planet->windStrength);
    float atmosphereSupport = 0.0f;
    switch (planet->atmosphereType) {
    case PLANET_ATMOSPHERE_NONE:       atmosphereSupport = 0.0f; break;
    case PLANET_ATMOSPHERE_THIN:       atmosphereSupport = 0.22f; break;
    case PLANET_ATMOSPHERE_BREATHABLE: atmosphereSupport = 0.88f; break;
    case PLANET_ATMOSPHERE_DENSE:      atmosphereSupport = 0.82f; break;
    case PLANET_ATMOSPHERE_CORROSIVE:  atmosphereSupport = 0.30f; break;
    default: break;
    }
    float pressureSupport = EcologyClamp((pressure - 0.01f) / 0.54f);
    pressureSupport *= 1.0f - EcologyClamp((pressure - 3.0f) / 7.0f) * 0.55f;
    float waterSupport = EcologyClamp(0.12f + water * 0.78f + ice * 0.10f);
    float climateSupport = temperatureComfort * pressureSupport *
                           (1.0f - ice * 0.62f) * (1.0f - wind * 0.35f);
    float life = climateSupport * atmosphereSupport * waterSupport;
    if (planet->style == SOLAR_STYLE_TEMPERATE) life *= 1.15f;
    if (planet->style == SOLAR_STYLE_ICE) life *= 0.66f;
    if (planet->style == SOLAR_STYLE_DESERT) life *= 0.52f;
    if (planet->style == SOLAR_STYLE_LAVA) life *= 0.20f;
    if (planet->style == SOLAR_STYLE_CRATER) life *= 0.18f;
    if (planet->style == SOLAR_STYLE_GAS || !planet->hasSolidSurface) life = 0.0f;

    uint32_t seedHash = EcologyHash(0, 0, 0x72a31u);
    PlanetLifeHistory lifeHistory = PlanetLifeHistoryDerive(
        planet->seed, planet->ageGyr, life, planet->hasSolidSurface);
    result.planetAgeGyr = lifeHistory.planetAgeGyr;
    result.lifeOriginProbability = lifeHistory.originProbability;
    result.complexLifeProbability = lifeHistory.complexLifeProbability;
    result.evolutionProgress = lifeHistory.evolutionProgress;
    result.lifeOriginated = lifeHistory.lifeOriginated;
    result.hasComplexLife = lifeHistory.hasComplexLife;
    life = PlanetLifeHistoryDensity(&lifeHistory, life);

    float chemistryRoll = (float)(seedHash & 0xffffu) / 65535.0f;
    if (temperature > 365.0f) {
        result.chemistry = chemistryRoll < 0.64f ?
                           PLANET_CHEMISTRY_SULFUR : PLANET_CHEMISTRY_SILICON;
    } else if (chemistryRoll < 0.28f) {
        result.chemistry = PLANET_CHEMISTRY_SILICON;
    } else if (chemistryRoll < 0.48f) {
        result.chemistry = PLANET_CHEMISTRY_SULFUR;
    } else {
        result.chemistry = PLANET_CHEMISTRY_CARBON;
    }

    switch (planet->style) {
    case SOLAR_STYLE_TEMPERATE:
        result.flora = PLANET_FLORA_ALIEN_CANOPY;
        break;
    case SOLAR_STYLE_ICE:
        result.flora = PLANET_FLORA_CRYSTAL;
        break;
    case SOLAR_STYLE_DESERT:
        result.flora = PLANET_FLORA_CRYSTAL;
        break;
    case SOLAR_STYLE_LAVA:
        result.flora = PLANET_FLORA_THERMAL_VENT;
        break;
    case SOLAR_STYLE_CRATER:
        result.flora = PLANET_FLORA_CRYSTAL;
        break;
    case SOLAR_STYLE_GAS:
        result.flora = PLANET_FLORA_SPORE;
        break;
    default:
        result.flora = PLANET_FLORA_SPORE;
        break;
    }

    result.lifeDensity = EcologyClamp(life);
    if (!result.lifeOriginated || !planet->hasSolidSurface ||
        result.lifeDensity < 0.055f) {
        result.biomass = PLANET_BIOMASS_BARREN;
    } else if (!result.hasComplexLife) {
        result.biomass = PLANET_BIOMASS_MICROBIAL;
    } else if (temperature > 360.0f && atmosphere > 0.16f) {
        result.biomass = PLANET_BIOMASS_CRYSTALLINE;
    } else if (darkSide && result.lifeDensity >= 0.12f) {
        result.biomass = PLANET_BIOMASS_ANOMALOUS;
    } else if (result.lifeDensity < 0.20f || planet->atmosphereType == PLANET_ATMOSPHERE_NONE) {
        result.biomass = PLANET_BIOMASS_MICROBIAL;
    } else if (result.lifeDensity > 0.60f && water > 0.25f) {
        result.biomass = PLANET_BIOMASS_LUSH;
    } else if ((seedHash % 5u) == 0u || water < 0.10f) {
        result.biomass = PLANET_BIOMASS_FUNGAL;
    } else {
        result.biomass = PLANET_BIOMASS_LUSH;
    }

    result.floraDensity = EcologyClamp(result.lifeDensity * 0.92f);
    result.faunaDensity = EcologyClamp((result.lifeDensity - 0.14f) * 1.12f);
    switch (result.biomass) {
    case PLANET_BIOMASS_BARREN:
        result.floraDensity = 0.0f;
        result.faunaDensity = 0.0f;
        break;
    case PLANET_BIOMASS_MICROBIAL:
        result.floraDensity = EcologyClamp(result.lifeDensity * 0.18f);
        result.faunaDensity = 0.0f;
        break;
    case PLANET_BIOMASS_FUNGAL:
        result.flora = PLANET_FLORA_SPORE;
        result.floraDensity = EcologyClamp(0.12f + result.lifeDensity * 0.66f);
        break;
    case PLANET_BIOMASS_CRYSTALLINE:
        result.flora = PLANET_FLORA_CRYSTAL;
        result.faunaDensity = EcologyClamp(result.lifeDensity * 0.48f);
        break;
    case PLANET_BIOMASS_ANOMALOUS:
        result.flora = PLANET_FLORA_SPORE;
        result.faunaDensity = EcologyClamp(result.lifeDensity * 0.92f);
        break;
    case PLANET_BIOMASS_LUSH:
    default:
        result.flora = PLANET_FLORA_ALIEN_CANOPY;
        break;
    }

    float gravity = EcologyClamp((planet->surfaceGravity - 0.35f) / 1.45f) * 1.45f + 0.35f;
    result.organismScale = EcologyClamp(1.10f / sqrtf(gravity));
    if (result.organismScale < 0.48f) result.organismScale = 0.48f;
    if (result.organismScale > 2.20f) result.organismScale = 2.20f;
    result.bodyArmor = EcologyClamp((gravity - 0.76f) / 0.88f + wind * 0.20f);
    result.supportsFlight = result.hasComplexLife && planet->hasSolidSurface &&
                           pressure >= 0.35f && wind < 0.88f &&
                           (planet->atmosphereDensity >= 0.72f || gravity <= 0.68f);
    result.darkSideColony = result.hasComplexLife && planet->hasSolidSurface &&
                            darkSide && result.lifeDensity >= 0.12f;
    if (result.darkSideColony) {
        result.bodyPlan = PLANET_BODY_COLONY;
        result.niche = PLANET_NICHE_BIOLUMINESCENT_COLONY;
    } else if (result.biomass == PLANET_BIOMASS_CRYSTALLINE) {
        result.bodyPlan = PLANET_BODY_HEXAPOD;
        result.niche = PLANET_NICHE_CRYSTAL_GRAZER;
    } else if (result.supportsFlight) {
        result.bodyPlan = PLANET_BODY_FLOATING;
        result.niche = PLANET_NICHE_FILTER_FEEDER;
    } else if (result.biomass == PLANET_BIOMASS_MICROBIAL) {
        result.bodyPlan = PLANET_BODY_SERPENTINE;
        result.niche = PLANET_NICHE_MICROBIAL;
    } else if (gravity < 0.70f) {
        result.bodyPlan = PLANET_BODY_BIPED;
        result.niche = PLANET_NICHE_GRAZER;
    } else if (gravity > 1.20f) {
        result.bodyPlan = PLANET_BODY_HEXAPOD;
        result.niche = PLANET_NICHE_GRAZER;
    } else {
        switch (seedHash % 3u) {
        case 0: result.bodyPlan = PLANET_BODY_QUADRUPED; break;
        case 1: result.bodyPlan = PLANET_BODY_BIPED; break;
        default: result.bodyPlan = PLANET_BODY_SERPENTINE; break;
        }
        result.niche = result.biomass == PLANET_BIOMASS_FUNGAL ?
                       PLANET_NICHE_DECOMPOSER : PLANET_NICHE_GRAZER;
    }
    result.limbCount = result.bodyPlan == PLANET_BODY_HEXAPOD ? 6 :
                       result.bodyPlan == PLANET_BODY_BIPED ? 2 :
                       result.bodyPlan == PLANET_BODY_QUADRUPED ? 4 : 0;
    float speedScale = 0.86f / sqrtf(gravity);
    speedScale *= 1.0f - wind * 0.28f;
    if (result.bodyPlan == PLANET_BODY_FLOATING) speedScale *= 0.85f;
    if (result.bodyPlan == PLANET_BODY_COLONY || result.biomass == PLANET_BIOMASS_CRYSTALLINE) {
        speedScale *= 0.34f;
    }
    result.movementSpeed = EcologyClamp(speedScale * 0.70f);
    if (result.movementSpeed < 0.18f && result.faunaDensity > 0.0f) result.movementSpeed = 0.18f;
    result.temperament = EcologyClamp((float)((seedHash >> 17) & 255u) / 255.0f * 0.72f +
                                      (result.biomass == PLANET_BIOMASS_ANOMALOUS ? 0.22f : 0.0f));

    int primary = EcologyPaletteIndex(result.chemistry, seedHash, false);
    int accent = EcologyPaletteIndex(result.chemistry, seedHash, true);
    if (accent == primary) accent = (accent + 37) & 255;
    result.primaryBlock = (BlockType)(BLOCK_COLOR_START + primary);
    result.accentBlock = (BlockType)(BLOCK_COLOR_START + accent);

    ecologyProfileCache.valid = true;
    ecologyProfileCache.darkSide = darkSide;
    ecologyProfileCache.worldSeed = worldSeed;
    ecologyProfileCache.generation++;
    if (ecologyProfileCache.generation == 0u) {
        ecologyProfileCache.generation = 1u;
    }
    memcpy(&ecologyProfileCache.planet, planet, sizeof(*planet));
    ecologyProfileCache.ecology = result;
    return result;
}

float PlanetEcologyFaunaDensity(void)
{
    return PlanetEcologyCurrent().faunaDensity;
}

static PlanetEcologySuitability EcologyStaticSuitabilityForProfile(
    int x, int z, const PlanetEcologyProfile *profile)
{
    PlanetLocalEnvironment environment = EcologyEnvironmentAt(
        x, z, 0.0, 1.0f, 0.0f, 0.0f, false, profile);
    PlanetEcologyTraits traits = EcologyTraitsForProfile(profile);
    return PlanetEcologyEvaluateLocal(&environment, &traits,
                                      profile ? profile->floraDensity : 0.0f,
                                      profile ? profile->faunaDensity : 0.0f);
}

PlanetEcologySuitability PlanetEcologyStaticSuitabilityAt(int x, int z)
{
    PlanetEcologyProfile profile = PlanetEcologyCurrent();
    return EcologyStaticSuitabilityForProfile(x, z, &profile);
}

static PlanetLocalEcology EcologyDynamicLocalAt(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile)
{
    PlanetLocalEcology local = { 0 };
    WeatherFieldSample weather = WeatherFieldSampleAtWorldTime(
        x, z, simulationTime);
    float sky = EcologyClamp(WeatherFieldSkyFactor(weather));
    float precipitation = EcologyClamp(weather.precipitation);
    float storm = EcologyClamp(weather.storm);
    float usableDaylight = EcologyClamp(daylight * (1.0f - sky * 0.68f));
    local.environment = EcologyEnvironmentAt(
        x, z, simulationTime, usableDaylight, precipitation, storm,
        true, profile);
    PlanetEcologyTraits traits = EcologyTraitsForProfile(profile);
    local.suitability = PlanetEcologyEvaluateLocal(
        &local.environment, &traits,
        profile ? profile->floraDensity : 0.0f,
        profile ? profile->faunaDensity : 0.0f);
    return local;
}

static EcologyPopulationRecord *EcologyPopulationRecordAt(
    uint32_t surfaceId, int regionX, int regionZ, bool *created)
{
    unsigned setIndex = EcologyPopulationSetIndex(surfaceId, regionX, regionZ);
    unsigned start = setIndex * ECOLOGY_POPULATION_SET_WAYS;
    EcologyPopulationRecord *selected = NULL;
    for (unsigned way = 0; way < ECOLOGY_POPULATION_SET_WAYS; way++) {
        EcologyPopulationRecord *record = &ecologyPopulationRecords[start + way];
        if (record->valid && record->surfaceId == surfaceId &&
            record->regionX == regionX && record->regionZ == regionZ) {
            selected = record;
            break;
        }
        if (!record->valid) {
            if (!selected || selected->valid) selected = record;
        } else if (!selected ||
                   (selected->valid && record->lastAccess < selected->lastAccess)) {
            selected = record;
        }
    }
    if (!selected) return NULL;

    bool isNew = !selected->valid || selected->surfaceId != surfaceId ||
                 selected->regionX != regionX || selected->regionZ != regionZ;
    if (isNew) {
        *selected = (EcologyPopulationRecord){
            .valid = true,
            .surfaceId = surfaceId,
            .regionX = regionX,
            .regionZ = regionZ
        };
    }
    ecologyPopulationAccessSerial++;
    if (ecologyPopulationAccessSerial == 0u) ecologyPopulationAccessSerial = 1u;
    selected->lastAccess = ecologyPopulationAccessSerial;
    if (created) *created = isNew;
    return selected;
}

static int EcologyPopulationFindRecordIndex(uint32_t surfaceId,
                                            int regionX, int regionZ)
{
    unsigned setIndex = EcologyPopulationSetIndex(surfaceId, regionX, regionZ);
    unsigned start = setIndex * ECOLOGY_POPULATION_SET_WAYS;
    for (unsigned way = 0; way < ECOLOGY_POPULATION_SET_WAYS; way++) {
        unsigned index = start + way;
        EcologyPopulationRecord *record = &ecologyPopulationRecords[index];
        if (record->valid && record->surfaceId == surfaceId &&
            record->regionX == regionX && record->regionZ == regionZ) {
            return (int)index;
        }
    }
    return -1;
}

static float EcologyEditWeight(BlockType type)
{
    switch (type) {
    case BLOCK_AIR: return 0.72f;
    case BLOCK_LAVA: return 0.94f;
    case BLOCK_WATER: return 0.04f;
    case BLOCK_FLOWER:
    case BLOCK_MUSHROOM: return 0.01f;
    case BLOCK_GRASS:
    case BLOCK_DIRT:
    case BLOCK_SAND:
    case BLOCK_SNOW:
    case BLOCK_ICE:
    case BLOCK_LEAVES:
    case BLOCK_CACTUS: return 0.05f;
    case BLOCK_STONE:
    case BLOCK_WOOD:
    case BLOCK_BEDROCK:
    case BLOCK_COAL_ORE:
    case BLOCK_IRON_ORE:
    case BLOCK_GOLD_ORE:
    case BLOCK_DIAMOND_ORE: return 0.09f;
    default: return 0.30f;
    }
}

static unsigned EcologyDisturbanceCacheIndex(
    uint32_t surfaceId, int regionX, int regionZ,
    int originX, int originZ, uint64_t editRevision)
{
    uint32_t hash = surfaceId * 0x9e3779b9u;
    hash ^= (uint32_t)regionX * 0x85ebca6bu;
    hash ^= (uint32_t)regionZ * 0xc2b2ae35u;
    hash ^= (uint32_t)originX * 0x27d4eb2fu;
    hash ^= (uint32_t)originZ * 0x165667b1u;
    hash ^= (uint32_t)editRevision * 0x369dea0fu;
    hash ^= (uint32_t)(editRevision >> 32) * 0xa24baed5u;
    return EcologyMix(hash) & (ECOLOGY_DISTURBANCE_CACHE_SIZE - 1u);
}

static float EcologyRegionalDisturbance(
    uint32_t surfaceId, int regionX, int regionZ, int originX, int originZ)
{
    uint64_t editRevision = WorldGetEditRevision();
    unsigned cacheIndex = EcologyDisturbanceCacheIndex(
        surfaceId, regionX, regionZ, originX, originZ, editRevision);
    EcologyDisturbanceCacheEntry *cached =
        &ecologyDisturbanceCache[cacheIndex];
    if (cached->valid && cached->surfaceId == surfaceId &&
        cached->editRevision == editRevision &&
        cached->regionX == regionX && cached->regionZ == regionZ &&
        cached->originX == originX && cached->originZ == originZ) {
        return cached->disturbance;
    }

    int editCount = WorldGetEditCount();
    float accumulated = 0.0f;
    for (int index = 0; index < editCount; index++) {
        BlockEdit edit = { 0 };
        if (!WorldGetEditForCurrentDimension(index, &edit)) continue;
        int globalX = originX + edit.x;
        int globalZ = originZ + edit.z;
        if (EcologyFloorDivide(globalX, ECOLOGY_POPULATION_REGION_SIZE) !=
                regionX ||
            EcologyFloorDivide(globalZ, ECOLOGY_POPULATION_REGION_SIZE) !=
                regionZ) continue;
        int surfaceHeight = PlanetTerrainHeight(edit.x, edit.z);
        float surfaceDistance = fabsf((float)edit.y - (float)surfaceHeight);
        if (surfaceDistance > 12.0f) continue;
        float weight = EcologyEditWeight(edit.type) *
                       expf(-surfaceDistance / 5.0f);
        accumulated += weight;
    }
    float disturbance = EcologyClamp(
        1.0f - expf(-accumulated / 6.0f));
    *cached = (EcologyDisturbanceCacheEntry){
        .valid = true,
        .surfaceId = surfaceId,
        .editRevision = editRevision,
        .regionX = regionX,
        .regionZ = regionZ,
        .originX = originX,
        .originZ = originZ,
        .disturbance = disturbance
    };
    return disturbance;
}

static double EcologyPopulationStepTime(double simulationTime)
{
    if (!isfinite(simulationTime) || simulationTime <= 0.0) return 0.0;
    return floor(simulationTime / ECOLOGY_POPULATION_STEP_DAYS) *
           ECOLOGY_POPULATION_STEP_DAYS;
}

static void EcologyPopulationConditionsAt(
    const EcologyPopulationRecord *record, double simulationTime,
    float daylight, const PlanetEcologyProfile *profile,
    int originX, int originZ, EcologyPopulationStepState *state)
{
    int centerGlobalX = record->regionX * ECOLOGY_POPULATION_REGION_SIZE +
                        ECOLOGY_POPULATION_REGION_SIZE / 2;
    int centerGlobalZ = record->regionZ * ECOLOGY_POPULATION_REGION_SIZE +
                        ECOLOGY_POPULATION_REGION_SIZE / 2;
    int localX = centerGlobalX - originX;
    int localZ = centerGlobalZ - originZ;
    PlanetLocalEcology regional = EcologyDynamicLocalAt(
        localX, localZ, simulationTime, daylight, profile);
    state->input = (PlanetPopulationInput){
        .floraCapacity = regional.suitability.floraCapacity,
        .faunaCapacity = regional.suitability.faunaCapacity,
        .floraActivity = regional.suitability.floraActivity,
        .faunaActivity = regional.suitability.faunaActivity
    };
    state->habitat = (PlanetMigrationHabitat){
        .floraSuitability = regional.suitability.floraActivity,
        .faunaSuitability = regional.suitability.faunaActivity,
        .stormPressure = regional.environment.currentStorm
    };
    float disturbance = EcologyRegionalDisturbance(
        record->surfaceId, record->regionX, record->regionZ,
        originX, originZ);
    state->floraStress = disturbance * 0.82f;
    state->faunaStress = disturbance * 0.94f;
    WeatherFieldSample weather = WeatherFieldSampleAtWorldTime(
        localX, localZ, simulationTime);
    float windAngle = WeatherWindAngleAtWorldTime(
        localX, localZ, simulationTime);
    state->windX = cosf(windAngle) * EcologyClamp(weather.wind);
    state->windZ = sinf(windAngle) * EcologyClamp(weather.wind);
}

static void EcologyPopulationInitializeRecord(
    EcologyPopulationRecord *record, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, int originX, int originZ)
{
    EcologyPopulationStepState state = { 0 };
    EcologyPopulationConditionsAt(
        record, simulationTime, daylight, profile, originX, originZ, &state);
    float floraOccupancy = EcologyPopulationOccupancy(
        record->surfaceId, record->regionX, record->regionZ,
        0x51f15eu, 0.58f, 0.37f);
    float faunaOccupancy = EcologyPopulationOccupancy(
        record->surfaceId, record->regionX, record->regionZ,
        0xc0a1e5u, 0.42f, 0.43f);
    record->population = PlanetPopulationInitialize(
        &state.input, floraOccupancy, faunaOccupancy);
    record->lastUpdateTime = simulationTime;
}

static void EcologyPopulationAdvanceRecords(
    uint32_t surfaceId, double targetTime, float daylight,
    const PlanetEcologyProfile *profile, int originX, int originZ)
{
    bool changed = false;
    for (unsigned index = 0; index < ECOLOGY_POPULATION_MAX_REGIONS; index++) {
        EcologyPopulationRecord *record = &ecologyPopulationRecords[index];
        if (!record->valid || record->surfaceId != surfaceId) continue;
        if (record->lastUpdateTime > targetTime) {
            record->lastUpdateTime = targetTime;
            record->migration = (PlanetPopulationMigrationState){ 0 };
            changed = true;
        }
    }

    for (;;) {
        double stepStart = INFINITY;
        for (unsigned index = 0; index < ECOLOGY_POPULATION_MAX_REGIONS;
             index++) {
            EcologyPopulationRecord *record = &ecologyPopulationRecords[index];
            if (!record->valid || record->surfaceId != surfaceId ||
                record->lastUpdateTime >= targetTime) {
                continue;
            }
            stepStart = fmin(stepStart, record->lastUpdateTime);
        }
        if (!isfinite(stepStart)) break;

        double stepEnd = fmin(stepStart + ECOLOGY_POPULATION_STEP_DAYS,
                              targetTime);
        double elapsedTime = stepEnd - stepStart;
        EcologyPopulationStepState
            states[ECOLOGY_POPULATION_MAX_REGIONS] = { 0 };
        double floraDelta[ECOLOGY_POPULATION_MAX_REGIONS] = { 0 };
        double faunaDelta[ECOLOGY_POPULATION_MAX_REGIONS] = { 0 };
        double floraFlowX[ECOLOGY_POPULATION_MAX_REGIONS] = { 0 };
        double floraFlowZ[ECOLOGY_POPULATION_MAX_REGIONS] = { 0 };
        double faunaFlowX[ECOLOGY_POPULATION_MAX_REGIONS] = { 0 };
        double faunaFlowZ[ECOLOGY_POPULATION_MAX_REGIONS] = { 0 };
        for (unsigned index = 0; index < ECOLOGY_POPULATION_MAX_REGIONS;
             index++) {
            EcologyPopulationRecord *record = &ecologyPopulationRecords[index];
            if (!record->valid || record->surfaceId != surfaceId ||
                record->lastUpdateTime != stepStart) {
                continue;
            }
            states[index].active = true;
            states[index].population = record->population;
            EcologyPopulationConditionsAt(
                record, stepEnd, daylight, profile,
                originX, originZ, &states[index]);
            PlanetPopulationAdvance(
                &states[index].population, &states[index].input, elapsedTime);
            PlanetPopulationApplyDisturbance(
                &states[index].population, states[index].floraStress,
                states[index].faunaStress, elapsedTime);
        }

        static const int directions[2][2] = { { 1, 0 }, { 0, 1 } };
        for (unsigned index = 0; index < ECOLOGY_POPULATION_MAX_REGIONS;
             index++) {
            if (!states[index].active) continue;
            EcologyPopulationRecord *record = &ecologyPopulationRecords[index];
            for (int direction = 0; direction < 2; direction++) {
                int neighborIndex = EcologyPopulationFindRecordIndex(
                    surfaceId,
                    record->regionX + directions[direction][0],
                    record->regionZ + directions[direction][1]);
                if (neighborIndex < 0 || !states[neighborIndex].active) continue;
                float windAlignment =
                    (states[index].windX + states[neighborIndex].windX) *
                        0.5f * (float)directions[direction][0] +
                    (states[index].windZ + states[neighborIndex].windZ) *
                        0.5f * (float)directions[direction][1];
                PlanetPopulationMigrationFlux flux =
                    PlanetPopulationMigrationBetween(
                        &states[index].population, &states[index].habitat,
                        &states[neighborIndex].population,
                        &states[neighborIndex].habitat,
                        windAlignment, elapsedTime);
                floraDelta[index] -= flux.flora;
                floraDelta[neighborIndex] += flux.flora;
                faunaDelta[index] -= flux.fauna;
                faunaDelta[neighborIndex] += flux.fauna;
                float directionX = (float)directions[direction][0];
                float directionZ = (float)directions[direction][1];
                floraFlowX[index] += flux.flora * directionX;
                floraFlowX[neighborIndex] += flux.flora * directionX;
                floraFlowZ[index] += flux.flora * directionZ;
                floraFlowZ[neighborIndex] += flux.flora * directionZ;
                faunaFlowX[index] += flux.fauna * directionX;
                faunaFlowX[neighborIndex] += flux.fauna * directionX;
                faunaFlowZ[index] += flux.fauna * directionZ;
                faunaFlowZ[neighborIndex] += flux.fauna * directionZ;
            }
        }

        for (unsigned index = 0; index < ECOLOGY_POPULATION_MAX_REGIONS;
             index++) {
            if (!states[index].active) continue;
            EcologyPopulationRecord *record = &ecologyPopulationRecords[index];
            states[index].population.floraDensity = EcologyClamp(
                states[index].population.floraDensity +
                (float)floraDelta[index]);
            states[index].population.faunaDensity = EcologyClamp(
                states[index].population.faunaDensity +
                (float)faunaDelta[index]);
            record->population = states[index].population;
            record->migration = (PlanetPopulationMigrationState){
                .floraNet = fminf(fmaxf(
                    (float)floraDelta[index], -1.0f), 1.0f),
                .faunaNet = fminf(fmaxf(
                    (float)faunaDelta[index], -1.0f), 1.0f),
                .floraFlowX = fminf(fmaxf(
                    (float)floraFlowX[index], -1.0f), 1.0f),
                .floraFlowZ = fminf(fmaxf(
                    (float)floraFlowZ[index], -1.0f), 1.0f),
                .faunaFlowX = fminf(fmaxf(
                    (float)faunaFlowX[index], -1.0f), 1.0f),
                .faunaFlowZ = fminf(fmaxf(
                    (float)faunaFlowZ[index], -1.0f), 1.0f)
            };
            record->lastUpdateTime = stepEnd;
            changed = true;
        }
    }

    if (changed) {
        ecologyPopulationEpoch++;
        if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    }
}

static PlanetRegionalPopulation EcologyRegionalPopulationAt(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile,
    PlanetPopulationMigrationState *outMigration)
{
    PlanetRegionalPopulation empty = { 0 };
    if (outMigration) *outMigration = (PlanetPopulationMigrationState){ 0 };
    int originX = PlanetWorldOriginX();
    int originZ = PlanetWorldOriginZ();
    int globalX = originX + x;
    int globalZ = originZ + z;
    int regionX = EcologyFloorDivide(globalX, ECOLOGY_POPULATION_REGION_SIZE);
    int regionZ = EcologyFloorDivide(globalZ, ECOLOGY_POPULATION_REGION_SIZE);
    uint32_t surfaceId = PlanetWorldSeed();
    double stepTime = EcologyPopulationStepTime(simulationTime);
    EcologyPopulationAdvanceRecords(
        surfaceId, stepTime, daylight, profile, originX, originZ);

    static const int neighborhood[5][2] = {
        { 0, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 }
    };
    bool createdAny = false;
    for (int index = 0; index < 5; index++) {
        bool created = false;
        EcologyPopulationRecord *neighbor = EcologyPopulationRecordAt(
            surfaceId,
            regionX + neighborhood[index][0],
            regionZ + neighborhood[index][1], &created);
        if (!neighbor) continue;
        if (created) {
            EcologyPopulationInitializeRecord(
                neighbor, stepTime, daylight, profile, originX, originZ);
            createdAny = true;
        }
    }
    int recordIndex = EcologyPopulationFindRecordIndex(
        surfaceId, regionX, regionZ);
    if (recordIndex < 0) return empty;
    if (createdAny) {
        ecologyPopulationEpoch++;
        if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    }
    EcologyPopulationRecord *record = &ecologyPopulationRecords[recordIndex];
    if (outMigration) *outMigration = record->migration;
    return record->population;
}

static bool EcologyPopulationStateValid(
    const PlanetRegionalPopulation *population)
{
    if (!population) return false;
    const float values[] = {
        population->floraDensity, population->faunaDensity,
        population->floraCarryingCapacity,
        population->faunaCarryingCapacity,
        population->seasonalMemory
    };
    for (unsigned index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
        if (!isfinite(values[index]) || values[index] < 0.0f ||
            values[index] > 1.0f) {
            return false;
        }
    }
    return true;
}

static bool EcologyPopulationMigrationStateValid(
    const PlanetPopulationMigrationState *migration)
{
    if (!migration) return false;
    const float values[] = {
        migration->floraNet, migration->faunaNet,
        migration->floraFlowX, migration->floraFlowZ,
        migration->faunaFlowX, migration->faunaFlowZ
    };
    for (unsigned index = 0; index < sizeof(values) / sizeof(values[0]);
         index++) {
        if (!isfinite(values[index]) || values[index] < -1.0f ||
            values[index] > 1.0f) {
            return false;
        }
    }
    return true;
}

void PlanetEcologyResetState(void)
{
    memset(ecologyPopulationRecords, 0, sizeof(ecologyPopulationRecords));
    memset(ecologyLocalCache, 0, sizeof(ecologyLocalCache));
    ecologyPopulationAccessSerial = 0u;
    ecologyPopulationEpoch++;
    if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
}

bool PlanetEcologySaveState(FILE *file)
{
    if (!file) return false;
    uint32_t count = 0u;
    for (unsigned index = 0; index < ECOLOGY_POPULATION_MAX_REGIONS; index++) {
        if (ecologyPopulationRecords[index].valid) count++;
    }
    const uint32_t header[2] = { ECOLOGY_POPULATION_STATE_VERSION, count };
    if (fwrite(header, sizeof(header), 1, file) != 1 ||
        fwrite(&ecologyPopulationAccessSerial,
               sizeof(ecologyPopulationAccessSerial), 1, file) != 1) {
        return false;
    }
    for (unsigned index = 0; index < ECOLOGY_POPULATION_MAX_REGIONS; index++) {
        const EcologyPopulationRecord *record = &ecologyPopulationRecords[index];
        if (!record->valid) continue;
        int32_t coordinates[2] = {
            (int32_t)record->regionX, (int32_t)record->regionZ
        };
        const float population[5] = {
            record->population.floraDensity,
            record->population.faunaDensity,
            record->population.floraCarryingCapacity,
            record->population.faunaCarryingCapacity,
            record->population.seasonalMemory
        };
        const float migration[6] = {
            record->migration.floraNet, record->migration.faunaNet,
            record->migration.floraFlowX, record->migration.floraFlowZ,
            record->migration.faunaFlowX, record->migration.faunaFlowZ
        };
        if (!EcologyPopulationStateValid(&record->population) ||
            !EcologyPopulationMigrationStateValid(&record->migration) ||
            !isfinite(record->lastUpdateTime) ||
            fwrite(&record->surfaceId, sizeof(record->surfaceId), 1, file) != 1 ||
            fwrite(coordinates, sizeof(coordinates), 1, file) != 1 ||
            fwrite(&record->lastUpdateTime,
                   sizeof(record->lastUpdateTime), 1, file) != 1 ||
            fwrite(&record->lastAccess, sizeof(record->lastAccess), 1, file) != 1 ||
            fwrite(population, sizeof(population), 1, file) != 1 ||
            fwrite(migration, sizeof(migration), 1, file) != 1) {
            return false;
        }
    }
    return true;
}

bool PlanetEcologyLoadState(FILE *file)
{
    uint32_t header[2];
    uint64_t loadedAccessSerial = 0u;
    if (!file || fread(header, sizeof(header), 1, file) != 1 ||
        fread(&loadedAccessSerial, sizeof(loadedAccessSerial), 1, file) != 1 ||
        (header[0] != ECOLOGY_POPULATION_STATE_VERSION &&
         header[0] != ECOLOGY_POPULATION_LEGACY_STATE_VERSION) ||
        header[1] > ECOLOGY_POPULATION_MAX_REGIONS) {
        return false;
    }

    EcologyPopulationRecord loaded[ECOLOGY_POPULATION_MAX_REGIONS] = { 0 };
    for (uint32_t item = 0; item < header[1]; item++) {
        uint32_t surfaceId = 0u;
        int32_t coordinates[2];
        double lastUpdateTime = 0.0;
        uint64_t lastAccess = 0u;
        float populationValues[5];
        float migrationValues[6] = { 0 };
        if (fread(&surfaceId, sizeof(surfaceId), 1, file) != 1 ||
            fread(coordinates, sizeof(coordinates), 1, file) != 1 ||
            fread(&lastUpdateTime, sizeof(lastUpdateTime), 1, file) != 1 ||
            fread(&lastAccess, sizeof(lastAccess), 1, file) != 1 ||
            fread(populationValues, sizeof(populationValues), 1, file) != 1) {
            return false;
        }
        if (header[0] >= ECOLOGY_POPULATION_STATE_VERSION &&
            fread(migrationValues, sizeof(migrationValues), 1, file) != 1) {
            return false;
        }
        PlanetRegionalPopulation population = {
            .floraDensity = populationValues[0],
            .faunaDensity = populationValues[1],
            .floraCarryingCapacity = populationValues[2],
            .faunaCarryingCapacity = populationValues[3],
            .seasonalMemory = populationValues[4]
        };
        PlanetPopulationMigrationState migration = {
            .floraNet = migrationValues[0],
            .faunaNet = migrationValues[1],
            .floraFlowX = migrationValues[2],
            .floraFlowZ = migrationValues[3],
            .faunaFlowX = migrationValues[4],
            .faunaFlowZ = migrationValues[5]
        };
        if (surfaceId == 0u || !isfinite(lastUpdateTime) ||
            lastUpdateTime < 0.0 || lastAccess == 0u ||
            lastAccess > loadedAccessSerial ||
            !EcologyPopulationStateValid(&population) ||
            !EcologyPopulationMigrationStateValid(&migration)) {
            return false;
        }

        unsigned setIndex = EcologyPopulationSetIndex(
            surfaceId, (int)coordinates[0], (int)coordinates[1]);
        unsigned start = setIndex * ECOLOGY_POPULATION_SET_WAYS;
        EcologyPopulationRecord *slot = NULL;
        for (unsigned way = 0; way < ECOLOGY_POPULATION_SET_WAYS; way++) {
            EcologyPopulationRecord *candidate = &loaded[start + way];
            if (candidate->valid && candidate->surfaceId == surfaceId &&
                candidate->regionX == (int)coordinates[0] &&
                candidate->regionZ == (int)coordinates[1]) {
                return false;
            }
            if (!candidate->valid && !slot) slot = candidate;
        }
        if (!slot) return false;
        *slot = (EcologyPopulationRecord){
            .valid = true,
            .surfaceId = surfaceId,
            .regionX = (int)coordinates[0],
            .regionZ = (int)coordinates[1],
            .lastUpdateTime = lastUpdateTime,
            .lastAccess = lastAccess,
            .population = population,
            .migration = migration
        };
    }

    memcpy(ecologyPopulationRecords, loaded, sizeof(loaded));
    memset(ecologyLocalCache, 0, sizeof(ecologyLocalCache));
    ecologyPopulationAccessSerial = loadedAccessSerial;
    ecologyPopulationEpoch++;
    if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    return true;
}

PlanetLocalEcology PlanetEcologyLocalAt(int x, int z, float daylight)
{
    PlanetLocalEcology local = { 0 };
    if (!PlanetWorldIsActive()) return local;

    PlanetEcologyProfile profile = PlanetEcologyCurrent();
    double simulationTime = SpaceSimulationTime();
    int originX = PlanetWorldOriginX();
    int originZ = PlanetWorldOriginZ();
    uint64_t editRevision = WorldGetEditRevision();
    uint32_t daylightBits = EcologyFloatBits(daylight);
    unsigned cacheIndex = EcologyLocalCacheIndex(
        x, z, simulationTime, daylight, ecologyProfileCache.generation,
        ecologyPopulationEpoch, originX, originZ, editRevision);
    EcologyLocalCacheEntry *cached = &ecologyLocalCache[cacheIndex];
    if (cached->valid && cached->x == x && cached->z == z &&
        cached->originX == originX && cached->originZ == originZ &&
        cached->simulationTime == simulationTime &&
        cached->daylightBits == daylightBits &&
        cached->profileGeneration == ecologyProfileCache.generation &&
        cached->populationEpoch == ecologyPopulationEpoch &&
        cached->editRevision == editRevision) {
        return cached->ecology;
    }

    local = EcologyDynamicLocalAt(x, z, simulationTime, daylight, &profile);
    PlanetPopulationMigrationState migration = { 0 };
    PlanetRegionalPopulation population = EcologyRegionalPopulationAt(
        x, z, simulationTime, daylight, &profile, &migration);
    local.population = population;
    local.migration = migration;
    local.environment.disturbance = EcologyRegionalDisturbance(
        PlanetWorldSeed(),
        EcologyFloorDivide(originX + x, ECOLOGY_POPULATION_REGION_SIZE),
        EcologyFloorDivide(originZ + z, ECOLOGY_POPULATION_REGION_SIZE),
        originX, originZ);
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
        .profileGeneration = ecologyProfileCache.generation,
        .populationEpoch = ecologyPopulationEpoch,
        .editRevision = editRevision,
        .ecology = local
    };
    return local;
}

float PlanetEcologyFaunaDensityAt(int x, int z, float daylight)
{
    return PlanetEcologyLocalAt(x, z, daylight).suitability.faunaActivity;
}

const char *PlanetEcologyLifeName(void)
{
    float density = PlanetEcologyCurrent().lifeDensity;
    if (density < 0.05f) return "Sterile";
    if (density < 0.20f) return "Trace life";
    if (density < 0.42f) return "Sparse life";
    if (density < 0.68f) return "Flourishing";
    return "Abundant life";
}

const char *PlanetEcologyBiomassName(void)
{
    switch (PlanetEcologyCurrent().biomass) {
    case PLANET_BIOMASS_MICROBIAL:    return "Microbial";
    case PLANET_BIOMASS_FUNGAL:       return "Fungal";
    case PLANET_BIOMASS_CRYSTALLINE:  return "Crystalline";
    case PLANET_BIOMASS_LUSH:         return "Lush";
    case PLANET_BIOMASS_ANOMALOUS:    return "Anomalous";
    case PLANET_BIOMASS_BARREN:
    default:                          return "Barren";
    }
}

const char *PlanetEcologyChemistryName(void)
{
    switch (PlanetEcologyCurrent().chemistry) {
    case PLANET_CHEMISTRY_SILICON: return "Silicon";
    case PLANET_CHEMISTRY_SULFUR:  return "Sulfur";
    case PLANET_CHEMISTRY_CARBON:
    default:                        return "Carbon";
    }
}

const char *PlanetEcologyBodyPlanName(void)
{
    PlanetEcologyProfile profile = PlanetEcologyCurrent();
    if (!profile.lifeOriginated) return "None";
    if (!profile.hasComplexLife) return "Microscopic";
    switch (profile.bodyPlan) {
    case PLANET_BODY_BIPED:      return "Biped";
    case PLANET_BODY_HEXAPOD:    return "Hexapod";
    case PLANET_BODY_SERPENTINE: return "Serpentine";
    case PLANET_BODY_FLOATING:   return "Floating";
    case PLANET_BODY_COLONY:     return "Colony";
    case PLANET_BODY_QUADRUPED:
    default:                     return "Quadruped";
    }
}

const char *PlanetEcologyNicheName(void)
{
    switch (PlanetEcologyCurrent().niche) {
    case PLANET_NICHE_MICROBIAL:             return "Microbial mat";
    case PLANET_NICHE_DECOMPOSER:            return "Decomposer";
    case PLANET_NICHE_CRYSTAL_GRAZER:       return "Crystal rock-eater";
    case PLANET_NICHE_FILTER_FEEDER:        return "Floating grazer";
    case PLANET_NICHE_BIOLUMINESCENT_COLONY: return "Bioluminescent colony";
    case PLANET_NICHE_GRAZER:
    default:                                 return "Grazer";
    }
}

static void EcologySet(Chunk *chunk, int x, int y, int z, BlockType type)
{
    if (InHeight(y)) SetChunkLocalBlock(chunk, x, y, z, type);
}

static void RegisterFloraStructure(Chunk *chunk, int x, int z, int ground,
                                   const PlanetEcologyProfile *profile,
                                   PlanetFloraArchetype archetype,
                                   uint32_t hash)
{
    FloraStructureKind kind = FLORA_STRUCTURE_SPORE;
    switch (archetype) {
    case PLANET_FLORA_ALIEN_CANOPY:
        kind = FLORA_STRUCTURE_ALIEN_CANOPY;
        break;
    case PLANET_FLORA_CRYSTAL:
        kind = FLORA_STRUCTURE_CRYSTAL;
        break;
    case PLANET_FLORA_SPORE:
        kind = FLORA_STRUCTURE_SPORE;
        break;
    case PLANET_FLORA_THERMAL_VENT:
        kind = FLORA_STRUCTURE_THERMAL_VENT;
        break;
    }

    int base = ground + 1;
    FloraStructureInstance structure = {
        .kind = kind,
        .shapeHash = hash,
        .rootX = x,
        .groundY = ground,
        .rootZ = z,
        .minX = x,
        .minY = base,
        .minZ = z,
        .maxX = x,
        .maxY = base,
        .maxZ = z,
        .primaryBlock = profile->primaryBlock,
        .accentBlock = profile->accentBlock,
        .windResponse = 1.0f
    };
    switch (kind) {
    case FLORA_STRUCTURE_ALIEN_CANOPY: {
        int trunkHeight = 3 + (int)(hash % 3u);
        structure.minX = x - 2;
        structure.maxX = x + 2;
        structure.minZ = z - 2;
        structure.maxZ = z + 2;
        structure.maxY = base + trunkHeight + 1;
        structure.windResponse = 1.0f;
    } break;
    case FLORA_STRUCTURE_CRYSTAL: {
        int height = 2 + (int)(hash % 4u);
        structure.minX = x - 1;
        structure.maxX = x + 1;
        structure.minZ = z - 1;
        structure.maxZ = z + 1;
        structure.maxY = base + height - 1;
        structure.windResponse = 0.12f;
    } break;
    case FLORA_STRUCTURE_SPORE: {
        int stemHeight = 2 + (int)(hash % 2u);
        structure.minX = x - 1;
        structure.maxX = x + 1;
        structure.minZ = z - 1;
        structure.maxZ = z + 1;
        structure.maxY = base + stemHeight + 1;
        structure.windResponse = 0.65f;
    } break;
    case FLORA_STRUCTURE_THERMAL_VENT: {
        int height = 2 + (int)(hash % 3u);
        structure.minX = x - 1;
        structure.maxX = x + 1;
        structure.maxZ = z + 1;
        structure.maxY = base + height;
        structure.windResponse = 0.05f;
    } break;
    }

    int chunkMinX = chunk->cx * CHUNK_SIZE;
    int chunkMinZ = chunk->cz * CHUNK_SIZE;
    int chunkMaxX = chunkMinX + CHUNK_SIZE - 1;
    int chunkMaxZ = chunkMinZ + CHUNK_SIZE - 1;
    if (structure.maxX < chunkMinX || structure.minX > chunkMaxX ||
        structure.maxZ < chunkMinZ || structure.minZ > chunkMaxZ ||
        structure.maxY < 0 || structure.minY >= WORLD_HEIGHT) return;

    for (int index = 0; index < chunk->floraStructureCount; index++) {
        const FloraStructureInstance *existing = &chunk->floraStructures[index];
        if (existing->kind == structure.kind &&
            existing->rootX == structure.rootX &&
            existing->rootZ == structure.rootZ) return;
    }
    if (chunk->floraStructureCount >= MAX_CHUNK_FLORA_STRUCTURES) return;
    chunk->floraStructures[chunk->floraStructureCount++] = structure;
}

static void PlaceAlienCanopy(Chunk *chunk, int x, int z, int ground,
                             const PlanetEcologyProfile *profile, uint32_t hash)
{
    int base = ground + 1;
    int trunkHeight = 3 + (int)(hash % 3u);
    for (int y = base; y < base + trunkHeight; y++) {
        EcologySet(chunk, x, y, z, profile->primaryBlock);
    }
    for (int ox = -2; ox <= 2; ox++) {
        for (int oz = -2; oz <= 2; oz++) {
            int distance = abs(ox) + abs(oz);
            if (distance > 3) continue;
            EcologySet(chunk, x + ox, base + trunkHeight - 1, z + oz,
                       profile->accentBlock);
            if (distance < 2) {
                EcologySet(chunk, x + ox, base + trunkHeight, z + oz,
                           profile->accentBlock);
            }
        }
    }
    EcologySet(chunk, x, base + trunkHeight + 1, z, BLOCK_GLOWSTONE);
}

static void PlaceCrystal(Chunk *chunk, int x, int z, int ground,
                         const PlanetEcologyProfile *profile, uint32_t hash)
{
    int base = ground + 1;
    int height = 2 + (int)(hash % 4u);
    for (int y = base; y < base + height; y++) {
        EcologySet(chunk, x, y, z,
                   y == base + height - 1 ? profile->accentBlock : profile->primaryBlock);
    }
    if (hash & 1u) {
        EcologySet(chunk, x - 1, base + 1, z, profile->accentBlock);
        EcologySet(chunk, x + 1, base, z, profile->accentBlock);
    } else {
        EcologySet(chunk, x, base + 1, z - 1, profile->accentBlock);
        EcologySet(chunk, x, base, z + 1, profile->accentBlock);
    }
}

static void PlaceSpore(Chunk *chunk, int x, int z, int ground,
                       const PlanetEcologyProfile *profile, uint32_t hash)
{
    int base = ground + 1;
    int stemHeight = 2 + (int)(hash % 2u);
    for (int y = base; y < base + stemHeight; y++) {
        EcologySet(chunk, x, y, z, BLOCK_MUSHROOM);
    }
    for (int ox = -1; ox <= 1; ox++) {
        for (int oz = -1; oz <= 1; oz++) {
            if (abs(ox) + abs(oz) <= 1) {
                EcologySet(chunk, x + ox, base + stemHeight, z + oz,
                           profile->accentBlock);
            }
        }
    }
    EcologySet(chunk, x, base + stemHeight + 1, z, profile->primaryBlock);
}

static void PlaceThermalVent(Chunk *chunk, int x, int z, int ground,
                             const PlanetEcologyProfile *profile, uint32_t hash)
{
    int base = ground + 1;
    int height = 2 + (int)(hash % 3u);
    for (int y = base; y < base + height; y++) EcologySet(chunk, x, y, z, BLOCK_NETHERRACK);
    EcologySet(chunk, x - 1, base, z, BLOCK_OBSIDIAN);
    EcologySet(chunk, x + 1, base, z, BLOCK_OBSIDIAN);
    EcologySet(chunk, x, base + height, z, BLOCK_GLOWSTONE);
    EcologySet(chunk, x, base + height, z + 1, profile->accentBlock);
}

static void PlacePlanetFlora(Chunk *chunk, int x, int z,
                             const PlanetEcologyProfile *profile, uint32_t hash)
{
    PlanetBiome biome = PlanetBiomeAt(x, z);
    if (biome == PLANET_BIOME_OCEAN || biome == PLANET_BIOME_LAVA_SEA ||
        biome == PLANET_BIOME_STORM_BANDS) return;
    int ground = PlanetTerrainHeight(x, z);
    if (ground > WORLD_HEIGHT - 7) return;

    PlanetFloraArchetype type = profile->flora;
    if (type == PLANET_FLORA_ALIEN_CANOPY && biome != PLANET_BIOME_FOREST &&
        biome != PLANET_BIOME_PLAINS && biome != PLANET_BIOME_OASIS) {
        type = PLANET_FLORA_SPORE;
    }
    RegisterFloraStructure(chunk, x, z, ground, profile, type, hash);
    switch (type) {
    case PLANET_FLORA_ALIEN_CANOPY:
        PlaceAlienCanopy(chunk, x, z, ground, profile, hash);
        break;
    case PLANET_FLORA_CRYSTAL:
        PlaceCrystal(chunk, x, z, ground, profile, hash);
        break;
    case PLANET_FLORA_SPORE:
        PlaceSpore(chunk, x, z, ground, profile, hash);
        break;
    case PLANET_FLORA_THERMAL_VENT:
    default:
        PlaceThermalVent(chunk, x, z, ground, profile, hash);
        break;
    }
}

void PlanetEcologyApplyToChunk(Chunk *chunk, int chunkX, int chunkZ)
{
    if (!PlanetWorldIsActive()) return;
    PlanetEcologyProfile profile = PlanetEcologyCurrent();
    if (profile.biomass == PLANET_BIOMASS_BARREN ||
        profile.floraDensity <= 0.0f) return;
    int divisor = 920 - (int)(profile.floraDensity * 760.0f);
    if (divisor < 140) divisor = 140;
    int startX = chunkX * CHUNK_SIZE - PLANET_FLORA_STRUCTURE_RADIUS;
    int startZ = chunkZ * CHUNK_SIZE - PLANET_FLORA_STRUCTURE_RADIUS;
    int endX = chunkX * CHUNK_SIZE + CHUNK_SIZE + PLANET_FLORA_STRUCTURE_RADIUS;
    int endZ = chunkZ * CHUNK_SIZE + CHUNK_SIZE + PLANET_FLORA_STRUCTURE_RADIUS;
    for (int x = startX; x < endX; x++) {
        for (int z = startZ; z < endZ; z++) {
            uint32_t hash = EcologyHash(x, z, 0x314159u);
            if (hash % (uint32_t)divisor != 0u) continue;
            PlanetEcologySuitability local = EcologyStaticSuitabilityForProfile(
                x, z, &profile);
            uint32_t localHash = EcologyMix(hash ^ 0x6d2b79f5u);
            float localRoll = (float)(localHash & 0x00ffffffu) / 16777215.0f;
            if (localRoll > local.carryingCapacity) continue;
            PlacePlanetFlora(chunk, x, z, &profile, hash);
        }
    }
}
