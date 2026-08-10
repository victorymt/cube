#ifndef VOXELCRAFT_ECOLOGY_H
#define VOXELCRAFT_ECOLOGY_H

#include "ecology_model.h"
#include "types.h"

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
} PlanetLocalEcology;

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
PlanetLocalEcology PlanetEcologyLocalAt(int x, int z, float daylight);
PlanetEcologySuitability PlanetEcologyStaticSuitabilityAt(int x, int z);
const char *PlanetEcologyLifeName(void);
const char *PlanetEcologyBiomassName(void);
const char *PlanetEcologyChemistryName(void);
const char *PlanetEcologyBodyPlanName(void);
const char *PlanetEcologyNicheName(void);
void PlanetEcologyApplyToChunk(Chunk *chunk, int chunkX, int chunkZ);

#endif
