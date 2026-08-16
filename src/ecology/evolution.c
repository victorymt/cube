#include "ecology/evolution.h"

#include <math.h>
#include <string.h>

#define EVOLUTION_MIN_DIMENSION 0.12f
#define EVOLUTION_MAX_DIMENSION 2.40f

typedef struct EvolutionRandom {
    uint32_t state;
} EvolutionRandom;

static uint32_t EvolutionMix(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static uint32_t EvolutionRandomNext(EvolutionRandom *random)
{
    uint32_t value = random->state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    random->state = value != 0u ? value : 0x6d2b79f5u;
    return random->state;
}

static float EvolutionUnit(EvolutionRandom *random)
{
    return (float)(EvolutionRandomNext(random) & 0x00ffffffu) /
           16777215.0f;
}

static float EvolutionClamp(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static uint8_t EvolutionByteAdd(uint8_t value, int change)
{
    int changed = (int)value + change;
    if (changed < 1) changed = 1;
    if (changed > 255) changed = 255;
    return (uint8_t)changed;
}

static DevelopmentGene EvolutionGene(
    uint16_t locusId, uint16_t parentLocusId, CreatureModuleType moduleType,
    uint8_t flags, uint8_t repeatCount, int8_t x, int8_t y, int8_t z,
    uint8_t length, uint8_t width, uint8_t height, uint8_t density,
    uint8_t efficiency)
{
    DevelopmentGene gene = {
        .locusId = locusId,
        .parentLocusId = parentLocusId,
        .moduleType = (uint8_t)moduleType,
        .flags = flags | DEVELOPMENT_GENE_ENABLED,
        .repeatCount = repeatCount,
        .dominance = 128u,
        .offsetX = x,
        .offsetY = y,
        .offsetZ = z,
        .length = length,
        .width = width,
        .height = height,
        .density = density,
        .efficiency = efficiency
    };
    return gene;
}

static void EvolutionAddGene(CreatureChromosome *chromosome,
                             DevelopmentGene gene)
{
    if (chromosome->geneCount >= EVOLUTION_GENES_PER_CHROMOSOME) return;
    chromosome->genes[chromosome->geneCount++] = gene;
}

static void EvolutionSeedController(CreatureChromosome *chromosome,
                                    EvolutionRandom *random)
{
    for (int index = 0; index < EVOLUTION_CONTROLLER_WEIGHTS; index++) {
        chromosome->controller[index] =
            (int8_t)((int)(EvolutionRandomNext(random) % 65u) - 32);
    }
}

static void EvolutionSeedGround(CreatureChromosome *chromosome)
{
    EvolutionAddGene(chromosome, EvolutionGene(
        1u, 0u, CREATURE_MODULE_TORSO, 0u, 1u, 0, 0, 0,
        188u, 126u, 108u, 132u, 156u));
    EvolutionAddGene(chromosome, EvolutionGene(
        2u, 1u, CREATURE_MODULE_HEAD, 0u, 1u, 108, 0, 30,
        94u, 84u, 82u, 118u, 164u));
    EvolutionAddGene(chromosome, EvolutionGene(
        3u, 1u, CREATURE_MODULE_LIMB, DEVELOPMENT_GENE_MIRRORED,
        2u, -48, 88, -74, 112u, 43u, 98u, 126u, 182u));
    EvolutionAddGene(chromosome, EvolutionGene(
        4u, 3u, CREATURE_MODULE_FOOT, DEVELOPMENT_GENE_MIRRORED,
        2u, 0, 0, -92, 66u, 48u, 28u, 110u, 174u));
    EvolutionAddGene(chromosome, EvolutionGene(
        5u, 1u, CREATURE_MODULE_TAIL, 0u, 1u, -116, 0, 12,
        128u, 40u, 36u, 102u, 146u));
    EvolutionAddGene(chromosome, EvolutionGene(
        6u, 2u, CREATURE_MODULE_SENSOR, DEVELOPMENT_GENE_MIRRORED,
        1u, 40, 32, 36, 34u, 22u, 30u, 72u, 202u));
}

static void EvolutionSeedFlight(CreatureChromosome *chromosome)
{
    EvolutionAddGene(chromosome, EvolutionGene(
        1u, 0u, CREATURE_MODULE_TORSO, 0u, 1u, 0, 0, 0,
        160u, 92u, 92u, 84u, 192u));
    EvolutionAddGene(chromosome, EvolutionGene(
        2u, 1u, CREATURE_MODULE_HEAD, 0u, 1u, 104, 0, 20,
        76u, 68u, 66u, 78u, 198u));
    EvolutionAddGene(chromosome, EvolutionGene(
        3u, 1u, CREATURE_MODULE_WING, DEVELOPMENT_GENE_MIRRORED,
        2u, 0, 96, 22, 218u, 112u, 24u, 42u, 220u));
    EvolutionAddGene(chromosome, EvolutionGene(
        4u, 1u, CREATURE_MODULE_LIMB, DEVELOPMENT_GENE_MIRRORED,
        1u, -54, 50, -66, 80u, 34u, 74u, 82u, 176u));
    EvolutionAddGene(chromosome, EvolutionGene(
        5u, 1u, CREATURE_MODULE_TAIL, 0u, 1u, -112, 0, 8,
        116u, 52u, 24u, 48u, 188u));
    EvolutionAddGene(chromosome, EvolutionGene(
        6u, 2u, CREATURE_MODULE_SENSOR, DEVELOPMENT_GENE_MIRRORED,
        1u, 38, 28, 28, 30u, 20u, 26u, 54u, 216u));
}

static void EvolutionSeedAquatic(CreatureChromosome *chromosome)
{
    EvolutionAddGene(chromosome, EvolutionGene(
        1u, 0u, CREATURE_MODULE_TORSO, 0u, 1u, 0, 0, 0,
        210u, 94u, 102u, 112u, 188u));
    EvolutionAddGene(chromosome, EvolutionGene(
        2u, 1u, CREATURE_MODULE_HEAD, 0u, 1u, 112, 0, 4,
        86u, 78u, 82u, 104u, 184u));
    EvolutionAddGene(chromosome, EvolutionGene(
        3u, 1u, CREATURE_MODULE_FIN, DEVELOPMENT_GENE_MIRRORED,
        2u, -12, 82, 0, 98u, 92u, 20u, 54u, 218u));
    EvolutionAddGene(chromosome, EvolutionGene(
        4u, 1u, CREATURE_MODULE_TAIL, 0u, 2u, -112, 0, 0,
        126u, 62u, 60u, 76u, 208u));
    DevelopmentGene tailFin = EvolutionGene(
        5u, 4u, CREATURE_MODULE_FIN, DEVELOPMENT_GENE_MIRRORED |
        DEVELOPMENT_GENE_CROSS_LINK, 1u, -78, 0, 0,
        88u, 72u, 20u, 46u, 224u);
    tailFin.crossLocusId = 1u;
    EvolutionAddGene(chromosome, tailFin);
    EvolutionAddGene(chromosome, EvolutionGene(
        6u, 2u, CREATURE_MODULE_SENSOR, DEVELOPMENT_GENE_MIRRORED,
        1u, 36, 26, 18, 30u, 20u, 26u, 52u, 214u));
}

CreatureGenome EvolutionGenomeSeed(uint32_t seed,
                                   EvolutionArchetype archetype)
{
    CreatureGenome genome = { 0 };
    EvolutionRandom random = { EvolutionMix(seed ^
        ((uint32_t)archetype + 1u) * 0x9e3779b9u) };
    if (random.state == 0u) random.state = 0x6d2b79f5u;
    for (int chromosomeIndex = 0; chromosomeIndex < 2; chromosomeIndex++) {
        CreatureChromosome *chromosome = &genome.chromosomes[chromosomeIndex];
        switch (archetype) {
        case EVOLUTION_ARCHETYPE_FLIGHT:
            EvolutionSeedFlight(chromosome);
            break;
        case EVOLUTION_ARCHETYPE_AQUATIC:
            EvolutionSeedAquatic(chromosome);
            break;
        case EVOLUTION_ARCHETYPE_GROUND:
        default:
            EvolutionSeedGround(chromosome);
            break;
        }
        for (unsigned index = 0; index < chromosome->geneCount; index++) {
            DevelopmentGene *gene = &chromosome->genes[index];
            int variation = (int)(EvolutionRandomNext(&random) % 11u) - 5;
            gene->length = EvolutionByteAdd(gene->length, variation);
            gene->width = EvolutionByteAdd(gene->width, -variation / 2);
            gene->dominance = (uint8_t)(112u +
                EvolutionRandomNext(&random) % 33u);
        }
        EvolutionSeedController(chromosome, &random);
    }
    if (archetype == EVOLUTION_ARCHETYPE_FLIGHT) {
        genome.diet = (uint8_t)(96u + EvolutionRandomNext(&random) % 64u);
    } else if (archetype == EVOLUTION_ARCHETYPE_AQUATIC) {
        genome.diet = (uint8_t)(120u + EvolutionRandomNext(&random) % 73u);
    } else {
        genome.diet = (uint8_t)(52u + EvolutionRandomNext(&random) % 56u);
    }
    genome.pigmentation = (uint8_t)(48u + EvolutionRandomNext(&random) % 176u);
    genome.maturity = (uint8_t)(92u + EvolutionRandomNext(&random) % 92u);
    genome.genomeId = EvolutionGenomeHash(&genome);
    return genome;
}

static const DevelopmentGene *EvolutionFindGene(
    const CreatureChromosome *chromosome, uint16_t locusId)
{
    if (!chromosome || locusId == 0u) return NULL;
    for (unsigned index = 0; index < chromosome->geneCount; index++) {
        if (chromosome->genes[index].locusId == locusId) {
            return &chromosome->genes[index];
        }
    }
    return NULL;
}

static const DevelopmentGene *EvolutionExpressedGene(
    const CreatureGenome *genome, uint16_t locusId)
{
    const DevelopmentGene *first = EvolutionFindGene(&genome->chromosomes[0],
                                                      locusId);
    const DevelopmentGene *second = EvolutionFindGene(&genome->chromosomes[1],
                                                       locusId);
    if (!first) return second;
    if (!second) return first;
    if ((first->flags & DEVELOPMENT_GENE_ENABLED) == 0u) return second;
    if ((second->flags & DEVELOPMENT_GENE_ENABLED) == 0u) return first;
    return first->dominance >= second->dominance ? first : second;
}

static void EvolutionMutateGene(DevelopmentGene *gene,
                                EvolutionRandom *random, float rate,
                                uint32_t *mutationCount)
{
    if (EvolutionUnit(random) < rate) {
        int change = (int)(EvolutionRandomNext(random) % 31u) - 15;
        switch (EvolutionRandomNext(random) % 10u) {
        case 0: gene->length = EvolutionByteAdd(gene->length, change); break;
        case 1: gene->width = EvolutionByteAdd(gene->width, change); break;
        case 2: gene->height = EvolutionByteAdd(gene->height, change); break;
        case 3: gene->density = EvolutionByteAdd(gene->density, change); break;
        case 4: gene->efficiency = EvolutionByteAdd(gene->efficiency, change); break;
        case 5:
            gene->offsetX = (int8_t)EvolutionClamp(
                (float)gene->offsetX + (float)change, -120.0f, 120.0f);
            break;
        case 6:
            gene->offsetY = (int8_t)EvolutionClamp(
                (float)gene->offsetY + (float)change, -120.0f, 120.0f);
            break;
        case 7:
            gene->offsetZ = (int8_t)EvolutionClamp(
                (float)gene->offsetZ + (float)change, -120.0f, 120.0f);
            break;
        case 8:
            gene->dominance = EvolutionByteAdd(gene->dominance, change);
            break;
        default:
            gene->repeatCount = (uint8_t)EvolutionClamp(
                (float)gene->repeatCount + (change < 0 ? -1.0f : 1.0f),
                1.0f, 3.0f);
            break;
        }
        (*mutationCount)++;
    }
    if (EvolutionUnit(random) < rate * 0.16f && gene->locusId != 1u) {
        gene->flags ^= DEVELOPMENT_GENE_ENABLED;
        (*mutationCount)++;
    }
    if (EvolutionUnit(random) < rate * 0.10f && gene->locusId != 1u) {
        gene->parentLocusId = (uint16_t)(1u +
            EvolutionRandomNext(random) % (gene->locusId - 1u));
        (*mutationCount)++;
    }
    if (gene->locusId > 2u && EvolutionUnit(random) < rate * 0.14f) {
        if ((gene->flags & DEVELOPMENT_GENE_CROSS_LINK) != 0u &&
            EvolutionUnit(random) < 0.25f) {
            gene->flags &= (uint8_t)~DEVELOPMENT_GENE_CROSS_LINK;
            gene->crossLocusId = 0u;
        } else {
            gene->flags |= DEVELOPMENT_GENE_CROSS_LINK;
            gene->crossLocusId = (uint16_t)(1u +
                EvolutionRandomNext(random) % (gene->locusId - 1u));
            if (gene->crossLocusId == gene->parentLocusId &&
                gene->locusId > 3u) {
                gene->crossLocusId = gene->crossLocusId == 1u ? 2u : 1u;
            }
        }
        (*mutationCount)++;
    }
}

static uint16_t EvolutionFirstUnusedLocus(
    const CreatureChromosome *chromosome)
{
    for (uint16_t locus = 2u; locus <= EVOLUTION_GENES_PER_CHROMOSOME;
         locus++) {
        if (!EvolutionFindGene(chromosome, locus)) return locus;
    }
    return 0u;
}

static void EvolutionDuplicateGene(CreatureChromosome *chromosome,
                                   EvolutionRandom *random,
                                   float mutationRate,
                                   uint32_t *mutationCount)
{
    if (!chromosome || chromosome->geneCount < 2u ||
        chromosome->geneCount >= EVOLUTION_GENES_PER_CHROMOSOME ||
        EvolutionUnit(random) >= mutationRate * 0.35f) {
        return;
    }
    uint16_t locus = EvolutionFirstUnusedLocus(chromosome);
    if (locus == 0u) return;
    unsigned sourceIndex = 1u + EvolutionRandomNext(random) %
        (chromosome->geneCount - 1u);
    DevelopmentGene duplicate = chromosome->genes[sourceIndex];
    duplicate.locusId = locus;
    duplicate.parentLocusId = chromosome->genes[sourceIndex].locusId;
    duplicate.crossLocusId = 0u;
    duplicate.flags = (uint8_t)((duplicate.flags |
        DEVELOPMENT_GENE_ENABLED) & ~DEVELOPMENT_GENE_CROSS_LINK);
    int offsetChange = (int)(EvolutionRandomNext(random) % 25u) - 12;
    duplicate.offsetX = (int8_t)EvolutionClamp(
        (float)duplicate.offsetX + (float)offsetChange, -120.0f, 120.0f);
    duplicate.offsetZ = (int8_t)EvolutionClamp(
        (float)duplicate.offsetZ - (float)offsetChange, -120.0f, 120.0f);
    chromosome->genes[chromosome->geneCount++] = duplicate;
    (*mutationCount)++;
}

static CreatureChromosome EvolutionGamete(const CreatureGenome *parent,
                                           EvolutionRandom *random,
                                           float mutationRate,
                                           uint32_t *mutationCount)
{
    CreatureChromosome gamete = { 0 };
    unsigned selected = EvolutionRandomNext(random) & 1u;
    uint16_t loci[EVOLUTION_GENES_PER_CHROMOSOME * 2] = { 0 };
    unsigned locusCount = 0u;
    for (int chromosomeIndex = 0; chromosomeIndex < 2; chromosomeIndex++) {
        const CreatureChromosome *chromosome =
            &parent->chromosomes[chromosomeIndex];
        for (unsigned index = 0; index < chromosome->geneCount; index++) {
            uint16_t locus = chromosome->genes[index].locusId;
            bool known = false;
            for (unsigned prior = 0; prior < locusCount; prior++) {
                if (loci[prior] == locus) known = true;
            }
            if (!known && locusCount < sizeof(loci) / sizeof(loci[0])) {
                loci[locusCount++] = locus;
            }
        }
    }
    for (unsigned first = 0; first < locusCount; first++) {
        for (unsigned second = first + 1; second < locusCount; second++) {
            if (loci[second] < loci[first]) {
                uint16_t swap = loci[first];
                loci[first] = loci[second];
                loci[second] = swap;
            }
        }
    }
    for (unsigned index = 0; index < locusCount &&
         gamete.geneCount < EVOLUTION_GENES_PER_CHROMOSOME; index++) {
        if (index > 0u && EvolutionUnit(random) < 0.18f) selected ^= 1u;
        const DevelopmentGene *gene = EvolutionFindGene(
            &parent->chromosomes[selected], loci[index]);
        if (!gene) gene = EvolutionFindGene(
            &parent->chromosomes[selected ^ 1u], loci[index]);
        if (!gene) continue;
        DevelopmentGene inherited = *gene;
        EvolutionMutateGene(&inherited, random, mutationRate, mutationCount);
        gamete.genes[gamete.geneCount++] = inherited;
    }
    EvolutionDuplicateGene(&gamete, random, mutationRate, mutationCount);
    selected = EvolutionRandomNext(random) & 1u;
    for (int index = 0; index < EVOLUTION_CONTROLLER_WEIGHTS; index++) {
        if (index > 0 && EvolutionUnit(random) < 0.08f) selected ^= 1u;
        int value = parent->chromosomes[selected].controller[index];
        if (EvolutionUnit(random) < mutationRate) {
            value += (int)(EvolutionRandomNext(random) % 17u) - 8;
            if (value < -127) value = -127;
            if (value > 127) value = 127;
            (*mutationCount)++;
        }
        gamete.controller[index] = (int8_t)value;
    }
    return gamete;
}

CreatureGenome EvolutionGenomeBreed(const CreatureGenome *mother,
                                    const CreatureGenome *father,
                                    uint32_t birthSeed,
                                    float mutationRate)
{
    if (!mother || !father) return (CreatureGenome){ 0 };
    CreatureGenome child = { 0 };
    EvolutionRandom random = { EvolutionMix(birthSeed ^ mother->genomeId ^
        (father->genomeId * 0x9e3779b9u)) };
    if (random.state == 0u) random.state = 0x6d2b79f5u;
    mutationRate = EvolutionClamp(mutationRate, 0.0f, 1.0f);
    child.chromosomes[0] = EvolutionGamete(
        mother, &random, mutationRate, &child.mutationCount);
    child.chromosomes[1] = EvolutionGamete(
        father, &random, mutationRate, &child.mutationCount);
    child.motherGenomeId = mother->genomeId;
    child.fatherGenomeId = father->genomeId;
    unsigned parentGeneration = mother->generation > father->generation ?
        mother->generation : father->generation;
    child.generation = (uint8_t)(parentGeneration < 255u ?
        parentGeneration + 1u : 255u);
    child.diet = EvolutionUnit(&random) < 0.5f ? mother->diet : father->diet;
    child.pigmentation = EvolutionUnit(&random) < 0.5f ?
        mother->pigmentation : father->pigmentation;
    child.maturity = EvolutionUnit(&random) < 0.5f ?
        mother->maturity : father->maturity;
    if (EvolutionUnit(&random) < mutationRate) {
        child.diet = EvolutionByteAdd(child.diet,
            (int)(EvolutionRandomNext(&random) % 25u) - 12);
        child.mutationCount++;
    }
    child.genomeId = EvolutionGenomeHash(&child);
    return child;
}

uint32_t EvolutionGenomeHash(const CreatureGenome *genome)
{
    if (!genome) return 0u;
    const unsigned char *bytes = (const unsigned char *)genome;
    size_t start = offsetof(CreatureGenome, motherGenomeId);
    uint32_t hash = 2166136261u;
    for (size_t index = start; index < sizeof(*genome); index++) {
        hash ^= bytes[index];
        hash *= 16777619u;
    }
    hash = EvolutionMix(hash);
    return hash != 0u ? hash : 1u;
}

float EvolutionGenomeDistance(const CreatureGenome *first,
                              const CreatureGenome *second)
{
    if (!first || !second) return 1.0f;
    double difference = 0.0;
    double comparisons = 0.0;
    for (unsigned locus = 1u; locus <= EVOLUTION_GENES_PER_CHROMOSOME;
         locus++) {
        const DevelopmentGene *a = EvolutionExpressedGene(first, (uint16_t)locus);
        const DevelopmentGene *b = EvolutionExpressedGene(second, (uint16_t)locus);
        if (!a && !b) continue;
        comparisons += 1.0;
        if (!a || !b) {
            difference += 1.0;
            continue;
        }
        difference += a->moduleType != b->moduleType ? 0.24 : 0.0;
        difference += a->parentLocusId != b->parentLocusId ? 0.18 : 0.0;
        difference += a->crossLocusId != b->crossLocusId ? 0.08 : 0.0;
        difference += ((a->flags ^ b->flags) & DEVELOPMENT_GENE_ENABLED) ?
                      0.18 : 0.0;
        difference += ((a->flags ^ b->flags) &
                       (DEVELOPMENT_GENE_MIRRORED |
                        DEVELOPMENT_GENE_CROSS_LINK)) ? 0.08 : 0.0;
        difference += a->repeatCount != b->repeatCount ? 0.06 : 0.0;
        difference += fabs((double)a->length - b->length) / 255.0 * 0.10;
        difference += fabs((double)a->width - b->width) / 255.0 * 0.10;
        difference += fabs((double)a->height - b->height) / 255.0 * 0.10;
        difference += fabs((double)a->efficiency - b->efficiency) /
                      255.0 * 0.10;
    }
    difference += fabs((double)first->diet - second->diet) / 255.0;
    comparisons += 1.0;
    return comparisons > 0.0 ?
        EvolutionClamp((float)(difference / comparisons), 0.0f, 1.0f) : 0.0f;
}

static float EvolutionDimension(uint8_t encoded)
{
    return EVOLUTION_MIN_DIMENSION + (float)encoded / 255.0f *
           (EVOLUTION_MAX_DIMENSION - EVOLUTION_MIN_DIMENSION);
}

static int EvolutionFirstModuleForLocus(const CreaturePhenotype *phenotype,
                                        uint16_t locusId)
{
    for (unsigned index = 0; index < phenotype->moduleCount; index++) {
        if (phenotype->modules[index].sourceLocusId == locusId) {
            return (int)index;
        }
    }
    return -1;
}

static void EvolutionAddConnection(CreaturePhenotype *phenotype,
                                   int first, int second, float strength,
                                   bool articulated)
{
    if (first < 0 || second < 0 || first == second ||
        phenotype->connectionCount >= EVOLUTION_MAX_CONNECTIONS) return;
    for (unsigned index = 0; index < phenotype->connectionCount; index++) {
        CreatureConnection *connection = &phenotype->connections[index];
        if ((connection->first == first && connection->second == second) ||
            (connection->first == second && connection->second == first)) {
            return;
        }
    }
    phenotype->connections[phenotype->connectionCount++] =
        (CreatureConnection){ (uint8_t)first, (uint8_t)second,
                              strength, articulated };
}

static void EvolutionAddModules(CreaturePhenotype *phenotype,
                                const DevelopmentGene *gene, int parent)
{
    unsigned repeats = gene->repeatCount;
    if (repeats < 1u) repeats = 1u;
    if (repeats > 3u) repeats = 3u;
    unsigned sides = (gene->flags & DEVELOPMENT_GENE_MIRRORED) ? 2u : 1u;
    for (unsigned repeat = 0; repeat < repeats; repeat++) {
        for (unsigned side = 0; side < sides; side++) {
            if (phenotype->moduleCount >= EVOLUTION_MAX_MODULES) {
                phenotype->repaired = true;
                return;
            }
            float sideSign = side == 0u ? 1.0f : -1.0f;
            float repeatOffset = repeats > 1u ?
                ((float)repeat - (float)(repeats - 1u) * 0.5f) * 0.48f : 0.0f;
            CreatureModule module = {
                .sourceLocusId = gene->locusId,
                .type = gene->moduleType < CREATURE_MODULE_TYPE_COUNT ?
                        gene->moduleType : CREATURE_MODULE_LIMB,
                .parentIndex = (int8_t)parent,
                .localX = (float)gene->offsetX / 80.0f + repeatOffset,
                .localY = (float)gene->offsetY / 80.0f * sideSign,
                .localZ = (float)gene->offsetZ / 80.0f,
                .length = EvolutionDimension(gene->length),
                .width = EvolutionDimension(gene->width),
                .height = EvolutionDimension(gene->height),
                .efficiency = 0.35f + (float)gene->efficiency / 255.0f * 0.90f
            };
            float density = 0.35f + (float)gene->density / 255.0f * 1.65f;
            module.mass = module.length * module.width * module.height * density;
            int moduleIndex = (int)phenotype->moduleCount;
            phenotype->modules[phenotype->moduleCount++] = module;
            if (parent >= 0) {
                EvolutionAddConnection(phenotype, parent, moduleIndex,
                                       module.efficiency, true);
            }
        }
    }
}

CreaturePhenotype EvolutionDevelop(const CreatureGenome *genome)
{
    CreaturePhenotype phenotype = { 0 };
    if (!genome) return phenotype;
    bool developed[EVOLUTION_GENES_PER_CHROMOSOME + 1] = { false };
    for (unsigned pass = 0; pass <= EVOLUTION_GENES_PER_CHROMOSOME; pass++) {
        bool changed = false;
        for (uint16_t locus = 1u; locus <= EVOLUTION_GENES_PER_CHROMOSOME;
             locus++) {
            if (developed[locus]) continue;
            const DevelopmentGene *gene = EvolutionExpressedGene(genome, locus);
            if (!gene || (gene->flags & DEVELOPMENT_GENE_ENABLED) == 0u) {
                developed[locus] = true;
                continue;
            }
            int parent = -1;
            if (gene->parentLocusId != 0u) {
                parent = EvolutionFirstModuleForLocus(
                    &phenotype, gene->parentLocusId);
                if (parent < 0 && pass < EVOLUTION_GENES_PER_CHROMOSOME) continue;
                if (parent < 0) {
                    parent = phenotype.moduleCount > 0u ? 0 : -1;
                    phenotype.repaired = true;
                }
            } else if (phenotype.moduleCount > 0u) {
                parent = 0;
                phenotype.repaired = true;
            }
            EvolutionAddModules(&phenotype, gene, parent);
            developed[locus] = true;
            changed = true;
        }
        if (!changed) break;
    }
    for (uint16_t locus = 1u; locus <= EVOLUTION_GENES_PER_CHROMOSOME;
         locus++) {
        if (developed[locus]) continue;
        const DevelopmentGene *gene = EvolutionExpressedGene(genome, locus);
        if (!gene || (gene->flags & DEVELOPMENT_GENE_ENABLED) == 0u) continue;
        int parent = EvolutionFirstModuleForLocus(
            &phenotype, gene->parentLocusId);
        if (parent < 0) parent = phenotype.moduleCount > 0u ? 0 : -1;
        EvolutionAddModules(&phenotype, gene, parent);
        developed[locus] = true;
        phenotype.repaired = true;
    }
    if (phenotype.moduleCount == 0u ||
        phenotype.modules[0].type != CREATURE_MODULE_TORSO) {
        return phenotype;
    }
    for (uint16_t locus = 1u; locus <= EVOLUTION_GENES_PER_CHROMOSOME;
         locus++) {
        const DevelopmentGene *gene = EvolutionExpressedGene(genome, locus);
        if (!gene || (gene->flags & DEVELOPMENT_GENE_CROSS_LINK) == 0u) continue;
        int first = EvolutionFirstModuleForLocus(&phenotype, locus);
        int second = EvolutionFirstModuleForLocus(&phenotype,
                                                  gene->crossLocusId);
        EvolutionAddConnection(&phenotype, first, second,
                               0.4f + (float)gene->efficiency / 425.0f,
                               false);
    }

    int limbs = 0;
    int feet = 0;
    int wings = 0;
    int fins = 0;
    float maxExtent = 0.0f;
    float protection = 0.0f;
    float propulsion = 0.0f;
    for (unsigned index = 0; index < phenotype.moduleCount; index++) {
        const CreatureModule *module = &phenotype.modules[index];
        phenotype.totalMass += module->mass;
        float extent = fabsf(module->localX) + module->length * 0.5f;
        if (extent > maxExtent) maxExtent = extent;
        float radius = fabsf(module->localY) + module->width * 0.5f;
        if (radius > phenotype.bodyRadius) phenotype.bodyRadius = radius;
        switch ((CreatureModuleType)module->type) {
        case CREATURE_MODULE_LIMB: limbs++; propulsion += module->efficiency; break;
        case CREATURE_MODULE_FOOT: feet++; break;
        case CREATURE_MODULE_WING:
            wings++;
            phenotype.lift += module->length * module->width * module->efficiency;
            break;
        case CREATURE_MODULE_FIN:
            fins++;
            phenotype.buoyancy += module->length * module->width *
                                  module->efficiency;
            break;
        case CREATURE_MODULE_ARMOR:
            protection += module->mass * module->efficiency;
            break;
        case CREATURE_MODULE_HEAD:
            phenotype.attack += module->mass * 0.65f + module->efficiency;
            break;
        default:
            break;
        }
    }
    phenotype.bodyLength = EvolutionClamp(maxExtent * 2.0f, 0.4f, 8.0f);
    phenotype.bodyRadius = EvolutionClamp(phenotype.bodyRadius, 0.18f, 2.5f);
    phenotype.diet = (float)genome->diet / 255.0f;
    phenotype.energyCost = EvolutionClamp(
        phenotype.totalMass * 0.12f + (float)phenotype.moduleCount * 0.025f,
        0.1f, 12.0f);
    phenotype.defense = EvolutionClamp(
        protection * 0.32f + phenotype.totalMass * 0.08f, 0.1f, 12.0f);
    phenotype.attack = EvolutionClamp(
        phenotype.attack + phenotype.diet * phenotype.totalMass * 0.12f,
        0.1f, 12.0f);
    phenotype.maturityAgeDays = 8.0f + (float)genome->maturity / 255.0f * 32.0f;

    float massRoot = sqrtf(fmaxf(phenotype.totalMass, 0.05f));
    if (wings >= 2 && phenotype.lift > phenotype.totalMass * 0.48f) {
        phenotype.locomotion = CREATURE_LOCOMOTION_FLIGHT;
        propulsion = phenotype.lift;
    } else if (fins >= 2 && phenotype.buoyancy > phenotype.totalMass * 0.18f) {
        phenotype.locomotion = CREATURE_LOCOMOTION_AQUATIC;
        propulsion = phenotype.buoyancy;
    } else if (limbs >= 2 && feet >= 2) {
        phenotype.locomotion = CREATURE_LOCOMOTION_GROUND;
    }
    phenotype.cruiseSpeed = phenotype.locomotion == CREATURE_LOCOMOTION_INVALID ?
        0.0f : EvolutionClamp((0.55f + propulsion * 0.18f) / massRoot,
                              0.18f, 5.5f);
    phenotype.valid = EvolutionPhenotypeValid(&phenotype);
    return phenotype;
}

bool EvolutionPhenotypeValid(const CreaturePhenotype *phenotype)
{
    if (!phenotype || phenotype->moduleCount == 0u ||
        phenotype->moduleCount > EVOLUTION_MAX_MODULES ||
        phenotype->connectionCount > EVOLUTION_MAX_CONNECTIONS ||
        phenotype->locomotion == CREATURE_LOCOMOTION_INVALID ||
        !isfinite(phenotype->totalMass) || phenotype->totalMass <= 0.0f ||
        !isfinite(phenotype->bodyRadius) || phenotype->bodyRadius <= 0.0f ||
        !isfinite(phenotype->cruiseSpeed) || phenotype->cruiseSpeed <= 0.0f) {
        return false;
    }
    bool reached[EVOLUTION_MAX_MODULES] = { false };
    reached[0] = true;
    for (unsigned pass = 0; pass < phenotype->moduleCount; pass++) {
        for (unsigned index = 0; index < phenotype->connectionCount; index++) {
            const CreatureConnection *connection = &phenotype->connections[index];
            if (connection->first >= phenotype->moduleCount ||
                connection->second >= phenotype->moduleCount) return false;
            if (reached[connection->first]) reached[connection->second] = true;
            if (reached[connection->second]) reached[connection->first] = true;
        }
    }
    for (unsigned index = 0; index < phenotype->moduleCount; index++) {
        const CreatureModule *module = &phenotype->modules[index];
        if (!reached[index] || module->type >= CREATURE_MODULE_TYPE_COUNT ||
            !isfinite(module->mass) || module->mass <= 0.0f ||
            !isfinite(module->length) || module->length < EVOLUTION_MIN_DIMENSION ||
            !isfinite(module->width) || module->width < EVOLUTION_MIN_DIMENSION ||
            !isfinite(module->height) || module->height < EVOLUTION_MIN_DIMENSION) {
            return false;
        }
    }
    return true;
}

bool EvolutionShouldSpeciate(float geneticDistance, float geneFlow,
                             unsigned isolatedGenerations)
{
    return isfinite(geneticDistance) && isfinite(geneFlow) &&
           geneticDistance > 0.35f && geneFlow < 0.05f &&
           isolatedGenerations >= 3u;
}

void EvolutionControllerEvaluate(
    const CreatureGenome *genome,
    const float inputs[EVOLUTION_CONTROLLER_INPUTS],
    float outputs[EVOLUTION_CONTROLLER_OUTPUTS])
{
    if (!outputs) return;
    for (int output = 0; output < EVOLUTION_CONTROLLER_OUTPUTS; output++) {
        outputs[output] = 0.0f;
    }
    if (!genome || !inputs) return;
    float hidden[EVOLUTION_CONTROLLER_HIDDEN] = { 0 };
    for (int neuron = 0; neuron < EVOLUTION_CONTROLLER_HIDDEN; neuron++) {
        float sum = 0.0f;
        for (int input = 0; input < EVOLUTION_CONTROLLER_INPUTS; input++) {
            int index = input * EVOLUTION_CONTROLLER_HIDDEN + neuron;
            float weight = ((float)genome->chromosomes[0].controller[index] +
                            (float)genome->chromosomes[1].controller[index]) /
                           254.0f;
            sum += EvolutionClamp(inputs[input], -1.0f, 1.0f) * weight;
        }
        hidden[neuron] = tanhf(sum);
    }
    int outputStart = EVOLUTION_CONTROLLER_INPUTS *
                      EVOLUTION_CONTROLLER_HIDDEN;
    for (int output = 0; output < EVOLUTION_CONTROLLER_OUTPUTS; output++) {
        float sum = 0.0f;
        for (int neuron = 0; neuron < EVOLUTION_CONTROLLER_HIDDEN; neuron++) {
            int index = outputStart + neuron * EVOLUTION_CONTROLLER_OUTPUTS +
                        output;
            float weight = ((float)genome->chromosomes[0].controller[index] +
                            (float)genome->chromosomes[1].controller[index]) /
                           254.0f;
            sum += hidden[neuron] * weight;
        }
        outputs[output] = tanhf(sum);
    }
}

const char *EvolutionLocomotionName(CreatureLocomotion locomotion)
{
    switch (locomotion) {
    case CREATURE_LOCOMOTION_GROUND: return "ground";
    case CREATURE_LOCOMOTION_FLIGHT: return "flight";
    case CREATURE_LOCOMOTION_AQUATIC: return "aquatic";
    case CREATURE_LOCOMOTION_INVALID:
    default: return "invalid";
    }
}
