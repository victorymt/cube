#include "ecology/entity.h"
#include "ecology/entity_internal.h"

#include "core/game_effects.h"
#include "ecology/fauna_motion.h"
#include "world/fluid.h"
#include "raymath.h"
#include "world/world.h"
#include "world/terrain.h"
#include "ecology/ecology.h"
#include "world/world_environment.h"
#include "world/weather.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ENTITY_STATE_VERSION 4u

Vector3 EntityFluidCurrent(const Entity *entity)
{
    if (!entity || !WorldIsSurfaceActive()) return Vector3Zero();
    Vector3 point = Vector3Add(entity->position, (Vector3){ 0.0f, 0.5f, 0.0f });
    FluidSample sample = FluidSampleAt(point);
    if (sample.volume == 0u || point.y >= sample.surfaceY) return Vector3Zero();
    return sample.velocity;
}

typedef struct EntityDiskStateV1 {
    uint32_t active;
    uint32_t type;
    float position[3];
    float velocity[3];
    float yaw;
    float moveTimer;
    float thinkTimer;
    float burnTimer;
    uint32_t bodyPlan;
    uint32_t chemistry;
    uint32_t niche;
    float organismScale;
    float bodyArmor;
    float movementSpeed;
    float temperament;
    int32_t limbCount;
    uint32_t airborne;
    uint32_t colony;
    float hoverHeight;
    float phase;
    float ecologyActivity;
    float ecologyCapacity;
    float ecologySampleTimer;
    float ecologyWindStrength;
    float ecologyWindAngle;
    uint32_t primaryBlock;
    uint32_t accentBlock;
} EntityDiskStateV1;

typedef struct EntityDiskStateV2 {
    EntityDiskStateV1 entity;
    float motionTargetYaw;
} EntityDiskStateV2;

typedef struct EntityDiskStateV3 {
    EntityDiskStateV2 entity;
    float ecologyFoodAvailability;
    float ecologyWaterAvailability;
    float ecologyShelterAvailability;
    float ecologyStormPressure;
    float ecologyTemperatureStress;
    float energy;
    float hydration;
    float fatigue;
    float stress;
    uint32_t behavior;
} EntityDiskStateV3;

typedef struct EntityDiskStateV4 {
    EntityDiskStateV3 entity;
    uint32_t evolvable;
    uint32_t aquatic;
    uint32_t corpse;
    uint32_t pregnant;
    uint32_t sex;
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
    int32_t targetEntity;
    CreatureGenome genome;
    CreatureGenome pendingOffspring;
} EntityDiskStateV4;

Entity entityStore[MAX_ENTITIES];
uint32_t entityRandomState = ENTITY_RANDOM_FALLBACK;
float entitySpawnTimer = 0.0f;

#define entities entityStore

uint32_t EntityMix(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static uint32_t EntityInitialRandomState(void)
{
    uint32_t state = WorldGetSeed() ^
                     WorldCurrentSurfaceId() * 0x9e3779b9u ^ 0x3c6ef372u;
    state = EntityMix(state);
    return state != 0u ? state : ENTITY_RANDOM_FALLBACK;
}

uint32_t EntityRandomNext(void)
{
    uint32_t state = entityRandomState;
    if (state == 0u) state = ENTITY_RANDOM_FALLBACK;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    entityRandomState = state;
    return state;
}

int EntityRandomBounded(unsigned bound)
{
    if (bound == 0u) return 0;
    return (int)(EntityRandomNext() % bound);
}

static EvolutionArchetype EntityEvolutionArchetypeForBodyPlan(
    PlanetBodyPlan bodyPlan)
{
    if (bodyPlan == PLANET_BODY_FLOATING) return EVOLUTION_ARCHETYPE_FLIGHT;
    if (bodyPlan == PLANET_BODY_SERPENTINE) return EVOLUTION_ARCHETYPE_AQUATIC;
    return EVOLUTION_ARCHETYPE_GROUND;
}

static PlanetBodyPlan EntityBodyPlanForPhenotype(
    const CreaturePhenotype *phenotype)
{
    if (!phenotype) return PLANET_BODY_QUADRUPED;
    if (phenotype->locomotion == CREATURE_LOCOMOTION_FLIGHT) {
        return PLANET_BODY_FLOATING;
    }
    if (phenotype->locomotion == CREATURE_LOCOMOTION_AQUATIC) {
        return PLANET_BODY_SERPENTINE;
    }
    int limbCount = 0;
    for (unsigned index = 0; index < phenotype->moduleCount; index++) {
        if (phenotype->modules[index].type == CREATURE_MODULE_LIMB) limbCount++;
    }
    if (limbCount >= 6) return PLANET_BODY_HEXAPOD;
    if (limbCount == 2) return PLANET_BODY_BIPED;
    return PLANET_BODY_QUADRUPED;
}

void EntityApplyEvolutionPhenotype(Entity *entity)
{
    if (!entity || !entity->evolvable) return;
    entity->phenotype = EvolutionDevelop(&entity->genome);
    if (!entity->phenotype.valid) {
        entity->evolvable = false;
        return;
    }
    entity->bodyPlan = EntityBodyPlanForPhenotype(&entity->phenotype);
    entity->aquatic = entity->phenotype.locomotion ==
                      CREATURE_LOCOMOTION_AQUATIC;
    entity->airborne = entity->phenotype.locomotion ==
                       CREATURE_LOCOMOTION_FLIGHT;
    entity->colony = false;
    entity->organismScale = fminf(fmaxf(
        sqrtf(entity->phenotype.totalMass) * 0.52f, 0.35f), 2.6f);
    entity->bodyArmor = fminf(entity->phenotype.defense / 8.0f, 1.0f);
    entity->movementSpeed = entity->phenotype.cruiseSpeed;
    entity->temperament = fminf(fmaxf(
        entity->phenotype.diet * 0.72f +
        entity->phenotype.attack / 20.0f, 0.05f), 1.0f);
    entity->maturityAgeDays = entity->phenotype.maturityAgeDays;
    entity->limbCount = 0;
    for (unsigned index = 0; index < entity->phenotype.moduleCount; index++) {
        if (entity->phenotype.modules[index].type == CREATURE_MODULE_LIMB ||
            entity->phenotype.modules[index].type == CREATURE_MODULE_WING ||
            entity->phenotype.modules[index].type == CREATURE_MODULE_FIN) {
            entity->limbCount++;
        }
    }
}

void EntityInitializeEvolution(Entity *entity,
                               EvolutionArchetype archetype,
                               uint32_t seed, bool juvenile)
{
    if (!entity) return;
    entity->evolvable = true;
    entity->genome = EvolutionGenomeSeed(seed, archetype);
    entity->organismId = EntityMix(seed ^ entity->genome.genomeId);
    if (entity->organismId == 0u) entity->organismId = 1u;
    entity->lineageId = EvolutionGenomeHash(&entity->genome) & 0x00ffffffu;
    if (entity->lineageId == 0u) entity->lineageId = 1u;
    entity->speciesId = EntityMix(entity->lineageId ^ 0x51ed270bu);
    if (entity->speciesId == 0u) entity->speciesId = entity->lineageId;
    entity->sex = (EntityMix(seed ^ 0xa511e9b3u) & 1u) ?
                  CREATURE_SEX_MALE : CREATURE_SEX_FEMALE;
    entity->ageDays = juvenile ? 0.0f :
        18.0f + (float)(EntityMix(seed ^ 0x18f2a43du) % 3200u) / 100.0f;
    entity->lifespanDays = 96.0f +
        (float)(EntityMix(seed ^ 0x77d89f21u) % 9600u) / 100.0f;
    entity->reproductionCooldownDays = juvenile ? 0.0f :
        (float)(EntityMix(seed ^ 0xb5297a4du) % 600u) / 100.0f;
    entity->gestationDurationDays = archetype == EVOLUTION_ARCHETYPE_AQUATIC ?
        3.0f : archetype == EVOLUTION_ARCHETYPE_FLIGHT ? 5.0f : 7.0f;
    entity->health = 1.0f;
    entity->targetEntity = -1;
    EntityApplyEvolutionPhenotype(entity);
}

void EntitiesInit(void)
{
    memset(entities, 0, sizeof(entities));
    entityRandomState = EntityInitialRandomState();
    entitySpawnTimer = 0.0f;
}

void EntitiesClear(void)
{
    EntitiesInit();
}

int GetActiveEntityCount(void)
{
    int count = 0;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if (entities[i].active) count++;
    }
    return count;
}

const Entity *EntitiesView(void)
{
    return entities;
}

int EntitiesCollectMapMarkers(EntityMapMarker *out, int capacity)
{
    if (!out || capacity <= 0) return 0;

    int written = 0;
    for (int i = 0; i < MAX_ENTITIES && written < capacity; i++) {
        const Entity *entity = &entities[i];
        if (!entity->active || entity->corpse) continue;

        EntityMapMarkerKind kind = ENTITY_MAP_MARKER_LAND;
        if (entity->type == ENTITY_ZOMBIE ||
            entity->type == ENTITY_SKELETON) {
            kind = ENTITY_MAP_MARKER_HOSTILE;
        } else if (entity->aquatic) {
            kind = ENTITY_MAP_MARKER_AQUATIC;
        } else if (entity->airborne ||
                   (entity->evolvable &&
                    entity->phenotype.locomotion == CREATURE_LOCOMOTION_FLIGHT)) {
            kind = ENTITY_MAP_MARKER_AERIAL;
        }

        out[written++] = (EntityMapMarker){
            .position = entity->position,
            .type = entity->type,
            .kind = kind,
            .speciesId = entity->speciesId,
            .evolvable = entity->evolvable
        };
    }
    return written;
}

static bool EntityFloatValid(float value)
{
    return isfinite(value) && fabsf(value) <= 1000000000.0f;
}

static bool EntityUnitFloatValid(float value)
{
    return isfinite(value) && value >= 0.0f && value <= 1.0f;
}

static bool EntityBlockValid(uint32_t value)
{
    return value <= (uint32_t)BLOCK_NETHER_PORTAL ||
           (value >= (uint32_t)BLOCK_NATURAL_START &&
            value <= (uint32_t)BLOCK_NATURAL_END) ||
           (value >= (uint32_t)BLOCK_COLOR_START &&
            value <= (uint32_t)BLOCK_COLOR_END);
}

static bool EntityDiskStateValid(const EntityDiskStateV1 *saved)
{
    if (!saved || saved->active > 1u) return false;
    if (saved->active == 0u) return true;
    if (saved->type > (uint32_t)ENTITY_SKELETON ||
        saved->bodyPlan > (uint32_t)PLANET_BODY_COLONY ||
        saved->chemistry > (uint32_t)PLANET_CHEMISTRY_SULFUR ||
        saved->niche > (uint32_t)PLANET_NICHE_BIOLUMINESCENT_COLONY ||
        saved->airborne > 1u || saved->colony > 1u ||
        saved->limbCount < 0 || saved->limbCount > 64 ||
        !EntityBlockValid(saved->primaryBlock) ||
        !EntityBlockValid(saved->accentBlock)) {
        return false;
    }
    for (int axis = 0; axis < 3; axis++) {
        if (!EntityFloatValid(saved->position[axis]) ||
            !EntityFloatValid(saved->velocity[axis])) {
            return false;
        }
    }
    const float values[] = {
        saved->yaw, saved->moveTimer, saved->thinkTimer, saved->burnTimer,
        saved->organismScale, saved->bodyArmor, saved->movementSpeed,
        saved->temperament, saved->hoverHeight, saved->phase,
        saved->ecologyActivity, saved->ecologyCapacity,
        saved->ecologySampleTimer, saved->ecologyWindStrength,
        saved->ecologyWindAngle
    };
    for (unsigned index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
        if (!EntityFloatValid(values[index])) return false;
    }
    return true;
}

static bool EntityDiskStateV2Valid(const EntityDiskStateV2 *saved)
{
    return saved && EntityDiskStateValid(&saved->entity) &&
           (saved->entity.active == 0u ||
            EntityFloatValid(saved->motionTargetYaw));
}

static bool EntityDiskStateV3Valid(const EntityDiskStateV3 *saved)
{
    if (!saved || !EntityDiskStateV2Valid(&saved->entity)) return false;
    if (saved->entity.entity.active == 0u) return true;
    const float values[] = {
        saved->ecologyFoodAvailability,
        saved->ecologyWaterAvailability,
        saved->ecologyShelterAvailability,
        saved->ecologyStormPressure,
        saved->ecologyTemperatureStress,
        saved->energy,
        saved->hydration,
        saved->fatigue,
        saved->stress
    };
    for (unsigned index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
        if (!EntityUnitFloatValid(values[index])) return false;
    }
    return FaunaBehaviorActionValid((FaunaBehaviorAction)saved->behavior);
}

static bool EntityGenomeValid(const CreatureGenome *genome)
{
    if (!genome || genome->genomeId == 0u ||
        genome->genomeId != EvolutionGenomeHash(genome)) return false;
    for (int chromosome = 0; chromosome < 2; chromosome++) {
        if (genome->chromosomes[chromosome].geneCount == 0u ||
            genome->chromosomes[chromosome].geneCount >
                EVOLUTION_GENES_PER_CHROMOSOME) return false;
        bool seen[EVOLUTION_GENES_PER_CHROMOSOME + 1] = { false };
        for (unsigned index = 0;
             index < genome->chromosomes[chromosome].geneCount; index++) {
            const DevelopmentGene *gene =
                &genome->chromosomes[chromosome].genes[index];
            if (gene->locusId == 0u ||
                gene->locusId > EVOLUTION_GENES_PER_CHROMOSOME ||
                seen[gene->locusId] ||
                gene->parentLocusId > EVOLUTION_GENES_PER_CHROMOSOME ||
                gene->crossLocusId > EVOLUTION_GENES_PER_CHROMOSOME ||
                gene->moduleType >= CREATURE_MODULE_TYPE_COUNT ||
                (gene->flags & ~(DEVELOPMENT_GENE_ENABLED |
                                 DEVELOPMENT_GENE_MIRRORED |
                                 DEVELOPMENT_GENE_CROSS_LINK)) != 0u) {
                return false;
            }
            seen[gene->locusId] = true;
        }
    }
    CreaturePhenotype phenotype = EvolutionDevelop(genome);
    return phenotype.valid;
}

static bool EntityDiskStateV4Valid(const EntityDiskStateV4 *saved)
{
    if (!saved || !EntityDiskStateV3Valid(&saved->entity)) return false;
    if (saved->entity.entity.entity.active == 0u) return true;
    if (saved->evolvable > 1u || saved->aquatic > 1u ||
        saved->corpse > 1u || saved->pregnant > 1u ||
        saved->sex > (uint32_t)CREATURE_SEX_MALE ||
        saved->targetEntity < -1 || saved->targetEntity >= MAX_ENTITIES) {
        return false;
    }
    const float values[] = {
        saved->ageDays, saved->lifespanDays, saved->maturityAgeDays,
        saved->reproductionCooldownDays, saved->gestationProgressDays,
        saved->gestationDurationDays, saved->health, saved->corpseEnergy
    };
    for (unsigned index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
        if (!EntityFloatValid(values[index]) || values[index] < 0.0f) {
            return false;
        }
    }
    if (saved->evolvable == 0u) return true;
    if (saved->organismId == 0u || saved->lineageId == 0u ||
        saved->speciesId == 0u || !EntityGenomeValid(&saved->genome)) {
        return false;
    }
    if (saved->pregnant != 0u) {
        if (saved->pendingFatherId == 0u ||
            !EntityGenomeValid(&saved->pendingOffspring)) return false;
    }
    return true;
}

void EntityInitializeBehaviorState(Entity *entity)
{
    entity->ecologyFoodAvailability = 0.72f;
    entity->ecologyWaterAvailability = 0.72f;
    entity->ecologyShelterAvailability = 0.55f;
    entity->ecologyStormPressure = 0.0f;
    entity->ecologyTemperatureStress = 0.0f;
    entity->needs = FaunaNeedsDefault();
    entity->behavior = FAUNA_ACTION_IDLE;
}

static void EntityWriteDiskStateV1(EntityDiskStateV1 *disk,
                                   const Entity *entity)
{
    memset(disk, 0, sizeof(*disk));
    disk->active = entity->active ? 1u : 0u;
    if (!entity->active) return;
    disk->type = (uint32_t)entity->type;
    disk->position[0] = entity->position.x;
    disk->position[1] = entity->position.y;
    disk->position[2] = entity->position.z;
    disk->velocity[0] = entity->velocity.x;
    disk->velocity[1] = entity->velocity.y;
    disk->velocity[2] = entity->velocity.z;
    disk->yaw = entity->yaw;
    disk->moveTimer = entity->moveTimer;
    disk->thinkTimer = entity->thinkTimer;
    disk->burnTimer = entity->burnTimer;
    disk->bodyPlan = (uint32_t)entity->bodyPlan;
    disk->chemistry = (uint32_t)entity->chemistry;
    disk->niche = (uint32_t)entity->niche;
    disk->organismScale = entity->organismScale;
    disk->bodyArmor = entity->bodyArmor;
    disk->movementSpeed = entity->movementSpeed;
    disk->temperament = entity->temperament;
    disk->limbCount = (int32_t)entity->limbCount;
    disk->airborne = entity->airborne ? 1u : 0u;
    disk->colony = entity->colony ? 1u : 0u;
    disk->hoverHeight = entity->hoverHeight;
    disk->phase = entity->phase;
    disk->ecologyActivity = entity->ecologyActivity;
    disk->ecologyCapacity = entity->ecologyCapacity;
    disk->ecologySampleTimer = entity->ecologySampleTimer;
    disk->ecologyWindStrength = entity->ecologyWindStrength;
    disk->ecologyWindAngle = entity->ecologyWindAngle;
    disk->primaryBlock = (uint32_t)entity->primaryBlock;
    disk->accentBlock = (uint32_t)entity->accentBlock;
}

static void EntityReadDiskStateV1(Entity *entity,
                                  const EntityDiskStateV1 *disk)
{
    memset(entity, 0, sizeof(*entity));
    if (disk->active == 0u) return;
    entity->active = true;
    entity->type = (EntityType)disk->type;
    entity->position = (Vector3){
        disk->position[0], disk->position[1], disk->position[2]
    };
    entity->velocity = (Vector3){
        disk->velocity[0], disk->velocity[1], disk->velocity[2]
    };
    entity->yaw = disk->yaw;
    entity->motionTargetYaw = disk->yaw;
    entity->moveTimer = disk->moveTimer;
    entity->thinkTimer = disk->thinkTimer;
    entity->burnTimer = disk->burnTimer;
    entity->bodyPlan = (PlanetBodyPlan)disk->bodyPlan;
    entity->chemistry = (PlanetChemistry)disk->chemistry;
    entity->niche = (PlanetEcologicalNiche)disk->niche;
    entity->organismScale = disk->organismScale;
    entity->bodyArmor = disk->bodyArmor;
    entity->movementSpeed = disk->movementSpeed;
    entity->temperament = disk->temperament;
    entity->limbCount = (int)disk->limbCount;
    entity->airborne = disk->airborne != 0u;
    entity->colony = disk->colony != 0u;
    entity->hoverHeight = disk->hoverHeight;
    entity->phase = disk->phase;
    entity->ecologyActivity = disk->ecologyActivity;
    entity->ecologyCapacity = disk->ecologyCapacity;
    entity->ecologySampleTimer = disk->ecologySampleTimer;
    entity->ecologyWindStrength = disk->ecologyWindStrength;
    entity->ecologyWindAngle = disk->ecologyWindAngle;
    EntityInitializeBehaviorState(entity);
    if (entity->moveTimer > 0.0f) {
        entity->behavior = FAUNA_ACTION_WANDER;
    }
    entity->primaryBlock = (BlockType)disk->primaryBlock;
    entity->accentBlock = (BlockType)disk->accentBlock;
}

static void EntityWriteDiskStateV2(EntityDiskStateV2 *disk,
                                   const Entity *entity)
{
    memset(disk, 0, sizeof(*disk));
    EntityWriteDiskStateV1(&disk->entity, entity);
    if (entity->active) disk->motionTargetYaw = entity->motionTargetYaw;
}

static void EntityReadDiskStateV2(Entity *entity,
                                  const EntityDiskStateV2 *disk)
{
    EntityReadDiskStateV1(entity, &disk->entity);
    if (entity->active) entity->motionTargetYaw = disk->motionTargetYaw;
}

static void EntityWriteDiskStateV3(EntityDiskStateV3 *disk,
                                   const Entity *entity)
{
    memset(disk, 0, sizeof(*disk));
    EntityWriteDiskStateV2(&disk->entity, entity);
    if (!entity->active) return;
    disk->ecologyFoodAvailability = entity->ecologyFoodAvailability;
    disk->ecologyWaterAvailability = entity->ecologyWaterAvailability;
    disk->ecologyShelterAvailability = entity->ecologyShelterAvailability;
    disk->ecologyStormPressure = entity->ecologyStormPressure;
    disk->ecologyTemperatureStress = entity->ecologyTemperatureStress;
    disk->energy = entity->needs.energy;
    disk->hydration = entity->needs.hydration;
    disk->fatigue = entity->needs.fatigue;
    disk->stress = entity->needs.stress;
    disk->behavior = (uint32_t)entity->behavior;
}

static void EntityReadDiskStateV3(Entity *entity,
                                  const EntityDiskStateV3 *disk)
{
    EntityReadDiskStateV2(entity, &disk->entity);
    if (!entity->active) return;
    entity->ecologyFoodAvailability = disk->ecologyFoodAvailability;
    entity->ecologyWaterAvailability = disk->ecologyWaterAvailability;
    entity->ecologyShelterAvailability = disk->ecologyShelterAvailability;
    entity->ecologyStormPressure = disk->ecologyStormPressure;
    entity->ecologyTemperatureStress = disk->ecologyTemperatureStress;
    entity->needs = (FaunaNeeds){
        .energy = disk->energy,
        .hydration = disk->hydration,
        .fatigue = disk->fatigue,
        .stress = disk->stress
    };
    entity->behavior = (FaunaBehaviorAction)disk->behavior;
}

static void EntityWriteDiskStateV4(EntityDiskStateV4 *disk,
                                   const Entity *entity)
{
    memset(disk, 0, sizeof(*disk));
    EntityWriteDiskStateV3(&disk->entity, entity);
    if (!entity->active) return;
    disk->evolvable = entity->evolvable ? 1u : 0u;
    disk->aquatic = entity->aquatic ? 1u : 0u;
    disk->corpse = entity->corpse ? 1u : 0u;
    disk->pregnant = entity->pregnant ? 1u : 0u;
    disk->sex = (uint32_t)entity->sex;
    disk->organismId = entity->organismId;
    disk->lineageId = entity->lineageId;
    disk->speciesId = entity->speciesId;
    disk->motherId = entity->motherId;
    disk->fatherId = entity->fatherId;
    disk->pendingFatherId = entity->pendingFatherId;
    disk->ageDays = entity->ageDays;
    disk->lifespanDays = entity->lifespanDays;
    disk->maturityAgeDays = entity->maturityAgeDays;
    disk->reproductionCooldownDays = entity->reproductionCooldownDays;
    disk->gestationProgressDays = entity->gestationProgressDays;
    disk->gestationDurationDays = entity->gestationDurationDays;
    disk->health = entity->health;
    disk->corpseEnergy = entity->corpseEnergy;
    disk->targetEntity = entity->targetEntity;
    if (entity->evolvable) disk->genome = entity->genome;
    if (entity->pregnant) disk->pendingOffspring = entity->pendingOffspring;
}

static void EntityReadDiskStateV4(Entity *entity,
                                  const EntityDiskStateV4 *disk)
{
    EntityReadDiskStateV3(entity, &disk->entity);
    if (!entity->active) return;
    entity->evolvable = disk->evolvable != 0u;
    entity->aquatic = disk->aquatic != 0u;
    entity->corpse = disk->corpse != 0u;
    entity->pregnant = disk->pregnant != 0u;
    entity->sex = (CreatureSex)disk->sex;
    entity->organismId = disk->organismId;
    entity->lineageId = disk->lineageId;
    entity->speciesId = disk->speciesId;
    entity->motherId = disk->motherId;
    entity->fatherId = disk->fatherId;
    entity->pendingFatherId = disk->pendingFatherId;
    entity->ageDays = disk->ageDays;
    entity->lifespanDays = disk->lifespanDays;
    entity->maturityAgeDays = disk->maturityAgeDays;
    entity->reproductionCooldownDays = disk->reproductionCooldownDays;
    entity->gestationProgressDays = disk->gestationProgressDays;
    entity->gestationDurationDays = disk->gestationDurationDays;
    entity->health = disk->health;
    entity->corpseEnergy = disk->corpseEnergy;
    entity->targetEntity = disk->targetEntity;
    if (entity->evolvable) {
        entity->genome = disk->genome;
        if (entity->pregnant) {
            entity->pendingOffspring = disk->pendingOffspring;
        }
        EntityApplyEvolutionPhenotype(entity);
    }
}

static void EntityMigrateEvolution(Entity *entity, uint32_t migrationSeed)
{
    if (!entity || !entity->active || !EntityIsAlien(entity->type)) return;
    EntityInitializeEvolution(entity,
        EntityEvolutionArchetypeForBodyPlan(entity->bodyPlan), migrationSeed,
        false);
}

bool EntitiesSaveState(FILE *file)
{
    if (!file || !isfinite(entitySpawnTimer)) return false;
    const uint32_t header[3] = {
        ENTITY_STATE_VERSION, MAX_ENTITIES, entityRandomState
    };
    if (fwrite(header, sizeof(header), 1, file) != 1 ||
        fwrite(&entitySpawnTimer, sizeof(entitySpawnTimer), 1, file) != 1) {
        return false;
    }

    EntityDiskStateV4 saved[MAX_ENTITIES] = { 0 };
    for (int index = 0; index < MAX_ENTITIES; index++) {
        const Entity *entity = &entities[index];
        EntityDiskStateV4 *disk = &saved[index];
        EntityWriteDiskStateV4(disk, entity);
        if (!EntityDiskStateV4Valid(disk)) return false;
    }
    return fwrite(saved, sizeof(saved), 1, file) == 1;
}

bool EntitiesLoadState(FILE *file)
{
    uint32_t header[3];
    float loadedSpawnTimer = 0.0f;
    if (!file || fread(header, sizeof(header), 1, file) != 1 ||
        fread(&loadedSpawnTimer, sizeof(loadedSpawnTimer), 1, file) != 1) {
        return false;
    }
    if ((header[0] < 1u || header[0] > ENTITY_STATE_VERSION) ||
        header[1] != MAX_ENTITIES ||
        header[2] == 0u || !EntityFloatValid(loadedSpawnTimer)) {
        return false;
    }

    Entity loaded[MAX_ENTITIES] = { 0 };
    if (header[0] == 1u) {
        EntityDiskStateV1 saved[MAX_ENTITIES];
        if (fread(saved, sizeof(saved), 1, file) != 1) return false;
        for (int index = 0; index < MAX_ENTITIES; index++) {
            if (!EntityDiskStateValid(&saved[index])) return false;
            EntityReadDiskStateV1(&loaded[index], &saved[index]);
        }
    } else if (header[0] == 2u) {
        EntityDiskStateV2 saved[MAX_ENTITIES];
        if (fread(saved, sizeof(saved), 1, file) != 1) return false;
        for (int index = 0; index < MAX_ENTITIES; index++) {
            if (!EntityDiskStateV2Valid(&saved[index])) return false;
            EntityReadDiskStateV2(&loaded[index], &saved[index]);
        }
    } else if (header[0] == 3u) {
        EntityDiskStateV3 saved[MAX_ENTITIES];
        if (fread(saved, sizeof(saved), 1, file) != 1) return false;
        for (int index = 0; index < MAX_ENTITIES; index++) {
            if (!EntityDiskStateV3Valid(&saved[index])) return false;
            EntityReadDiskStateV3(&loaded[index], &saved[index]);
        }
    } else {
        EntityDiskStateV4 saved[MAX_ENTITIES];
        if (fread(saved, sizeof(saved), 1, file) != 1) return false;
        for (int index = 0; index < MAX_ENTITIES; index++) {
            if (!EntityDiskStateV4Valid(&saved[index])) return false;
            EntityReadDiskStateV4(&loaded[index], &saved[index]);
        }
    }
    if (header[0] < 4u) {
        for (int index = 0; index < MAX_ENTITIES; index++) {
            EntityMigrateEvolution(&loaded[index], EntityMix(
                header[2] ^ (uint32_t)index * 0x9e3779b9u));
        }
    }

    memcpy(entities, loaded, sizeof(entities));
    entityRandomState = header[2];
    entitySpawnTimer = loadedSpawnTimer;
    return true;
}

int EntityRayHit(Vector3 origin, Vector3 direction, float maxDistance)
{
    float best = maxDistance;
    int bestIndex = -1;

    for (int i = 0; i < MAX_ENTITIES; i++) {
        const Entity *entity = &entities[i];
        if (!entity->active) continue;

        float radius = (entity->type == ENTITY_CHICKEN) ? 0.45f : 0.6f;
        float height = (entity->type == ENTITY_CHICKEN) ? 1.1f : 1.5f;
        if (entity->type == ENTITY_ALIEN_GRAZER) {
            radius = 1.0f;
            height = 1.8f;
        } else if (entity->type == ENTITY_ALIEN_HOPPER) {
            radius = 0.65f;
            height = 1.6f;
        } else if (entity->type == ENTITY_ALIEN_STRIDER) {
            radius = 0.9f;
            height = 2.8f;
        }
        if (EntityUsesEcology(entity)) {
            float scale = entity->organismScale > 0.1f ? entity->organismScale : 1.0f;
            radius = 0.62f * scale;
            height = 1.55f * scale;
            if (entity->evolvable && entity->phenotype.valid) {
                float growth = EntityEvolutionGrowthScale(entity);
                radius = fminf(fmaxf(
                    entity->phenotype.bodyRadius * scale * growth,
                    0.25f), 3.0f);
                height = fminf(fmaxf(
                    entity->phenotype.bodyRadius * 1.7f * scale * growth,
                    0.45f), 4.5f);
                if (entity->corpse) height *= 0.42f;
            } else if (entity->bodyPlan == PLANET_BODY_FLOATING) {
                radius = 0.95f * scale;
                height = 1.15f * scale;
            } else if (entity->bodyPlan == PLANET_BODY_SERPENTINE) {
                radius = 0.70f * scale;
                height = 0.85f * scale;
            } else if (entity->bodyPlan == PLANET_BODY_COLONY) {
                radius = 0.95f * scale;
                height = 1.25f * scale;
            }
        }
        Vector3 center = { entity->position.x, entity->position.y + height * 0.5f, entity->position.z };
        if (EntityUsesEcology(entity) &&
            (entity->bodyPlan == PLANET_BODY_FLOATING || entity->aquatic)) {
            center.y = entity->position.y;
        }

        Vector3 toCenter = Vector3Subtract(center, origin);
        float proj = Vector3DotProduct(toCenter, direction);
        if (proj < 0.0f || proj > best) continue;

        Vector3 closest = Vector3Add(origin, Vector3Scale(direction, proj));
        Vector3 diff = Vector3Subtract(center, closest);
        float radial = sqrtf(diff.x * diff.x + diff.z * diff.z);
        if (radial > radius) continue;
        if (fabsf(diff.y) > height * 0.5f + radius) continue;

        best = proj;
        bestIndex = i;
    }
    return bestIndex;
}

bool EntityKill(int index, EntityDeathCause cause, float daylight)
{
    if (index < 0 || index >= MAX_ENTITIES) return false;
    Entity *entity = &entities[index];
    if (!entity->active) return false;

    if (cause == ENTITY_DEATH_PLAYER && EntityUsesEcology(entity)) {
        PlanetEcologyRecordFaunaHarvest(
            (int)floorf(entity->position.x),
            (int)floorf(entity->position.z), daylight,
            entity->organismScale, entity->ecologyCapacity);
    }

    GameEffectsEmitParticleBurst(
        entity->position, EntityParticleColor(entity->type),
        18, 3.0f, 0.7f);
    GameEffectsPlayAudio(GAME_AUDIO_BREAK);
    if (cause == ENTITY_DEATH_PREDATION && entity->evolvable) {
        PlanetEcologyRecordEvolutionEvent(
            (int)floorf(entity->position.x),
            (int)floorf(entity->position.z), daylight,
            entity->lineageId, PLANET_EVOLUTION_EVENT_PREDATION_DEATH,
            entity->phenotype.totalMass);
        EntityBecomeCorpse(entity);
    } else {
        entity->active = false;
    }
    return true;
}

int EntityNearestEvolvable(Vector3 position, float radius)
{
    if (!isfinite(radius) || radius <= 0.0f) return -1;
    float bestDistance = radius * radius;
    int best = -1;
    for (int index = 0; index < MAX_ENTITIES; index++) {
        const Entity *entity = &entities[index];
        if (!entity->active || !entity->evolvable) continue;
        float dx = entity->position.x - position.x;
        float dy = entity->position.y - position.y;
        float dz = entity->position.z - position.z;
        float distance = dx * dx + dy * dy + dz * dz;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }
    return best;
}

int EntityEvolutionFindByOrganism(uint32_t organismId)
{
    if (organismId == 0u) return -1;
    for (int index = 0; index < MAX_ENTITIES; index++) {
        const Entity *entity = &entities[index];
        if (entity->active && entity->evolvable &&
            entity->organismId == organismId) return index;
    }
    return -1;
}

bool EntityEvolutionInspect(int index, EntityEvolutionDebugInfo *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (index < 0 || index >= MAX_ENTITIES) return false;
    const Entity *entity = &entities[index];
    if (!entity->active || !entity->evolvable || !entity->phenotype.valid) {
        return false;
    }
    *out = (EntityEvolutionDebugInfo){
        .valid = true,
        .corpse = entity->corpse,
        .juvenile = entity->ageDays < entity->maturityAgeDays,
        .pregnant = entity->pregnant,
        .positionX = entity->position.x,
        .positionZ = entity->position.z,
        .organismId = entity->organismId,
        .lineageId = entity->lineageId,
        .speciesId = entity->speciesId,
        .motherId = entity->motherId,
        .fatherId = entity->fatherId,
        .genomeId = entity->genome.genomeId,
        .generation = entity->genome.generation,
        .mutationCount = entity->genome.mutationCount,
        .sex = entity->sex,
        .locomotion = entity->phenotype.locomotion,
        .ageDays = entity->ageDays,
        .maturityAgeDays = entity->maturityAgeDays,
        .health = entity->health,
        .energy = entity->needs.energy,
        .diet = entity->phenotype.diet,
        .mass = entity->phenotype.totalMass,
        .speed = entity->phenotype.cruiseSpeed,
        .moduleCount = entity->phenotype.moduleCount,
        .genome = entity->genome,
        .phenotype = entity->phenotype
    };
    return true;
}
