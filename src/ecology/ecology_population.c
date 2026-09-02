#include "ecology/ecology_internal.h"
#include "ecology/ecology_nutrient_model.h"

#include "world/terrain.h"
#include "world/weather.h"
#include "world/world.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

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

typedef struct EcologyPopulationRecord {
    bool valid;
    uint32_t surfaceId;
    int regionX;
    int regionZ;
    double lastUpdateTime;
    uint64_t lastAccess;
    PlanetRegionalPopulation population;
    PlanetPopulationMigrationState migration;
    PlanetEvolutionRegion evolution;
    EcologyNutrientState nutrients;
    uint8_t nutrientInitialized;
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
    float aquaticSuitability;
    PlanetEvolutionRegion evolution;
    EcologyNutrientState nutrients;
    EcologyNutrientEnvironment nutrientEnvironment;
    uint8_t nutrientInitialized;
} EcologyPopulationStepState;

typedef struct EcologyPopulationStepContext {
    uint32_t surfaceId;
    double stepStart;
    double stepEnd;
    double elapsedTime;
    float daylight;
    const PlanetEcologyProfile *profile;
    int originX;
    int originZ;
} EcologyPopulationStepContext;

typedef struct EcologyPopulationStepBuffers {
    EcologyPopulationStepState states[ECOLOGY_POPULATION_MAX_REGIONS];
    double floraDelta[ECOLOGY_POPULATION_MAX_REGIONS];
    double faunaDelta[ECOLOGY_POPULATION_MAX_REGIONS];
    double floraFlowX[ECOLOGY_POPULATION_MAX_REGIONS];
    double floraFlowZ[ECOLOGY_POPULATION_MAX_REGIONS];
    double faunaFlowX[ECOLOGY_POPULATION_MAX_REGIONS];
    double faunaFlowZ[ECOLOGY_POPULATION_MAX_REGIONS];
} EcologyPopulationStepBuffers;

static ECOLOGY_THREAD_LOCAL EcologyDisturbanceCacheEntry
    ecologyDisturbanceCache[ECOLOGY_DISTURBANCE_CACHE_SIZE] = { 0 };
static EcologyPopulationRecord
    ecologyPopulationRecords[ECOLOGY_POPULATION_MAX_REGIONS] = { 0 };
static uint64_t ecologyPopulationAccessSerial = 0u;
static uint32_t ecologyPopulationEpoch = 1u;
static pthread_mutex_t ecologyPopulationMutex = PTHREAD_MUTEX_INITIALIZER;

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

static double EcologyNutrientNonNegative(double value)
{
    return isfinite(value) && value > 0.0 ? value : 0.0;
}

/*
 * Keep the nutrient model in mass units while the public population model
 * remains a normalized [0, 1] density.  The fixed scale makes the adapter
 * deterministic and gives old population records a stable upgrade path.
 */
static EcologyNutrientState EcologyNutrientInitializeState(
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

static float EcologyNutrientDensity(const EcologyNutrientState *state)
{
    if (!state || !EcologyNutrientStateValid(state)) return 0.0f;
    return EcologyClamp((float)(state->plantBiomass /
                                ECOLOGY_NUTRIENT_BIOMASS_SCALE));
}

static void EcologyNutrientApplyFloraStress(
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

static EcologyNutrientEnvironment EcologyNutrientEnvironmentAt(
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

static void EcologyEvolutionRefreshTrophic(PlanetEvolutionRegion *evolution)
{
    evolution->herbivoreDensity = 0.0f;
    evolution->omnivoreDensity = 0.0f;
    evolution->carnivoreDensity = 0.0f;
    evolution->lineageCount = 0u;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        PlanetEvolutionLineage *lineage = &evolution->lineages[index];
        if (!lineage->active) continue;
        if (lineage->density <= 0.0f) {
            lineage->active = 0u;
            continue;
        }
        evolution->lineageCount++;
        if (lineage->dietMean < 0.36f) {
            evolution->herbivoreDensity += lineage->density;
        } else if (lineage->dietMean < 0.64f) {
            evolution->omnivoreDensity += lineage->density;
        } else {
            evolution->carnivoreDensity += lineage->density;
        }
    }
    evolution->herbivoreDensity = EcologyClamp(evolution->herbivoreDensity);
    evolution->omnivoreDensity = EcologyClamp(evolution->omnivoreDensity);
    evolution->carnivoreDensity = EcologyClamp(evolution->carnivoreDensity);
}

static void EcologyEvolutionScaleToFauna(
    PlanetEvolutionRegion *evolution, float faunaDensity)
{
    if (!evolution || !isfinite(faunaDensity) || faunaDensity < 0.0f) return;
    float lineageDensity = 0.0f;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        if (evolution->lineages[index].active) {
            lineageDensity += evolution->lineages[index].density;
        }
    }
    float densityScale = lineageDensity > 0.0001f
        ? faunaDensity / lineageDensity : 1.0f;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        PlanetEvolutionLineage *lineage = &evolution->lineages[index];
        if (!lineage->active) continue;
        lineage->density = EcologyClamp(lineage->density * densityScale);
    }
    EcologyEvolutionRefreshTrophic(evolution);
}

static uint32_t EcologyEvolutionNonzeroId(uint32_t value, uint32_t fallback);

/*
 * A migration edge carries more than biomass: a small, deterministic sample
 * of the source and receiver genomes is recombined in the receiver lineage.
 * The lineage slot and identity stay stable, so this is compatible with the
 * current bounded regional schema while making geneFlow a real genetic
 * signal instead of a post-hoc scalar annotation.
 */
static void EcologyEvolutionMixMigrationDirectional(
    const PlanetEvolutionRegion *sourceBase,
    const PlanetEvolutionRegion *receiverBase,
    PlanetEvolutionRegion *sourceOut, PlanetEvolutionRegion *receiverOut,
    float faunaFlux, uint32_t salt)
{
    if (!sourceBase || !receiverBase || !sourceOut || !receiverOut ||
        !isfinite(faunaFlux)) return;
    float magnitude = fmaxf(faunaFlux, 0.0f);
    if (magnitude <= 0.000001f) return;
    const PlanetEvolutionRegion *source = sourceBase;
    const PlanetEvolutionRegion *receiver = receiverBase;
    float sourceDensity = 0.0f;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        if (source->lineages[index].active) {
            sourceDensity += EcologyClamp(source->lineages[index].density);
        }
    }
    float mixing = EcologyClamp(magnitude /
                                fmaxf(sourceDensity, 0.0001f) * 0.42f);
    if (mixing <= 0.000001f) return;
    float sourceArchetypeDensity[3] = { 0.0f, 0.0f, 0.0f };
    float receiverArchetypeDensity[3] = { 0.0f, 0.0f, 0.0f };
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        const PlanetEvolutionLineage *lineage = &source->lineages[index];
        if (!lineage->active || lineage->archetype >= 3u) continue;
        sourceArchetypeDensity[lineage->archetype] +=
            EcologyClamp(lineage->density);
    }
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        const PlanetEvolutionLineage *lineage = &receiver->lineages[index];
        if (!lineage->active || lineage->archetype >= 3u) continue;
        receiverArchetypeDensity[lineage->archetype] +=
            EcologyClamp(lineage->density);
    }
    for (unsigned archetypeIndex = 0; archetypeIndex < 3u;
         archetypeIndex++) {
        EvolutionArchetype archetype =
            (EvolutionArchetype)archetypeIndex;
        float archetypeDensity = sourceArchetypeDensity[archetypeIndex];
        float receiverDensity = receiverArchetypeDensity[archetypeIndex];
        if (archetypeDensity <= 0.0001f || receiverDensity <= 0.0001f) {
            continue;
        }
        for (unsigned sourceIndex = 0;
             sourceIndex < PLANET_EVOLUTION_MAX_LINEAGES; sourceIndex++) {
            const PlanetEvolutionLineage *sourceLineage =
                &source->lineages[sourceIndex];
            if (!sourceLineage->active ||
                sourceLineage->archetype != (uint8_t)archetype ||
                sourceLineage->founderSeed == 0u) {
                continue;
            }
            float sourceWeight = EcologyClamp(sourceLineage->density) /
                                 archetypeDensity;
            for (unsigned receiverIndex = 0;
                 receiverIndex < PLANET_EVOLUTION_MAX_LINEAGES;
                 receiverIndex++) {
                const PlanetEvolutionLineage *receiverLineage =
                    &receiver->lineages[receiverIndex];
                if (!receiverLineage->active ||
                    receiverLineage->archetype != (uint8_t)archetype ||
                    receiverLineage->founderSeed == 0u) {
                    continue;
                }
                PlanetEvolutionLineage *sourceTarget =
                    &sourceOut->lineages[sourceIndex];
                PlanetEvolutionLineage *receiverTarget =
                    &receiverOut->lineages[receiverIndex];
                float receiverWeight = EcologyClamp(
                    EcologyClamp(receiverLineage->density) / receiverDensity);
                float pairMix = EcologyClamp(
                    mixing * sourceWeight * receiverWeight);
                if (pairMix <= 0.000001f) continue;
                CreatureGenome sourceGenome = EvolutionGenomeSeed(
                    sourceLineage->founderSeed, archetype);
                CreatureGenome receiverGenome = EvolutionGenomeSeed(
                    receiverLineage->founderSeed, archetype);
                uint32_t birthSeed = EcologyMix(
                    salt ^ sourceLineage->lineageId ^
                    (receiverLineage->lineageId * 0x9e3779b9u) ^
                    (uint32_t)(archetypeIndex + 1u) * 0x85ebca6bu);
                CreatureGenome child = EvolutionGenomeBreed(
                    &sourceGenome, &receiverGenome, birthSeed,
                    0.004f + receiverLineage->geneticVariance * 0.025f);
                if (!EvolutionDevelop(&child).valid) continue;
                receiverTarget->founderSeed = EcologyEvolutionNonzeroId(
                    child.genomeId, receiverTarget->founderSeed);
                receiverTarget->dietMean = EcologyClamp(
                    receiverTarget->dietMean * (1.0f - pairMix) +
                    ((float)child.diet / 255.0f) * pairMix);
                receiverTarget->geneticVariance = EcologyClamp(
                    receiverTarget->geneticVariance + pairMix * 0.045f);
                receiverTarget->geneFlow = EcologyClamp(
                    receiverTarget->geneFlow + pairMix);
                sourceTarget->geneFlow = EcologyClamp(
                    sourceTarget->geneFlow + pairMix * 0.35f);
                receiverTarget->isolatedGenerations = 0u;
            }
        }
    }
}

static void EcologyEvolutionMixMigration(
    PlanetEvolutionRegion *first, PlanetEvolutionRegion *second,
    const PlanetPopulationMigrationFlux *flux, uint32_t salt)
{
    if (!first || !second || !flux) return;
    PlanetEvolutionRegion firstBase = *first;
    PlanetEvolutionRegion secondBase = *second;
    EcologyEvolutionMixMigrationDirectional(
        &firstBase, &secondBase, first, second,
        flux->faunaForward, salt ^ 0x243f6a88u);
    EcologyEvolutionMixMigrationDirectional(
        &secondBase, &firstBase, second, first,
        flux->faunaReverse, salt ^ 0x85a308d3u);
}

static uint32_t EcologyEvolutionNonzeroId(uint32_t value, uint32_t fallback)
{
    uint32_t mixed = EcologyMix(value);
    return mixed != 0u ? mixed : fallback;
}

static bool EcologyEvolutionIdUsed(const PlanetEvolutionRegion *evolution,
                                   uint32_t id, bool species)
{
    if (!evolution || id == 0u) return false;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        const PlanetEvolutionLineage *lineage = &evolution->lineages[index];
        if (!lineage->active) continue;
        if ((species ? lineage->speciesId : lineage->lineageId) == id) {
            return true;
        }
    }
    return false;
}

static uint32_t EcologyEvolutionUniqueId(
    const PlanetEvolutionRegion *evolution, uint32_t candidate, bool species)
{
    uint32_t id = EcologyEvolutionNonzeroId(candidate, 1u);
    for (unsigned probe = 0; probe < 64u; probe++) {
        if (!EcologyEvolutionIdUsed(evolution, id, species)) return id;
        id = EcologyEvolutionNonzeroId(
            id ^ (0x9e3779b9u + probe * 0x85ebca6bu), id + 1u);
    }
    return 0u;
}

static float EcologyEvolutionArchetypeFitness(
    EvolutionArchetype archetype, const CreaturePhenotype *phenotype,
    const EcologyPopulationStepState *state,
    const PlanetEcologyProfile *profile)
{
    if (!phenotype || !phenotype->valid || !state || !profile ||
        phenotype->locomotion != (CreatureLocomotion)(archetype + 1)) {
        return 0.0f;
    }
    float habitat = state->habitat.faunaSuitability;
    if (archetype == EVOLUTION_ARCHETYPE_GROUND) {
        habitat = state->habitat.floraSuitability *
                  (1.0f - state->habitat.stormPressure * 0.45f);
    } else if (archetype == EVOLUTION_ARCHETYPE_FLIGHT) {
        habitat *= profile->supportsFlight ?
            1.0f - state->habitat.stormPressure * 0.70f : 0.05f;
    } else {
        habitat = state->aquaticSuitability;
    }
    float efficiency = 1.0f /
        (1.0f + phenotype->energyCost * 0.18f);
    float mobility = fminf(phenotype->cruiseSpeed / 3.0f, 1.0f);
    float plantFood = state->population.floraDensity;
    float animalFood = state->population.faunaDensity;
    float food = plantFood * (1.0f - phenotype->diet) +
                 animalFood * phenotype->diet;
    return EcologyClamp(habitat * 0.52f + efficiency * 0.20f +
                        mobility * 0.10f + food * 0.18f);
}

/*
 * Turn a speciation signal into a bounded lineage branch.  The parent keeps
 * its identity while a deterministic self-crossed genome seeds a new slot.
 * New slots are deliberately not advanced until the next generation: this
 * keeps a branch event independent of slot order and prevents same-step
 * recursive branching.
 */
static bool EcologyEvolutionTrySpeciate(
    PlanetEvolutionRegion *evolution, unsigned parentIndex,
    const EcologyPopulationStepState *state,
    const PlanetEcologyProfile *profile, uint32_t surfaceId,
    int regionX, int regionZ, float *childFitness)
{
    if (!evolution || parentIndex >= PLANET_EVOLUTION_MAX_LINEAGES ||
        !state || !profile || !childFitness) return false;
    PlanetEvolutionLineage *parent = &evolution->lineages[parentIndex];
    if (!parent->active || parent->archetype >= 3u ||
        parent->founderSeed == 0u || !isfinite(parent->density) ||
        parent->density <= 0.0001f) return false;

    unsigned childIndex = PLANET_EVOLUTION_MAX_LINEAGES;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        if (!evolution->lineages[index].active) {
            childIndex = index;
            break;
        }
    }
    if (childIndex >= PLANET_EVOLUTION_MAX_LINEAGES) return false;

    EvolutionArchetype archetype =
        (EvolutionArchetype)parent->archetype;
    CreatureGenome parentGenome = EvolutionGenomeSeed(
        parent->founderSeed, archetype);
    parentGenome.generation = parent->generation;
    uint32_t branchSalt = surfaceId ^
        (uint32_t)regionX * 0x27d4eb2fu ^
        (uint32_t)regionZ * 0x165667b1u ^
        parent->lineageId ^ (parent->speciesId * 0x9e3779b9u) ^
        (uint32_t)parent->generation * 0x85ebca6bu;
    uint32_t birthFallback = EcologyEvolutionNonzeroId(
        parent->founderSeed ^ 0x6d2b79f5u, 1u);
    uint32_t birthSeed = EcologyEvolutionNonzeroId(
        branchSalt ^ 0xd1b54a32u, birthFallback);
    CreatureGenome childGenome = EvolutionGenomeBreed(
        &parentGenome, &parentGenome, birthSeed,
        0.012f + EcologyClamp(parent->geneticVariance) * 0.018f);
    CreaturePhenotype childPhenotype = EvolutionDevelop(&childGenome);
    if (!childPhenotype.valid ||
        childPhenotype.locomotion != (CreatureLocomotion)(archetype + 1)) {
        /* A rare invalid mutation must not consume a lineage slot. */
        childGenome = EvolutionGenomeBreed(
            &parentGenome, &parentGenome, birthSeed ^ 0x9e3779b9u, 0.0f);
        childPhenotype = EvolutionDevelop(&childGenome);
    }
    if (!childPhenotype.valid ||
        childPhenotype.locomotion != (CreatureLocomotion)(archetype + 1)) {
        return false;
    }

    uint32_t founderFallback = EcologyEvolutionNonzeroId(
        parent->founderSeed ^ 0x51ed270bu, 1u);
    uint32_t childFounderSeed = EcologyEvolutionNonzeroId(
        childGenome.genomeId, founderFallback);
    uint32_t childLineageId = EcologyEvolutionUniqueId(
        evolution, parent->lineageId ^ childFounderSeed ^ 0x243f6a88u,
        false);
    uint32_t childSpeciesId = EcologyEvolutionUniqueId(
        evolution, parent->speciesId ^ childLineageId ^ 0xa511e9b3u,
        true);
    if (childLineageId == 0u || childSpeciesId == 0u) return false;
    float parentDensity = EcologyClamp(parent->density);
    float childDensity = EcologyClamp(parentDensity * 0.25f);
    float genomeDistance = EcologyClamp(EvolutionGenomeDistance(
        &parentGenome, &childGenome));
    float parentVariance = EcologyClamp(parent->geneticVariance);

    PlanetEvolutionLineage child = { 0 };
    child.lineageId = childLineageId;
    child.speciesId = childSpeciesId;
    child.founderSeed = childFounderSeed;
    child.density = childDensity;
    child.dietMean = EcologyClamp(childPhenotype.diet);
    child.geneticVariance = EcologyClamp(
        parentVariance * 0.35f + genomeDistance * 0.08f);
    child.fitness = EcologyEvolutionArchetypeFitness(
        archetype, &childPhenotype, state, profile);
    child.archetype = parent->archetype;
    /* The regional clock advances once per step; sampled genomes derive the
     * current generation from this lineage value, so a same-step branch keeps
     * the parent's clock rather than jumping ahead. */
    child.generation = parent->generation;
    child.isolatedGenerations = 0u;
    child.active = 1u;

    parent->density = EcologyClamp(parentDensity - childDensity);
    parent->geneticVariance = EcologyClamp(parentVariance * 0.55f);
    parent->geneFlow = 0.0f;
    parent->isolatedGenerations = 0u;
    evolution->lineages[childIndex] = child;
    *childFitness = child.fitness;
    return true;
}

static void EcologyEvolutionInitialize(
    EcologyPopulationRecord *record, const EcologyPopulationStepState *state,
    const PlanetEcologyProfile *profile)
{
    record->evolution = (PlanetEvolutionRegion){ 0 };
    if (!state || !profile || record->population.faunaDensity <= 0.0f) return;
    float weights[3] = {
        0.52f,
        profile->supportsFlight ? 0.25f : 0.02f,
        state->aquaticSuitability > 0.18f ? 0.23f : 0.02f
    };
    float weightSum = weights[0] + weights[1] + weights[2];
    for (unsigned index = 0; index < 3u; index++) {
        uint32_t seed = EcologyEvolutionNonzeroId(record->surfaceId ^
            (uint32_t)record->regionX * 0x9e3779b9u ^
            (uint32_t)record->regionZ * 0x85ebca6bu ^
            (index + 1u) * 0xc2b2ae35u, index + 1u);
        PlanetEvolutionLineage *lineage = &record->evolution.lineages[index];
        lineage->active = 1u;
        lineage->archetype = (uint8_t)index;
        lineage->founderSeed = seed;
        lineage->lineageId = EcologyEvolutionNonzeroId(
            seed ^ 0x51ed270bu, index + 1u);
        lineage->speciesId = EcologyEvolutionNonzeroId(
            lineage->lineageId ^ 0xa511e9b3u, lineage->lineageId);
        lineage->density = record->population.faunaDensity *
                           weights[index] / weightSum;
        CreatureGenome genome = EvolutionGenomeSeed(
            seed, (EvolutionArchetype)index);
        CreaturePhenotype phenotype = EvolutionDevelop(&genome);
        lineage->dietMean = phenotype.diet;
        lineage->fitness = EcologyEvolutionArchetypeFitness(
            (EvolutionArchetype)index, &phenotype, state, profile);
    }
    EcologyEvolutionRefreshTrophic(&record->evolution);
}

static void EcologyEvolutionAdvance(
    PlanetEvolutionRegion *evolution, const EcologyPopulationStepState *state,
    const PlanetEcologyProfile *profile, uint32_t surfaceId,
    int regionX, int regionZ, double elapsedTime)
{
    if (!evolution || !state || !profile || elapsedTime <= 0.0) return;
    unsigned generations = (unsigned)fmax(
        1.0, floor(elapsedTime / ECOLOGY_POPULATION_STEP_DAYS + 0.5));
    for (unsigned generation = 0; generation < generations; generation++) {
        if (evolution->bootstrapGeneration < 24u) {
            evolution->bootstrapGeneration++;
        }
        uint8_t activeAtStart[PLANET_EVOLUTION_MAX_LINEAGES] = { 0 };
        for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES;
             index++) {
            activeAtStart[index] = evolution->lineages[index].active;
        }
        float fitnessSum = 0.0f;
        for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
            PlanetEvolutionLineage *lineage = &evolution->lineages[index];
            if (!activeAtStart[index]) continue;
            EvolutionArchetype archetype =
                (EvolutionArchetype)lineage->archetype;
            uint32_t bestSeed = lineage->founderSeed;
            float bestFitness = -1.0f;
            float bestDiet = lineage->dietMean;
            for (unsigned candidate = 0; candidate < 8u; candidate++) {
                uint32_t candidateSeed = EcologyEvolutionNonzeroId(
                    lineage->founderSeed ^ surfaceId ^
                    (uint32_t)regionX * 0x27d4eb2fu ^
                    (uint32_t)regionZ * 0x165667b1u ^
                    evolution->bootstrapGeneration * 0x9e3779b9u ^
                    candidate * 0x85ebca6bu,
                    lineage->founderSeed != 0u ? lineage->founderSeed : 1u);
                CreatureGenome genome = EvolutionGenomeSeed(
                    candidateSeed, archetype);
                CreaturePhenotype phenotype = EvolutionDevelop(&genome);
                float fitness = EcologyEvolutionArchetypeFitness(
                    archetype, &phenotype, state, profile);
                if (fitness > bestFitness) {
                    bestFitness = fitness;
                    bestSeed = candidateSeed;
                    bestDiet = phenotype.diet;
                }
            }
            lineage->founderSeed = bestSeed;
            lineage->fitness = EcologyClamp(bestFitness);
            lineage->dietMean = EcologyClamp(
                lineage->dietMean * 0.85f + bestDiet * 0.15f);
            lineage->generation = (uint8_t)fminf(
                (float)lineage->generation + 1.0f, 255.0f);
            lineage->geneticVariance = EcologyClamp(
                lineage->geneticVariance + 0.0035f +
                state->population.radiationMemory * 0.012f);
            lineage->geneFlow = EcologyClamp(lineage->geneFlow * 0.88f);
            if (lineage->geneFlow < 0.05f) {
                if (lineage->isolatedGenerations < 255u) {
                    lineage->isolatedGenerations++;
                }
            } else {
                lineage->isolatedGenerations = 0u;
            }
            if (EvolutionShouldSpeciate(
                    lineage->geneticVariance, lineage->geneFlow,
                    lineage->isolatedGenerations)) {
                float childFitness = 0.0f;
                if (EcologyEvolutionTrySpeciate(
                        evolution, index, state, profile, surfaceId,
                        regionX, regionZ, &childFitness)) {
                    fitnessSum += childFitness;
                } else {
                    /* Capacity or an invalid mutant: cool down and retry later. */
                    lineage->isolatedGenerations = 0u;
                    lineage->geneticVariance = EcologyClamp(
                        lineage->geneticVariance * 0.55f);
                }
            }
            fitnessSum += lineage->fitness;
        }
        if (fitnessSum > 0.0001f) {
            for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES;
                 index++) {
                PlanetEvolutionLineage *lineage = &evolution->lineages[index];
                if (!lineage->active) continue;
                float target = state->population.faunaDensity *
                               lineage->fitness / fitnessSum;
                lineage->density = EcologyClamp(
                    lineage->density * 0.82f + target * 0.18f);
            }
        }
    }
    evolution->bootstrapComplete = evolution->bootstrapGeneration >= 24u;
    EcologyEvolutionRefreshTrophic(evolution);
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
        .faunaActivity = regional.suitability.faunaActivity,
        .radiationExposure = regional.environment.radiationExposure,
        .ejectaExposure = regional.environment.ejectaExposure
    };
    state->habitat = (PlanetMigrationHabitat){
        .floraSuitability = regional.suitability.floraActivity,
        .faunaSuitability = regional.suitability.faunaActivity,
        .stormPressure = regional.environment.currentStorm
    };
    state->aquaticSuitability = EcologyClamp(
        regional.environment.liquidWaterAccess * 0.72f +
        regional.suitability.waterScore * 0.28f);
    float disturbance = EcologyRegionalDisturbance(
        record->surfaceId, record->regionX, record->regionZ,
        originX, originZ);
    state->floraStress = disturbance * 0.82f;
    float habitatStress = disturbance * 0.94f;
    float harvestStress = EcologyClamp(
        record->population.faunaHarvestPressure) * 0.88f;
    state->faunaStress = EcologyClamp(
        1.0f - (1.0f - habitatStress) * (1.0f - harvestStress));
    state->nutrientEnvironment = EcologyNutrientEnvironmentAt(
        &regional, &record->population, state->floraStress);
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
    state.population = record->population;
    record->nutrients = EcologyNutrientInitializeState(
        &state.input, &record->population);
    record->nutrientInitialized = 1u;
    EcologyEvolutionInitialize(record, &state, profile);
    record->lastUpdateTime = simulationTime;
}

static void EcologyEvolutionEnsureInitialized(
    EcologyPopulationRecord *record, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, int originX, int originZ)
{
    if (!record || record->evolution.lineageCount > 0u ||
        record->population.faunaDensity <= 0.0f) return;
    EcologyPopulationStepState state = { 0 };
    EcologyPopulationConditionsAt(
        record, simulationTime, daylight, profile, originX, originZ, &state);
    state.population = record->population;
    if (!record->nutrientInitialized) {
        record->nutrients = EcologyNutrientInitializeState(
            &state.input, &record->population);
        record->nutrientInitialized = 1u;
    }
    EcologyEvolutionInitialize(record, &state, profile);
}

static bool EcologyPopulationRewindFutureRecords(
    uint32_t surfaceId, double targetTime)
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
    return changed;
}

static double EcologyPopulationEarliestStepStart(
    uint32_t surfaceId, double targetTime)
{
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
    return stepStart;
}

static void EcologyPopulationPrepareStep(
    const EcologyPopulationStepContext *context,
    EcologyPopulationStepBuffers *buffers)
{
    for (unsigned index = 0; index < ECOLOGY_POPULATION_MAX_REGIONS;
         index++) {
        EcologyPopulationRecord *record = &ecologyPopulationRecords[index];
        if (!record->valid || record->surfaceId != context->surfaceId ||
            record->lastUpdateTime != context->stepStart) {
            continue;
        }
        buffers->states[index].active = true;
        buffers->states[index].population = record->population;
        buffers->states[index].evolution = record->evolution;
        buffers->states[index].nutrients = record->nutrients;
        buffers->states[index].nutrientInitialized =
            record->nutrientInitialized;
        EcologyPopulationConditionsAt(
            record, context->stepEnd, context->daylight, context->profile,
            context->originX, context->originZ, &buffers->states[index]);
        if (!buffers->states[index].nutrientInitialized ||
            !EcologyNutrientStateValid(&buffers->states[index].nutrients)) {
            buffers->states[index].nutrients = EcologyNutrientInitializeState(
                &buffers->states[index].input,
                &buffers->states[index].population);
            buffers->states[index].nutrientInitialized = 1u;
        }
        PlanetPopulationAdvance(
            &buffers->states[index].population,
            &buffers->states[index].input, context->elapsedTime);
        PlanetPopulationApplyDisturbance(
            &buffers->states[index].population,
            buffers->states[index].floraStress,
            buffers->states[index].faunaStress, context->elapsedTime);
        EcologyNutrientApplyFloraStress(
            &buffers->states[index].nutrients,
            buffers->states[index].floraStress);
        EcologyNutrientEnvironment nutrientEnvironment =
            buffers->states[index].nutrientEnvironment;
        nutrientEnvironment.rainfallMm *= context->elapsedTime;
        (void)EcologyNutrientStep(
            &buffers->states[index].nutrients, NULL,
            &nutrientEnvironment, context->elapsedTime, NULL);
        float nutrientDensity = EcologyNutrientDensity(
            &buffers->states[index].nutrients);
        float floraCapacity = EcologyClamp(
            buffers->states[index].input.floraCapacity);
        nutrientDensity = fminf(nutrientDensity, floraCapacity);
        buffers->states[index].population.floraDensity = EcologyClamp(
            buffers->states[index].population.floraDensity * 0.84f +
            nutrientDensity * 0.16f);
        EcologyEvolutionAdvance(
            &buffers->states[index].evolution, &buffers->states[index],
            context->profile, record->surfaceId, record->regionX,
            record->regionZ, context->elapsedTime);
    }
}

static void EcologyPopulationAccumulateMigration(
    const EcologyPopulationStepContext *context,
    EcologyPopulationStepBuffers *buffers)
{
    static const int directions[2][2] = { { 1, 0 }, { 0, 1 } };
    /*
     * Records live in a four-way hashed/LRU cache.  Iterating raw slots would
     * make multi-edge gene flow depend on the order in which callers happened
     * to warm that cache.  Sort active regions by their canonical coordinates
     * so every replay applies the same edge order.
     */
    unsigned order[ECOLOGY_POPULATION_MAX_REGIONS];
    unsigned orderCount = 0u;
    for (unsigned index = 0; index < ECOLOGY_POPULATION_MAX_REGIONS;
         index++) {
        if (!buffers->states[index].active) continue;
        unsigned insertion = orderCount;
        while (insertion > 0u) {
            unsigned previous = order[insertion - 1u];
            EcologyPopulationRecord *previousRecord =
                &ecologyPopulationRecords[previous];
            EcologyPopulationRecord *record = &ecologyPopulationRecords[index];
            if (previousRecord->regionX < record->regionX ||
                (previousRecord->regionX == record->regionX &&
                 previousRecord->regionZ <= record->regionZ)) {
                break;
            }
            order[insertion] = previous;
            insertion--;
        }
        order[insertion] = index;
        orderCount++;
    }
    for (unsigned ordered = 0u; ordered < orderCount; ordered++) {
        unsigned index = order[ordered];
        if (!buffers->states[index].active) continue;
        EcologyPopulationRecord *record = &ecologyPopulationRecords[index];
        for (int direction = 0; direction < 2; direction++) {
            int neighborIndex = EcologyPopulationFindRecordIndex(
                context->surfaceId,
                record->regionX + directions[direction][0],
                record->regionZ + directions[direction][1]);
            if (neighborIndex < 0 ||
                !buffers->states[neighborIndex].active) continue;
            float windAlignment =
                (buffers->states[index].windX +
                 buffers->states[neighborIndex].windX) *
                    0.5f * (float)directions[direction][0] +
                (buffers->states[index].windZ +
                 buffers->states[neighborIndex].windZ) *
                    0.5f * (float)directions[direction][1];
            PlanetPopulationMigrationFlux flux =
                PlanetPopulationMigrationBetween(
                    &buffers->states[index].population,
                    &buffers->states[index].habitat,
                    &buffers->states[neighborIndex].population,
                    &buffers->states[neighborIndex].habitat,
                    windAlignment, context->elapsedTime);
            uint32_t migrationSalt = context->surfaceId ^
                (uint32_t)record->regionX * 0x9e3779b9u ^
                (uint32_t)record->regionZ * 0x85ebca6bu ^
                (uint32_t)(context->stepEnd / ECOLOGY_POPULATION_STEP_DAYS);
            EcologyEvolutionMixMigration(
                &buffers->states[index].evolution,
                &buffers->states[neighborIndex].evolution,
                &flux, migrationSalt);
            buffers->floraDelta[index] -= flux.flora;
            buffers->floraDelta[neighborIndex] += flux.flora;
            buffers->faunaDelta[index] -= flux.fauna;
            buffers->faunaDelta[neighborIndex] += flux.fauna;
            float directionX = (float)directions[direction][0];
            float directionZ = (float)directions[direction][1];
            buffers->floraFlowX[index] += flux.flora * directionX;
            buffers->floraFlowX[neighborIndex] += flux.flora * directionX;
            buffers->floraFlowZ[index] += flux.flora * directionZ;
            buffers->floraFlowZ[neighborIndex] += flux.flora * directionZ;
            buffers->faunaFlowX[index] += flux.fauna * directionX;
            buffers->faunaFlowX[neighborIndex] += flux.fauna * directionX;
            buffers->faunaFlowZ[index] += flux.fauna * directionZ;
            buffers->faunaFlowZ[neighborIndex] += flux.fauna * directionZ;
        }
    }
}

static bool EcologyPopulationCommitStep(
    const EcologyPopulationStepContext *context,
    EcologyPopulationStepBuffers *buffers)
{
    bool changed = false;
    for (unsigned index = 0; index < ECOLOGY_POPULATION_MAX_REGIONS;
         index++) {
        if (!buffers->states[index].active) continue;
        EcologyPopulationRecord *record = &ecologyPopulationRecords[index];
        buffers->states[index].population.floraDensity = EcologyClamp(
            buffers->states[index].population.floraDensity +
            (float)buffers->floraDelta[index]);
        buffers->states[index].population.faunaDensity = EcologyClamp(
            buffers->states[index].population.faunaDensity +
            (float)buffers->faunaDelta[index]);
        record->population = buffers->states[index].population;
        record->evolution = buffers->states[index].evolution;
        record->nutrients = buffers->states[index].nutrients;
        record->nutrientInitialized =
            buffers->states[index].nutrientInitialized;
        EcologyEvolutionScaleToFauna(
            &record->evolution, record->population.faunaDensity);
        record->migration = (PlanetPopulationMigrationState){
            .floraNet = fminf(fmaxf(
                (float)buffers->floraDelta[index], -1.0f), 1.0f),
            .faunaNet = fminf(fmaxf(
                (float)buffers->faunaDelta[index], -1.0f), 1.0f),
            .floraFlowX = fminf(fmaxf(
                (float)buffers->floraFlowX[index], -1.0f), 1.0f),
            .floraFlowZ = fminf(fmaxf(
                (float)buffers->floraFlowZ[index], -1.0f), 1.0f),
            .faunaFlowX = fminf(fmaxf(
                (float)buffers->faunaFlowX[index], -1.0f), 1.0f),
            .faunaFlowZ = fminf(fmaxf(
                (float)buffers->faunaFlowZ[index], -1.0f), 1.0f)
        };
        record->lastUpdateTime = context->stepEnd;
        changed = true;
    }
    return changed;
}

static void EcologyPopulationAdvanceRecords(
    uint32_t surfaceId, double targetTime, float daylight,
    const PlanetEcologyProfile *profile, int originX, int originZ)
{
    bool changed = EcologyPopulationRewindFutureRecords(
        surfaceId, targetTime);

    for (;;) {
        double stepStart = EcologyPopulationEarliestStepStart(
            surfaceId, targetTime);
        if (!isfinite(stepStart)) break;

        double stepEnd = fmin(stepStart + ECOLOGY_POPULATION_STEP_DAYS,
                              targetTime);
        double elapsedTime = stepEnd - stepStart;
        EcologyPopulationStepContext context = {
            .surfaceId = surfaceId,
            .stepStart = stepStart,
            .stepEnd = stepEnd,
            .elapsedTime = elapsedTime,
            .daylight = daylight,
            .profile = profile,
            .originX = originX,
            .originZ = originZ
        };
        EcologyPopulationStepBuffers buffers = { 0 };
        EcologyPopulationPrepareStep(&context, &buffers);
        EcologyPopulationAccumulateMigration(&context, &buffers);
        if (EcologyPopulationCommitStep(&context, &buffers)) {
            changed = true;
        }
    }

    if (changed) {
        ecologyPopulationEpoch++;
        if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    }
}

PlanetRegionalPopulation EcologyRegionalPopulationAt(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile,
    PlanetPopulationMigrationState *outMigration)
{
    PlanetRegionalPopulation empty = { 0 };
    if (outMigration) *outMigration = (PlanetPopulationMigrationState){ 0 };
    EcologyCanonicalizeCell(&x, &z);
    pthread_mutex_lock(&ecologyPopulationMutex);
    int originX = EcologyWorldOriginX();
    int originZ = EcologyWorldOriginZ();
    int globalX = originX + x;
    int globalZ = originZ + z;
    int regionX = EcologyFloorDivide(globalX, ECOLOGY_POPULATION_REGION_SIZE);
    int regionZ = EcologyFloorDivide(globalZ, ECOLOGY_POPULATION_REGION_SIZE);
    uint32_t surfaceId = EcologyWorldSurfaceId();
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
        } else {
            EcologyEvolutionEnsureInitialized(
                neighbor, stepTime, daylight, profile, originX, originZ);
        }
    }
    int recordIndex = EcologyPopulationFindRecordIndex(
        surfaceId, regionX, regionZ);
    if (recordIndex < 0) {
        pthread_mutex_unlock(&ecologyPopulationMutex);
        return empty;
    }
    if (createdAny) {
        ecologyPopulationEpoch++;
        if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    }
    EcologyPopulationRecord *record = &ecologyPopulationRecords[recordIndex];
    if (outMigration) *outMigration = record->migration;
    PlanetRegionalPopulation result = record->population;
    pthread_mutex_unlock(&ecologyPopulationMutex);
    return result;
}

static EcologyPopulationRecord *EcologyEvolutionRecordAt(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, bool *created)
{
    EcologyCanonicalizeCell(&x, &z);
    int originX = EcologyWorldOriginX();
    int originZ = EcologyWorldOriginZ();
    int regionX = EcologyFloorDivide(
        originX + x, ECOLOGY_POPULATION_REGION_SIZE);
    int regionZ = EcologyFloorDivide(
        originZ + z, ECOLOGY_POPULATION_REGION_SIZE);
    uint32_t surfaceId = EcologyWorldSurfaceId();
    double stepTime = EcologyPopulationStepTime(simulationTime);
    EcologyPopulationAdvanceRecords(
        surfaceId, stepTime, daylight, profile, originX, originZ);
    bool isNew = false;
    EcologyPopulationRecord *record = EcologyPopulationRecordAt(
        surfaceId, regionX, regionZ, &isNew);
    if (!record) return NULL;
    if (isNew) {
        EcologyPopulationInitializeRecord(
            record, stepTime, daylight, profile, originX, originZ);
    } else {
        EcologyEvolutionEnsureInitialized(
            record, stepTime, daylight, profile, originX, originZ);
    }
    if (created) *created = isNew;
    return record;
}

bool EcologyEvolutionRegionAt(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, PlanetEvolutionRegion *out)
{
    if (!profile || !out || !isfinite(simulationTime) || simulationTime < 0.0 ||
        !isfinite(daylight)) return false;
    pthread_mutex_lock(&ecologyPopulationMutex);
    bool created = false;
    EcologyPopulationRecord *record = EcologyEvolutionRecordAt(
        x, z, simulationTime, daylight, profile, &created);
    if (!record) {
        pthread_mutex_unlock(&ecologyPopulationMutex);
        return false;
    }
    *out = record->evolution;
    if (created) {
        ecologyPopulationEpoch++;
        if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    }
    pthread_mutex_unlock(&ecologyPopulationMutex);
    return true;
}

bool EcologyEvolutionSampleGenome(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, uint32_t sampleSeed,
    CreatureGenome *outGenome, uint32_t *outLineageId,
    uint32_t *outSpeciesId)
{
    if (!profile || !outGenome || sampleSeed == 0u) return false;
    EcologyCanonicalizeCell(&x, &z);
    pthread_mutex_lock(&ecologyPopulationMutex);
    EcologyPopulationRecord *record = EcologyEvolutionRecordAt(
        x, z, simulationTime, daylight, profile, NULL);
    if (!record || record->evolution.lineageCount == 0u) {
        pthread_mutex_unlock(&ecologyPopulationMutex);
        return false;
    }
    float densityTotal = record->evolution.herbivoreDensity +
        record->evolution.omnivoreDensity +
        record->evolution.carnivoreDensity;
    float roll = (float)(EcologyMix(sampleSeed) & 0x00ffffffu) /
                 16777215.0f * fmaxf(densityTotal, 0.0001f);
    PlanetEvolutionLineage *selected = NULL;
    float accumulated = 0.0f;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        PlanetEvolutionLineage *lineage = &record->evolution.lineages[index];
        if (!lineage->active) continue;
        accumulated += lineage->density;
        if (!selected || roll <= accumulated) selected = lineage;
        if (roll <= accumulated) break;
    }
    if (!selected) {
        pthread_mutex_unlock(&ecologyPopulationMutex);
        return false;
    }
    EvolutionArchetype archetype = (EvolutionArchetype)selected->archetype;
    CreatureGenome first = EvolutionGenomeSeed(
        selected->founderSeed, archetype);
    CreatureGenome second = EvolutionGenomeSeed(
        EcologyMix(selected->founderSeed ^ 0x9e3779b9u ^
                   selected->generation), archetype);
    uint8_t parentGeneration = selected->generation > 0u ?
        (uint8_t)(selected->generation - 1u) : 0u;
    first.generation = parentGeneration;
    second.generation = parentGeneration;
    first.genomeId = EvolutionGenomeHash(&first);
    second.genomeId = EvolutionGenomeHash(&second);
    CreatureGenome sampled = EvolutionGenomeBreed(
        &first, &second, sampleSeed,
        0.006f + selected->geneticVariance * 0.055f);
    CreaturePhenotype phenotype = EvolutionDevelop(&sampled);
    if (!phenotype.valid) sampled = first;
    *outGenome = sampled;
    if (outLineageId) *outLineageId = selected->lineageId;
    if (outSpeciesId) *outSpeciesId = selected->speciesId;
    pthread_mutex_unlock(&ecologyPopulationMutex);
    return true;
}

static PlanetEvolutionLineage *EcologyEvolutionFindLineage(
    PlanetEvolutionRegion *evolution, uint32_t lineageId)
{
    if (!evolution || lineageId == 0u) return NULL;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        PlanetEvolutionLineage *lineage = &evolution->lineages[index];
        if (lineage->active && lineage->lineageId == lineageId) {
            return lineage;
        }
    }
    return NULL;
}

static bool EcologyEvolutionGenomeUsable(
    const CreatureGenome *genome, CreaturePhenotype *outPhenotype)
{
    if (!genome || genome->genomeId == 0u ||
        genome->genomeId != EvolutionGenomeHash(genome)) return false;
    CreaturePhenotype phenotype = EvolutionDevelop(genome);
    if (!phenotype.valid) return false;
    if (outPhenotype) *outPhenotype = phenotype;
    return true;
}

static uint8_t EcologyEvolutionArchetypeForPhenotype(
    const CreaturePhenotype *phenotype)
{
    if (!phenotype) return (uint8_t)EVOLUTION_ARCHETYPE_GROUND;
    return phenotype->locomotion == CREATURE_LOCOMOTION_FLIGHT
        ? (uint8_t)EVOLUTION_ARCHETYPE_FLIGHT
        : phenotype->locomotion == CREATURE_LOCOMOTION_AQUATIC
        ? (uint8_t)EVOLUTION_ARCHETYPE_AQUATIC
        : (uint8_t)EVOLUTION_ARCHETYPE_GROUND;
}

static bool EcologyEvolutionLineageMatchesIdentity(
    const PlanetEvolutionLineage *lineage, uint32_t speciesId,
    const CreaturePhenotype *phenotype)
{
    if (!lineage || !lineage->active) return false;
    if (speciesId != 0u && lineage->speciesId != speciesId) return false;
    return !phenotype || lineage->archetype ==
        EcologyEvolutionArchetypeForPhenotype(phenotype);
}

static PlanetEvolutionLineage *EcologyEvolutionCreateBirthLineage(
    PlanetEvolutionRegion *evolution, uint32_t lineageId, uint32_t speciesId,
    const CreatureGenome *genome, const CreaturePhenotype *phenotype)
{
    if (!evolution || lineageId == 0u || !genome || !phenotype) return NULL;
    uint32_t resolvedLineageId = lineageId;
    if (EcologyEvolutionIdUsed(evolution, resolvedLineageId, false)) {
        resolvedLineageId = EcologyEvolutionUniqueId(
            evolution, resolvedLineageId, false);
    }
    if (resolvedLineageId == 0u) return NULL;
    uint32_t speciesCandidate = speciesId != 0u ? speciesId :
        EcologyEvolutionNonzeroId(
            genome->genomeId ^ 0xa511e9b3u, resolvedLineageId);
    uint32_t resolvedSpeciesId = speciesCandidate;
    if (EcologyEvolutionIdUsed(evolution, resolvedSpeciesId, true)) {
        resolvedSpeciesId = EcologyEvolutionUniqueId(
            evolution, resolvedSpeciesId, true);
    }
    if (resolvedSpeciesId == 0u) return NULL;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        PlanetEvolutionLineage *lineage = &evolution->lineages[index];
        if (lineage->active) continue;
        uint8_t archetype = EcologyEvolutionArchetypeForPhenotype(phenotype);
        *lineage = (PlanetEvolutionLineage){
            .lineageId = resolvedLineageId,
            .speciesId = resolvedSpeciesId,
            .founderSeed = EcologyEvolutionNonzeroId(
                genome->genomeId, resolvedLineageId),
            .density = 0.0f,
            .dietMean = EcologyClamp(phenotype->diet),
            .geneticVariance = EcologyClamp(
                (float)genome->mutationCount / 100.0f),
            .fitness = 0.5f,
            .archetype = archetype,
            .generation = genome->generation,
            .active = 1u
        };
        return lineage;
    }
    return NULL;
}

static PlanetEvolutionLineage *EcologyEvolutionFallbackLineage(
    PlanetEvolutionRegion *evolution, const CreaturePhenotype *phenotype)
{
    if (!evolution || !phenotype) return NULL;
    uint8_t archetype = phenotype->locomotion == CREATURE_LOCOMOTION_FLIGHT
        ? (uint8_t)EVOLUTION_ARCHETYPE_FLIGHT
        : phenotype->locomotion == CREATURE_LOCOMOTION_AQUATIC
        ? (uint8_t)EVOLUTION_ARCHETYPE_AQUATIC
        : (uint8_t)EVOLUTION_ARCHETYPE_GROUND;
    PlanetEvolutionLineage *first = NULL;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        PlanetEvolutionLineage *lineage = &evolution->lineages[index];
        if (!lineage->active) continue;
        if (!first) first = lineage;
        if (lineage->archetype == archetype) return lineage;
    }
    return first;
}

static bool EcologyEvolutionApplyEvent(
    EcologyPopulationRecord *record, PlanetEvolutionLineage *selected,
    PlanetEvolutionEvent event, float biomass)
{
    if (!record || !selected) return false;
    float strength = EcologyClamp(biomass / 120.0f);
    float change = event == PLANET_EVOLUTION_EVENT_BIRTH ? strength * 0.12f
        : -strength * (event == PLANET_EVOLUTION_EVENT_PREDATION_DEATH
                       ? 0.10f : 0.07f);
    float previousDensity = selected->density;
    selected->density = EcologyClamp(previousDensity + change);
    float appliedChange = selected->density - previousDensity;
    record->population.faunaDensity = EcologyClamp(
        record->population.faunaDensity + appliedChange);
    EcologyEvolutionRefreshTrophic(&record->evolution);
    ecologyPopulationEpoch++;
    if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    return true;
}

static bool EcologyEvolutionRecordEventInternal(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, uint32_t lineageId,
    uint32_t speciesId, const CreatureGenome *genome,
    PlanetEvolutionEvent event, float biomass, bool createBirthLineage,
    uint32_t *outLineageId, uint32_t *outSpeciesId)
{
    if (outLineageId) *outLineageId = 0u;
    if (outSpeciesId) *outSpeciesId = 0u;
    if (!profile || lineageId == 0u || !isfinite(biomass) || biomass <= 0.0f ||
        event < PLANET_EVOLUTION_EVENT_BIRTH ||
        event > PLANET_EVOLUTION_EVENT_PREDATION_DEATH) return false;
    CreaturePhenotype phenotype = { 0 };
    if (createBirthLineage &&
        !EcologyEvolutionGenomeUsable(genome, &phenotype)) return false;
    EcologyCanonicalizeCell(&x, &z);
    pthread_mutex_lock(&ecologyPopulationMutex);
    EcologyPopulationRecord *record = EcologyEvolutionRecordAt(
        x, z, simulationTime, daylight, profile, NULL);
    if (!record) {
        pthread_mutex_unlock(&ecologyPopulationMutex);
        return false;
    }
    PlanetEvolutionLineage *selected = EcologyEvolutionFindLineage(
        &record->evolution, lineageId);
    if (selected && createBirthLineage &&
        !EcologyEvolutionLineageMatchesIdentity(
            selected, speciesId, &phenotype)) {
        selected = NULL;
    }
    if (!selected && createBirthLineage) {
        selected = EcologyEvolutionCreateBirthLineage(
            &record->evolution, lineageId, speciesId, genome, &phenotype);
        if (!selected && event == PLANET_EVOLUTION_EVENT_BIRTH) {
            selected = EcologyEvolutionFallbackLineage(
                &record->evolution, &phenotype);
        }
    }
    if (selected && event != PLANET_EVOLUTION_EVENT_BIRTH &&
        selected->density <= 0.0f) {
        /* A migrated individual can die before its identity is represented in
         * this region. Seed a small share from the existing fauna pool so the
         * death decrement is reflected without inventing extra biomass. */
        float strength = EcologyClamp(biomass / 120.0f);
        float seedDensity = fminf(
            EcologyClamp(record->population.faunaDensity),
            fmaxf(strength, 0.001f));
        selected->density = seedDensity;
        EcologyEvolutionScaleToFauna(
            &record->evolution, record->population.faunaDensity);
    }
    bool applied = EcologyEvolutionApplyEvent(
        record, selected, event, biomass);
    if (applied && selected) {
        if (outLineageId) *outLineageId = selected->lineageId;
        if (outSpeciesId) *outSpeciesId = selected->speciesId;
    }
    pthread_mutex_unlock(&ecologyPopulationMutex);
    return applied;
}

bool EcologyEvolutionRecordEvent(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, uint32_t lineageId,
    PlanetEvolutionEvent event, float biomass)
{
    return EcologyEvolutionRecordEventInternal(
        x, z, simulationTime, daylight, profile, lineageId, 0u, NULL,
        event, biomass, false, NULL, NULL);
}

bool EcologyEvolutionRecordEventResolved(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, uint32_t lineageId,
    uint32_t speciesId, const CreatureGenome *genome,
    PlanetEvolutionEvent event, float biomass,
    uint32_t *outLineageId, uint32_t *outSpeciesId)
{
    return EcologyEvolutionRecordEventInternal(
        x, z, simulationTime, daylight, profile, lineageId, speciesId, genome,
        event, biomass, true, outLineageId, outSpeciesId);
}

bool EcologyEvolutionRecordBirth(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, uint32_t lineageId,
    uint32_t speciesId, const CreatureGenome *genome, float biomass)
{
    return EcologyEvolutionRecordBirthResolved(
        x, z, simulationTime, daylight, profile, lineageId, speciesId,
        genome, biomass, NULL, NULL);
}

bool EcologyEvolutionRecordBirthResolved(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, uint32_t lineageId,
    uint32_t speciesId, const CreatureGenome *genome, float biomass,
    uint32_t *outLineageId, uint32_t *outSpeciesId)
{
    return EcologyEvolutionRecordEventInternal(
        x, z, simulationTime, daylight, profile, lineageId, speciesId, genome,
        PLANET_EVOLUTION_EVENT_BIRTH, biomass, true,
        outLineageId, outSpeciesId);
}

bool EcologyPopulationRecordFaunaHarvest(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, float organismScale,
    float ecologyCapacity)
{
    float eventStrength = PlanetFaunaHarvestEventStrength(
        organismScale, ecologyCapacity);
    if (!profile || eventStrength <= 0.0f || !isfinite(simulationTime) ||
        simulationTime < 0.0 || !isfinite(daylight)) {
        return false;
    }
    EcologyCanonicalizeCell(&x, &z);

    pthread_mutex_lock(&ecologyPopulationMutex);
    int originX = EcologyWorldOriginX();
    int originZ = EcologyWorldOriginZ();
    int regionX = EcologyFloorDivide(
        originX + x, ECOLOGY_POPULATION_REGION_SIZE);
    int regionZ = EcologyFloorDivide(
        originZ + z, ECOLOGY_POPULATION_REGION_SIZE);
    uint32_t surfaceId = EcologyWorldSurfaceId();
    double stepTime = EcologyPopulationStepTime(simulationTime);
    EcologyPopulationAdvanceRecords(
        surfaceId, stepTime, daylight, profile, originX, originZ);

    bool created = false;
    EcologyPopulationRecord *record = EcologyPopulationRecordAt(
        surfaceId, regionX, regionZ, &created);
    if (!record) {
        pthread_mutex_unlock(&ecologyPopulationMutex);
        return false;
    }
    if (created) {
        EcologyPopulationInitializeRecord(
            record, stepTime, daylight, profile, originX, originZ);
    }
    PlanetPopulationApplyFaunaHarvest(&record->population, eventStrength);
    EcologyEvolutionScaleToFauna(
        &record->evolution, record->population.faunaDensity);
    ecologyPopulationEpoch++;
    if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    pthread_mutex_unlock(&ecologyPopulationMutex);
    return true;
}

static bool EcologyPopulationStateValid(
    const PlanetRegionalPopulation *population)
{
    if (!population) return false;
    const float values[] = {
        population->floraDensity, population->faunaDensity,
        population->floraCarryingCapacity,
        population->faunaCarryingCapacity,
        population->seasonalMemory, population->faunaHarvestPressure,
        population->radiationMemory
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

static bool EcologyEvolutionStateValid(const PlanetEvolutionRegion *evolution)
{
    if (!evolution || evolution->lineageCount > PLANET_EVOLUTION_MAX_LINEAGES ||
        evolution->bootstrapGeneration > 24u) return false;
    const float trophic[] = {
        evolution->herbivoreDensity, evolution->omnivoreDensity,
        evolution->carnivoreDensity
    };
    for (unsigned index = 0; index < sizeof(trophic) / sizeof(trophic[0]);
         index++) {
        if (!isfinite(trophic[index]) || trophic[index] < 0.0f ||
            trophic[index] > 1.0f) return false;
    }
    unsigned activeCount = 0u;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        const PlanetEvolutionLineage *lineage = &evolution->lineages[index];
        if (lineage->active > 1u || lineage->archetype >
            (uint8_t)EVOLUTION_ARCHETYPE_AQUATIC) return false;
        if (!lineage->active) continue;
        activeCount++;
        const float values[] = {
            lineage->density, lineage->dietMean, lineage->geneticVariance,
            lineage->geneFlow, lineage->fitness
        };
        if (lineage->lineageId == 0u || lineage->speciesId == 0u ||
            lineage->founderSeed == 0u) return false;
        for (unsigned value = 0; value < sizeof(values) / sizeof(values[0]);
             value++) {
            if (!isfinite(values[value]) || values[value] < 0.0f ||
                values[value] > 1.0f) return false;
        }
    }
    return activeCount == evolution->lineageCount;
}

uint32_t EcologyPopulationEpoch(void)
{
    pthread_mutex_lock(&ecologyPopulationMutex);
    uint32_t result = ecologyPopulationEpoch;
    pthread_mutex_unlock(&ecologyPopulationMutex);
    return result;
}

void EcologyPopulationResetState(void)
{
    pthread_mutex_lock(&ecologyPopulationMutex);
    memset(ecologyPopulationRecords, 0, sizeof(ecologyPopulationRecords));
    ecologyPopulationAccessSerial = 0u;
    ecologyPopulationEpoch++;
    if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    pthread_mutex_unlock(&ecologyPopulationMutex);
}

bool EcologyPopulationSaveState(FILE *file)
{
    bool success = false;
    pthread_mutex_lock(&ecologyPopulationMutex);
    if (!file) goto save_done;
    uint32_t count = 0u;
    for (unsigned index = 0; index < ECOLOGY_POPULATION_MAX_REGIONS; index++) {
        if (ecologyPopulationRecords[index].valid) count++;
    }
    const uint32_t header[2] = { ECOLOGY_POPULATION_STATE_VERSION, count };
    if (fwrite(header, sizeof(header), 1, file) != 1 ||
        fwrite(&ecologyPopulationAccessSerial,
               sizeof(ecologyPopulationAccessSerial), 1, file) != 1) {
        goto save_done;
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
            !EcologyEvolutionStateValid(&record->evolution) ||
            record->nutrientInitialized > 1u ||
            (record->nutrientInitialized &&
             !EcologyNutrientStateValid(&record->nutrients)) ||
            !isfinite(record->lastUpdateTime) ||
            fwrite(&record->surfaceId, sizeof(record->surfaceId), 1, file) != 1 ||
            fwrite(coordinates, sizeof(coordinates), 1, file) != 1 ||
            fwrite(&record->lastUpdateTime,
                   sizeof(record->lastUpdateTime), 1, file) != 1 ||
            fwrite(&record->lastAccess, sizeof(record->lastAccess), 1, file) != 1 ||
            fwrite(population, sizeof(population), 1, file) != 1 ||
            fwrite(migration, sizeof(migration), 1, file) != 1 ||
            fwrite(&record->population.faunaHarvestPressure,
                   sizeof(record->population.faunaHarvestPressure),
                   1, file) != 1 ||
            fwrite(&record->population.radiationMemory,
                   sizeof(record->population.radiationMemory), 1, file) != 1 ||
            fwrite(&record->evolution,
                   sizeof(record->evolution), 1, file) != 1 ||
            fwrite(&record->nutrientInitialized,
                   sizeof(record->nutrientInitialized), 1, file) != 1) {
            goto save_done;
        }
        if (record->nutrientInitialized &&
            fwrite(&record->nutrients, sizeof(record->nutrients), 1, file) != 1) {
            goto save_done;
        }
        if (!record->nutrientInitialized) {
            const EcologyNutrientState emptyNutrients = { 0 };
            if (fwrite(&emptyNutrients, sizeof(emptyNutrients), 1, file) != 1) {
                goto save_done;
            }
        }
    }
    success = true;
save_done:
    pthread_mutex_unlock(&ecologyPopulationMutex);
    return success;
}

bool EcologyPopulationLoadState(FILE *file)
{
    bool success = false;
    pthread_mutex_lock(&ecologyPopulationMutex);
    uint32_t header[2];
    uint64_t loadedAccessSerial = 0u;
    if (!file || fread(header, sizeof(header), 1, file) != 1 ||
        fread(&loadedAccessSerial, sizeof(loadedAccessSerial), 1, file) != 1 ||
        (header[0] < ECOLOGY_POPULATION_LEGACY_STATE_VERSION ||
         header[0] > ECOLOGY_POPULATION_STATE_VERSION) ||
        header[1] > ECOLOGY_POPULATION_MAX_REGIONS) {
        goto load_done;
    }

    EcologyPopulationRecord loaded[ECOLOGY_POPULATION_MAX_REGIONS] = { 0 };
    for (uint32_t item = 0; item < header[1]; item++) {
        uint32_t surfaceId = 0u;
        int32_t coordinates[2];
        double lastUpdateTime = 0.0;
        uint64_t lastAccess = 0u;
        float populationValues[5];
        float migrationValues[6] = { 0 };
        float faunaHarvestPressure = 0.0f;
        float radiationMemory = 0.0f;
        PlanetEvolutionRegion evolution = { 0 };
        EcologyNutrientState nutrients = { 0 };
        uint8_t nutrientInitialized = 0u;
        if (fread(&surfaceId, sizeof(surfaceId), 1, file) != 1 ||
            fread(coordinates, sizeof(coordinates), 1, file) != 1 ||
            fread(&lastUpdateTime, sizeof(lastUpdateTime), 1, file) != 1 ||
            fread(&lastAccess, sizeof(lastAccess), 1, file) != 1 ||
            fread(populationValues, sizeof(populationValues), 1, file) != 1) {
            goto load_done;
        }
        if (header[0] >= ECOLOGY_POPULATION_MIGRATION_STATE_VERSION &&
            fread(migrationValues, sizeof(migrationValues), 1, file) != 1) {
            goto load_done;
        }
        if (header[0] >= ECOLOGY_POPULATION_HARVEST_STATE_VERSION &&
            fread(&faunaHarvestPressure,
                  sizeof(faunaHarvestPressure), 1, file) != 1) {
            goto load_done;
        }
        if (header[0] >= ECOLOGY_POPULATION_RADIATION_STATE_VERSION &&
            fread(&radiationMemory, sizeof(radiationMemory), 1, file) != 1) {
            goto load_done;
        }
        if (header[0] >= ECOLOGY_POPULATION_EVOLUTION_STATE_VERSION &&
            fread(&evolution, sizeof(evolution), 1, file) != 1) {
            goto load_done;
        }
        if (header[0] >= ECOLOGY_POPULATION_NUTRIENT_STATE_VERSION &&
            (fread(&nutrientInitialized, sizeof(nutrientInitialized), 1, file) != 1 ||
             fread(&nutrients, sizeof(nutrients), 1, file) != 1)) {
            goto load_done;
        }
        PlanetRegionalPopulation population = {
            .floraDensity = populationValues[0],
            .faunaDensity = populationValues[1],
            .floraCarryingCapacity = populationValues[2],
            .faunaCarryingCapacity = populationValues[3],
            .seasonalMemory = populationValues[4],
            .faunaHarvestPressure = faunaHarvestPressure,
            .radiationMemory = radiationMemory
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
            !EcologyPopulationMigrationStateValid(&migration) ||
            !EcologyEvolutionStateValid(&evolution) ||
            nutrientInitialized > 1u ||
            (nutrientInitialized && !EcologyNutrientStateValid(&nutrients))) {
            goto load_done;
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
                goto load_done;
            }
            if (!candidate->valid && !slot) slot = candidate;
        }
        if (!slot) goto load_done;
        *slot = (EcologyPopulationRecord){
            .valid = true,
            .surfaceId = surfaceId,
            .regionX = (int)coordinates[0],
            .regionZ = (int)coordinates[1],
            .lastUpdateTime = lastUpdateTime,
            .lastAccess = lastAccess,
            .population = population,
            .migration = migration,
            .evolution = evolution,
            .nutrients = nutrients,
            .nutrientInitialized = nutrientInitialized
        };
    }

    memcpy(ecologyPopulationRecords, loaded, sizeof(loaded));
    ecologyPopulationAccessSerial = loadedAccessSerial;
    ecologyPopulationEpoch++;
    if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    success = true;
load_done:
    pthread_mutex_unlock(&ecologyPopulationMutex);
    return success;
}
