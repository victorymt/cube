#include "ecology/ecology_population_internal.h"
#include "world/terrain.h"
#include "world/world.h"

#include <math.h>

#define ECOLOGY_DISTURBANCE_CACHE_SIZE 256u
#define ECOLOGY_NUTRIENT_BIOMASS_SCALE 96.0
#define ECOLOGY_NUTRIENT_RAINFALL_MM_PER_RATE_DAY 5.0

#if defined(__GNUC__) || defined(__clang__)
#define ECOLOGY_THREAD_LOCAL __thread
#else
#define ECOLOGY_THREAD_LOCAL
#endif

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

static ECOLOGY_THREAD_LOCAL EcologyDisturbanceCacheEntry
    ecologyDisturbanceCache[ECOLOGY_DISTURBANCE_CACHE_SIZE] = { 0 };

static double EcologyNutrientNonNegative(double value)
{
    return isfinite(value) && value > 0.0 ? value : 0.0;
}

/*
 * Keep the nutrient model in mass units while the public population model
 * remains a normalized [0, 1] density.  The fixed scale makes the adapter
 * deterministic and gives old population records a stable upgrade path.
 */
EcologyNutrientState EcologyNutrientInitializeState(
    const PlanetPopulationInput *input,
    const PlanetRegionalPopulation *population)
{
    EcologyNutrientState state = { 0 };
    if (!input || !population) return state;
    double flora = EcologyNutrientNonNegative(
        (double)EcologyClamp(population->floraDensity));
    double capacity = EcologyNutrientNonNegative(
        (double)EcologyClamp(input->floraCapacity));
    double biomass = flora * ECOLOGY_NUTRIENT_BIOMASS_SCALE;
    double potential = fmax(flora, capacity) *
                       ECOLOGY_NUTRIENT_BIOMASS_SCALE;
    state.mineralNitrogen = 0.80 + potential * 0.020;
    state.mineralPhosphorus = 0.30 + potential * 0.006;
    state.soilOrganicNitrogen = potential * 0.035;
    state.soilOrganicPhosphorus = potential * 0.010;
    state.plantBiomass = biomass;
    state.plantNitrogen = biomass * 0.030;
    state.plantPhosphorus = biomass * 0.004;
    state.litterBiomass = biomass * 0.12;
    state.litterNitrogen = state.litterBiomass * 0.030;
    state.litterPhosphorus = state.litterBiomass * 0.004;
    state.seedBank = biomass * 0.05;
    state.meanPlantAgeDays = 0.0;
    return state;
}

float EcologyNutrientDensity(const EcologyNutrientState *state)
{
    if (!state || !EcologyNutrientStateValid(state)) return 0.0f;
    return EcologyClamp((float)(state->plantBiomass /
                                ECOLOGY_NUTRIENT_BIOMASS_SCALE));
}

void EcologyNutrientApplyFloraStress(
    EcologyNutrientState *state, float floraStress)
{
    if (!state || !EcologyNutrientStateValid(state)) return;
    float stress = EcologyClamp(floraStress);
    if (stress <= 0.0f) return;
    double factor = 1.0 - (double)stress * 0.78;
    double removedBiomass = state->plantBiomass * (1.0 - factor);
    double removedNitrogen = state->plantNitrogen * (1.0 - factor);
    double removedPhosphorus = state->plantPhosphorus * (1.0 - factor);
    state->plantBiomass -= removedBiomass;
    state->plantNitrogen -= removedNitrogen;
    state->plantPhosphorus -= removedPhosphorus;
    state->litterBiomass += removedBiomass;
    state->litterNitrogen += removedNitrogen;
    state->litterPhosphorus += removedPhosphorus;
}

EcologyNutrientEnvironment EcologyNutrientEnvironmentAt(
    const PlanetLocalEcology *regional,
    const PlanetRegionalPopulation *population,
    float floraStress)
{
    EcologyNutrientEnvironment environment =
        EcologyNutrientDefaultEnvironment();
    if (!regional || !population) return environment;
    float floraCapacity = EcologyClamp(regional->suitability.floraCapacity);
    float floraTemperature = floraCapacity > 0.0001f
        ? EcologyClamp(regional->suitability.temperatureScore) : 0.0f;
    float moisture = EcologyClamp(
        regional->environment.soilMoisture * 0.68f +
        regional->suitability.waterScore * 0.32f);
    float competition = EcologyClamp(
        EcologyClamp(population->faunaDensity) * 0.36f +
        EcologyClamp(population->floraDensity) * 0.10f +
        EcologyClamp(floraStress) * 0.24f);
    environment.temperatureSuitability = floraTemperature;
    environment.moistureSuitability = moisture;
    environment.competition = competition;
    /* precipitationRate is a normalized daily rate; map it to mm/day. */
    environment.rainfallMm =
        EcologyClamp(regional->environment.precipitationRate) *
        ECOLOGY_NUTRIENT_RAINFALL_MM_PER_RATE_DAY;
    return environment;
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

float EcologyRegionalDisturbance(
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

