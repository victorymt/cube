#include "ecology/ecology_internal.h"

#include "space/space.h"
#include "world/terrain.h"
#include "world/weather.h"
#include "world/world.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define ECOLOGY_THREAD_LOCAL __thread
#else
#define ECOLOGY_THREAD_LOCAL
#endif

typedef struct EcologyProfileCache {
    bool valid;
    bool homeWorld;
    bool darkSide;
    uint32_t worldSeed;
    uint32_t generation;
    PlanetProfile planet;
    PlanetEcologyProfile ecology;
} EcologyProfileCache;

static ECOLOGY_THREAD_LOCAL EcologyProfileCache ecologyProfileCache = { 0 };

static float EcologyProfileFiniteNonNegative(float value)
{
    if (!isfinite(value) || value < 0.0f) return 0.0f;
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

static PlanetLocalEnvironment EcologyHomeEnvironmentAt(
    int x, int z, float daylight, float precipitationRate,
    float currentStorm, bool dynamic)
{
    PlanetLocalEnvironment environment = { 0 };
    int height = TerrainHeight(x, z, WorldTerrainMode());
    int east = TerrainHeight(x + 2, z, WorldTerrainMode());
    int west = TerrainHeight(x - 2, z, WorldTerrainMode());
    int south = TerrainHeight(x, z + 2, WorldTerrainMode());
    int north = TerrainHeight(x, z - 2, WorldTerrainMode());
    int seaLevel = TerrainSeaLevel(WorldTerrainMode());
    Biome biome = BiomeAt(x, z);
    float temperature = 288.0f;
    float moisture = 0.58f;
    float biomeSupport = 0.86f;
    float meanPrecipitation = 0.48f;
    switch (biome) {
    case BIOME_FOREST:
        temperature = 285.0f;
        moisture = 0.82f;
        biomeSupport = 1.0f;
        meanPrecipitation = 0.68f;
        break;
    case BIOME_DESERT:
        temperature = 306.0f;
        moisture = 0.12f;
        biomeSupport = 0.34f;
        meanPrecipitation = 0.12f;
        break;
    case BIOME_SNOW:
        temperature = 263.0f;
        moisture = 0.46f;
        biomeSupport = 0.48f;
        meanPrecipitation = 0.42f;
        break;
    case BIOME_MOUNTAIN:
        temperature = 276.0f;
        moisture = 0.42f;
        biomeSupport = 0.56f;
        meanPrecipitation = 0.46f;
        break;
    case BIOME_PLAINS:
    default:
        break;
    }

    float maxSlope = fmaxf(fabsf((float)(east - west)),
                           fabsf((float)(south - north))) * 0.25f;
    environment.slope = EcologyClamp(maxSlope / 3.5f);
    environment.elevation = EcologyClamp(
        ((float)height - (float)(seaLevel >= 0 ? seaLevel : 8) + 18.0f) /
        90.0f);
    float surrounding = ((float)east + (float)west + (float)south +
                         (float)north) * 0.25f;
    environment.shelter = EcologyClamp(
        0.50f + (surrounding - (float)height) / 7.0f);
    float lapseCooling = fmaxf(
        (float)height - (float)(seaLevel >= 0 ? seaLevel + 14 : 22), 0.0f) *
        0.16f;
    environment.meanTemperatureK = EcologyProfileFiniteNonNegative(
        temperature - lapseCooling);
    float dayTemperature = dynamic ? (daylight - 0.5f) * 8.0f : 0.0f;
    environment.currentTemperatureK = EcologyProfileFiniteNonNegative(
        environment.meanTemperatureK + dayTemperature -
        EcologyClamp(precipitationRate) * 2.0f);
    environment.seasonalAmplitudeK = 10.0f;
    float drainage = 1.0f - environment.slope * 0.62f;
    environment.soilMoisture = EcologyClamp(moisture * drainage);
    environment.meanPrecipitation = meanPrecipitation;
    environment.precipitationRate = dynamic ?
        EcologyClamp(precipitationRate) : meanPrecipitation;
    bool submerged = seaLevel >= 0 && height < seaLevel;
    environment.liquidWaterAccess = submerged ? 1.0f : EcologyClamp(
        environment.soilMoisture * 0.62f + meanPrecipitation * 0.22f);
    environment.meanUsableLight = 0.74f;
    environment.currentUsableLight = dynamic ? EcologyClamp(daylight) :
                                               environment.meanUsableLight;
    environment.stormExposure = EcologyClamp(
        0.34f * (1.0f - environment.shelter * 0.30f));
    environment.currentStorm = dynamic ? EcologyClamp(currentStorm) : 0.0f;
    environment.biomeSupport = submerged ? 0.72f : biomeSupport;
    return environment;
}

PlanetEcologyTraits EcologyTraitsForProfile(
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

PlanetLocalEnvironment EcologyEnvironmentAt(
    int x, int z, double simulationTime, float daylight,
    float precipitationRate, float currentStorm, bool dynamic,
    const PlanetEcologyProfile *ecology)
{
    PlanetLocalEnvironment environment = { 0 };
    if (!EcologyWorldIsActive()) return environment;

    if (!PlanetWorldIsActive()) {
        return EcologyHomeEnvironmentAt(
            x, z, daylight, precipitationRate, currentStorm, dynamic);
    }

    const PlanetProfile *planet = PlanetWorldProfile();
    if (!planet) return environment;
    simulationTime = isfinite(simulationTime) && simulationTime >= 0.0
        ? simulationTime : 0.0;
    daylight = EcologyClamp(daylight);
    precipitationRate = EcologyClamp(precipitationRate);
    currentStorm = EcologyClamp(currentStorm);
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
    environment.meanTemperatureK = EcologyProfileFiniteNonNegative(
        surface.meanTemperature - lapseCooling);
    environment.currentTemperatureK = EcologyProfileFiniteNonNegative(
        surface.temperature - lapseCooling);
    environment.seasonalAmplitudeK = EcologyProfileFiniteNonNegative(
        surface.seasonalAmplitude);

    float drainage = 1.0f - environment.slope * 0.62f;
    environment.soilMoisture = EcologyClamp(
        EcologyClamp(surface.moisture) *
        (1.0f - EcologyClamp(surface.iceCoverage) * 0.76f) * drainage);
    float windAngle = isfinite(planet->prevailingWindAngle)
        ? planet->prevailingWindAngle : 0.0f;
    int upwindX = x - (int)lroundf(cosf(windAngle) * 8.0f);
    int upwindZ = z - (int)lroundf(sinf(windAngle) * 8.0f);
    int upwindHeight = PlanetTerrainHeight(upwindX, upwindZ);
    float orographicLift = EcologyClamp(
        0.50f + ((float)height - (float)upwindHeight) / 10.0f);
    environment.meanPrecipitation = EcologyClamp(
        EcologyClamp(surface.moisture) *
        (0.36f + EcologyClamp(planet->cloudCoverage) * 0.64f) *
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

    float windStrength = EcologyClamp(planet->windStrength);
    float cloudCoverage = EcologyClamp(planet->cloudCoverage);
    float oceanCoverage = EcologyClamp(planet->oceanCoverage);
    float roughness = EcologyClamp((planet->terrainRoughness - 0.35f) / 1.20f);
    environment.stormExposure = EcologyClamp(
        windStrength * 0.55f + cloudCoverage * 0.20f +
        oceanCoverage * 0.15f + roughness * 0.10f);
    environment.stormExposure *= 1.0f - environment.shelter * 0.35f;
    environment.precipitationRate = dynamic
        ? EcologyClamp(precipitationRate) : environment.meanPrecipitation;
    environment.currentStorm = dynamic ? EcologyClamp(currentStorm) : 0.0f;
    SpaceRemnantEnvironment remnant = PlanetWorldRemnantEnvironment();
    float atmosphericShield = 0.28f +
        EcologyClamp(planet->atmosphereDensity) * 0.52f +
        environment.elevation * 0.08f;
    environment.radiationExposure = EcologyClamp(
        remnant.radiationHazard * (1.0f - EcologyClamp(atmosphericShield)));
    environment.ejectaExposure = EcologyClamp(
        remnant.ejectaDensity *
        (0.72f + roughness * 0.18f) *
        (1.0f - environment.shelter * 0.22f));
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

static float EcologyClimateLife(const PlanetProfile *planet,
                                float temperature, float pressure,
                                float water, float ice, float wind)
{
    float temperatureComfort = 1.0f -
        EcologyClamp(fabsf(temperature - 288.0f) / 150.0f);
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
    pressureSupport *= 1.0f - EcologyClamp(
        (pressure - 3.0f) / 7.0f) * 0.55f;

    float thermalSupport = 0.12f + temperatureComfort * 0.88f;
    float atmosphereAvailability = 0.08f + atmosphereSupport * 0.92f;
    float pressureAvailability = 0.08f + pressureSupport * 0.92f;
    float atmosphereClimate = sqrtf(atmosphereAvailability *
                                    pressureAvailability);
    float waterAvailability = EcologyClamp(
        0.08f + water * 0.78f + ice * 0.35f);
    float limitingSupport = cbrtf(thermalSupport * atmosphereClimate *
                                  waterAvailability);
    float climateStability = (1.0f - ice * 0.25f) *
                             (1.0f - wind * 0.25f);
    float life = limitingSupport * climateStability;

    if (planet->style == SOLAR_STYLE_TEMPERATE) life *= 1.24f;
    if (planet->style == SOLAR_STYLE_ICE) life *= 0.86f;
    if (planet->style == SOLAR_STYLE_DESERT) life *= 0.82f;
    if (planet->style == SOLAR_STYLE_LAVA) life *= 0.34f;
    if (planet->style == SOLAR_STYLE_CRATER) life *= 0.42f;
    if (planet->style == SOLAR_STYLE_GAS || !planet->hasSolidSurface) {
        life = 0.0f;
    }
    return EcologyClamp(life);
}

static float EcologyApplyLifeHistory(PlanetEcologyProfile *result,
                                     const PlanetProfile *planet, float life)
{
    PlanetLifeHistory history = PlanetLifeHistoryDerive(
        planet->seed, planet->ageGyr, life, planet->hasSolidSurface);
    result->planetAgeGyr = history.planetAgeGyr;
    result->lifeOriginProbability = history.originProbability;
    result->complexLifeProbability = history.complexLifeProbability;
    result->evolutionProgress = history.evolutionProgress;
    result->lifeOriginated = history.lifeOriginated;
    result->hasComplexLife = history.hasComplexLife;
    return PlanetLifeHistoryDensity(&history, life);
}

static PlanetChemistry EcologyChemistryFor(float temperature,
                                           uint32_t seedHash)
{
    float chemistryRoll = (float)(seedHash & 0xffffu) / 65535.0f;
    if (temperature > 365.0f) {
        return chemistryRoll < 0.64f ?
            PLANET_CHEMISTRY_SULFUR : PLANET_CHEMISTRY_SILICON;
    }
    if (chemistryRoll < 0.28f) return PLANET_CHEMISTRY_SILICON;
    if (chemistryRoll < 0.48f) return PLANET_CHEMISTRY_SULFUR;
    return PLANET_CHEMISTRY_CARBON;
}

static PlanetFloraArchetype EcologyFloraForStyle(SolarBodyStyle style)
{
    switch (style) {
    case SOLAR_STYLE_TEMPERATE: return PLANET_FLORA_ALIEN_CANOPY;
    case SOLAR_STYLE_ICE:
    case SOLAR_STYLE_DESERT:
    case SOLAR_STYLE_CRATER: return PLANET_FLORA_CRYSTAL;
    case SOLAR_STYLE_LAVA: return PLANET_FLORA_THERMAL_VENT;
    case SOLAR_STYLE_GAS:
    default: return PLANET_FLORA_SPORE;
    }
}

static void EcologyApplyBiomass(PlanetEcologyProfile *result,
                                const PlanetProfile *planet,
                                float temperature, float atmosphere,
                                float water, uint32_t seedHash,
                                bool darkSide)
{
    result->lifeDensity = EcologyClamp(result->lifeDensity);
    if (!result->lifeOriginated || !planet->hasSolidSurface ||
        result->lifeDensity < 0.055f) {
        result->biomass = PLANET_BIOMASS_BARREN;
    } else if (!result->hasComplexLife) {
        result->biomass = PLANET_BIOMASS_MICROBIAL;
    } else if (temperature > 360.0f && atmosphere > 0.16f) {
        result->biomass = PLANET_BIOMASS_CRYSTALLINE;
    } else if (darkSide && result->lifeDensity >= 0.12f) {
        result->biomass = PLANET_BIOMASS_ANOMALOUS;
    } else if (result->lifeDensity < 0.20f ||
               planet->atmosphereType == PLANET_ATMOSPHERE_NONE) {
        result->biomass = PLANET_BIOMASS_MICROBIAL;
    } else if (result->lifeDensity > 0.60f && water > 0.25f) {
        result->biomass = PLANET_BIOMASS_LUSH;
    } else if ((seedHash % 5u) == 0u || water < 0.10f) {
        result->biomass = PLANET_BIOMASS_FUNGAL;
    } else {
        result->biomass = PLANET_BIOMASS_LUSH;
    }

    result->floraDensity = EcologyClamp(result->lifeDensity * 0.92f);
    result->faunaDensity = EcologyClamp((result->lifeDensity - 0.14f) * 1.12f);
    switch (result->biomass) {
    case PLANET_BIOMASS_BARREN:
        result->floraDensity = 0.0f;
        result->faunaDensity = 0.0f;
        break;
    case PLANET_BIOMASS_MICROBIAL:
        result->floraDensity = EcologyClamp(result->lifeDensity * 0.18f);
        result->faunaDensity = 0.0f;
        break;
    case PLANET_BIOMASS_FUNGAL:
        result->flora = PLANET_FLORA_SPORE;
        result->floraDensity = EcologyClamp(0.12f + result->lifeDensity * 0.66f);
        break;
    case PLANET_BIOMASS_CRYSTALLINE:
        result->flora = PLANET_FLORA_CRYSTAL;
        result->faunaDensity = EcologyClamp(result->lifeDensity * 0.48f);
        break;
    case PLANET_BIOMASS_ANOMALOUS:
        result->flora = PLANET_FLORA_SPORE;
        result->faunaDensity = EcologyClamp(result->lifeDensity * 0.92f);
        break;
    case PLANET_BIOMASS_LUSH:
    default:
        result->flora = PLANET_FLORA_ALIEN_CANOPY;
        break;
    }
}

static void EcologyApplyMorphology(PlanetEcologyProfile *result,
                                   const PlanetProfile *planet,
                                   float pressure, float wind,
                                   bool darkSide, uint32_t seedHash)
{
    float gravity = EcologyClamp(
        (planet->surfaceGravity - 0.35f) / 1.45f) * 1.45f + 0.35f;
    result->organismScale = EcologyClamp(1.10f / sqrtf(gravity));
    if (result->organismScale < 0.48f) result->organismScale = 0.48f;
    if (result->organismScale > 2.20f) result->organismScale = 2.20f;
    result->bodyArmor = EcologyClamp((gravity - 0.76f) / 0.88f + wind * 0.20f);
    result->supportsFlight = result->hasComplexLife && planet->hasSolidSurface &&
                           pressure >= 0.35f && wind < 0.88f &&
                           (planet->atmosphereDensity >= 0.72f || gravity <= 0.68f);
    result->darkSideColony = result->hasComplexLife && planet->hasSolidSurface &&
                            darkSide && result->lifeDensity >= 0.12f;
    if (result->darkSideColony) {
        result->bodyPlan = PLANET_BODY_COLONY;
        result->niche = PLANET_NICHE_BIOLUMINESCENT_COLONY;
    } else if (result->biomass == PLANET_BIOMASS_CRYSTALLINE) {
        result->bodyPlan = PLANET_BODY_HEXAPOD;
        result->niche = PLANET_NICHE_CRYSTAL_GRAZER;
    } else if (result->supportsFlight) {
        result->bodyPlan = PLANET_BODY_FLOATING;
        result->niche = PLANET_NICHE_FILTER_FEEDER;
    } else if (result->biomass == PLANET_BIOMASS_MICROBIAL) {
        result->bodyPlan = PLANET_BODY_SERPENTINE;
        result->niche = PLANET_NICHE_MICROBIAL;
    } else if (gravity < 0.70f) {
        result->bodyPlan = PLANET_BODY_BIPED;
        result->niche = PLANET_NICHE_GRAZER;
    } else if (gravity > 1.20f) {
        result->bodyPlan = PLANET_BODY_HEXAPOD;
        result->niche = PLANET_NICHE_GRAZER;
    } else {
        switch (seedHash % 3u) {
        case 0: result->bodyPlan = PLANET_BODY_QUADRUPED; break;
        case 1: result->bodyPlan = PLANET_BODY_BIPED; break;
        default: result->bodyPlan = PLANET_BODY_SERPENTINE; break;
        }
        result->niche = result->biomass == PLANET_BIOMASS_FUNGAL ?
                       PLANET_NICHE_DECOMPOSER : PLANET_NICHE_GRAZER;
    }
    result->limbCount = result->bodyPlan == PLANET_BODY_HEXAPOD ? 6 :
                       result->bodyPlan == PLANET_BODY_BIPED ? 2 :
                       result->bodyPlan == PLANET_BODY_QUADRUPED ? 4 : 0;
    float speedScale = 0.86f / sqrtf(gravity);
    speedScale *= 1.0f - wind * 0.28f;
    if (result->bodyPlan == PLANET_BODY_FLOATING) speedScale *= 0.85f;
    if (result->bodyPlan == PLANET_BODY_COLONY ||
        result->biomass == PLANET_BIOMASS_CRYSTALLINE) {
        speedScale *= 0.34f;
    }
    result->movementSpeed = EcologyClamp(speedScale * 0.70f);
    if (result->movementSpeed < 0.18f && result->faunaDensity > 0.0f) {
        result->movementSpeed = 0.18f;
    }
    result->temperament = EcologyClamp(
        (float)((seedHash >> 17) & 255u) / 255.0f * 0.72f +
        (result->biomass == PLANET_BIOMASS_ANOMALOUS ? 0.22f : 0.0f));
}

static void EcologyApplyPalette(PlanetEcologyProfile *result,
                                uint32_t seedHash)
{
    int primary = EcologyPaletteIndex(result->chemistry, seedHash, false);
    int accent = EcologyPaletteIndex(result->chemistry, seedHash, true);
    if (accent == primary) accent = (accent + 37) & 255;
    result->primaryBlock = (BlockType)(BLOCK_COLOR_START + primary);
    result->accentBlock = (BlockType)(BLOCK_COLOR_START + accent);
}

PlanetEcologyProfile PlanetEcologyProfileForPlanet(
    const struct PlanetProfile *planetValue, uint32_t worldSeed,
    bool darkSide)
{
    PlanetEcologyProfile result = { 0 };
    const PlanetProfile *planet = planetValue;
    if (!planet) return result;

    float temperature = EcologyProfileFiniteNonNegative(
        planet->equilibriumTempK);
    float pressure = EcologyProfileFiniteNonNegative(
        planet->surfacePressureAtm);
    float atmosphere = EcologyClamp(planet->atmosphereDensity);
    float water = EcologyClamp(planet->oceanCoverage);
    float ice = EcologyClamp(planet->iceCoverage);
    float wind = EcologyClamp(planet->windStrength);
    float life = EcologyClimateLife(
        planet, temperature, pressure, water, ice, wind);

    uint32_t seedHash = EcologyMix(worldSeed ^ 0x72a31u);
    life = EcologyApplyLifeHistory(&result, planet, life);
    result.chemistry = EcologyChemistryFor(temperature, seedHash);
    result.flora = EcologyFloraForStyle(planet->style);

    result.lifeDensity = life;
    EcologyApplyBiomass(
        &result, planet, temperature, atmosphere, water, seedHash, darkSide);

    EcologyApplyMorphology(
        &result, planet, pressure, wind, darkSide, seedHash);
    EcologyApplyPalette(&result, seedHash);

    return result;
}

PlanetEcologyProfile PlanetEcologyCurrent(void)
{
    PlanetEcologyProfile result = { 0 };
    bool homeWorld = HomeWorldSurfaceIsActive();
    if (!PlanetWorldIsActive() && !homeWorld) return result;

    const PlanetProfile *planet = homeWorld ? NULL : PlanetWorldProfile();
    uint32_t worldSeed = homeWorld ? WorldGetSeed() : PlanetWorldSeed();
    bool darkSide = homeWorld ? false : PlanetWorldIsDarkSide();
    if (ecologyProfileCache.valid &&
        ecologyProfileCache.homeWorld == homeWorld &&
        ecologyProfileCache.worldSeed == worldSeed &&
        ecologyProfileCache.darkSide == darkSide &&
        (homeWorld ||
         memcmp(&ecologyProfileCache.planet, planet, sizeof(*planet)) == 0)) {
        return ecologyProfileCache.ecology;
    }

    if (homeWorld) {
        result = (PlanetEcologyProfile){
            .flora = PLANET_FLORA_ALIEN_CANOPY,
            .biomass = PLANET_BIOMASS_LUSH,
            .chemistry = PLANET_CHEMISTRY_CARBON,
            .bodyPlan = PLANET_BODY_QUADRUPED,
            .niche = PLANET_NICHE_GRAZER,
            .floraDensity = 0.90f,
            .faunaDensity = 0.76f,
            .lifeDensity = 0.94f,
            .planetAgeGyr = 4.54f,
            .lifeOriginProbability = 1.0f,
            .complexLifeProbability = 1.0f,
            .evolutionProgress = 1.0f,
            .organismScale = 1.0f,
            .bodyArmor = 0.12f,
            .movementSpeed = 0.88f,
            .temperament = 0.24f,
            .limbCount = 4,
            .lifeOriginated = true,
            .hasComplexLife = true,
            .supportsFlight = true,
            .darkSideColony = false,
            .primaryBlock = BLOCK_GRASS,
            .accentBlock = BLOCK_DIRT
        };
    } else {
        result = PlanetEcologyProfileForPlanet(planet, worldSeed, darkSide);
    }

    ecologyProfileCache.valid = true;
    ecologyProfileCache.homeWorld = homeWorld;
    ecologyProfileCache.darkSide = darkSide;
    ecologyProfileCache.worldSeed = worldSeed;
    ecologyProfileCache.generation++;
    if (ecologyProfileCache.generation == 0u) {
        ecologyProfileCache.generation = 1u;
    }
    if (planet) memcpy(&ecologyProfileCache.planet, planet, sizeof(*planet));
    else memset(&ecologyProfileCache.planet, 0, sizeof(ecologyProfileCache.planet));
    ecologyProfileCache.ecology = result;
    return result;
}

float PlanetEcologyFaunaDensity(void)
{
    return PlanetEcologyCurrent().faunaDensity;
}

PlanetEcologySuitability EcologyStaticSuitabilityForProfile(
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

PlanetLocalEcology EcologyDynamicLocalAt(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile)
{
    PlanetLocalEcology local = { 0 };
    simulationTime = isfinite(simulationTime) && simulationTime >= 0.0
        ? simulationTime : 0.0;
    daylight = EcologyClamp(daylight);
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

uint32_t EcologyProfileGeneration(void)
{
    return ecologyProfileCache.generation;
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
