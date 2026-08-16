#ifndef VOXELCRAFT_EVOLUTION_H
#define VOXELCRAFT_EVOLUTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EVOLUTION_GENES_PER_CHROMOSOME 32
#define EVOLUTION_MAX_MODULES 32
#define EVOLUTION_MAX_CONNECTIONS 48
#define EVOLUTION_CONTROLLER_INPUTS 8
#define EVOLUTION_CONTROLLER_HIDDEN 6
#define EVOLUTION_CONTROLLER_OUTPUTS 6
#define EVOLUTION_CONTROLLER_WEIGHTS \
    (EVOLUTION_CONTROLLER_INPUTS * EVOLUTION_CONTROLLER_HIDDEN + \
     EVOLUTION_CONTROLLER_HIDDEN * EVOLUTION_CONTROLLER_OUTPUTS)

typedef enum EvolutionArchetype {
    EVOLUTION_ARCHETYPE_GROUND = 0,
    EVOLUTION_ARCHETYPE_FLIGHT,
    EVOLUTION_ARCHETYPE_AQUATIC
} EvolutionArchetype;

typedef enum CreatureModuleType {
    CREATURE_MODULE_TORSO = 0,
    CREATURE_MODULE_HEAD,
    CREATURE_MODULE_LIMB,
    CREATURE_MODULE_FOOT,
    CREATURE_MODULE_WING,
    CREATURE_MODULE_FIN,
    CREATURE_MODULE_TAIL,
    CREATURE_MODULE_SENSOR,
    CREATURE_MODULE_ARMOR,
    CREATURE_MODULE_TYPE_COUNT
} CreatureModuleType;

typedef enum CreatureLocomotion {
    CREATURE_LOCOMOTION_INVALID = 0,
    CREATURE_LOCOMOTION_GROUND,
    CREATURE_LOCOMOTION_FLIGHT,
    CREATURE_LOCOMOTION_AQUATIC
} CreatureLocomotion;

typedef enum DevelopmentGeneFlags {
    DEVELOPMENT_GENE_ENABLED = 1u << 0,
    DEVELOPMENT_GENE_MIRRORED = 1u << 1,
    DEVELOPMENT_GENE_CROSS_LINK = 1u << 2
} DevelopmentGeneFlags;

typedef struct DevelopmentGene {
    uint16_t locusId;
    uint16_t parentLocusId;
    uint16_t crossLocusId;
    uint8_t moduleType;
    uint8_t flags;
    uint8_t repeatCount;
    uint8_t dominance;
    int8_t offsetX;
    int8_t offsetY;
    int8_t offsetZ;
    uint8_t length;
    uint8_t width;
    uint8_t height;
    uint8_t density;
    uint8_t efficiency;
} DevelopmentGene;

typedef struct CreatureChromosome {
    uint8_t geneCount;
    DevelopmentGene genes[EVOLUTION_GENES_PER_CHROMOSOME];
    int8_t controller[EVOLUTION_CONTROLLER_WEIGHTS];
} CreatureChromosome;

typedef struct CreatureGenome {
    uint32_t genomeId;
    uint32_t motherGenomeId;
    uint32_t fatherGenomeId;
    uint32_t mutationCount;
    uint8_t generation;
    uint8_t diet;
    uint8_t pigmentation;
    uint8_t maturity;
    CreatureChromosome chromosomes[2];
} CreatureGenome;

typedef struct CreatureModule {
    uint16_t sourceLocusId;
    uint8_t type;
    int8_t parentIndex;
    float localX;
    float localY;
    float localZ;
    float length;
    float width;
    float height;
    float mass;
    float efficiency;
} CreatureModule;

typedef struct CreatureConnection {
    uint8_t first;
    uint8_t second;
    float strength;
    bool articulated;
} CreatureConnection;

typedef struct CreaturePhenotype {
    CreatureModule modules[EVOLUTION_MAX_MODULES];
    CreatureConnection connections[EVOLUTION_MAX_CONNECTIONS];
    uint8_t moduleCount;
    uint8_t connectionCount;
    CreatureLocomotion locomotion;
    float totalMass;
    float bodyLength;
    float bodyRadius;
    float cruiseSpeed;
    float energyCost;
    float attack;
    float defense;
    float buoyancy;
    float lift;
    float diet;
    float maturityAgeDays;
    bool repaired;
    bool valid;
} CreaturePhenotype;

CreatureGenome EvolutionGenomeSeed(uint32_t seed,
                                   EvolutionArchetype archetype);
CreatureGenome EvolutionGenomeBreed(const CreatureGenome *mother,
                                    const CreatureGenome *father,
                                    uint32_t birthSeed,
                                    float mutationRate);
uint32_t EvolutionGenomeHash(const CreatureGenome *genome);
float EvolutionGenomeDistance(const CreatureGenome *first,
                              const CreatureGenome *second);
CreaturePhenotype EvolutionDevelop(const CreatureGenome *genome);
bool EvolutionPhenotypeValid(const CreaturePhenotype *phenotype);
bool EvolutionShouldSpeciate(float geneticDistance, float geneFlow,
                             unsigned isolatedGenerations);
void EvolutionControllerEvaluate(
    const CreatureGenome *genome,
    const float inputs[EVOLUTION_CONTROLLER_INPUTS],
    float outputs[EVOLUTION_CONTROLLER_OUTPUTS]);
const char *EvolutionLocomotionName(CreatureLocomotion locomotion);

#endif
