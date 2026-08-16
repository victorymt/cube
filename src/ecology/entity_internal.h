#ifndef VOXELCRAFT_ENTITY_INTERNAL_H
#define VOXELCRAFT_ENTITY_INTERNAL_H

#include "ecology/entity.h"

#include <stdint.h>

#define ENTITY_RANDOM_FALLBACK 0x6d2b79f5u
#define ENTITY_EVOLUTION_DAYS_PER_SECOND 0.02f
#define ENTITY_AQUATIC_SPAWN_SEARCH_RADIUS 12
#define ENTITY_GROUND_SPAWN_VERTICAL_RANGE 48.0f
#define ENTITY_DESPAWN_DISTANCE 96.0f

extern Entity entityStore[MAX_ENTITIES];
extern uint32_t entityRandomState;
extern float entitySpawnTimer;

Vector3 EntityFluidCurrent(const Entity *entity);
uint32_t EntityMix(uint32_t value);
uint32_t EntityRandomNext(void);
int EntityRandomBounded(unsigned bound);
void EntityApplyEvolutionPhenotype(Entity *entity);
void EntityInitializeEvolution(Entity *entity, EvolutionArchetype archetype,
                               uint32_t seed, bool juvenile);
void EntityInitializeBehaviorState(Entity *entity);
bool EntityIsAlien(EntityType type);
bool EntityUsesEcology(const Entity *entity);
Color EntityParticleColor(EntityType type);
void EntityBecomeCorpse(Entity *entity);

#endif
