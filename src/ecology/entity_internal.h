#ifndef VOXELCRAFT_ENTITY_INTERNAL_H
#define VOXELCRAFT_ENTITY_INTERNAL_H

#include "ecology/entity.h"
#include "world/weather_model.h"

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
int EntityNextFreeSlot(void);
int EntitySurfaceHeight(int x, int z);
EntityType EntityEvolutionTypeForArchetype(
    EvolutionArchetype archetype, bool alienWorld);
bool EntityFindAquaticSpawnY(int x, int z, int preferredY,
                             int fallbackY, int *outY);
void EntityApplyLocalBehaviorEnvironment(
    Entity *entity, const PlanetLocalEcology *local,
    WeatherFieldSample weather);
void EntityApplyEvolutionPhenotype(Entity *entity);
void EntityInitializeEvolution(Entity *entity, EvolutionArchetype archetype,
                               uint32_t seed, bool juvenile);
void EntityInitializeBehaviorState(Entity *entity);
bool EntityIsAlien(EntityType type);
bool EntityUsesEcology(const Entity *entity);
Color EntityParticleColor(EntityType type);
void EntityBecomeCorpse(Entity *entity);
void EntitySpawnPassive(const Player *player, float daylight);
void EntitySpawnHostile(const Player *player, float daylight);

#endif
