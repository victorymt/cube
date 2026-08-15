#include "evolution_catalog.h"

#include <string.h>

#define EVOLUTION_CATALOG_STATE_VERSION 1u

static EvolutionCatalogSpecies catalogSpecies[EVOLUTION_CATALOG_MAX_SPECIES];
static EvolutionCatalogIndividual catalogIndividuals[EVOLUTION_CATALOG_MAX_INDIVIDUALS];
static uint64_t catalogSerial = 1u;

static bool CatalogObservationValid(const EvolutionCatalogObservation *observation)
{
    if (!observation || observation->organismId == 0u ||
        observation->lineageId == 0u || observation->speciesId == 0u ||
        !observation->phenotype.valid ||
        observation->phenotype.moduleCount > EVOLUTION_MAX_MODULES ||
        observation->genome.genomeId == 0u ||
        observation->genome.genomeId != EvolutionGenomeHash(&observation->genome)) {
        return false;
    }
    return EvolutionPhenotypeValid(&observation->phenotype);
}

static bool CatalogSpeciesKeyMatches(const EvolutionCatalogSpecies *species,
                                     uint32_t worldSeed, uint32_t surfaceId,
                                     uint32_t speciesId)
{
    return species && species->valid && species->worldSeed == worldSeed &&
           species->surfaceId == surfaceId && species->speciesId == speciesId;
}

static bool CatalogIndividualKeyMatches(
    const EvolutionCatalogIndividual *individual, uint32_t worldSeed,
    uint32_t surfaceId, uint32_t organismId)
{
    return individual && individual->valid &&
           individual->worldSeed == worldSeed &&
           individual->surfaceId == surfaceId &&
           individual->organismId == organismId;
}

static int CatalogFindSpeciesIndex(uint32_t worldSeed, uint32_t surfaceId,
                                   uint32_t speciesId)
{
    for (int index = 0; index < EVOLUTION_CATALOG_MAX_SPECIES; index++) {
        if (CatalogSpeciesKeyMatches(&catalogSpecies[index], worldSeed,
                                     surfaceId, speciesId)) return index;
    }
    return -1;
}

static int CatalogFindIndividualIndex(uint32_t worldSeed, uint32_t surfaceId,
                                      uint32_t organismId)
{
    for (int index = 0; index < EVOLUTION_CATALOG_MAX_INDIVIDUALS; index++) {
        if (CatalogIndividualKeyMatches(&catalogIndividuals[index], worldSeed,
                                        surfaceId, organismId)) return index;
    }
    return -1;
}

static int CatalogFreeSpeciesIndex(void)
{
    for (int index = 0; index < EVOLUTION_CATALOG_MAX_SPECIES; index++) {
        if (!catalogSpecies[index].valid) return index;
    }
    int oldest = 0;
    for (int index = 1; index < EVOLUTION_CATALOG_MAX_SPECIES; index++) {
        if (catalogSpecies[index].observationCount <
            catalogSpecies[oldest].observationCount) oldest = index;
    }
    return oldest;
}

static int CatalogFreeIndividualIndex(void)
{
    for (int index = 0; index < EVOLUTION_CATALOG_MAX_INDIVIDUALS; index++) {
        if (!catalogIndividuals[index].valid) return index;
    }
    return 0;
}

static void CatalogAddChild(uint32_t worldSeed, uint32_t surfaceId,
                            uint32_t parentId, uint32_t childId)
{
    if (parentId == 0u || childId == 0u || parentId == childId) return;
    int index = CatalogFindIndividualIndex(worldSeed, surfaceId, parentId);
    if (index < 0) return;
    EvolutionCatalogIndividual *parent = &catalogIndividuals[index];
    for (unsigned child = 0; child < parent->childCount; child++) {
        if (parent->childIds[child] == childId) return;
    }
    if (parent->childCount < EVOLUTION_CATALOG_MAX_CHILDREN) {
        parent->childIds[parent->childCount++] = childId;
    }
}

static void CatalogAddKnownChildren(uint32_t worldSeed, uint32_t surfaceId,
                                    uint32_t parentId)
{
    for (int index = 0; index < EVOLUTION_CATALOG_MAX_INDIVIDUALS; index++) {
        const EvolutionCatalogIndividual *child = &catalogIndividuals[index];
        if (!child->valid || child->worldSeed != worldSeed ||
            child->surfaceId != surfaceId ||
            (child->motherId != parentId && child->fatherId != parentId)) {
            continue;
        }
        CatalogAddChild(worldSeed, surfaceId, parentId, child->organismId);
    }
}

void EvolutionCatalogReset(void)
{
    memset(catalogSpecies, 0, sizeof(catalogSpecies));
    memset(catalogIndividuals, 0, sizeof(catalogIndividuals));
    catalogSerial = 1u;
}

bool EvolutionCatalogObserve(const EvolutionCatalogObservation *observation)
{
    if (!CatalogObservationValid(observation)) return false;

    int speciesIndex = CatalogFindSpeciesIndex(
        observation->worldSeed, observation->surfaceId,
        observation->speciesId);
    if (speciesIndex < 0) {
        speciesIndex = CatalogFreeSpeciesIndex();
        catalogSpecies[speciesIndex] = (EvolutionCatalogSpecies){
            .valid = true,
            .worldSeed = observation->worldSeed,
            .surfaceId = observation->surfaceId,
            .speciesId = observation->speciesId,
            .lineageId = observation->lineageId,
            .firstX = observation->x,
            .firstZ = observation->z,
            .observationCount = 0u,
            .representativeGenome = observation->genome
        };
    }
    EvolutionCatalogSpecies *species = &catalogSpecies[speciesIndex];
    species->observationCount++;
    if (observation->genome.generation > species->representativeGenome.generation ||
        observation->phenotype.moduleCount >
            EvolutionDevelop(&species->representativeGenome).moduleCount) {
        species->lineageId = observation->lineageId;
        species->representativeGenome = observation->genome;
    }

    int individualIndex = CatalogFindIndividualIndex(
        observation->worldSeed, observation->surfaceId,
        observation->organismId);
    if (individualIndex < 0) {
        individualIndex = CatalogFreeIndividualIndex();
        catalogIndividuals[individualIndex] = (EvolutionCatalogIndividual){
            .valid = true,
            .worldSeed = observation->worldSeed,
            .surfaceId = observation->surfaceId,
            .organismId = observation->organismId,
            .lineageId = observation->lineageId,
            .speciesId = observation->speciesId,
            .motherId = observation->motherId,
            .fatherId = observation->fatherId,
            .generation = observation->genome.generation
        };
    }
    EvolutionCatalogIndividual *individual = &catalogIndividuals[individualIndex];
    if (observation->motherId != 0u) individual->motherId = observation->motherId;
    if (observation->fatherId != 0u) individual->fatherId = observation->fatherId;
    CatalogAddKnownChildren(observation->worldSeed, observation->surfaceId,
                            observation->organismId);
    CatalogAddChild(observation->worldSeed, observation->surfaceId,
                    observation->motherId, observation->organismId);
    CatalogAddChild(observation->worldSeed, observation->surfaceId,
                    observation->fatherId, observation->organismId);
    catalogSerial++;
    if (catalogSerial == 0u) catalogSerial = 1u;
    return true;
}

int EvolutionCatalogSpeciesCount(void)
{
    int count = 0;
    for (int index = 0; index < EVOLUTION_CATALOG_MAX_SPECIES; index++) {
        if (catalogSpecies[index].valid) count++;
    }
    return count;
}

int EvolutionCatalogIndividualCount(void)
{
    int count = 0;
    for (int index = 0; index < EVOLUTION_CATALOG_MAX_INDIVIDUALS; index++) {
        if (catalogIndividuals[index].valid) count++;
    }
    return count;
}

int EvolutionCatalogFirstSpeciesSlot(void)
{
    for (int index = 0; index < EVOLUTION_CATALOG_MAX_SPECIES; index++) {
        if (catalogSpecies[index].valid) return index;
    }
    return -1;
}

bool EvolutionCatalogGetSpecies(int index, EvolutionCatalogSpecies *out)
{
    if (!out || index < 0 || index >= EVOLUTION_CATALOG_MAX_SPECIES ||
        !catalogSpecies[index].valid) return false;
    *out = catalogSpecies[index];
    return true;
}

bool EvolutionCatalogFindSpecies(uint32_t worldSeed, uint32_t surfaceId,
                                 uint32_t speciesId,
                                 EvolutionCatalogSpecies *out)
{
    int index = CatalogFindSpeciesIndex(worldSeed, surfaceId, speciesId);
    if (index < 0 || !out) return false;
    *out = catalogSpecies[index];
    return true;
}

bool EvolutionCatalogGetIndividual(uint32_t worldSeed, uint32_t surfaceId,
                                   uint32_t organismId,
                                   EvolutionCatalogIndividual *out)
{
    int index = CatalogFindIndividualIndex(worldSeed, surfaceId, organismId);
    if (index < 0 || !out) return false;
    *out = catalogIndividuals[index];
    return true;
}

bool EvolutionCatalogSaveState(FILE *file)
{
    if (!file) return false;
    uint32_t header[4] = {
        EVOLUTION_CATALOG_STATE_VERSION,
        (uint32_t)EvolutionCatalogSpeciesCount(),
        (uint32_t)EvolutionCatalogIndividualCount(),
        0u
    };
    if (fwrite(header, sizeof(header), 1, file) != 1 ||
        fwrite(&catalogSerial, sizeof(catalogSerial), 1, file) != 1) {
        return false;
    }
    for (int index = 0; index < EVOLUTION_CATALOG_MAX_SPECIES; index++) {
        const EvolutionCatalogSpecies *species = &catalogSpecies[index];
        if (!species->valid) continue;
        if (fwrite(species, sizeof(*species), 1, file) != 1) return false;
    }
    for (int index = 0; index < EVOLUTION_CATALOG_MAX_INDIVIDUALS; index++) {
        const EvolutionCatalogIndividual *individual = &catalogIndividuals[index];
        if (!individual->valid) continue;
        if (fwrite(individual, sizeof(*individual), 1, file) != 1) return false;
    }
    return true;
}

bool EvolutionCatalogLoadState(FILE *file)
{
    if (!file) return false;
    uint32_t header[4] = { 0 };
    uint64_t serial = 0u;
    if (fread(header, sizeof(header), 1, file) != 1 ||
        fread(&serial, sizeof(serial), 1, file) != 1 ||
        header[0] != EVOLUTION_CATALOG_STATE_VERSION ||
        header[1] > EVOLUTION_CATALOG_MAX_SPECIES ||
        header[2] > EVOLUTION_CATALOG_MAX_INDIVIDUALS || serial == 0u) {
        return false;
    }
    EvolutionCatalogSpecies loadedSpecies[EVOLUTION_CATALOG_MAX_SPECIES] = { 0 };
    EvolutionCatalogIndividual loadedIndividuals[EVOLUTION_CATALOG_MAX_INDIVIDUALS] = { 0 };
    for (uint32_t item = 0; item < header[1]; item++) {
        EvolutionCatalogSpecies *species = &loadedSpecies[item];
        if (fread(species, sizeof(*species), 1, file) != 1 ||
            !species->valid || species->speciesId == 0u ||
            species->lineageId == 0u || species->observationCount == 0u ||
            species->representativeGenome.genomeId == 0u ||
            species->representativeGenome.genomeId !=
                EvolutionGenomeHash(&species->representativeGenome)) {
            return false;
        }
        CreaturePhenotype phenotype = EvolutionDevelop(&species->representativeGenome);
        if (!phenotype.valid) return false;
    }
    for (uint32_t item = 0; item < header[2]; item++) {
        EvolutionCatalogIndividual *individual = &loadedIndividuals[item];
        if (fread(individual, sizeof(*individual), 1, file) != 1 ||
            !individual->valid || individual->organismId == 0u ||
            individual->lineageId == 0u || individual->speciesId == 0u ||
            individual->childCount > EVOLUTION_CATALOG_MAX_CHILDREN) {
            return false;
        }
    }
    memcpy(catalogSpecies, loadedSpecies, sizeof(catalogSpecies));
    memcpy(catalogIndividuals, loadedIndividuals, sizeof(catalogIndividuals));
    catalogSerial = serial;
    return true;
}
