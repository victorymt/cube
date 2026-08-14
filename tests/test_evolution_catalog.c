#include "evolution_catalog.h"

#include <assert.h>
#include <stdio.h>

static EvolutionCatalogObservation Observation(uint32_t seed,
                                               uint32_t organismId,
                                               uint32_t speciesId,
                                               uint32_t motherId,
                                               uint32_t fatherId)
{
    EvolutionCatalogObservation observation = {
        .worldSeed = seed,
        .surfaceId = 0x484f4d45u,
        .x = (int)organismId,
        .z = -(int)organismId,
        .organismId = organismId,
        .lineageId = speciesId ^ 0x100u,
        .speciesId = speciesId,
        .motherId = motherId,
        .fatherId = fatherId,
        .genome = EvolutionGenomeSeed(organismId + 17u,
                                       EVOLUTION_ARCHETYPE_GROUND)
    };
    observation.phenotype = EvolutionDevelop(&observation.genome);
    return observation;
}

int main(void)
{
    EvolutionCatalogReset();
    EvolutionCatalogObservation mother = Observation(42u, 1u, 100u, 0u, 0u);
    EvolutionCatalogObservation father = Observation(42u, 2u, 100u, 0u, 0u);
    EvolutionCatalogObservation child = Observation(42u, 3u, 100u, 1u, 2u);
    assert(EvolutionCatalogObserve(&mother));
    assert(EvolutionCatalogObserve(&father));
    assert(EvolutionCatalogObserve(&child));
    assert(EvolutionCatalogObserve(&child));
    assert(EvolutionCatalogSpeciesCount() == 1);
    assert(EvolutionCatalogIndividualCount() == 3);

    EvolutionCatalogSpecies species = { 0 };
    assert(EvolutionCatalogFindSpecies(42u, 0x484f4d45u, 100u, &species));
    assert(species.observationCount == 4u);
    assert(species.firstX == 1 && species.firstZ == -1);

    EvolutionCatalogIndividual savedMother = { 0 };
    assert(EvolutionCatalogGetIndividual(
        42u, 0x484f4d45u, 1u, &savedMother));
    assert(savedMother.childCount == 1u && savedMother.childIds[0] == 3u);

    EvolutionCatalogReset();
    assert(EvolutionCatalogObserve(&child));
    assert(EvolutionCatalogObserve(&mother));
    assert(EvolutionCatalogObserve(&father));
    assert(EvolutionCatalogGetIndividual(
        42u, 0x484f4d45u, 1u, &savedMother));
    assert(savedMother.childCount == 1u && savedMother.childIds[0] == 3u);

    EvolutionCatalogObservation zeroSeed = Observation(0u, 5u, 101u, 0u, 0u);
    zeroSeed.surfaceId = 0u;
    assert(EvolutionCatalogObserve(&zeroSeed));

    FILE *file = tmpfile();
    assert(file);
    assert(EvolutionCatalogSaveState(file));
    EvolutionCatalogReset();
    assert(EvolutionCatalogSpeciesCount() == 0);
    rewind(file);
    assert(EvolutionCatalogLoadState(file));
    fclose(file);

    EvolutionCatalogIndividual loadedChild = { 0 };
    assert(EvolutionCatalogGetIndividual(
        42u, 0x484f4d45u, 3u, &loadedChild));
    assert(loadedChild.motherId == 1u && loadedChild.fatherId == 2u);
    assert(EvolutionCatalogSpeciesCount() == 2);
    assert(EvolutionCatalogFindSpecies(
        0u, 0u, 101u, &species));

    EvolutionCatalogObservation otherSurface = Observation(42u, 4u, 100u, 0u, 0u);
    otherSurface.surfaceId = 0x12345678u;
    assert(EvolutionCatalogObserve(&otherSurface));
    assert(EvolutionCatalogSpeciesCount() == 3);
    puts("evolution catalog tests passed");
    return 0;
}
