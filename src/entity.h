#ifndef VOXELCRAFT_ENTITY_H
#define VOXELCRAFT_ENTITY_H

#include "types.h"
#include "ecology.h"
#include "evolution.h"
#include "fauna_behavior.h"

#include <stdio.h>

typedef enum EntityType {
    ENTITY_COW = 0,
    ENTITY_SHEEP,
    ENTITY_PIG,
    ENTITY_CHICKEN,
    ENTITY_ALIEN_GRAZER,
    ENTITY_ALIEN_HOPPER,
    ENTITY_ALIEN_STRIDER,
    ENTITY_ZOMBIE,
    ENTITY_SKELETON
} EntityType;

typedef enum EntityDeathCause {
    ENTITY_DEATH_PLAYER = 0,
    ENTITY_DEATH_ENVIRONMENT,
    ENTITY_DEATH_PREDATION
} EntityDeathCause;

typedef enum CreatureSex {
    CREATURE_SEX_FEMALE = 0,
    CREATURE_SEX_MALE
} CreatureSex;

#define MAX_ENTITIES 48

typedef struct Entity {
    bool active;
    EntityType type;
    Vector3 position;
    Vector3 velocity;
    float yaw;
    float motionTargetYaw;
    float moveTimer;
    float thinkTimer;
    float burnTimer;
    PlanetBodyPlan bodyPlan;
    PlanetChemistry chemistry;
    PlanetEcologicalNiche niche;
    float organismScale;
    float bodyArmor;
    float movementSpeed;
    float temperament;
    int limbCount;
    bool airborne;
    bool colony;
    float hoverHeight;
    float phase;
    float ecologyActivity;
    float ecologyCapacity;
    float ecologySampleTimer;
    float ecologyWindStrength;
    float ecologyWindAngle;
    float ecologyFoodAvailability;
    float ecologyWaterAvailability;
    float ecologyShelterAvailability;
    float ecologyStormPressure;
    float ecologyTemperatureStress;
    FaunaNeeds needs;
    FaunaBehaviorAction behavior;
    bool evolvable;
    bool aquatic;
    bool corpse;
    bool pregnant;
    CreatureSex sex;
    uint32_t organismId;
    uint32_t lineageId;
    uint32_t speciesId;
    uint32_t motherId;
    uint32_t fatherId;
    uint32_t pendingFatherId;
    float ageDays;
    float lifespanDays;
    float maturityAgeDays;
    float reproductionCooldownDays;
    float gestationProgressDays;
    float gestationDurationDays;
    float health;
    float corpseEnergy;
    int targetEntity;
    CreatureGenome genome;
    CreatureGenome pendingOffspring;
    CreaturePhenotype phenotype;
    BlockType primaryBlock;
    BlockType accentBlock;
} Entity;

typedef struct EntityEvolutionDebugInfo {
    bool valid;
    bool corpse;
    bool juvenile;
    bool pregnant;
    uint32_t organismId;
    uint32_t lineageId;
    uint32_t speciesId;
    uint32_t genomeId;
    uint32_t generation;
    uint32_t mutationCount;
    CreatureSex sex;
    CreatureLocomotion locomotion;
    float ageDays;
    float maturityAgeDays;
    float health;
    float energy;
    float diet;
    float mass;
    float speed;
    unsigned moduleCount;
} EntityEvolutionDebugInfo;

void EntitiesInit(void);
void EntitiesUpdate(float dt, const Player *player, float daylight);
void EntitiesDraw(void);
void EntitiesClear(void);
bool EntitiesSaveState(FILE *file);
bool EntitiesLoadState(FILE *file);
int GetActiveEntityCount(void);
int EntityRayHit(Vector3 origin, Vector3 direction, float maxDistance);
bool EntityKill(int index, EntityDeathCause cause, float daylight);
int EntityNearestEvolvable(Vector3 position, float radius);
bool EntityEvolutionInspect(int index, EntityEvolutionDebugInfo *out);

#endif
