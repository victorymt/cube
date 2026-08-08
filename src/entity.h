#ifndef VOXELCRAFT_ENTITY_H
#define VOXELCRAFT_ENTITY_H

#include "types.h"

typedef enum EntityType {
    ENTITY_COW = 0,
    ENTITY_SHEEP,
    ENTITY_PIG,
    ENTITY_CHICKEN,
    ENTITY_ZOMBIE,
    ENTITY_SKELETON
} EntityType;

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
} Entity;

void EntitiesInit(void);
void EntitiesUpdate(float dt, const Player *player, float daylight);
void EntitiesDraw(void);
void EntitiesClear(void);
int GetActiveEntityCount(void);
int EntityRayHit(Vector3 origin, Vector3 direction, float maxDistance);
void EntityKill(int index);

#endif
