#ifndef VOXELCRAFT_ENTITY_H
#define VOXELCRAFT_ENTITY_H

#include "world/world_types.h"
#include "gameplay/player_types.h"
#include "ecology/ecology.h"
#include "ecology/evolution.h"
#include "ecology/fauna_behavior.h"
#include "world/weather_model.h"
#include "world/tornado_model.h"

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

typedef enum EntityMapMarkerKind {
    ENTITY_MAP_MARKER_LAND = 0,
    ENTITY_MAP_MARKER_AQUATIC,
    ENTITY_MAP_MARKER_AERIAL,
    ENTITY_MAP_MARKER_HOSTILE
} EntityMapMarkerKind;

typedef struct EntityMapMarker {
    Vector3 position;
    EntityType type;
    EntityMapMarkerKind kind;
    uint32_t speciesId;
    bool evolvable;
} EntityMapMarker;

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
    float positionX;
    float positionZ;
    uint32_t organismId;
    uint32_t lineageId;
    uint32_t speciesId;
    uint32_t motherId;
    uint32_t fatherId;
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
    CreatureGenome genome;
    CreaturePhenotype phenotype;
} EntityEvolutionDebugInfo;

void EntitiesInit(void);
void EntitiesUpdate(float dt, const Player *player, float daylight);
void EntitiesApplyWeatherHazards(float dt, WeatherFieldSample weather,
                                 float daylight);
void EntitiesApplyTornadoHazards(float dt, const TornadoState *tornado,
                                 bool damageEnabled, float daylight);
void EntitiesClear(void);
const Entity *EntitiesView(void);
float EntityEvolutionGrowthScale(const Entity *entity);
bool EntitiesSaveState(FILE *file);
bool EntitiesLoadState(FILE *file);
int GetActiveEntityCount(void);
int EntitiesCollectMapMarkers(EntityMapMarker *out, int capacity);
int EntityRayHit(Vector3 origin, Vector3 direction, float maxDistance);
bool EntityKill(int index, EntityDeathCause cause, float daylight);
int EntityNearestEvolvable(Vector3 position, float radius);
int EntityEvolutionFindByOrganism(uint32_t organismId);
bool EntityEvolutionInspect(int index, EntityEvolutionDebugInfo *out);

#ifdef ENTITY_TESTING
bool EntityTestFindAquaticSpawnY(int x, int preferredY, int z,
                                 int fallbackY, int *outY);
bool EntityTestBlockTypeBlocks(BlockType type);
#endif

#endif
