#ifndef VOXELCRAFT_EVOLUTION_CATALOG_H
#define VOXELCRAFT_EVOLUTION_CATALOG_H

#include "ecology/evolution.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define EVOLUTION_CATALOG_MAX_SPECIES 128
#define EVOLUTION_CATALOG_MAX_INDIVIDUALS 256
#define EVOLUTION_CATALOG_MAX_CHILDREN 8

typedef struct EvolutionCatalogObservation {
    uint32_t worldSeed;
    uint32_t surfaceId;
    int x;
    int z;
    uint32_t organismId;
    uint32_t lineageId;
    uint32_t speciesId;
    uint32_t motherId;
    uint32_t fatherId;
    CreatureGenome genome;
    CreaturePhenotype phenotype;
} EvolutionCatalogObservation;

typedef struct EvolutionCatalogSpecies {
    bool valid;
    uint32_t worldSeed;
    uint32_t surfaceId;
    uint32_t speciesId;
    uint32_t lineageId;
    int firstX;
    int firstZ;
    uint32_t observationCount;
    CreatureGenome representativeGenome;
} EvolutionCatalogSpecies;

typedef struct EvolutionCatalogIndividual {
    bool valid;
    uint32_t worldSeed;
    uint32_t surfaceId;
    uint32_t organismId;
    uint32_t lineageId;
    uint32_t speciesId;
    uint32_t motherId;
    uint32_t fatherId;
    uint8_t generation;
    uint8_t childCount;
    uint32_t childIds[EVOLUTION_CATALOG_MAX_CHILDREN];
} EvolutionCatalogIndividual;

void EvolutionCatalogReset(void);
bool EvolutionCatalogObserve(const EvolutionCatalogObservation *observation);
int EvolutionCatalogSpeciesCount(void);
int EvolutionCatalogIndividualCount(void);
int EvolutionCatalogFirstSpeciesSlot(void);
bool EvolutionCatalogGetSpecies(int index, EvolutionCatalogSpecies *out);
bool EvolutionCatalogFindSpecies(uint32_t worldSeed, uint32_t surfaceId,
                                 uint32_t speciesId,
                                 EvolutionCatalogSpecies *out);
bool EvolutionCatalogGetIndividual(uint32_t worldSeed, uint32_t surfaceId,
                                   uint32_t organismId,
                                   EvolutionCatalogIndividual *out);
bool EvolutionCatalogSaveState(FILE *file);
bool EvolutionCatalogLoadState(FILE *file);

#endif
