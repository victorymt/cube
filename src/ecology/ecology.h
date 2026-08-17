#ifndef VOXELCRAFT_ECOLOGY_H
#define VOXELCRAFT_ECOLOGY_H

#include "ecology/ecology_model.h"
#include "ecology/evolution.h"
#include "world/world_types.h"

#include <stdio.h>

struct PlanetProfile;

typedef enum PlanetFloraArchetype {
    PLANET_FLORA_ALIEN_CANOPY = 0,
    PLANET_FLORA_CRYSTAL,
    PLANET_FLORA_SPORE,
    PLANET_FLORA_THERMAL_VENT
} PlanetFloraArchetype;

typedef enum PlanetBiomassClass {
    PLANET_BIOMASS_BARREN = 0,
    PLANET_BIOMASS_MICROBIAL,
    PLANET_BIOMASS_FUNGAL,
    PLANET_BIOMASS_CRYSTALLINE,
    PLANET_BIOMASS_LUSH,
    PLANET_BIOMASS_ANOMALOUS
} PlanetBiomassClass;

typedef enum PlanetChemistry {
    PLANET_CHEMISTRY_CARBON = 0,
    PLANET_CHEMISTRY_SILICON,
    PLANET_CHEMISTRY_SULFUR
} PlanetChemistry;

typedef enum PlanetBodyPlan {
    PLANET_BODY_QUADRUPED = 0,
    PLANET_BODY_BIPED,
    PLANET_BODY_HEXAPOD,
    PLANET_BODY_SERPENTINE,
    PLANET_BODY_FLOATING,
    PLANET_BODY_COLONY
} PlanetBodyPlan;

typedef enum PlanetEcologicalNiche {
    PLANET_NICHE_GRAZER = 0,
    PLANET_NICHE_MICROBIAL,
    PLANET_NICHE_DECOMPOSER,
    PLANET_NICHE_CRYSTAL_GRAZER,
    PLANET_NICHE_FILTER_FEEDER,
    PLANET_NICHE_BIOLUMINESCENT_COLONY
} PlanetEcologicalNiche;

typedef struct PlanetEcologyProfile {
    PlanetFloraArchetype flora;
    PlanetBiomassClass biomass;
    PlanetChemistry chemistry;
    PlanetBodyPlan bodyPlan;
    PlanetEcologicalNiche niche;
    float floraDensity;
    float faunaDensity;
    float lifeDensity;
    float planetAgeGyr;
    float lifeOriginProbability;
    float complexLifeProbability;
    float evolutionProgress;
    float organismScale;
    float bodyArmor;
    float movementSpeed;
    float temperament;
    int limbCount;
    bool lifeOriginated;
    bool hasComplexLife;
    bool supportsFlight;
    bool darkSideColony;
    BlockType primaryBlock;
    BlockType accentBlock;
} PlanetEcologyProfile;

typedef struct PlanetLocalEcology {
    PlanetLocalEnvironment environment;
    PlanetEcologySuitability suitability;
    PlanetRegionalPopulation population;
    PlanetPopulationMigrationState migration;
    struct {
        int regionX;
        int regionZ;
        float habitatStress;
        float harvestStress;
        float faunaStress;
        float faunaNetRecoveryRate;
        float radiationMemory;
    } diagnostics;
} PlanetLocalEcology;

#define PLANET_EVOLUTION_MAX_LINEAGES 8

typedef struct PlanetEvolutionLineage {
    uint32_t lineageId;
    uint32_t speciesId;
    uint32_t founderSeed;
    float density;
    float dietMean;
    float geneticVariance;
    float geneFlow;
    float fitness;
    uint8_t archetype;
    uint8_t generation;
    uint8_t isolatedGenerations;
    uint8_t active;
} PlanetEvolutionLineage;

typedef struct PlanetEvolutionRegion {
    float herbivoreDensity;
    float omnivoreDensity;
    float carnivoreDensity;
    uint32_t bootstrapGeneration;
    uint32_t lineageCount;
    bool bootstrapComplete;
    PlanetEvolutionLineage lineages[PLANET_EVOLUTION_MAX_LINEAGES];
} PlanetEvolutionRegion;

typedef enum PlanetEvolutionEvent {
    PLANET_EVOLUTION_EVENT_BIRTH = 0,
    PLANET_EVOLUTION_EVENT_ENVIRONMENT_DEATH,
    PLANET_EVOLUTION_EVENT_PREDATION_DEATH
} PlanetEvolutionEvent;

PlanetEcologyProfile PlanetEcologyProfileForPlanet(
    const struct PlanetProfile *planet, uint32_t worldSeed, bool darkSide);
PlanetEcologyProfile PlanetEcologyCurrent(void);
void PlanetEcologyResetState(void);
bool PlanetEcologySaveState(FILE *file);
bool PlanetEcologyLoadState(FILE *file);
float PlanetEcologyFaunaDensity(void);
float PlanetEcologyFaunaDensityAt(int x, int z, float daylight);
bool PlanetEcologyRecordFaunaHarvest(int x, int z, float daylight,
                                     float organismScale,
                                     float ecologyCapacity);
bool PlanetEcologyEvolutionRegionAt(int x, int z, float daylight,
                                    PlanetEvolutionRegion *out);
bool PlanetEcologySampleGenome(int x, int z, float daylight,
                               uint32_t sampleSeed, CreatureGenome *outGenome,
                               uint32_t *outLineageId,
                               uint32_t *outSpeciesId);
bool PlanetEcologyRecordEvolutionEvent(
    int x, int z, float daylight, uint32_t lineageId,
    PlanetEvolutionEvent event, float biomass);
/*
 * Local queries and population save/load/reset serialize their shared
 * population cache internally. The active PlanetWorld must remain unchanged
 * while a query is running; world activation and dimension edits stay on the
 * simulation thread. PlanetEcologyEvaluateLocal and the other ecology-model
 * functions are pure and reentrant.
 */
PlanetLocalEcology PlanetEcologyLocalAt(int x, int z, float daylight);
PlanetEcologySuitability PlanetEcologyStaticSuitabilityAt(int x, int z);
const char *PlanetEcologyLifeName(void);
const char *PlanetEcologyBiomassName(void);
const char *PlanetEcologyChemistryName(void);
const char *PlanetEcologyBodyPlanName(void);
const char *PlanetEcologyNicheName(void);
void PlanetEcologyApplyToChunk(Chunk *chunk, int chunkX, int chunkZ);

#ifdef CHUNKS_TESTING
BlockType PlanetEcologyTestGroundCoverBlock(
    PlanetBiomassClass biomass, PlanetFloraArchetype flora,
    int biome, uint32_t hash);
void PlanetEcologyTestApplyProfileToChunk(
    Chunk *chunk, int chunkX, int chunkZ,
    const PlanetEcologyProfile *profile);
#endif

#endif
