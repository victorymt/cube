#ifndef VOXELCRAFT_ENTITY_H
#define VOXELCRAFT_ENTITY_H

#include "types.h"
#include "ecology.h"

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

#define MAX_ENTITIES 48

typedef struct Entity {
    bool active;
    EntityType type;
    Vector3 position;
    Vector3 velocity;
    float yaw;
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
    BlockType primaryBlock;
    BlockType accentBlock;
} Entity;

void EntitiesInit(void);
void EntitiesUpdate(float dt, const Player *player, float daylight);
void EntitiesDraw(void);
void EntitiesClear(void);
bool EntitiesSaveState(FILE *file);
bool EntitiesLoadState(FILE *file);
int GetActiveEntityCount(void);
int EntityRayHit(Vector3 origin, Vector3 direction, float maxDistance);
bool EntityKill(int index, EntityDeathCause cause, float daylight);

#endif
