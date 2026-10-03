#include "ecology/ecology_population_internal.h"
#include "ecology/ecology_nutrient_model.h"
#include "space/space_state.h"

#include "world/terrain.h"
#include "world/weather.h"
#include "world/world.h"

#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ECOLOGY_POPULATION_MAX_RADIUS_CELLS \
    (14 * ECOLOGY_POPULATION_REGION_SIZE)
#define ECOLOGY_POPULATION_LEGACY_STEP_BUDGET 4096u

#if defined(__GNUC__) || defined(__clang__)
#define ECOLOGY_THREAD_LOCAL __thread
#else
#define ECOLOGY_THREAD_LOCAL
#endif

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

typedef struct EcologyPopulationWorkingSet {
    EcologyPopulationRecord records[ECOLOGY_POPULATION_MAX_REGIONS];
    unsigned count;
} EcologyPopulationWorkingSet;

typedef struct EcologyPopulationRegionKey {
    uint32_t surfaceId;
    int regionX;
    int regionZ;
} EcologyPopulationRegionKey;

typedef struct EcologyPopulationStepContext {
    uint32_t surfaceId;
    double stepStart;
    double stepEnd;
    double elapsedTime;
    float daylight;
    const PlanetEcologyProfile *profile;
    int originX;
    int originZ;
    EcologyPopulationWorkingSet *workingSet;
    const int (*neighbors)[2];
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

static ECOLOGY_THREAD_LOCAL bool ecologyPopulationDiagnosticsEnabled;
static ECOLOGY_THREAD_LOCAL EcologyPopulationDiagnostics ecologyPopulationDiagnostics;

static double EcologyPopulationCpuNowMs(void)
{
    if (!ecologyPopulationDiagnosticsEnabled) return 0.0;
    struct timespec value = { 0 };
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) return 0.0;
    return (double)value.tv_sec * 1000.0 +
           (double)value.tv_nsec / 1000000.0;
}

void EcologyPopulationSetDiagnosticsEnabled(bool enabled)
{
    ecologyPopulationDiagnosticsEnabled = enabled;
    ecologyPopulationDiagnostics = (EcologyPopulationDiagnostics){ 0 };
}

static void EcologyPopulationWorkerDiagnostics(EcologyPopulationDiagnostics *out);

void EcologyPopulationGetDiagnostics(EcologyPopulationDiagnostics *out)
{
    if (!out) return;
    *out = ecologyPopulationDiagnostics;
    EcologyPopulationWorkerDiagnostics(out);
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

static bool EcologyPopulationCanonicalRegionKey(
    uint32_t surfaceId, int regionX, int regionZ,
    int originX, int originZ, EcologyPopulationRegionKey *out)
{
    int64_t localX = (int64_t)regionX * ECOLOGY_POPULATION_REGION_SIZE +
        ECOLOGY_POPULATION_REGION_SIZE / 2 - originX;
    int64_t localZ = (int64_t)regionZ * ECOLOGY_POPULATION_REGION_SIZE +
        ECOLOGY_POPULATION_REGION_SIZE / 2 - originZ;
    if (localX < INT_MIN || localX > INT_MAX ||
        localZ < INT_MIN || localZ > INT_MAX) return false;
    int x = (int)localX;
    int z = (int)localZ;
    EcologyCanonicalizeCell(&x, &z);
    int64_t globalX = (int64_t)originX + x;
    int64_t globalZ = (int64_t)originZ + z;
    if (globalX < INT_MIN || globalX > INT_MAX ||
        globalZ < INT_MIN || globalZ > INT_MAX) return false;
    *out = (EcologyPopulationRegionKey){
        .surfaceId = surfaceId,
        .regionX = EcologyFloorDivide((int)globalX,
                                      ECOLOGY_POPULATION_REGION_SIZE),
        .regionZ = EcologyFloorDivide((int)globalZ,
                                      ECOLOGY_POPULATION_REGION_SIZE)
    };
    return true;
}

static int EcologyPopulationFindWorkingRecordIndex(
    const EcologyPopulationWorkingSet *workingSet,
    const EcologyPopulationRegionKey *key)
{
    for (unsigned index = 0u; index < workingSet->count; index++) {
        const EcologyPopulationRecord *record = &workingSet->records[index];
        if (record->surfaceId == key->surfaceId &&
            record->regionX == key->regionX && record->regionZ == key->regionZ)
            return (int)index;
    }
    return -1;
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
    /* Both online stepping and catch-up use the same time/region-dependent
     * baseline. A caller's current player light or manual weather must not
     * become a historical input merely because a region is revisited. */
    PlanetLocalEcology regional = EcologyHistoricalLocalAt(
        localX, localZ, simulationTime, daylight, profile,
        &state->windX, &state->windZ);
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
    /* Edits have no timestamped history yet. Apply observed disturbance at
     * the current regional boundary, never retroactively over an absence. */
    double observedTime = EcologyPopulationStepTime(SpaceElapsedSimulationTime());
    float disturbance = simulationTime < observedTime ? 0.0f :
        EcologyRegionalDisturbance(record->surfaceId, record->regionX,
                                   record->regionZ, originX, originZ);
    state->floraStress = disturbance * 0.82f;
    float habitatStress = disturbance * 0.94f;
    float harvestStress = EcologyClamp(
        record->population.faunaHarvestPressure) * 0.88f;
    state->faunaStress = EcologyClamp(
        1.0f - (1.0f - habitatStress) * (1.0f - harvestStress));
    state->nutrientEnvironment = EcologyNutrientEnvironmentAt(
        &regional, &record->population, state->floraStress);
}

static void EcologyPopulationInitializeRecordState(
    EcologyPopulationRecord *record, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, int originX, int originZ,
    bool includeEvolution)
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
    if (includeEvolution) EcologyEvolutionInitialize(record, &state, profile);
    record->lastUpdateTime = simulationTime;
}

static void EcologyPopulationInitializeRecord(
    EcologyPopulationRecord *record, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, int originX, int originZ)
{
    EcologyPopulationInitializeRecordState(
        record, simulationTime, daylight, profile, originX, originZ, true);
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
    EcologyPopulationWorkingSet *workingSet, double targetTime)
{
    bool changed = false;
    for (unsigned index = 0; index < workingSet->count; index++) {
        EcologyPopulationRecord *record = &workingSet->records[index];
        if (record->lastUpdateTime > targetTime) {
            record->lastUpdateTime = targetTime;
            record->migration = (PlanetPopulationMigrationState){ 0 };
            changed = true;
        }
    }
    return changed;
}

static double EcologyPopulationEarliestStepStart(
    const EcologyPopulationWorkingSet *workingSet, double targetTime)
{
    double stepStart = INFINITY;
    for (unsigned index = 0; index < workingSet->count; index++) {
        const EcologyPopulationRecord *record = &workingSet->records[index];
        if (record->lastUpdateTime >= targetTime) {
            continue;
        }
        stepStart = fmin(stepStart, record->lastUpdateTime);
    }
    return stepStart;
}

static void EcologyPopulationSampleStep(
    const EcologyPopulationStepContext *context,
    EcologyPopulationStepBuffers *buffers)
{
    for (unsigned index = 0; index < context->workingSet->count; index++) {
        EcologyPopulationRecord *record =
            &context->workingSet->records[index];
        if (record->lastUpdateTime != context->stepStart) {
            continue;
        }
        EcologyEvolutionEnsureInitialized(
            record, context->stepStart, context->daylight, context->profile,
            context->originX, context->originZ);
        buffers->states[index].active = true;
        buffers->states[index].population = record->population;
        buffers->states[index].evolution = record->evolution;
        buffers->states[index].nutrients = record->nutrients;
        buffers->states[index].nutrientInitialized =
            record->nutrientInitialized;
        EcologyPopulationConditionsAt(
            record, context->stepEnd, context->daylight, context->profile,
            context->originX, context->originZ, &buffers->states[index]);
    }
}

/* Pure computation: all world, light and terrain inputs were sampled above. */
static void EcologyPopulationPrepareStep(
    const EcologyPopulationStepContext *context,
    EcologyPopulationStepBuffers *buffers)
{
    for (unsigned index = 0; index < context->workingSet->count; index++) {
        if (!buffers->states[index].active) continue;
        const EcologyPopulationRecord *record =
            &context->workingSet->records[index];
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

/* Resolve topology from the copied working set before migration. */
static void EcologyPopulationBuildNeighbors(
    const EcologyPopulationStepContext *context, int (*neighbors)[2])
{
    static const int directions[2][2] = { { 1, 0 }, { 0, 1 } };
    for (unsigned index = 0; index < context->workingSet->count; index++) {
        const EcologyPopulationRecord *record = &context->workingSet->records[index];
        for (int direction = 0; direction < 2; direction++) {
            EcologyPopulationRegionKey key;
            neighbors[index][direction] = -1;
            if (EcologyPopulationCanonicalRegionKey(
                context->surfaceId,
                record->regionX + directions[direction][0],
                record->regionZ + directions[direction][1],
                    context->originX, context->originZ, &key)) {
                neighbors[index][direction] = EcologyPopulationFindWorkingRecordIndex(
                    context->workingSet, &key);
            }
        }
    }
}

static void EcologyPopulationAccumulateMigration(
    const EcologyPopulationStepContext *context,
    EcologyPopulationStepBuffers *buffers)
{
    static const int directions[2][2] = { { 1, 0 }, { 0, 1 } };
    /*
     * Records may live in either storage tier. Iterating cache slots would
     * make multi-edge gene flow depend on the order in which callers happened
     * to warm that cache.  Sort active regions by their canonical coordinates
     * so every replay applies the same edge order.
     */
    unsigned order[ECOLOGY_POPULATION_MAX_REGIONS];
    unsigned orderCount = 0u;
    for (unsigned index = 0; index < context->workingSet->count; index++) {
        if (!buffers->states[index].active) continue;
        unsigned insertion = orderCount;
        while (insertion > 0u) {
            unsigned previous = order[insertion - 1u];
            EcologyPopulationRecord *previousRecord =
                &context->workingSet->records[previous];
            EcologyPopulationRecord *record =
                &context->workingSet->records[index];
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
        EcologyPopulationRecord *record =
            &context->workingSet->records[index];
        for (int direction = 0; direction < 2; direction++) {
            int neighborIndex = context->neighbors[index][direction];
            if (neighborIndex < 0 || (unsigned)neighborIndex == index ||
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
    for (unsigned index = 0; index < context->workingSet->count; index++) {
        if (!buffers->states[index].active) continue;
        EcologyPopulationRecord *record =
            &context->workingSet->records[index];
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

/* One speculative batch. Ownership passes under the control mutex; compute
 * runs without it against the copied working set and result buffers. */
typedef enum EcologyJobState {
    ECOLOGY_JOB_IDLE, ECOLOGY_JOB_QUEUED, ECOLOGY_JOB_RUNNING, ECOLOGY_JOB_READY
} EcologyJobState;

typedef struct EcologyPopulationJob {
    EcologyPopulationWorkingSet source;
    EcologyPopulationWorkingSet result;
    EcologyPopulationStepBuffers buffers;
    PlanetEcologyProfile profile;
    EcologyPopulationStepContext context;
    int neighbors[ECOLOGY_POPULATION_MAX_REGIONS][2];
    uint32_t epoch;
    uint32_t profileGeneration;
    uint32_t seed;
    uint64_t editRevision;
    double cpuMs;
} EcologyPopulationJob;

static struct {
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    pthread_t thread;
    bool started;
    bool stopping;
    bool canceled;
    EcologyJobState state;
    EcologyPopulationJob *job;
    uint64_t submitted;
    uint64_t committed;
    uint64_t discarded;
    uint64_t synchronous;
    double lastCpuMs;
} ecologyWorker = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .wake = PTHREAD_COND_INITIALIZER
};

static void *EcologyPopulationWorkerMain(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&ecologyWorker.mutex);
    for (;;) {
        while (!ecologyWorker.stopping && ecologyWorker.state != ECOLOGY_JOB_QUEUED)
            pthread_cond_wait(&ecologyWorker.wake, &ecologyWorker.mutex);
        if (ecologyWorker.stopping) break;
        EcologyPopulationJob *job = ecologyWorker.job;
        ecologyWorker.state = ECOLOGY_JOB_RUNNING;
        pthread_mutex_unlock(&ecologyWorker.mutex);

        struct timespec start, finish;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
        EcologyPopulationBuildNeighbors(&job->context, job->neighbors);
        job->context.neighbors = job->neighbors;
        memset(&job->buffers, 0, sizeof(job->buffers));
        EcologyPopulationSampleStep(&job->context, &job->buffers);
        EcologyPopulationPrepareStep(&job->context, &job->buffers);
        EcologyPopulationAccumulateMigration(&job->context, &job->buffers);
        EcologyPopulationCommitStep(&job->context, &job->buffers);
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &finish);
        job->cpuMs = (double)(finish.tv_sec - start.tv_sec) * 1000.0 +
                     (double)(finish.tv_nsec - start.tv_nsec) / 1000000.0;

        pthread_mutex_lock(&ecologyWorker.mutex);
        ecologyWorker.lastCpuMs = job->cpuMs;
        if (ecologyWorker.canceled) {
            ecologyWorker.discarded++;
            ecologyWorker.state = ECOLOGY_JOB_IDLE;
        } else {
            ecologyWorker.state = ECOLOGY_JOB_READY;
        }
        pthread_cond_broadcast(&ecologyWorker.wake);
    }
    pthread_mutex_unlock(&ecologyWorker.mutex);
    return NULL;
}

bool EcologyPopulationStartWorker(void)
{
    if (ecologyWorker.started) return true;
    ecologyWorker.job = calloc(1, sizeof(*ecologyWorker.job));
    if (!ecologyWorker.job) return false;
    ecologyWorker.stopping = false;
    ecologyWorker.state = ECOLOGY_JOB_IDLE;
    ecologyWorker.submitted = ecologyWorker.committed = 0u;
    ecologyWorker.discarded = ecologyWorker.synchronous = 0u;
    ecologyWorker.lastCpuMs = 0.0;
    if (pthread_create(&ecologyWorker.thread, NULL,
                       EcologyPopulationWorkerMain, NULL) != 0) {
        free(ecologyWorker.job);
        ecologyWorker.job = NULL;
        return false;
    }
    ecologyWorker.started = true;
    return true;
}

/* Caller holds the control mutex. The running job retains its memory. */
static void EcologyPopulationCancelJob(void)
{
    if (ecologyWorker.state == ECOLOGY_JOB_RUNNING) {
        ecologyWorker.canceled = true;
    } else if (ecologyWorker.state != ECOLOGY_JOB_IDLE) {
        ecologyWorker.discarded++;
        ecologyWorker.state = ECOLOGY_JOB_IDLE;
    }
}

void EcologyPopulationCancelWorker(void)
{
    pthread_mutex_lock(&ecologyWorker.mutex);
    EcologyPopulationCancelJob();
    /* World reset/load replaces the data read by historical sampling. Wait
     * for an in-flight snapshot job before that storage is changed. */
    while (ecologyWorker.state == ECOLOGY_JOB_RUNNING)
        pthread_cond_wait(&ecologyWorker.wake, &ecologyWorker.mutex);
    pthread_mutex_unlock(&ecologyWorker.mutex);
}

void EcologyPopulationStopWorker(void)
{
    if (!ecologyWorker.started) return;
    pthread_mutex_lock(&ecologyWorker.mutex);
    ecologyWorker.stopping = true;
    pthread_cond_signal(&ecologyWorker.wake);
    pthread_mutex_unlock(&ecologyWorker.mutex);
    pthread_join(ecologyWorker.thread, NULL);
    free(ecologyWorker.job);
    ecologyWorker.job = NULL;
    ecologyWorker.started = false;
    ecologyWorker.state = ECOLOGY_JOB_IDLE;
}

static void EcologyPopulationWorkerDiagnostics(EcologyPopulationDiagnostics *out)
{
    pthread_mutex_lock(&ecologyWorker.mutex);
    out->jobsSubmitted = ecologyWorker.submitted;
    out->jobsCommitted = ecologyWorker.committed;
    out->jobsDiscarded = ecologyWorker.discarded;
    out->synchronousBatches = ecologyWorker.synchronous;
    out->workerCpuMs = ecologyWorker.lastCpuMs;
    out->workerEnabled = ecologyWorker.started;
    out->workerBusy = ecologyWorker.state == ECOLOGY_JOB_QUEUED ||
                      ecologyWorker.state == ECOLOGY_JOB_RUNNING;
    pthread_mutex_unlock(&ecologyWorker.mutex);
}

/* Simulation thread, with population storage locked. Includes identity as
 * epochs can restart after load. Full sampled inputs are rechecked at commit. */
static bool EcologyPopulationJobMatches(const EcologyPopulationJob *job,
    const EcologyPopulationStepContext *context)
{
    if (job->epoch != ecologyPopulationEpoch ||
        job->profileGeneration != EcologyProfileGeneration() ||
        job->seed != EcologyWorldSeed() ||
        job->editRevision != WorldGetEditRevision() ||
        job->context.surfaceId != context->surfaceId ||
        job->context.originX != context->originX ||
        job->context.originZ != context->originZ ||
        job->context.stepStart != context->stepStart ||
        job->context.stepEnd != context->stepEnd ||
        job->context.elapsedTime != context->elapsedTime ||
        fabsf(job->context.daylight - context->daylight) > 0.02f ||
        job->result.count != context->workingSet->count) return false;
    for (unsigned i = 0; i < job->result.count; i++) {
        const EcologyPopulationRecord *a = &job->source.records[i];
        const EcologyPopulationRecord *b = &context->workingSet->records[i];
        if (a->surfaceId != b->surfaceId || a->regionX != b->regionX ||
            a->regionZ != b->regionZ || b->lastUpdateTime != context->stepStart ||
            memcmp(&a->population, &b->population, sizeof(a->population)) != 0 ||
            memcmp(&a->migration, &b->migration, sizeof(a->migration)) != 0 ||
            memcmp(&a->evolution, &b->evolution, sizeof(a->evolution)) != 0 ||
            memcmp(&a->nutrients, &b->nutrients, sizeof(a->nutrients)) != 0 ||
            a->nutrientInitialized != b->nutrientInitialized)
            return false;
    }
    return true;
}

static bool EcologyPopulationTryJob(EcologyPopulationStepContext *context)
{
    if (!ecologyWorker.started) return false;
    pthread_mutex_lock(&ecologyWorker.mutex);
    double started = EcologyPopulationCpuNowMs();
    bool ready = ecologyWorker.state == ECOLOGY_JOB_READY;
    EcologyPopulationJob *job = ecologyWorker.job;
    bool valid = ready && EcologyPopulationJobMatches(job, context);
    if (valid) {
        if (ecologyPopulationDiagnosticsEnabled)
            ecologyPopulationDiagnostics.validationCpuMs +=
                EcologyPopulationCpuNowMs() - started;
    }
    if (valid) {
        memcpy(context->workingSet->records, job->result.records,
               job->result.count * sizeof(job->result.records[0]));
        ecologyWorker.committed++;
        ecologyWorker.state = ECOLOGY_JOB_IDLE;
    } else {
        EcologyPopulationCancelJob();
        ecologyWorker.synchronous++;
    }
    pthread_mutex_unlock(&ecologyWorker.mutex);
    return valid;
}

/* Speculation is bounded to one job, within 0.25 simulation seconds of the
 * next boundary. Events remain synchronous; a changed epoch rejects the job. */
static void EcologyPopulationPrefetch(const EcologyPopulationWorkingSet *source,
    uint32_t surfaceId, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, int originX, int originZ)
{
    if (!ecologyWorker.started || !source->count) return;
    double stepStart = EcologyPopulationStepTime(simulationTime);
    double stepEnd = stepStart + ECOLOGY_POPULATION_STEP_DAYS;
    if (stepEnd - simulationTime > 0.25 || !(stepEnd > simulationTime)) return;
    for (unsigned i = 0; i < source->count; i++)
        if (source->records[i].lastUpdateTime != stepStart) return;

    EcologyPopulationStepContext context = {
        .surfaceId = surfaceId, .stepStart = stepStart, .stepEnd = stepEnd,
        .elapsedTime = stepEnd - stepStart, .daylight = daylight,
        .profile = profile, .originX = originX, .originZ = originZ,
        .workingSet = (EcologyPopulationWorkingSet *)source
    };
    pthread_mutex_lock(&ecologyWorker.mutex);
    if (ecologyWorker.state == ECOLOGY_JOB_RUNNING ||
        ecologyWorker.state == ECOLOGY_JOB_QUEUED) {
        pthread_mutex_unlock(&ecologyWorker.mutex);
        return;
    }
    EcologyPopulationJob *job = ecologyWorker.job;
    if (ecologyWorker.state == ECOLOGY_JOB_READY) {
        if (EcologyPopulationJobMatches(job, &context)) {
            pthread_mutex_unlock(&ecologyWorker.mutex);
            return;
        }
        EcologyPopulationCancelJob();
    }
    double started = EcologyPopulationCpuNowMs();
    job->source.count = source->count;
    memcpy(job->source.records, source->records,
           source->count * sizeof(source->records[0]));
    job->result.count = source->count;
    memcpy(job->result.records, source->records,
           source->count * sizeof(source->records[0]));
    job->profile = *profile;
    job->context = context;
    job->context.profile = &job->profile;
    job->context.workingSet = &job->result;
    job->context.neighbors = job->neighbors;
    job->epoch = ecologyPopulationEpoch;
    job->profileGeneration = EcologyProfileGeneration();
    job->seed = EcologyWorldSeed();
    job->editRevision = WorldGetEditRevision();
    ecologyWorker.canceled = false;
    ecologyWorker.state = ECOLOGY_JOB_QUEUED;
    ecologyWorker.submitted++;
    if (ecologyPopulationDiagnosticsEnabled)
        ecologyPopulationDiagnostics.prefetchCpuMs +=
            EcologyPopulationCpuNowMs() - started;
    pthread_cond_signal(&ecologyWorker.wake);
    pthread_mutex_unlock(&ecologyWorker.mutex);
}

static bool EcologyPopulationAdvanceWorkingSet(
    EcologyPopulationWorkingSet *workingSet,
    uint32_t surfaceId, double targetTime, float daylight,
    const PlanetEcologyProfile *profile, int originX, int originZ,
    unsigned maxStepBatches)
{
    bool changed = false;
    for (unsigned batch = 0u; batch < maxStepBatches; batch++) {
        double stepStart = EcologyPopulationEarliestStepStart(
            workingSet, targetTime);
        if (!isfinite(stepStart)) break;

        double stepEnd = fmin(stepStart + ECOLOGY_POPULATION_STEP_DAYS,
                              targetTime);
        /* Legacy checkpoints may not use the current four-day grid. Stop at
         * the next record time so its neighbors can join the next full batch. */
        for (unsigned index = 0u; index < workingSet->count; index++) {
            double nextTime = workingSet->records[index].lastUpdateTime;
            if (nextTime > stepStart) stepEnd = fmin(stepEnd, nextTime);
        }
        if (!(stepEnd > stepStart)) break;
        double elapsedTime = stepEnd - stepStart;
        EcologyPopulationStepContext context = {
            .surfaceId = surfaceId,
            .stepStart = stepStart,
            .stepEnd = stepEnd,
            .elapsedTime = elapsedTime,
            .daylight = daylight,
            .profile = profile,
            .originX = originX,
            .originZ = originZ,
            .workingSet = workingSet
        };
        if (EcologyPopulationTryJob(&context)) {
            changed = true;
            if (ecologyPopulationDiagnosticsEnabled)
                ecologyPopulationDiagnostics.batchCount++;
            continue;
        }
        EcologyPopulationStepBuffers buffers = { 0 };
        int neighbors[ECOLOGY_POPULATION_MAX_REGIONS][2];
        context.neighbors = neighbors;
        double stageStarted = EcologyPopulationCpuNowMs();
        EcologyPopulationBuildNeighbors(&context, neighbors);
        EcologyPopulationSampleStep(&context, &buffers);
        if (ecologyPopulationDiagnosticsEnabled) {
            ecologyPopulationDiagnostics.sampleCpuMs +=
                EcologyPopulationCpuNowMs() - stageStarted;
        }
        stageStarted = EcologyPopulationCpuNowMs();
        EcologyPopulationPrepareStep(&context, &buffers);
        if (ecologyPopulationDiagnosticsEnabled) {
            ecologyPopulationDiagnostics.prepareCpuMs +=
                EcologyPopulationCpuNowMs() - stageStarted;
        }
        stageStarted = EcologyPopulationCpuNowMs();
        EcologyPopulationAccumulateMigration(&context, &buffers);
        if (ecologyPopulationDiagnosticsEnabled) {
            ecologyPopulationDiagnostics.migrationCpuMs +=
                EcologyPopulationCpuNowMs() - stageStarted;
        }
        stageStarted = EcologyPopulationCpuNowMs();
        if (EcologyPopulationCommitStep(&context, &buffers)) {
            changed = true;
        }
        if (ecologyPopulationDiagnosticsEnabled) {
            ecologyPopulationDiagnostics.commitCpuMs +=
                EcologyPopulationCpuNowMs() - stageStarted;
            ecologyPopulationDiagnostics.batchCount++;
        }
    }

    if (changed) {
        ecologyPopulationEpoch++;
        if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    }
    return !isfinite(EcologyPopulationEarliestStepStart(workingSet, targetTime));
}

/* Retained for explicit legacy adapters. Production uses the scheduled set
 * below, whose membership is independent of rendering and cache residency. */
static void EcologyPopulationCommitWorkingSet(
    const EcologyPopulationWorkingSet *workingSet);

static bool EcologyPopulationAdvanceRecords(
    uint32_t surfaceId, double targetTime, float daylight,
    const PlanetEcologyProfile *profile, int originX, int originZ)
{
    EcologyPopulationWorkingSet workingSet = { 0 };
    for (unsigned index = 0; index < ECOLOGY_POPULATION_MAX_REGIONS; index++) {
        EcologyPopulationRecord *record = &ecologyPopulationRecords[index];
        if (record->valid && record->surfaceId == surfaceId)
            workingSet.records[workingSet.count++] = *record;
    }
    if (EcologyPopulationRewindFutureRecords(&workingSet, targetTime)) {
        ecologyPopulationEpoch++;
        if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    }
    bool complete = EcologyPopulationAdvanceWorkingSet(
        &workingSet, surfaceId, targetTime, daylight, profile, originX, originZ,
        ECOLOGY_POPULATION_LEGACY_STEP_BUDGET);
    EcologyPopulationCommitWorkingSet(&workingSet);
    return complete;
}

static int EcologyPopulationCompareRegionKeys(const void *first,
                                             const void *second)
{
    const EcologyPopulationRegionKey *a = first;
    const EcologyPopulationRegionKey *b = second;
    if (a->surfaceId != b->surfaceId)
        return a->surfaceId < b->surfaceId ? -1 : 1;
    if (a->regionX != b->regionX) return a->regionX < b->regionX ? -1 : 1;
    if (a->regionZ != b->regionZ) return a->regionZ < b->regionZ ? -1 : 1;
    return 0;
}

static void EcologyPopulationCommitWorkingSet(
    const EcologyPopulationWorkingSet *workingSet)
{
    if (!workingSet) return;
    for (unsigned index = 0u; index < workingSet->count; index++) {
        const EcologyPopulationRecord *source = &workingSet->records[index];
        EcologyPopulationRecord *target = EcologyPopulationFindStoredRecord(
            source->surfaceId, source->regionX, source->regionZ);
        if (!target) continue;
        target->lastUpdateTime = source->lastUpdateTime;
        target->population = source->population;
        target->migration = source->migration;
        target->evolution = source->evolution;
        target->nutrients = source->nutrients;
        target->nutrientInitialized = source->nutrientInitialized;
    }
}

bool EcologyPopulationAdvanceActiveRegionsAt(
    int centerX, int centerZ, int radiusCells, double simulationTime,
    float daylight, const PlanetEcologyProfile *profile,
    unsigned maxStepBatches)
{
    ecologyPopulationDiagnostics = (EcologyPopulationDiagnostics){ 0 };
    double totalStarted = EcologyPopulationCpuNowMs();
    if (!profile || !isfinite(simulationTime) || simulationTime < 0.0 ||
        !isfinite(daylight) || radiusCells < 0 ||
        radiusCells > ECOLOGY_POPULATION_MAX_RADIUS_CELLS) return false;
    EcologyCanonicalizeCell(&centerX, &centerZ);
    int originX = EcologyWorldOriginX();
    int originZ = EcologyWorldOriginZ();
    int64_t globalX = (int64_t)originX + centerX;
    int64_t globalZ = (int64_t)originZ + centerZ;
    if (globalX < INT_MIN || globalX > INT_MAX ||
        globalZ < INT_MIN || globalZ > INT_MAX) return false;
    int centerRegionX = EcologyFloorDivide(
        (int)globalX, ECOLOGY_POPULATION_REGION_SIZE);
    int centerRegionZ = EcologyFloorDivide(
        (int)globalZ, ECOLOGY_POPULATION_REGION_SIZE);
    /* A whole-region halo lets edge regions exchange with their immediate
     * neighbors. Canonicalize before sorting so seam aliases occur only once. */
    int extent = (radiusCells + ECOLOGY_POPULATION_REGION_SIZE - 1) /
        ECOLOGY_POPULATION_REGION_SIZE + 1;
    uint32_t surfaceId = EcologyWorldSurfaceId();
    EcologyPopulationRegionKey keys[ECOLOGY_POPULATION_MAX_REGIONS];
    unsigned keyCount = 0u;
    for (int dx = -extent; dx <= extent; dx++) {
        for (int dz = -extent; dz <= extent; dz++) {
            if (!EcologyPopulationCanonicalRegionKey(
                    surfaceId, centerRegionX + dx, centerRegionZ + dz,
                    originX, originZ, &keys[keyCount])) return false;
            keyCount++;
        }
    }
    qsort(keys, keyCount, sizeof(keys[0]), EcologyPopulationCompareRegionKeys);
    unsigned uniqueCount = 0u;
    for (unsigned index = 0u; index < keyCount; index++) {
        if (uniqueCount && EcologyPopulationCompareRegionKeys(
                &keys[uniqueCount - 1u], &keys[index]) == 0) continue;
        keys[uniqueCount++] = keys[index];
    }
    if (ecologyPopulationDiagnosticsEnabled) {
        ecologyPopulationDiagnostics.keyPreparationCpuMs =
            EcologyPopulationCpuNowMs() - totalStarted;
    }

    double targetTime = EcologyPopulationStepTime(simulationTime);
    pthread_mutex_lock(&ecologyPopulationMutex);
    double stageStarted = EcologyPopulationCpuNowMs();
    bool available = true;
    bool createdAny = false;
    for (unsigned index = 0u; index < uniqueCount; index++) {
        const EcologyPopulationRegionKey *key = &keys[index];
        if (EcologyPopulationFindStoredRecord(
                key->surfaceId, key->regionX, key->regionZ)) continue;
        bool created = false;
        EcologyPopulationRecord *record = EcologyPopulationRecordAt(
            key->surfaceId, key->regionX, key->regionZ, &created);
        if (!record) {
            available = false;
            continue;
        }
        if (created) {
            EcologyPopulationInitializeRecord(
                record, targetTime, daylight, profile, originX, originZ);
            createdAny = true;
            if (ecologyPopulationDiagnosticsEnabled) {
                ecologyPopulationDiagnostics.createdRegions++;
            }
        }
    }
    if (ecologyPopulationDiagnosticsEnabled) {
        ecologyPopulationDiagnostics.admissionCpuMs =
            EcologyPopulationCpuNowMs() - stageStarted;
    }
    if (createdAny) {
        ecologyPopulationEpoch++;
        if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    }
    /* Admission may move the cold vector. Copy records only after all
     * admissions, and never depend on which records fit the hot hash table. */
    EcologyPopulationWorkingSet workingSet = { 0 };
    stageStarted = EcologyPopulationCpuNowMs();
    if (available) {
        for (unsigned index = 0u; index < uniqueCount; index++) {
            const EcologyPopulationRegionKey *key = &keys[index];
            EcologyPopulationRecord *record = EcologyPopulationFindStoredRecord(
                key->surfaceId, key->regionX, key->regionZ);
            if (!record) {
                available = false;
                break;
            }
            workingSet.records[workingSet.count++] = *record;
        }
    }
    if (ecologyPopulationDiagnosticsEnabled) {
        ecologyPopulationDiagnostics.workingSetCpuMs =
            EcologyPopulationCpuNowMs() - stageStarted;
        ecologyPopulationDiagnostics.regionCount = workingSet.count;
    }
    bool complete = available && EcologyPopulationAdvanceWorkingSet(
        &workingSet, surfaceId, targetTime, daylight, profile, originX, originZ,
        maxStepBatches);
    if (available) {
        stageStarted = EcologyPopulationCpuNowMs();
        EcologyPopulationCommitWorkingSet(&workingSet);
        if (ecologyPopulationDiagnosticsEnabled) {
            ecologyPopulationDiagnostics.commitCpuMs +=
                EcologyPopulationCpuNowMs() - stageStarted;
        }
    }
    if (complete) {
        EcologyPopulationPrefetch(&workingSet, surfaceId, simulationTime,
            daylight, profile, originX, originZ);
    }
    pthread_mutex_unlock(&ecologyPopulationMutex);
    if (ecologyPopulationDiagnosticsEnabled) {
        ecologyPopulationDiagnostics.totalCpuMs =
            EcologyPopulationCpuNowMs() - totalStarted;
    }
    return complete;
}

/* Called with the population mutex held. A miss is evaluated on the stack;
 * reads never admit a region, promote it, touch LRU order or run catch-up. */
static bool EcologyPopulationReadRecordAt(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, bool includeEvolution,
    EcologyPopulationRecord *out)
{
    if (!out || !profile || !isfinite(simulationTime) || simulationTime < 0.0 ||
        !isfinite(daylight)) return false;
    EcologyCanonicalizeCell(&x, &z);
    int originX = EcologyWorldOriginX();
    int originZ = EcologyWorldOriginZ();
    int regionX = EcologyFloorDivide(originX + x,
                                     ECOLOGY_POPULATION_REGION_SIZE);
    int regionZ = EcologyFloorDivide(originZ + z,
                                     ECOLOGY_POPULATION_REGION_SIZE);
    uint32_t surfaceId = EcologyWorldSurfaceId();
    const EcologyPopulationRecord *stored = EcologyPopulationFindStoredRecord(
        surfaceId, regionX, regionZ);
    if (stored) {
        *out = *stored;
    } else {
        *out = (EcologyPopulationRecord){
            .valid = true, .surfaceId = surfaceId,
            .regionX = regionX, .regionZ = regionZ
        };
        /* Population-only previews do not consume lineages. Avoid developing
         * three founder genomes for every uncached visual/sample query. */
        EcologyPopulationInitializeRecordState(
            out, EcologyPopulationStepTime(simulationTime), daylight, profile,
            originX, originZ, includeEvolution);
    }
    return true;
}

PlanetRegionalPopulation EcologyRegionalPopulationReadAt(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile,
    PlanetPopulationMigrationState *outMigration)
{
    EcologyPopulationRecord record = { 0 };
    pthread_mutex_lock(&ecologyPopulationMutex);
    (void)EcologyPopulationReadRecordAt(
        x, z, simulationTime, daylight, profile, false, &record);
    pthread_mutex_unlock(&ecologyPopulationMutex);
    if (outMigration) *outMigration = record.migration;
    return record.population;
}

bool EcologyEvolutionReadRegionAt(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile, PlanetEvolutionRegion *out)
{
    if (!out) return false;
    EcologyPopulationRecord record = { 0 };
    pthread_mutex_lock(&ecologyPopulationMutex);
    bool valid = EcologyPopulationReadRecordAt(
        x, z, simulationTime, daylight, profile, true, &record);
    pthread_mutex_unlock(&ecologyPopulationMutex);
    *out = record.evolution;
    return valid;
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
    /* A neighboring key can share the same four-way set. Re-pin the requested
     * region after warming neighbors, then catch restored history up before
     * publishing it. Migration only joins records at the same step time. */
    EcologyPopulationRecord *record = EcologyPopulationRecordAt(
        surfaceId, regionX, regionZ, NULL);
    if (!record) {
        pthread_mutex_unlock(&ecologyPopulationMutex);
        return empty;
    }
    EcologyPopulationAdvanceRecords(
        surfaceId, stepTime, daylight, profile, originX, originZ);
    if (createdAny) {
        ecologyPopulationEpoch++;
        if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    }
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
    bool isNew = false;
    EcologyPopulationRecord *record = EcologyPopulationRecordAt(
        surfaceId, regionX, regionZ, &isNew);
    if (!record) return NULL;
    if (isNew) {
        EcologyPopulationInitializeRecord(
            record, stepTime, daylight, profile, originX, originZ);
    }
    EcologyPopulationAdvanceRecords(
        surfaceId, stepTime, daylight, profile, originX, originZ);
    EcologyEvolutionEnsureInitialized(
        record, stepTime, daylight, profile, originX, originZ);
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
    EcologyPopulationRecord snapshot = { 0 };
    bool valid = EcologyPopulationReadRecordAt(
        x, z, simulationTime, daylight, profile, true, &snapshot);
    const EcologyPopulationRecord *record = &snapshot;
    if (!valid || record->evolution.lineageCount == 0u) {
        pthread_mutex_unlock(&ecologyPopulationMutex);
        return false;
    }
    float densityTotal = record->evolution.herbivoreDensity +
        record->evolution.omnivoreDensity +
        record->evolution.carnivoreDensity;
    float roll = (float)(EcologyMix(sampleSeed) & 0x00ffffffu) /
                 16777215.0f * fmaxf(densityTotal, 0.0001f);
    const PlanetEvolutionLineage *selected = NULL;
    float accumulated = 0.0f;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        const PlanetEvolutionLineage *lineage = &record->evolution.lineages[index];
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

static EcologyPopulationRecord *EcologyPopulationEventRecordAt(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile)
{
    EcologyCanonicalizeCell(&x, &z);
    int originX = EcologyWorldOriginX();
    int originZ = EcologyWorldOriginZ();
    int regionX = EcologyFloorDivide(originX + x,
                                     ECOLOGY_POPULATION_REGION_SIZE);
    int regionZ = EcologyFloorDivide(originZ + z,
                                     ECOLOGY_POPULATION_REGION_SIZE);
    uint32_t surfaceId = EcologyWorldSurfaceId();
    double targetTime = EcologyPopulationStepTime(simulationTime);
    EcologyPopulationRecord *record = EcologyPopulationFindStoredRecord(
        surfaceId, regionX, regionZ);
    /* The simulation scheduler owns catch-up. Do not apply an event to an
     * earlier state or secretly run an unbounded regional simulation here. */
    if (record) return record->lastUpdateTime == targetTime ? record : NULL;
    bool created = false;
    record = EcologyPopulationRecordAt(surfaceId, regionX, regionZ, &created);
    if (!record) return NULL;
    if (created) {
        EcologyPopulationInitializeRecord(
            record, targetTime, daylight, profile, originX, originZ);
        ecologyPopulationEpoch++;
        if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    }
    return record;
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
        !isfinite(simulationTime) || simulationTime < 0.0 || !isfinite(daylight) ||
        event < PLANET_EVOLUTION_EVENT_BIRTH ||
        event > PLANET_EVOLUTION_EVENT_PREDATION_DEATH) return false;
    CreaturePhenotype phenotype = { 0 };
    if (createBirthLineage &&
        !EcologyEvolutionGenomeUsable(genome, &phenotype)) return false;
    EcologyCanonicalizeCell(&x, &z);
    pthread_mutex_lock(&ecologyPopulationMutex);
    EcologyPopulationRecord *record = EcologyPopulationEventRecordAt(
        x, z, simulationTime, daylight, profile);
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
    EcologyPopulationRecord *record = EcologyPopulationEventRecordAt(
        x, z, simulationTime, daylight, profile);
    if (!record) {
        pthread_mutex_unlock(&ecologyPopulationMutex);
        return false;
    }
    PlanetPopulationApplyFaunaHarvest(&record->population, eventStrength);
    EcologyEvolutionScaleToFauna(
        &record->evolution, record->population.faunaDensity);
    ecologyPopulationEpoch++;
    if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    pthread_mutex_unlock(&ecologyPopulationMutex);
    return true;
}
