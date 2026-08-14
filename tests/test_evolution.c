#include "evolution.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void AssertPhenotypeValid(const CreaturePhenotype *phenotype)
{
    assert(phenotype->valid);
    assert(EvolutionPhenotypeValid(phenotype));
    assert(phenotype->moduleCount > 0u);
    assert(phenotype->moduleCount <= EVOLUTION_MAX_MODULES);
    assert(phenotype->connectionCount <= EVOLUTION_MAX_CONNECTIONS);
    assert(isfinite(phenotype->totalMass));
    assert(phenotype->totalMass > 0.0f);
    assert(isfinite(phenotype->cruiseSpeed));
    assert(phenotype->cruiseSpeed > 0.0f);
}

static void TestSeedArchetypes(void)
{
    const CreatureLocomotion expected[] = {
        CREATURE_LOCOMOTION_GROUND,
        CREATURE_LOCOMOTION_FLIGHT,
        CREATURE_LOCOMOTION_AQUATIC
    };
    for (unsigned archetype = 0; archetype < 3u; archetype++) {
        CreatureGenome genome = EvolutionGenomeSeed(
            0x12345678u, (EvolutionArchetype)archetype);
        CreaturePhenotype phenotype = EvolutionDevelop(&genome);
        AssertPhenotypeValid(&phenotype);
        assert(phenotype.locomotion == expected[archetype]);
        assert(genome.genomeId != 0u);
    }
}

static void TestDeterministicInheritance(void)
{
    CreatureGenome mother = EvolutionGenomeSeed(
        144u, EVOLUTION_ARCHETYPE_GROUND);
    CreatureGenome father = EvolutionGenomeSeed(
        912u, EVOLUTION_ARCHETYPE_GROUND);
    CreatureGenome first = EvolutionGenomeBreed(
        &mother, &father, 4001u, 0.08f);
    CreatureGenome replay = EvolutionGenomeBreed(
        &mother, &father, 4001u, 0.08f);
    CreatureGenome sibling = EvolutionGenomeBreed(
        &mother, &father, 4002u, 0.08f);
    assert(memcmp(&first, &replay, sizeof(first)) == 0);
    assert(first.genomeId == EvolutionGenomeHash(&first));
    assert(first.genomeId != sibling.genomeId);
    assert(first.motherGenomeId == mother.genomeId);
    assert(first.fatherGenomeId == father.genomeId);
    CreaturePhenotype phenotype = EvolutionDevelop(&first);
    AssertPhenotypeValid(&phenotype);
}

static void TestCrossSeedProperties(void)
{
    for (uint32_t seed = 1u; seed <= 300u; seed++) {
        EvolutionArchetype archetype = (EvolutionArchetype)(seed % 3u);
        CreatureGenome first = EvolutionGenomeSeed(seed, archetype);
        CreatureGenome second = EvolutionGenomeSeed(seed * 17u, archetype);
        CreatureGenome child = EvolutionGenomeBreed(
            &first, &second, seed * 101u, 0.025f);
        CreaturePhenotype phenotype = EvolutionDevelop(&child);
        AssertPhenotypeValid(&phenotype);
        float distance = EvolutionGenomeDistance(&first, &child);
        assert(isfinite(distance));
        assert(distance >= 0.0f && distance <= 1.0f);
    }
}

static void TestRepairAndLimits(void)
{
    CreatureGenome genome = EvolutionGenomeSeed(
        55u, EVOLUTION_ARCHETYPE_GROUND);
    DevelopmentGene *gene = &genome.chromosomes[0].genes[2];
    gene->parentLocusId = 31u;
    gene->repeatCount = 255u;
    genome.chromosomes[1].genes[2] = *gene;
    CreaturePhenotype phenotype = EvolutionDevelop(&genome);
    AssertPhenotypeValid(&phenotype);
    assert(phenotype.repaired);
    assert(phenotype.moduleCount <= EVOLUTION_MAX_MODULES);
}

static void TestControllerAndSpeciation(void)
{
    CreatureGenome genome = EvolutionGenomeSeed(
        818u, EVOLUTION_ARCHETYPE_FLIGHT);
    float inputs[EVOLUTION_CONTROLLER_INPUTS] = {
        1.0f, -0.5f, 0.2f, 0.8f, -1.0f, 0.0f, 0.4f, 0.7f
    };
    float first[EVOLUTION_CONTROLLER_OUTPUTS];
    float replay[EVOLUTION_CONTROLLER_OUTPUTS];
    EvolutionControllerEvaluate(&genome, inputs, first);
    EvolutionControllerEvaluate(&genome, inputs, replay);
    assert(memcmp(first, replay, sizeof(first)) == 0);
    for (int index = 0; index < EVOLUTION_CONTROLLER_OUTPUTS; index++) {
        assert(isfinite(first[index]));
        assert(first[index] >= -1.0f && first[index] <= 1.0f);
    }
    assert(!EvolutionShouldSpeciate(0.35f, 0.01f, 4u));
    assert(!EvolutionShouldSpeciate(0.50f, 0.05f, 4u));
    assert(!EvolutionShouldSpeciate(0.50f, 0.01f, 2u));
    assert(EvolutionShouldSpeciate(0.50f, 0.01f, 3u));
}

static void AssertChromosomeStructure(const CreatureChromosome *chromosome)
{
    assert(chromosome->geneCount > 0u);
    assert(chromosome->geneCount <= EVOLUTION_GENES_PER_CHROMOSOME);
    bool seen[EVOLUTION_GENES_PER_CHROMOSOME + 1] = { false };
    for (unsigned index = 0; index < chromosome->geneCount; index++) {
        const DevelopmentGene *gene = &chromosome->genes[index];
        assert(gene->locusId > 0u);
        assert(gene->locusId <= EVOLUTION_GENES_PER_CHROMOSOME);
        assert(!seen[gene->locusId]);
        seen[gene->locusId] = true;
        assert(gene->parentLocusId <= EVOLUTION_GENES_PER_CHROMOSOME);
        assert(gene->crossLocusId <= EVOLUTION_GENES_PER_CHROMOSOME);
        assert(gene->moduleType < CREATURE_MODULE_TYPE_COUNT);
        if ((gene->flags & DEVELOPMENT_GENE_CROSS_LINK) != 0u) {
            assert(gene->crossLocusId > 0u);
            assert(gene->crossLocusId != gene->locusId);
        }
    }
}

static void TestStructuralMutation(void)
{
    CreatureGenome mother = EvolutionGenomeSeed(
        0x91a72f31u, EVOLUTION_ARCHETYPE_GROUND);
    CreatureGenome father = EvolutionGenomeSeed(
        0x73c15b29u, EVOLUTION_ARCHETYPE_GROUND);
    mother.generation = 255u;
    father.generation = 254u;
    mother.genomeId = EvolutionGenomeHash(&mother);
    father.genomeId = EvolutionGenomeHash(&father);
    bool sawDuplication = false;
    bool sawCrossLink = false;
    for (uint32_t seed = 1u; seed <= 256u; seed++) {
        CreatureGenome child = EvolutionGenomeBreed(
            &mother, &father, seed * 0x9e3779b9u, 1.0f);
        CreatureGenome replay = EvolutionGenomeBreed(
            &mother, &father, seed * 0x9e3779b9u, 1.0f);
        assert(memcmp(&child, &replay, sizeof(child)) == 0);
        assert(child.generation == 255u);
        assert(child.mutationCount > 0u);
        for (int chromosome = 0; chromosome < 2; chromosome++) {
            const CreatureChromosome *genes = &child.chromosomes[chromosome];
            AssertChromosomeStructure(genes);
            if (genes->geneCount > mother.chromosomes[0].geneCount) {
                sawDuplication = true;
            }
            for (unsigned index = 0; index < genes->geneCount; index++) {
                if ((genes->genes[index].flags &
                     DEVELOPMENT_GENE_CROSS_LINK) != 0u) {
                    sawCrossLink = true;
                }
            }
        }
    }
    assert(sawDuplication);
    assert(sawCrossLink);
}

int main(void)
{
    TestSeedArchetypes();
    TestDeterministicInheritance();
    TestCrossSeedProperties();
    TestRepairAndLimits();
    TestControllerAndSpeciation();
    TestStructuralMutation();
    puts("evolution tests passed");
    return 0;
}
