#include "entity.h"

#include "creature_renderer.h"
#include "fauna_motion.h"
#include "fluid.h"
#include "raymath.h"
#include "world.h"
#include "terrain.h"
#include "ecology.h"
#include "space.h"
#include "world_environment.h"
#include "particles.h"
#include "audio.h"
#include "weather.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ENTITY_STATE_VERSION 4u
#define ENTITY_RANDOM_FALLBACK 0x6d2b79f5u
#define ENTITY_EVOLUTION_DAYS_PER_SECOND 0.02f

static Vector3 EntityFluidCurrent(const Entity *entity)
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

static Entity entities[MAX_ENTITIES];
static uint32_t entityRandomState = ENTITY_RANDOM_FALLBACK;
static float entitySpawnTimer = 0.0f;

static bool EntityIsAlien(EntityType type);

static uint32_t EntityMix(uint32_t value)
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

static uint32_t EntityRandomNext(void)
{
    uint32_t state = entityRandomState;
    if (state == 0u) state = ENTITY_RANDOM_FALLBACK;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    entityRandomState = state;
    return state;
}

static int EntityRandomBounded(unsigned bound)
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

static EvolutionArchetype EntityEvolutionArchetypeForType(EntityType type)
{
    if (type == ENTITY_ALIEN_HOPPER) return EVOLUTION_ARCHETYPE_FLIGHT;
    if (type == ENTITY_ALIEN_STRIDER) return EVOLUTION_ARCHETYPE_AQUATIC;
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

static void EntityApplyEvolutionPhenotype(Entity *entity)
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

static void EntityInitializeEvolution(Entity *entity,
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

static void EntityInitializeBehaviorState(Entity *entity)
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

static int NextFreeEntity(void)
{
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if (!entities[i].active) return i;
    }
    return -1;
}

static bool BlockBlocksEntity(int x, int y, int z)
{
    BlockType type = GetBlockAt(x, y, z);
    return type != BLOCK_AIR && type != BLOCK_WATER && type != BLOCK_LAVA;
}

static bool GroundBelow(Vector3 position)
{
    int x = (int)floorf(position.x);
    int z = (int)floorf(position.z);
    int y = (int)floorf(position.y - 0.1f);
    if (WorldBlockRegionAt(y) != WORLD_BLOCK_REGION_SURFACE) return false;
    return BlockBlocksEntity(x, y, z);
}

static int EntitySurfaceHeight(int x, int z)
{
    return WorldSurfaceHeightAt(x, z);
}

static bool PlanetBiomeSupportsFauna(int x, int z)
{
    PlanetBiome biome = PlanetBiomeAt(x, z);
    return biome != PLANET_BIOME_OCEAN && biome != PLANET_BIOME_LAVA_SEA &&
           biome != PLANET_BIOME_STORM_BANDS && biome != PLANET_BIOME_VOLCANIC_RIDGE;
}

static bool EntityIsAlien(EntityType type)
{
    return type >= ENTITY_ALIEN_GRAZER && type <= ENTITY_ALIEN_STRIDER;
}

static bool EntityUsesEcology(const Entity *entity)
{
    return entity && (entity->evolvable || EntityIsAlien(entity->type));
}

static bool EntityAnyEvolvable(void)
{
    for (int index = 0; index < MAX_ENTITIES; index++) {
        if (entities[index].active && entities[index].evolvable) return true;
    }
    return false;
}

static EntityType EntityEvolutionTypeForArchetype(
    EvolutionArchetype archetype, bool alienWorld)
{
    if (alienWorld) {
        return archetype == EVOLUTION_ARCHETYPE_FLIGHT ? ENTITY_ALIEN_HOPPER :
            archetype == EVOLUTION_ARCHETYPE_AQUATIC ? ENTITY_ALIEN_STRIDER :
            ENTITY_ALIEN_GRAZER;
    }
    return archetype == EVOLUTION_ARCHETYPE_FLIGHT ? ENTITY_CHICKEN :
        archetype == EVOLUTION_ARCHETYPE_AQUATIC ? ENTITY_PIG : ENTITY_COW;
}

static float EntityUnit(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    return fminf(value, 1.0f);
}

static float EntityFoodAvailability(
    PlanetEcologicalNiche niche, const PlanetLocalEcology *local)
{
    if (!local) return 0.0f;
    float flora = EntityUnit(fmaxf(local->population.floraDensity,
                                   local->suitability.floraActivity));
    float fauna = EntityUnit(fmaxf(local->population.faunaDensity,
                                   local->suitability.faunaActivity));
    float moisture = EntityUnit(local->environment.soilMoisture);
    float light = EntityUnit(local->environment.currentUsableLight);
    float precipitation = EntityUnit(local->environment.precipitationRate);
    switch (niche) {
    case PLANET_NICHE_MICROBIAL:
        return EntityUnit(moisture * 0.58f + flora * 0.24f +
                          precipitation * 0.18f);
    case PLANET_NICHE_DECOMPOSER:
        return EntityUnit(flora * 0.55f + fauna * 0.25f + moisture * 0.20f);
    case PLANET_NICHE_FILTER_FEEDER:
        return EntityUnit(flora * 0.38f + precipitation * 0.34f +
                          fauna * 0.18f + light * 0.10f);
    case PLANET_NICHE_BIOLUMINESCENT_COLONY:
        return EntityUnit(flora * 0.45f + moisture * 0.30f + light * 0.25f);
    case PLANET_NICHE_CRYSTAL_GRAZER:
        return EntityUnit(flora * 0.68f +
                          local->environment.biomeSupport * 0.32f);
    case PLANET_NICHE_GRAZER:
    default:
        return flora;
    }
}

static float EntityWaterAvailability(const PlanetLocalEcology *local)
{
    if (!local) return 0.0f;
    return EntityUnit(local->environment.liquidWaterAccess * 0.68f +
                      local->environment.soilMoisture * 0.18f +
                      local->environment.precipitationRate * 0.14f);
}

static float EntityShelterAvailability(const PlanetLocalEcology *local)
{
    if (!local) return 0.0f;
    float storm = EntityUnit(local->environment.currentStorm);
    return EntityUnit(local->environment.shelter * (1.0f - storm * 0.35f));
}

static void EntityApplyLocalBehaviorEnvironment(
    Entity *entity, const PlanetLocalEcology *local,
    WeatherFieldSample weather)
{
    if (!entity || !local) return;
    entity->ecologyFoodAvailability = EntityFoodAvailability(
        entity->niche, local);
    entity->ecologyWaterAvailability = EntityWaterAvailability(local);
    entity->ecologyShelterAvailability = EntityShelterAvailability(local);
    entity->ecologyStormPressure = EntityUnit(fmaxf(
        local->environment.currentStorm, weather.storm));
    entity->ecologyTemperatureStress = EntityUnit(
        1.0f - local->suitability.temperatureScore);
}

static float EntityFoodDependence(PlanetEcologicalNiche niche)
{
    switch (niche) {
    case PLANET_NICHE_MICROBIAL: return 0.48f;
    case PLANET_NICHE_FILTER_FEEDER: return 0.62f;
    case PLANET_NICHE_BIOLUMINESCENT_COLONY: return 0.42f;
    default: return 0.88f;
    }
}

static float EntityWaterDependence(PlanetChemistry chemistry)
{
    switch (chemistry) {
    case PLANET_CHEMISTRY_SILICON: return 0.22f;
    case PLANET_CHEMISTRY_SULFUR: return 0.48f;
    case PLANET_CHEMISTRY_CARBON:
    default: return 0.92f;
    }
}

static void DespawnDistantAlien(const Player *player)
{
    int farthest = -1;
    float farthestDistanceSquared = -1.0f;
    for (int index = 0; index < MAX_ENTITIES; index++) {
        Entity *entity = &entities[index];
        if (!entity->active || !EntityIsAlien(entity->type)) continue;
        float dx = entity->position.x - player->position.x;
        float dz = entity->position.z - player->position.z;
        float distanceSquared = dx * dx + dz * dz;
        if (distanceSquared > farthestDistanceSquared) {
            farthestDistanceSquared = distanceSquared;
            farthest = index;
        }
    }
    if (farthest >= 0) entities[farthest].active = false;
}

static void SpawnPassive(const Player *player, float daylight)
{
    int slot = NextFreeEntity();
    if (slot < 0) return;

    bool alienWorld = PlanetWorldIsActive();
    bool homeWorld = !alienWorld &&
        WorldBlockRegionAt((int)floorf(player->position.y)) ==
        WORLD_BLOCK_REGION_SURFACE;
    PlanetEcologyProfile ecology = { 0 };
    if (alienWorld) {
        ecology = PlanetEcologyCurrent();
        if (!ecology.supportsFlight && player->position.y > 40.0f) return;
    } else if (!homeWorld && player->position.y > 40.0f) {
        return;
    }
    EntityType type = ENTITY_COW;
    EvolutionArchetype evolutionArchetype = EVOLUTION_ARCHETYPE_GROUND;
    bool evolvableSpawn = alienWorld ||
        (homeWorld && (!EntityAnyEvolvable() ||
                       EntityRandomBounded(100u) < 45));
    if (evolvableSpawn) {
        ecology = PlanetEcologyCurrent();
        if (ecology.faunaDensity <= 0.0f) return;
        uint32_t speciesSeed = PlanetWorldSeed();
        if (alienWorld) {
            int species = (int)((speciesSeed +
                (uint32_t)EntityRandomBounded(3u) * 17u) % 3u);
            type = (EntityType)(ENTITY_ALIEN_GRAZER + species);
            evolutionArchetype = EntityEvolutionArchetypeForType(type);
            if (evolutionArchetype == EVOLUTION_ARCHETYPE_FLIGHT &&
                !ecology.supportsFlight) {
                type = ENTITY_ALIEN_GRAZER;
                evolutionArchetype = EVOLUTION_ARCHETYPE_GROUND;
            }
        }
    } else {
        EntityType types[4] = { ENTITY_COW, ENTITY_SHEEP, ENTITY_PIG, ENTITY_CHICKEN };
        type = types[EntityRandomBounded(4u)];
    }

    PlanetLocalEcology localEcology = { 0 };
    CreatureGenome sampledGenome = { 0 };
    uint32_t sampledLineage = 0u;
    uint32_t sampledSpecies = 0u;
    uint32_t organismSeed = 0u;
    bool haveSampledGenome = false;
    float angle = (float)EntityRandomBounded(628u) / 100.0f;
    float dist = evolvableSpawn ? 12.0f + (float)EntityRandomBounded(160u) / 10.0f
                            : 14.0f + (float)EntityRandomBounded(300u) / 10.0f;
    int gx = (int)floorf(player->position.x + cosf(angle) * dist);
    int gz = (int)floorf(player->position.z + sinf(angle) * dist);
    if (evolvableSpawn) {
        localEcology = PlanetEcologyLocalAt(gx, gz, daylight);
        float faunaActivity = localEcology.suitability.faunaActivity;
        if (!PlanetFaunaSpawnAccepted(
                faunaActivity, (uint32_t)EntityRandomBounded(1000u))) {
            return;
        }
        organismSeed = EntityMix((PlanetWorldIsActive() ? PlanetWorldSeed() :
            WorldGetSeed()) ^
            (uint32_t)gx * 0x9e3779b9u ^ (uint32_t)gz * 0x85ebca6bu ^
            EntityRandomNext());
        if (organismSeed == 0u) organismSeed = 1u;
        haveSampledGenome = PlanetEcologySampleGenome(
            gx, gz, daylight, organismSeed, &sampledGenome,
            &sampledLineage, &sampledSpecies);
        if (haveSampledGenome) {
            CreaturePhenotype sampledPhenotype = EvolutionDevelop(
                &sampledGenome);
            haveSampledGenome = sampledPhenotype.valid;
            if (haveSampledGenome) {
                evolutionArchetype = sampledPhenotype.locomotion ==
                    CREATURE_LOCOMOTION_FLIGHT ? EVOLUTION_ARCHETYPE_FLIGHT :
                    sampledPhenotype.locomotion ==
                    CREATURE_LOCOMOTION_AQUATIC ?
                    EVOLUTION_ARCHETYPE_AQUATIC : EVOLUTION_ARCHETYPE_GROUND;
                type = EntityEvolutionTypeForArchetype(
                    evolutionArchetype, alienWorld);
            }
        }
    }
    if (evolvableSpawn && evolutionArchetype == EVOLUTION_ARCHETYPE_FLIGHT &&
        !ecology.supportsFlight) return;
    if (evolvableSpawn && evolutionArchetype == EVOLUTION_ARCHETYPE_GROUND &&
        !PlanetBiomeSupportsFauna(gx, gz) &&
        ecology.niche != PLANET_NICHE_CRYSTAL_GRAZER) return;
    int groundY = EntitySurfaceHeight(gx, gz);
    int waterY = -1;
    if (evolvableSpawn && evolutionArchetype == EVOLUTION_ARCHETYPE_AQUATIC) {
        int seaLevel = homeWorld ? HOME_SEA_LEVEL : -1;
        int scanTop = fmaxf((float)groundY + 2.0f,
                            (float)(seaLevel >= 0 ? seaLevel : groundY + 2));
        scanTop = (int)fminf(scanTop, (float)WORLD_HEIGHT - 2.0f);
        for (int y = scanTop; y >= groundY - 5; y--) {
            if (GetBlockAt(gx, y, gz) == BLOCK_WATER) {
                waterY = y;
                break;
            }
        }
        if (waterY < 0) return;
    }
    if (!evolvableSpawn || evolutionArchetype == EVOLUTION_ARCHETYPE_GROUND) {
        BlockType spawnAt = GetBlockAt(gx, groundY + 1, gz);
        BlockType spawnAbove = GetBlockAt(gx, groundY + 2, gz);
        if (evolvableSpawn) {
            if (spawnAt != BLOCK_AIR || spawnAbove != BLOCK_AIR) return;
        } else {
            if (spawnAt != BLOCK_AIR && spawnAt != BLOCK_WATER && spawnAt != BLOCK_LAVA) return;
            if (spawnAbove != BLOCK_AIR && spawnAbove != BLOCK_WATER && spawnAbove != BLOCK_LAVA) return;
        }
    }

    Entity *entity = &entities[slot];
    memset(entity, 0, sizeof(*entity));
    entity->active = true;
    entity->type = type;
    float spawnY = evolutionArchetype == EVOLUTION_ARCHETYPE_AQUATIC ?
        (float)waterY + 0.35f : (float)groundY + 1.0f;
    if (evolvableSpawn && evolutionArchetype == EVOLUTION_ARCHETYPE_FLIGHT) {
        float flightBase = fmaxf(spawnY + 3.0f, player->position.y + 1.5f);
        spawnY = fminf((float)WORLD_HEIGHT - 3.0f,
                       flightBase + (float)EntityRandomBounded(45u) / 10.0f);
    }
    entity->position = (Vector3){ (float)gx + 0.5f, spawnY, (float)gz + 0.5f };
    entity->velocity = Vector3Zero();
    entity->yaw = (float)EntityRandomBounded(628u) / 100.0f;
    entity->motionTargetYaw = entity->yaw;
    entity->moveTimer = 0.0f;
    entity->thinkTimer = 1.0f + (float)EntityRandomBounded(200u) / 100.0f;
    entity->burnTimer = 0.0f;
    entity->bodyPlan = evolvableSpawn ? ecology.bodyPlan : PLANET_BODY_QUADRUPED;
    entity->chemistry = evolvableSpawn ? ecology.chemistry : PLANET_CHEMISTRY_CARBON;
    entity->niche = evolvableSpawn ? ecology.niche : PLANET_NICHE_GRAZER;
    entity->organismScale = evolvableSpawn ? ecology.organismScale : 1.0f;
    entity->bodyArmor = evolvableSpawn ? ecology.bodyArmor : 0.0f;
    entity->movementSpeed = evolvableSpawn ? ecology.movementSpeed : 0.85f;
    entity->temperament = evolvableSpawn ? ecology.temperament : 0.2f;
    entity->limbCount = evolvableSpawn ? ecology.limbCount : 4;
    entity->airborne = evolvableSpawn && ecology.bodyPlan == PLANET_BODY_FLOATING;
    entity->colony = evolvableSpawn && ecology.bodyPlan == PLANET_BODY_COLONY;
    if (evolvableSpawn) {
        if (organismSeed == 0u) {
            organismSeed = EntityMix((PlanetWorldIsActive() ? PlanetWorldSeed() :
                WorldGetSeed()) ^
                (uint32_t)gx * 0x9e3779b9u ^
                (uint32_t)gz * 0x85ebca6bu ^ EntityRandomNext());
            if (organismSeed == 0u) organismSeed = 1u;
        }
        EntityInitializeEvolution(entity, evolutionArchetype,
                                  organismSeed, false);
        if (haveSampledGenome) {
            entity->genome = sampledGenome;
            entity->lineageId = sampledLineage;
            entity->speciesId = sampledSpecies;
            EntityApplyEvolutionPhenotype(entity);
        }
    }
    entity->hoverHeight = spawnY;
    entity->phase = (float)EntityRandomBounded(628u) / 100.0f;
    entity->ecologyActivity = evolvableSpawn
        ? localEcology.suitability.faunaActivity : 1.0f;
    entity->ecologyCapacity = evolvableSpawn
        ? localEcology.suitability.faunaCapacity : 1.0f;
    entity->ecologyWindStrength = 0.0f;
    entity->ecologyWindAngle = 0.0f;
    EntityInitializeBehaviorState(entity);
    if (evolvableSpawn) {
        WeatherFieldSample weather = WeatherFieldSampleAtWorld(gx, gz);
        entity->ecologyWindStrength = weather.wind;
        entity->ecologyWindAngle = WeatherWindAngleAtWorld(gx, gz);
        EntityApplyLocalBehaviorEnvironment(entity, &localEcology, weather);
    }
    entity->ecologySampleTimer = evolvableSpawn
        ? 0.25f + (float)slot / (float)MAX_ENTITIES : 1.0f;
    entity->primaryBlock = evolvableSpawn ? ecology.primaryBlock : BLOCK_GRASS;
    entity->accentBlock = evolvableSpawn ? ecology.accentBlock : BLOCK_DIRT;
}

static void SpawnHostile(const Player *player, float daylight)
{
    if (daylight > 0.15f) return;
    if (player->position.y > 30.0f || player->position.y < -1.0f) return;

    int slot = NextFreeEntity();
    if (slot < 0) return;

    EntityType type = EntityRandomBounded(2u) == 0
        ? ENTITY_ZOMBIE : ENTITY_SKELETON;
    float angle = (float)EntityRandomBounded(628u) / 100.0f;
    float dist = 18.0f + (float)EntityRandomBounded(200u) / 10.0f;
    int gx = (int)floorf(player->position.x + cosf(angle) * dist);
    int gz = (int)floorf(player->position.z + sinf(angle) * dist);
    int groundY = EntitySurfaceHeight(gx, gz);
    BlockType spawnAt = GetBlockAt(gx, groundY + 1, gz);
    BlockType spawnAbove = GetBlockAt(gx, groundY + 2, gz);
    if (spawnAt != BLOCK_AIR && spawnAt != BLOCK_WATER && spawnAt != BLOCK_LAVA) return;
    if (spawnAbove != BLOCK_AIR && spawnAbove != BLOCK_WATER && spawnAbove != BLOCK_LAVA) return;

    Entity *entity = &entities[slot];
    memset(entity, 0, sizeof(*entity));
    entity->active = true;
    entity->type = type;
    entity->position = (Vector3){ (float)gx + 0.5f, (float)groundY + 1.0f, (float)gz + 0.5f };
    entity->velocity = Vector3Zero();
    entity->yaw = (float)EntityRandomBounded(628u) / 100.0f;
    entity->motionTargetYaw = entity->yaw;
    entity->moveTimer = 0.0f;
    entity->thinkTimer = 0.1f;
    entity->burnTimer = 2.0f;
    entity->bodyPlan = PLANET_BODY_QUADRUPED;
    entity->chemistry = PLANET_CHEMISTRY_CARBON;
    entity->niche = PLANET_NICHE_GRAZER;
    entity->organismScale = 1.0f;
    entity->bodyArmor = 0.0f;
    entity->movementSpeed = 1.0f;
    entity->temperament = 1.0f;
    entity->limbCount = 2;
    entity->airborne = false;
    entity->colony = false;
    entity->hoverHeight = entity->position.y;
    entity->phase = 0.0f;
    entity->ecologyActivity = 1.0f;
    entity->ecologyCapacity = 1.0f;
    entity->ecologySampleTimer = 1.0f;
    EntityInitializeBehaviorState(entity);
    entity->primaryBlock = BLOCK_GRASS;
    entity->accentBlock = BLOCK_DIRT;
}

static void MoveEntityHorizontal(Entity *entity, Vector3 delta, float dt)
{
    Vector3 next = entity->position;
    next.x += delta.x * dt;
    if (!BlockBlocksEntity((int)floorf(next.x + 0.3f * (delta.x >= 0 ? 1 : -1)),
                           (int)floorf(entity->position.y), (int)floorf(entity->position.z)) &&
        !BlockBlocksEntity((int)floorf(next.x + 0.3f * (delta.x >= 0 ? 1 : -1)),
                           (int)floorf(entity->position.y) + 1, (int)floorf(entity->position.z))) {
        entity->position.x = next.x;
    }

    next = entity->position;
    next.z += delta.z * dt;
    if (!BlockBlocksEntity((int)floorf(entity->position.x),
                           (int)floorf(entity->position.y), (int)floorf(next.z + 0.3f * (delta.z >= 0 ? 1 : -1))) &&
        !BlockBlocksEntity((int)floorf(entity->position.x),
                           (int)floorf(entity->position.y) + 1, (int)floorf(next.z + 0.3f * (delta.z >= 0 ? 1 : -1)))) {
        entity->position.z = next.z;
    }
}

static FaunaLocomotionArchetype EntityLocomotionArchetype(
    PlanetBodyPlan bodyPlan)
{
    switch (bodyPlan) {
    case PLANET_BODY_BIPED: return FAUNA_LOCOMOTION_BIPED;
    case PLANET_BODY_HEXAPOD: return FAUNA_LOCOMOTION_HEXAPOD;
    case PLANET_BODY_SERPENTINE: return FAUNA_LOCOMOTION_SERPENTINE;
    case PLANET_BODY_FLOATING: return FAUNA_LOCOMOTION_FLOATING;
    case PLANET_BODY_COLONY: return FAUNA_LOCOMOTION_COLONY;
    case PLANET_BODY_QUADRUPED:
    default: return FAUNA_LOCOMOTION_QUADRUPED;
    }
}

static FaunaMotionProfile EntityMotionProfile(const Entity *entity,
                                               float baseSpeed)
{
    FaunaMotionProfileInput input = {
        .archetype = EntityLocomotionArchetype(entity->bodyPlan),
        .baseSpeed = baseSpeed,
        .sprintMultiplier = 1.25f + fmaxf(entity->temperament, 0.0f),
        .organismScale = entity->organismScale,
        .gravityScale = WorldGravityScale(),
        .windStrength = entity->ecologyWindStrength
    };
    FaunaMotionProfile result = FaunaMotionProfileDerive(&input);
    if (entity->evolvable && entity->phenotype.valid) {
        result.bodyRadius = fminf(fmaxf(
            entity->phenotype.bodyRadius * 0.42f, 0.18f), 0.90f);
    }
    if (entity->aquatic) {
        result.airborne = true;
        result.canTraverseLiquid = true;
        result.hoverClearance = 0.0f;
        result.windCoupling = 0.0f;
    }
    return result;
}

static bool EntityStandHeightAt(const Entity *entity,
                                const FaunaMotionProfile *profile,
                                float x, float z, float *outStandY,
                                bool *outLiquid, bool *outLava)
{
    int blockX = (int)floorf(x);
    int blockZ = (int)floorf(z);
    int currentFloor = (int)floorf(entity->position.y - 0.1f);
    int maximumFloor = currentFloor + (int)ceilf(profile->stepHeight);
    int minimumFloor = currentFloor - (int)ceilf(profile->maxDrop);
    for (int floorY = maximumFloor; floorY >= minimumFloor; floorY--) {
        if (!BlockBlocksEntity(blockX, floorY, blockZ)) continue;
        BlockType foot = GetBlockAt(blockX, floorY + 1, blockZ);
        BlockType head = GetBlockAt(blockX, floorY + 2, blockZ);
        if (BlockBlocksEntity(blockX, floorY + 1, blockZ) ||
            BlockBlocksEntity(blockX, floorY + 2, blockZ)) {
            continue;
        }
        *outStandY = (float)floorY + 1.0f;
        *outLiquid = foot == BLOCK_WATER || head == BLOCK_WATER;
        *outLava = foot == BLOCK_LAVA || head == BLOCK_LAVA;
        return true;
    }
    return false;
}

static FaunaTerrainCandidate EntityGroundCandidateAt(
    const Entity *entity, const FaunaMotionProfile *profile,
    float yaw, float distance, float *outStandY)
{
    FaunaTerrainCandidate candidate = { .yaw = yaw };
    float forwardX = sinf(yaw);
    float forwardZ = cosf(yaw);
    float sideX = forwardZ;
    float sideZ = -forwardX;
    float centerX = entity->position.x + forwardX * distance;
    float centerZ = entity->position.z + forwardZ * distance;
    float centerStandY = entity->position.y;

    for (int sample = -1; sample <= 1; sample++) {
        float lateral = (float)sample * profile->bodyRadius;
        float standY = entity->position.y;
        bool liquid = false;
        bool lava = false;
        if (!EntityStandHeightAt(entity, profile,
                                 centerX + sideX * lateral,
                                 centerZ + sideZ * lateral,
                                 &standY, &liquid, &lava)) {
            candidate.unsupported = true;
            continue;
        }
        if (sample == 0) centerStandY = standY;
        candidate.liquid = candidate.liquid || liquid;
        candidate.lava = candidate.lava || lava;
    }
    candidate.heightDelta = centerStandY - entity->position.y;
    if (outStandY) *outStandY = centerStandY;
    return candidate;
}

static FaunaTerrainCandidate EntityAirCandidateAt(
    const Entity *entity, const FaunaMotionProfile *profile,
    float yaw, float distance)
{
    FaunaTerrainCandidate candidate = {
        .yaw = yaw,
        .unsupported = true
    };
    float forwardX = sinf(yaw);
    float forwardZ = cosf(yaw);
    float sideX = forwardZ;
    float sideZ = -forwardX;
    float centerX = entity->position.x + forwardX * distance;
    float centerZ = entity->position.z + forwardZ * distance;
    int y = (int)floorf(entity->position.y);
    for (int sample = -1; sample <= 1; sample++) {
        float lateral = (float)sample * profile->bodyRadius;
        int x = (int)floorf(centerX + sideX * lateral);
        int z = (int)floorf(centerZ + sideZ * lateral);
        BlockType body = GetBlockAt(x, y, z);
        BlockType head = GetBlockAt(x, y + 1, z);
        candidate.blocked = candidate.blocked ||
            BlockBlocksEntity(x, y, z) || BlockBlocksEntity(x, y + 1, z);
        candidate.liquid = candidate.liquid ||
            body == BLOCK_WATER || head == BLOCK_WATER;
        candidate.lava = candidate.lava ||
            body == BLOCK_LAVA || head == BLOCK_LAVA;
    }
    if (entity->aquatic && !candidate.liquid) candidate.blocked = true;
    return candidate;
}

static void EntityMotionCandidates(
    const Entity *entity, const FaunaMotionProfile *profile,
    float targetYaw, float lookahead,
    FaunaTerrainCandidate candidates[FAUNA_MOTION_CANDIDATE_COUNT])
{
    static const float offsets[FAUNA_MOTION_CANDIDATE_COUNT] = {
        0.0f, -0.55f, 0.55f, -1.10f, 1.10f
    };
    for (int index = 0; index < FAUNA_MOTION_CANDIDATE_COUNT; index++) {
        float yaw = targetYaw + offsets[index];
        candidates[index] = profile->airborne
            ? EntityAirCandidateAt(entity, profile, yaw, lookahead)
            : EntityGroundCandidateAt(entity, profile, yaw, lookahead, NULL);
    }
}

static bool MoveEntityGrounded(Entity *entity,
                               const FaunaMotionProfile *profile,
                               Vector3 velocity, float dt)
{
    float deltaX = velocity.x * dt;
    float deltaZ = velocity.z * dt;
    float distance = sqrtf(deltaX * deltaX + deltaZ * deltaZ);
    if (distance <= 0.000001f) return false;
    float yaw = atan2f(deltaX, deltaZ);
    float standY = entity->position.y;
    FaunaTerrainCandidate candidate = EntityGroundCandidateAt(
        entity, profile, yaw, distance, &standY);
    if (!FaunaMotionCandidateUsable(profile, &candidate)) return false;
    entity->position.x += deltaX;
    entity->position.z += deltaZ;
    if (standY > entity->position.y) {
        entity->position.y = standY;
        entity->velocity.y = 0.0f;
    }
    return true;
}

static bool MoveEntityAirborne(Entity *entity,
                               const FaunaMotionProfile *profile,
                               Vector3 velocity, float dt)
{
    float deltaX = velocity.x * dt;
    float deltaZ = velocity.z * dt;
    float distance = sqrtf(deltaX * deltaX + deltaZ * deltaZ);
    if (distance <= 0.000001f) return false;
    float yaw = atan2f(deltaX, deltaZ);
    FaunaTerrainCandidate candidate = EntityAirCandidateAt(
        entity, profile, yaw, distance);
    if (!FaunaMotionCandidateUsable(profile, &candidate)) return false;
    entity->position.x += deltaX;
    entity->position.z += deltaZ;
    return true;
}

typedef struct EntityBehaviorDirections {
    FaunaBehaviorDirection food;
    FaunaBehaviorDirection water;
    FaunaBehaviorDirection shelter;
    FaunaBehaviorDirection habitat;
} EntityBehaviorDirections;

static FaunaBehaviorDirection EntityChooseResourceDirection(
    float current, const float neighbors[4])
{
    static const float yaws[4] = {
        3.14159265358979323846f,
        0.5f * 3.14159265358979323846f,
        0.0f,
        -0.5f * 3.14159265358979323846f
    };
    FaunaBehaviorDirection result = { 0 };
    current = EntityUnit(current);
    float selected = current;
    int selectedIndex = -1;
    for (int index = 0; index < 4; index++) {
        float candidate = EntityUnit(neighbors[index]);
        if (candidate > selected) {
            selected = candidate;
            selectedIndex = index;
        }
    }
    result.improvement = selected - current;
    result.shouldSeek = selectedIndex >= 0 && result.improvement >= 0.06f;
    if (result.shouldSeek) result.yaw = yaws[selectedIndex];
    return result;
}

static EntityBehaviorDirections AlienBehaviorDirectionsAt(
    const Entity *entity, float daylight)
{
    EntityBehaviorDirections result = { 0 };
    int x = (int)floorf(entity->position.x);
    int z = (int)floorf(entity->position.z);
    const int offsets[4][2] = {
        { 0, -10 }, { 10, 0 }, { 0, 10 }, { -10, 0 }
    };
    float foods[4];
    float waters[4];
    float shelters[4];
    float habitats[4];
    for (int index = 0; index < 4; index++) {
        PlanetLocalEcology local = PlanetEcologyLocalAt(
            x + offsets[index][0], z + offsets[index][1], daylight);
        foods[index] = EntityFoodAvailability(entity->niche, &local);
        waters[index] = EntityWaterAvailability(&local);
        shelters[index] = EntityShelterAvailability(&local);
        habitats[index] = local.suitability.faunaActivity;
    }
    result.food = EntityChooseResourceDirection(
        entity->ecologyFoodAvailability, foods);
    result.water = EntityChooseResourceDirection(
        entity->ecologyWaterAvailability, waters);
    result.shelter = EntityChooseResourceDirection(
        entity->ecologyShelterAvailability, shelters);
    result.habitat = EntityChooseResourceDirection(
        entity->ecologyActivity, habitats);
    return result;
}

static float EntityBehaviorMovementFloor(FaunaBehaviorAction action)
{
    switch (action) {
    case FAUNA_ACTION_FLEE: return 0.28f;
    case FAUNA_ACTION_SEEK_WATER: return 0.24f;
    case FAUNA_ACTION_SEEK_FOOD:
    case FAUNA_ACTION_SEEK_HABITAT: return 0.22f;
    case FAUNA_ACTION_SEEK_SHELTER: return 0.20f;
    case FAUNA_ACTION_HUNT: return 0.30f;
    case FAUNA_ACTION_SCAVENGE: return 0.20f;
    case FAUNA_ACTION_MATE: return 0.16f;
    default: return 0.0f;
    }
}

static float EntityDistanceSquared(const Entity *first, const Entity *second)
{
    float dx = first->position.x - second->position.x;
    float dy = first->position.y - second->position.y;
    float dz = first->position.z - second->position.z;
    return dx * dx + dy * dy + dz * dz;
}

static bool EntityAdult(const Entity *entity)
{
    return entity && entity->evolvable && !entity->corpse &&
           entity->ageDays >= entity->maturityAgeDays;
}

static bool EntityCompatibleMate(const Entity *first, const Entity *second)
{
    if (!EntityAdult(first) || !EntityAdult(second) ||
        first->sex == second->sex || first->pregnant || second->pregnant ||
        first->reproductionCooldownDays > 0.0f ||
        second->reproductionCooldownDays > 0.0f ||
        first->phenotype.locomotion != second->phenotype.locomotion ||
        first->needs.energy < 0.62f || second->needs.energy < 0.62f) {
        return false;
    }
    return EvolutionGenomeDistance(&first->genome, &second->genome) <= 0.35f;
}

static int EntityFindEvolutionTarget(int selfIndex, FaunaBehaviorAction action)
{
    if (selfIndex < 0 || selfIndex >= MAX_ENTITIES) return -1;
    const Entity *self = &entities[selfIndex];
    int selected = -1;
    float selectedDistance = 18.0f * 18.0f;
    for (int index = 0; index < MAX_ENTITIES; index++) {
        const Entity *candidate = &entities[index];
        if (index == selfIndex || !candidate->active || !candidate->evolvable) {
            continue;
        }
        bool eligible = false;
        if (action == FAUNA_ACTION_MATE) {
            eligible = EntityCompatibleMate(self, candidate);
        } else if (action == FAUNA_ACTION_SCAVENGE) {
            eligible = candidate->corpse && candidate->corpseEnergy > 0.02f;
        } else if (action == FAUNA_ACTION_HUNT) {
            eligible = !candidate->corpse &&
                self->phenotype.diet > candidate->phenotype.diet + 0.18f &&
                candidate->phenotype.totalMass <=
                    self->phenotype.totalMass * 1.65f;
        }
        if (!eligible) continue;
        float distance = EntityDistanceSquared(self, candidate);
        if (distance < selectedDistance) {
            selected = index;
            selectedDistance = distance;
        }
    }
    return selected;
}

static void EntityBecomeCorpse(Entity *entity)
{
    if (!entity || entity->corpse) return;
    entity->corpse = true;
    entity->pregnant = false;
    entity->pendingFatherId = 0u;
    memset(&entity->pendingOffspring, 0, sizeof(entity->pendingOffspring));
    entity->health = 0.0f;
    entity->corpseEnergy = fminf(fmaxf(
        entity->phenotype.totalMass * 0.16f, 0.12f), 1.0f);
    entity->velocity = Vector3Zero();
    entity->moveTimer = 0.0f;
    entity->behavior = FAUNA_ACTION_IDLE;
    entity->targetEntity = -1;
}

static void EntityConceive(Entity *first, Entity *second)
{
    Entity *mother = first->sex == CREATURE_SEX_FEMALE ? first : second;
    Entity *father = first->sex == CREATURE_SEX_MALE ? first : second;
    uint32_t birthSeed = EntityMix(EntityRandomNext() ^ mother->organismId ^
        father->organismId ^ mother->genome.genomeId);
    CreatureGenome child = EvolutionGenomeBreed(
        &mother->genome, &father->genome, birthSeed, 0.025f);
    CreaturePhenotype phenotype = EvolutionDevelop(&child);
    if (!phenotype.valid) {
        mother->reproductionCooldownDays = 1.0f;
        father->reproductionCooldownDays = 1.0f;
        return;
    }
    mother->pendingOffspring = child;
    mother->pregnant = true;
    mother->gestationProgressDays = 0.0f;
    mother->pendingFatherId = father->organismId;
    mother->reproductionCooldownDays = mother->gestationDurationDays + 4.0f;
    father->reproductionCooldownDays = 4.0f;
    mother->needs.energy = fmaxf(0.0f, mother->needs.energy - 0.12f);
    father->needs.energy = fmaxf(0.0f, father->needs.energy - 0.05f);
    first->targetEntity = -1;
    second->targetEntity = -1;
}

static bool EntityBirthOffspring(Entity *mother, float daylight)
{
    int slot = NextFreeEntity();
    if (slot < 0 || !mother || !mother->pregnant) return false;
    CreaturePhenotype phenotype = EvolutionDevelop(&mother->pendingOffspring);
    if (!phenotype.valid) {
        mother->pregnant = false;
        mother->pendingFatherId = 0u;
        memset(&mother->pendingOffspring, 0, sizeof(mother->pendingOffspring));
        return false;
    }
    Entity *child = &entities[slot];
    memset(child, 0, sizeof(*child));
    child->active = true;
    child->type = EntityEvolutionTypeForArchetype(
        phenotype.locomotion == CREATURE_LOCOMOTION_FLIGHT ?
            EVOLUTION_ARCHETYPE_FLIGHT :
            phenotype.locomotion == CREATURE_LOCOMOTION_AQUATIC ?
            EVOLUTION_ARCHETYPE_AQUATIC : EVOLUTION_ARCHETYPE_GROUND,
        EntityIsAlien(mother->type));
    child->position = mother->position;
    child->position.x += (float)EntityRandomBounded(21u) / 20.0f - 0.5f;
    child->position.z += (float)EntityRandomBounded(21u) / 20.0f - 0.5f;
    child->yaw = (float)EntityRandomBounded(628u) / 100.0f;
    child->motionTargetYaw = child->yaw;
    child->thinkTimer = 0.5f;
    child->hoverHeight = child->position.y;
    child->phase = (float)EntityRandomBounded(628u) / 100.0f;
    child->chemistry = mother->chemistry;
    child->niche = mother->niche;
    child->ecologyActivity = mother->ecologyActivity;
    child->ecologyCapacity = mother->ecologyCapacity;
    child->ecologySampleTimer = 0.25f + (float)slot / (float)MAX_ENTITIES;
    child->ecologyWindStrength = mother->ecologyWindStrength;
    child->ecologyWindAngle = mother->ecologyWindAngle;
    child->primaryBlock = mother->primaryBlock;
    child->accentBlock = mother->accentBlock;
    EntityInitializeBehaviorState(child);
    child->evolvable = true;
    child->genome = mother->pendingOffspring;
    child->organismId = EntityMix(child->genome.genomeId ^ EntityRandomNext());
    if (child->organismId == 0u) child->organismId = 1u;
    child->motherId = mother->organismId;
    child->fatherId = mother->pendingFatherId;
    child->lineageId = EvolutionGenomeDistance(
        &mother->genome, &child->genome) < 0.35f ?
        mother->lineageId : child->genome.genomeId & 0x00ffffffu;
    if (child->lineageId == 0u) child->lineageId = 1u;
    child->speciesId = EvolutionShouldSpeciate(
        EvolutionGenomeDistance(&mother->genome, &child->genome),
        0.0f, 3u) ? EntityMix(child->lineageId ^ child->genome.genomeId) :
        mother->speciesId;
    if (child->speciesId == 0u) child->speciesId = child->lineageId;
    child->sex = (EntityRandomNext() & 1u) ?
                 CREATURE_SEX_MALE : CREATURE_SEX_FEMALE;
    child->ageDays = 0.0f;
    child->lifespanDays = 96.0f +
        (float)(EntityRandomNext() % 9600u) / 100.0f;
    child->gestationDurationDays = phenotype.locomotion ==
        CREATURE_LOCOMOTION_AQUATIC ? 3.0f : phenotype.locomotion ==
        CREATURE_LOCOMOTION_FLIGHT ? 5.0f : 7.0f;
    child->health = 1.0f;
    child->targetEntity = -1;
    EntityApplyEvolutionPhenotype(child);
    PlanetEcologyRecordEvolutionEvent(
        (int)floorf(child->position.x), (int)floorf(child->position.z),
        daylight, child->lineageId, PLANET_EVOLUTION_EVENT_BIRTH,
        child->phenotype.totalMass);
    mother->pregnant = false;
    mother->gestationProgressDays = 0.0f;
    mother->pendingFatherId = 0u;
    memset(&mother->pendingOffspring, 0, sizeof(mother->pendingOffspring));
    return true;
}

static void EntityUpdateEvolutionLifecycle(Entity *entity, int entityIndex,
                                           float dt, float daylight)
{
    if (!entity->evolvable) return;
    float elapsedDays = fmaxf(dt, 0.0f) * ENTITY_EVOLUTION_DAYS_PER_SECOND;
    if (entity->corpse) {
        entity->corpseEnergy = fmaxf(
            0.0f, entity->corpseEnergy - elapsedDays * 0.055f);
        if (entity->corpseEnergy <= 0.0f) entity->active = false;
        return;
    }
    entity->ageDays += elapsedDays;
    entity->reproductionCooldownDays = fmaxf(
        0.0f, entity->reproductionCooldownDays - elapsedDays);
    if (entity->pregnant) {
        entity->gestationProgressDays += elapsedDays;
        entity->behavior = FAUNA_ACTION_NEST;
        if (entity->gestationProgressDays >= entity->gestationDurationDays) {
            EntityBirthOffspring(entity, daylight);
        }
    }
    if (entity->ageDays >= entity->lifespanDays ||
        entity->needs.energy <= 0.001f || entity->needs.hydration <= 0.001f ||
        entity->health <= 0.0f) {
        PlanetEcologyRecordEvolutionEvent(
            (int)floorf(entity->position.x),
            (int)floorf(entity->position.z), daylight,
            entity->lineageId, PLANET_EVOLUTION_EVENT_ENVIRONMENT_DEATH,
            entity->phenotype.totalMass);
        EntityBecomeCorpse(entity);
        return;
    }
    if (entity->targetEntity >= 0 &&
        (entity->targetEntity >= MAX_ENTITIES ||
         !entities[entity->targetEntity].active ||
         entity->targetEntity == entityIndex)) {
        entity->targetEntity = -1;
    }
}

static void UpdatePassive(Entity *entity, int entityIndex,
                          const Player *player, float dt,
                          float daylight)
{
    EntityUpdateEvolutionLifecycle(entity, entityIndex, dt, daylight);
    if (!entity->active || entity->corpse) return;
    bool ecological = EntityUsesEcology(entity);
    PlanetFaunaRuntimeState runtime = PlanetEcologyFaunaRuntime(1.0f, 1.0f);
    if (ecological) {
        entity->ecologySampleTimer -= dt;
        if (entity->ecologySampleTimer <= 0.0f) {
            PlanetLocalEcology local = PlanetEcologyLocalAt(
                (int)floorf(entity->position.x),
                (int)floorf(entity->position.z), daylight);
            entity->ecologyActivity = local.suitability.faunaActivity;
            entity->ecologyCapacity = local.suitability.faunaCapacity;
            WeatherFieldSample weather = WeatherFieldSampleAtWorld(
                (int)floorf(entity->position.x),
                (int)floorf(entity->position.z));
            entity->ecologyWindStrength = weather.wind;
            entity->ecologyWindAngle = WeatherWindAngleAtWorld(
                (int)floorf(entity->position.x),
                (int)floorf(entity->position.z));
            EntityApplyLocalBehaviorEnvironment(entity, &local, weather);
            entity->ecologySampleTimer = 1.0f;
        }
        runtime = PlanetEcologyFaunaRuntime(entity->ecologyActivity,
                                             entity->ecologyCapacity);
    }

    float baseSpeed = (entity->type == ENTITY_CHICKEN) ? 0.7f : 1.0f;
    if (ecological) {
        baseSpeed = entity->movementSpeed;
        if (entity->airborne) baseSpeed *= 1.25f;
        else if (entity->aquatic) baseSpeed *= 1.10f;
        else baseSpeed *= 0.92f;
    }
    Vector3 toPlayer = Vector3Subtract(player->position, entity->position);
    float playerDist = Vector3Length(toPlayer);
    bool threatened = playerDist < 5.0f;
    float movementScale = ecological ? runtime.movementScale : 1.0f;
    if (ecological && threatened) movementScale = fmaxf(movementScale, 0.28f);
    float windDrift = ecological
        ? PlanetEcologyWindDrift(entity->ecologyWindStrength,
                                 entity->airborne)
        : 0.0f;
    FaunaMotionProfile motionProfile = EntityMotionProfile(entity, baseSpeed);
    float horizontalSpeed = sqrtf(
        entity->velocity.x * entity->velocity.x +
        entity->velocity.z * entity->velocity.z);
    bool actionActive = entity->moveTimer > 0.0f;
    bool filterFeeding = entity->niche == PLANET_NICHE_FILTER_FEEDER &&
                         entity->behavior == FAUNA_ACTION_SEEK_FOOD;
    FaunaNeedInput needInput = {
        .activityRatio = runtime.activityRatio,
        .movementRatio = motionProfile.sprintSpeed > 0.0001f
            ? horizontalSpeed / motionProfile.sprintSpeed : 0.0f,
        .foodAvailability = entity->ecologyFoodAvailability,
        .waterAvailability = entity->ecologyWaterAvailability,
        .shelterAvailability = entity->ecologyShelterAvailability,
        .stormPressure = entity->ecologyStormPressure,
        .temperatureStress = entity->ecologyTemperatureStress,
        .moving = actionActive && FaunaBehaviorActionMoves(entity->behavior),
        .threatened = threatened,
        .feeding = actionActive &&
            (entity->behavior == FAUNA_ACTION_FORAGE || filterFeeding),
        .drinking = actionActive && entity->behavior == FAUNA_ACTION_DRINK,
        .resting = actionActive && entity->behavior == FAUNA_ACTION_REST
    };
    entity->needs = FaunaNeedsAdvance(&entity->needs, &needInput, dt);

    entity->thinkTimer -= dt;
    if (threatened && entity->behavior != FAUNA_ACTION_FLEE) {
        entity->thinkTimer = 0.0f;
    }
    if (entity->thinkTimer <= 0.0f) {
        float baseThinkInterval = 2.0f +
            (float)EntityRandomBounded(300u) / 100.0f;
        EntityBehaviorDirections directions = { 0 };
        if (ecological && !threatened && !entity->colony) {
            directions = AlienBehaviorDirectionsAt(entity, daylight);
        }
        FaunaBehaviorInput behaviorInput = {
            .needs = entity->needs,
            .environment = needInput,
            .food = directions.food,
            .water = directions.water,
            .shelter = directions.shelter,
            .habitat = directions.habitat,
            .foodDependence = EntityFoodDependence(entity->niche),
            .waterDependence = EntityWaterDependence(entity->chemistry),
            .fleeYaw = atan2f(-toPlayer.x, -toPlayer.z),
            .baseThinkInterval = baseThinkInterval,
            .wanderRoll = (unsigned)EntityRandomBounded(100u),
            .wanderYaw = (float)EntityRandomBounded(628u) / 100.0f,
            .baseWanderDuration = 1.0f +
                (float)EntityRandomBounded(200u) / 100.0f,
            .currentAction = entity->behavior,
            .colony = entity->colony,
            .dormant = ecological && runtime.dormant
        };
        FaunaBehaviorDecision decision = FaunaBehaviorEvaluate(&behaviorInput);
        int evolutionTarget = -1;
        FaunaBehaviorAction evolutionAction = FAUNA_ACTION_IDLE;
        if (entity->evolvable && !entity->pregnant && !threatened) {
            if (entity->phenotype.diet >= 0.36f && entity->needs.energy < 0.78f) {
                evolutionTarget = EntityFindEvolutionTarget(
                    entityIndex, FAUNA_ACTION_SCAVENGE);
                evolutionAction = FAUNA_ACTION_SCAVENGE;
            }
            if (evolutionTarget < 0 && entity->phenotype.diet >= 0.58f &&
                entity->needs.energy < 0.70f) {
                evolutionTarget = EntityFindEvolutionTarget(
                    entityIndex, FAUNA_ACTION_HUNT);
                evolutionAction = FAUNA_ACTION_HUNT;
            }
            if (evolutionTarget < 0 && EntityAdult(entity) &&
                entity->reproductionCooldownDays <= 0.0f) {
                evolutionTarget = EntityFindEvolutionTarget(
                    entityIndex, FAUNA_ACTION_MATE);
                evolutionAction = FAUNA_ACTION_MATE;
            }
        }
        if (evolutionTarget >= 0) {
            Entity *target = &entities[evolutionTarget];
            Vector3 toTarget = Vector3Subtract(target->position,
                                               entity->position);
            decision.action = evolutionAction;
            decision.yaw = atan2f(toTarget.x, toTarget.z);
            decision.moveDuration = 1.25f;
            decision.thinkInterval = 0.35f;
            entity->targetEntity = evolutionTarget;
        } else if (entity->targetEntity >= 0) {
            entity->targetEntity = -1;
        }
        if (entity->pregnant) {
            decision.action = FAUNA_ACTION_NEST;
            decision.moveDuration = 0.6f;
        }
        entity->behavior = decision.action;
        entity->thinkTimer = decision.thinkInterval;
        entity->moveTimer = decision.moveDuration;
        if (FaunaBehaviorActionMoves(decision.action)) {
            entity->motionTargetYaw = decision.yaw;
        }
    }

    if (entity->evolvable && entity->targetEntity >= 0) {
        Entity *target = &entities[entity->targetEntity];
        Vector3 toTarget = Vector3Subtract(target->position, entity->position);
        float targetDistance = Vector3Length(toTarget);
        entity->motionTargetYaw = atan2f(toTarget.x, toTarget.z);
        if (targetDistance <= entity->phenotype.bodyRadius +
                              target->phenotype.bodyRadius + 0.55f) {
            if (entity->behavior == FAUNA_ACTION_SCAVENGE && target->corpse) {
                float consumed = fminf(target->corpseEnergy, dt * 0.10f);
                target->corpseEnergy -= consumed;
                entity->needs.energy = fminf(1.0f,
                    entity->needs.energy + consumed * 0.85f);
                if (target->corpseEnergy <= 0.0f) {
                    target->active = false;
                    entity->targetEntity = -1;
                }
            } else if (entity->behavior == FAUNA_ACTION_HUNT &&
                       !target->corpse) {
                float damage = dt * (0.08f + entity->phenotype.attack * 0.035f) /
                    fmaxf(0.5f, target->phenotype.defense * 0.22f);
                target->health -= damage;
                if (target->health <= 0.0f) {
                    PlanetEcologyRecordEvolutionEvent(
                        (int)floorf(target->position.x),
                        (int)floorf(target->position.z), daylight,
                        target->lineageId,
                        PLANET_EVOLUTION_EVENT_PREDATION_DEATH,
                        target->phenotype.totalMass);
                    EntityBecomeCorpse(target);
                    entity->needs.energy = fminf(1.0f,
                        entity->needs.energy + 0.12f);
                    entity->targetEntity = -1;
                }
            } else if (entity->behavior == FAUNA_ACTION_MATE &&
                       EntityCompatibleMate(entity, target)) {
                EntityConceive(entity, target);
            }
        }
    }

    actionActive = entity->moveTimer > 0.0f;
    float animationScale = ecological ? runtime.animationScale : 1.0f;
    if (actionActive && entity->behavior == FAUNA_ACTION_REST) {
        animationScale *= 0.35f;
    }
    float controllerOutputs[EVOLUTION_CONTROLLER_OUTPUTS] = { 0 };
    if (entity->evolvable) {
        float controllerInputs[EVOLUTION_CONTROLLER_INPUTS] = {
            1.0f - entity->needs.energy,
            1.0f - entity->needs.hydration,
            entity->needs.fatigue,
            entity->needs.stress,
            entity->ecologyFoodAvailability * 2.0f - 1.0f,
            entity->ecologyWaterAvailability * 2.0f - 1.0f,
            entity->targetEntity >= 0 ? 1.0f : -1.0f,
            entity->aquatic ? 1.0f : entity->airborne ? 0.5f : -0.5f
        };
        EvolutionControllerEvaluate(&entity->genome, controllerInputs,
                                    controllerOutputs);
        entity->motionTargetYaw += controllerOutputs[0] * 0.22f;
        movementScale *= fminf(fmaxf(
            1.0f + controllerOutputs[1] * 0.16f, 0.82f), 1.18f);
    }
    float neuralPhaseScale = 1.0f + controllerOutputs[2] * 0.15f;
    entity->phase += dt * (0.7f + baseSpeed * 0.35f) * animationScale *
                     neuralPhaseScale;
    if (entity->airborne && !entity->aquatic) {
        int groundY = EntitySurfaceHeight(
            (int)floorf(entity->position.x),
            (int)floorf(entity->position.z));
        float terrainHover = fminf(
            (float)WORLD_HEIGHT - 3.0f,
            (float)groundY + 1.0f + motionProfile.hoverClearance);
        entity->hoverHeight += (terrainHover - entity->hoverHeight) *
            fminf(1.0f, dt * 0.75f);
        float targetY = entity->hoverHeight + sinf(entity->phase) *
                        (0.45f + entity->organismScale * 0.22f) *
                        (0.20f + animationScale * 0.80f);
        entity->position.y += (targetY - entity->position.y) * fminf(1.0f, dt * 2.2f);
    }

    bool moving = entity->moveTimer > 0.0f && !entity->colony &&
                  FaunaBehaviorActionMoves(entity->behavior);
    if (entity->moveTimer > 0.0f) entity->moveTimer -= dt;

    FaunaMotionInput motionInput = {
        .profile = motionProfile,
        .currentYaw = entity->yaw,
        .targetYaw = entity->motionTargetYaw,
        .currentSpeed = horizontalSpeed,
        .movementScale = fmaxf(
            movementScale, EntityBehaviorMovementFloor(entity->behavior)),
        .deltaTime = dt,
        .moving = moving,
        .sprinting = entity->behavior == FAUNA_ACTION_FLEE
    };
    float lookahead = motionProfile.bodyRadius + 0.38f +
        fminf(motionInput.currentSpeed * 0.25f, 0.45f);
    if (moving) {
        EntityMotionCandidates(entity, &motionProfile,
                               entity->motionTargetYaw, lookahead,
                               motionInput.candidates);
    }
    FaunaMotionStep motion = FaunaMotionAdvance(&motionInput);
    entity->yaw = motion.yaw;
    entity->velocity.x = sinf(entity->yaw) * motion.speed;
    entity->velocity.z = cosf(entity->yaw) * motion.speed;

    Vector3 move = {
        entity->velocity.x,
        0.0f,
        entity->velocity.z
    };
    Vector3 current = EntityFluidCurrent(entity);
    move.x += current.x;
    move.z += current.z;
    if (entity->aquatic && fabsf(current.y) > 0.0001f) {
        entity->position.y += current.y * dt;
    }
    if (ecological && windDrift > 0.0f) {
        float coupledDrift = windDrift * motionProfile.windCoupling;
        move.x += cosf(entity->ecologyWindAngle) * coupledDrift;
        move.z += sinf(entity->ecologyWindAngle) * coupledDrift;
    }
    if (motion.speed > 0.0f || fabsf(current.x) > 0.0001f ||
        fabsf(current.z) > 0.0001f ||
        (ecological && entity->airborne && windDrift > 0.0f)) {
        if (entity->airborne || entity->aquatic) {
            MoveEntityAirborne(entity, &motionProfile, move, dt);
        } else {
            MoveEntityGrounded(entity, &motionProfile, move, dt);
        }
    }
}

static void UpdateHostile(Entity *entity, const Player *player, float dt, float daylight)
{
    Vector3 toPlayer = Vector3Subtract(player->position, entity->position);
    toPlayer.y = 0.0f;
    float playerDist = Vector3Length(toPlayer);
    float speed = (entity->type == ENTITY_ZOMBIE) ? 1.4f : 1.2f;

    Vector3 move = EntityFluidCurrent(entity);
    move.y = 0.0f;
    if (playerDist < 34.0f) {
        entity->yaw = atan2f(toPlayer.x, toPlayer.z);
        move.x += sinf(entity->yaw) * speed;
        move.z += cosf(entity->yaw) * speed;
        if (entity->type == ENTITY_ZOMBIE) {
            int x = (int)floorf(entity->position.x);
            int z = (int)floorf(entity->position.z);
            int groundY = EntitySurfaceHeight(x, z);
            if (entity->position.y < (float)groundY + 2.2f &&
                !BlockBlocksEntity(x, (int)floorf(entity->position.y + 1.7f), z)) {
                entity->position.y += 7.5f * dt;
            }
        }
    } else {
        entity->moveTimer = 0.0f;
    }
    if (Vector3LengthSqr(move) > 0.000001f) {
        MoveEntityHorizontal(entity, move, dt);
    }

    if (entity->type == ENTITY_ZOMBIE && daylight > 0.5f &&
        !IsLiquidBlock(GetBlockAt((int)floorf(entity->position.x),
                                  (int)floorf(entity->position.y + 0.5f),
                                  (int)floorf(entity->position.z)))) {
        entity->burnTimer -= dt;
        if ((int)(entity->burnTimer * 5.0f) != (int)((entity->burnTimer + dt) * 5.0f)) {
            ParticlesEmitBurst(entity->position, (Color){ 255, 140, 40, 255 }, 4, 1.5f, 0.5f);
        }
        if (entity->burnTimer <= 0.0f) {
            ParticlesEmitBurst(entity->position, (Color){ 255, 170, 60, 255 }, 14, 2.5f, 0.6f);
            entity->active = false;
            return;
        }
    }
}

void EntitiesUpdate(float dt, const Player *player, float daylight)
{
    entitySpawnTimer -= dt;
    if (entitySpawnTimer <= 0.0f) {
        entitySpawnTimer = 1.5f;
        int populationCap = MAX_ENTITIES - 4;
        bool ecologyWorld = PlanetWorldIsActive() ||
            WorldBlockRegionAt((int)floorf(player->position.y)) ==
            WORLD_BLOCK_REGION_SURFACE;
        if (ecologyWorld) {
            int playerX = (int)floorf(player->position.x);
            int playerZ = (int)floorf(player->position.z);
            float localFauna = PlanetEcologyFaunaDensityAt(
                playerX, playerZ, daylight);
            populationCap = PlanetFaunaPopulationCap(
                localFauna, MAX_ENTITIES - 4);
        }
        int activeCount = GetActiveEntityCount();
        if (activeCount < populationCap) {
            if (PlanetWorldIsActive() || daylight > 0.5f) SpawnPassive(player, daylight);
            else SpawnHostile(player, daylight);
        } else if (PlanetWorldIsActive() && activeCount > populationCap) {
            DespawnDistantAlien(player);
        }
    }

    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *entity = &entities[i];
        if (!entity->active) continue;

        float dx = entity->position.x - player->position.x;
        float dz = entity->position.z - player->position.z;
        if (dx * dx + dz * dz > 96.0f * 96.0f) {
            entity->active = false;
            continue;
        }

        if (!entity->airborne && !entity->aquatic) {
            float gravityScale = WorldGravityScale();
            entity->velocity.y -= 24.0f * gravityScale * dt;
            entity->position.y += entity->velocity.y * dt;

            if (GroundBelow(entity->position)) {
                entity->position.y = floorf(entity->position.y) + 1.0f;
                entity->velocity.y = 0.0f;
            }
        }
        if (entity->position.y < (float)NETHER_LAYER_Y) {
            entity->active = false;
            continue;
        }

        if (entity->type >= ENTITY_ZOMBIE) {
            UpdateHostile(entity, player, dt, daylight);
        } else {
            UpdatePassive(entity, i, player, dt, daylight);
        }
    }
}

static Color EntityBodyColor(EntityType type)
{
    switch (type) {
    case ENTITY_COW: return (Color){ 138, 96, 62, 255 };
    case ENTITY_SHEEP: return (Color){ 238, 236, 228, 255 };
    case ENTITY_PIG: return (Color){ 236, 176, 168, 255 };
    case ENTITY_CHICKEN: return (Color){ 240, 236, 222, 255 };
    case ENTITY_ALIEN_GRAZER:
    case ENTITY_ALIEN_HOPPER:
    case ENTITY_ALIEN_STRIDER:
        return ColorLerp(ColorPalette256((int)(PlanetWorldSeed() % 216u) + 20), WHITE, 0.12f);
    case ENTITY_ZOMBIE: return (Color){ 110, 150, 84, 255 };
    case ENTITY_SKELETON: return (Color){ 226, 226, 224, 255 };
    default: return MAGENTA;
    }
}

static Color EntityHeadColor(EntityType type)
{
    switch (type) {
    case ENTITY_COW: return (Color){ 92, 62, 40, 255 };
    case ENTITY_SHEEP: return (Color){ 218, 210, 200, 255 };
    case ENTITY_PIG: return (Color){ 226, 154, 148, 255 };
    case ENTITY_CHICKEN: return (Color){ 238, 232, 214, 255 };
    case ENTITY_ALIEN_GRAZER:
    case ENTITY_ALIEN_HOPPER:
    case ENTITY_ALIEN_STRIDER:
        return ColorLerp(ColorPalette256((int)((PlanetWorldSeed() >> 8) % 216u) + 20), WHITE, 0.22f);
    case ENTITY_ZOMBIE: return (Color){ 96, 134, 70, 255 };
    case ENTITY_SKELETON: return (Color){ 214, 214, 212, 255 };
    default: return MAGENTA;
    }
}

static Color AlienBodyColor(const Entity *entity)
{
    Color color = BlockBaseColor(entity->primaryBlock);
    return ColorLerp(color, WHITE, 0.08f + entity->bodyArmor * 0.10f);
}

static Color AlienAccentColor(const Entity *entity)
{
    Color color = BlockBaseColor(entity->accentBlock);
    if (entity->niche == PLANET_NICHE_BIOLUMINESCENT_COLONY) {
        color = ColorLerp(color, (Color){ 120, 244, 255, 255 }, 0.60f);
    }
    return ColorLerp(color, WHITE, 0.14f);
}

static void DrawEntityBox(Vector3 center, Vector3 size, Color color)
{
    DrawCubeV(center, size, color);
}

static Vector3 AlienPartPosition(Vector3 origin, Vector3 forward, Vector3 side,
                                 float along, float across, float y)
{
    Vector3 result = Vector3Add(origin, Vector3Scale(forward, along));
    result = Vector3Add(result, Vector3Scale(side, across));
    result.y += y;
    return result;
}

static Color AlienActivityColor(Color color, float visualPresence)
{
    float stress = 1.0f - fminf(fmaxf(visualPresence, 0.0f), 1.0f);
    return ColorLerp(color, (Color){ 72, 78, 82, 255 }, stress * 0.72f);
}

static float EntityEvolutionGrowthScale(const Entity *entity)
{
    if (!entity->evolvable || entity->maturityAgeDays <= 0.0f) return 1.0f;
    float maturity = fminf(fmaxf(
        entity->ageDays / entity->maturityAgeDays, 0.0f), 1.0f);
    return 0.35f + maturity * 0.65f;
}

static void DrawEvolvedAlienEntity(const Entity *entity,
                                   PlanetFaunaRuntimeState runtime,
                                   Color body, Color accent, float scale)
{
    Vector3 positions[EVOLUTION_MAX_MODULES] = { 0 };
    Vector3 forward = { sinf(entity->yaw), 0.0f, cosf(entity->yaw) };
    Vector3 side = { forward.z, 0.0f, -forward.x };
    float growth = EntityEvolutionGrowthScale(entity);
    scale *= growth;
    if (entity->corpse) {
        body = ColorLerp(body, (Color){ 74, 68, 62, 255 }, 0.68f);
        accent = ColorLerp(accent, body, 0.72f);
    }
    if (entity->phenotype.locomotion == CREATURE_LOCOMOTION_AQUATIC &&
        CreatureRendererDrawAquatic(entity, runtime, body, accent, scale)) {
        return;
    }
    for (unsigned index = 0; index < entity->phenotype.moduleCount; index++) {
        const CreatureModule *module = &entity->phenotype.modules[index];
        Vector3 base = entity->position;
        if (module->parentIndex >= 0 &&
            module->parentIndex < (int)index) {
            base = positions[(unsigned)module->parentIndex];
        } else if (index == 0u) {
            base.y += module->height * scale * 0.5f;
        }
        Vector3 position = AlienPartPosition(
            base, forward, side, module->localX * scale,
            module->localY * scale, module->localZ * scale);
        float gaitSide = module->localY >= 0.0f ? 0.0f : 3.14159265f;
        if (!entity->corpse &&
            (module->type == CREATURE_MODULE_LIMB ||
             module->type == CREATURE_MODULE_FOOT)) {
            position.y += sinf(entity->phase * 2.2f + gaitSide +
                               (float)index * 0.45f) * 0.10f * scale *
                          runtime.animationScale;
        } else if (!entity->corpse && module->type == CREATURE_MODULE_WING) {
            position.y += sinf(entity->phase * 3.4f + gaitSide) *
                          module->width * 0.34f * scale *
                          runtime.animationScale;
        } else if (!entity->corpse && module->type == CREATURE_MODULE_FIN) {
            position = Vector3Add(position, Vector3Scale(
                side, sinf(entity->phase * 2.6f + (float)index) *
                0.09f * scale * runtime.animationScale));
        }
        if (entity->corpse) position.y = entity->position.y +
            fmaxf(0.10f, module->height * scale * 0.18f);
        positions[index] = position;
    }
    for (unsigned index = 0; index < entity->phenotype.connectionCount; index++) {
        const CreatureConnection *connection =
            &entity->phenotype.connections[index];
        if (connection->articulated ||
            connection->first >= entity->phenotype.moduleCount ||
            connection->second >= entity->phenotype.moduleCount) continue;
        Vector3 first = positions[connection->first];
        Vector3 second = positions[connection->second];
        Vector3 center = Vector3Scale(Vector3Add(first, second), 0.5f);
        Vector3 delta = Vector3Subtract(first, second);
        DrawEntityBox(center, (Vector3){
            fmaxf(fabsf(delta.x), 0.08f * scale),
            fmaxf(fabsf(delta.y), 0.08f * scale),
            fmaxf(fabsf(delta.z), 0.08f * scale)
        }, ColorLerp(body, accent, 0.35f));
    }
    for (unsigned index = 0; index < entity->phenotype.moduleCount; index++) {
        const CreatureModule *module = &entity->phenotype.modules[index];
        Color color = body;
        if (module->type == CREATURE_MODULE_HEAD ||
            module->type == CREATURE_MODULE_SENSOR ||
            module->type == CREATURE_MODULE_WING ||
            module->type == CREATURE_MODULE_FIN) {
            color = accent;
        } else if (module->type == CREATURE_MODULE_ARMOR) {
            color = ColorLerp(body, (Color){ 190, 194, 202, 255 }, 0.42f);
        }
        Vector3 size = {
            module->width * scale,
            module->height * scale * (entity->corpse ? 0.36f : 1.0f),
            module->length * scale
        };
        DrawEntityBox(positions[index], size, color);
    }
}

static void DrawAlienEntity(const Entity *entity)
{
    PlanetFaunaRuntimeState runtime = PlanetEcologyFaunaRuntime(
        entity->ecologyActivity, entity->ecologyCapacity);
    Vector3 pos = entity->position;
    Vector3 forward = { sinf(entity->yaw), 0.0f, cosf(entity->yaw) };
    Vector3 side = { forward.z, 0.0f, -forward.x };
    Color body = AlienActivityColor(AlienBodyColor(entity),
                                    runtime.visualPresence);
    Color accent = AlienActivityColor(AlienAccentColor(entity),
                                      runtime.visualPresence);
    float scale = entity->organismScale > 0.1f ? entity->organismScale : 1.0f;
    scale *= runtime.visualScale;
    float armor = 1.0f + entity->bodyArmor * 0.38f;

    if (entity->evolvable && entity->phenotype.valid) {
        Color geneticAccent = ColorPalette256(
            20 + (int)(entity->genome.pigmentation % 216u));
        accent = ColorLerp(accent, geneticAccent, 0.32f);
        DrawEvolvedAlienEntity(entity, runtime, body, accent, scale);
        return;
    }

    if (entity->bodyPlan == PLANET_BODY_FLOATING) {
        DrawEntityBox(pos, (Vector3){ 1.25f * scale, 0.66f * scale,
                                      1.55f * scale }, body);
        DrawEntityBox((Vector3){ pos.x, pos.y + 0.58f * scale, pos.z },
                      (Vector3){ 0.86f * scale, 0.34f * scale, 0.92f * scale }, accent);
        DrawEntityBox(AlienPartPosition(pos, forward, side, -0.42f * scale,
                                        -0.62f * scale, 0.0f),
                      (Vector3){ 0.58f * scale, 0.58f * scale, 0.58f * scale }, accent);
        DrawEntityBox(AlienPartPosition(pos, forward, side, -0.42f * scale,
                                        0.62f * scale, 0.0f),
                      (Vector3){ 0.58f * scale, 0.58f * scale, 0.58f * scale }, accent);
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.90f * scale,
                                        0.0f, 0.10f * scale),
                      (Vector3){ 0.12f * scale, 0.72f * scale, 0.12f * scale }, accent);
        return;
    }

    if (entity->bodyPlan == PLANET_BODY_COLONY) {
        Color glow = ColorLerp(accent, (Color){ 110, 250, 255, 255 },
                               0.55f * runtime.activityRatio);
        DrawEntityBox((Vector3){ pos.x, pos.y + 0.48f * scale, pos.z },
                      (Vector3){ 0.54f * scale, 0.72f * scale, 0.54f * scale }, body);
        for (int i = 0; i < 6; i++) {
            float angle = (float)i * 1.0471976f + entity->phase * 0.08f;
            float radius = 0.58f * scale;
            Vector3 node = { pos.x + cosf(angle) * radius,
                             pos.y + 0.28f * scale + (float)(i & 1) * 0.20f * scale,
                             pos.z + sinf(angle) * radius };
            DrawEntityBox(node, (Vector3){ 0.26f * scale, 0.34f * scale,
                                           0.26f * scale }, glow);
        }
        return;
    }

    if (entity->bodyPlan == PLANET_BODY_SERPENTINE) {
        for (int segment = 0; segment < 5; segment++) {
            float segmentScale = scale * (1.0f - segment * 0.10f);
            float along = (0.42f - segment * 0.42f) * scale;
            float across = sinf(entity->phase + segment * 0.75f) * 0.16f * scale;
            DrawEntityBox(AlienPartPosition(pos, forward, side, along, across,
                                             0.30f * segmentScale),
                          (Vector3){ 0.48f * segmentScale, 0.48f * segmentScale,
                                     0.62f * segmentScale },
                          segment == 0 ? accent : body);
        }
        return;
    }

    if (entity->bodyPlan == PLANET_BODY_BIPED) {
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.0f, 0.0f,
                                        1.02f * scale),
                      (Vector3){ 0.78f * scale * armor, 1.10f * scale,
                                 0.62f * scale }, body);
        for (int pair = -1; pair <= 1; pair += 2) {
            DrawEntityBox(AlienPartPosition(pos, forward, side, -0.08f * scale,
                                            pair * 0.22f * scale, 0.42f * scale),
                          (Vector3){ 0.20f * scale, 0.84f * scale,
                                     0.20f * scale }, body);
        }
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.48f * scale,
                                        0.0f, 1.78f * scale),
                      (Vector3){ 0.52f * scale, 0.52f * scale,
                                 0.52f * scale }, accent);
        DrawEntityBox(AlienPartPosition(pos, forward, side, -0.10f * scale,
                                        -0.58f * scale, 1.16f * scale),
                      (Vector3){ 0.14f * scale, 0.70f * scale,
                                 0.14f * scale }, accent);
        DrawEntityBox(AlienPartPosition(pos, forward, side, -0.10f * scale,
                                        0.58f * scale, 1.16f * scale),
                      (Vector3){ 0.14f * scale, 0.70f * scale,
                                 0.14f * scale }, accent);
        return;
    }

    if (entity->bodyPlan == PLANET_BODY_HEXAPOD) {
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.0f, 0.0f,
                                        0.66f * scale),
                      (Vector3){ 1.20f * scale * armor, 0.58f * scale,
                                 1.35f * scale }, body);
        for (int row = -1; row <= 1; row++) {
            for (int pair = -1; pair <= 1; pair += 2) {
                DrawEntityBox(AlienPartPosition(pos, forward, side,
                                                row * 0.42f * scale,
                                                pair * 0.48f * scale,
                                                0.33f * scale),
                              (Vector3){ 0.14f * scale * armor, 0.66f * scale,
                                         0.14f * scale * armor }, body);
            }
        }
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.78f * scale,
                                        0.0f, 0.88f * scale),
                      (Vector3){ 0.56f * scale, 0.48f * scale,
                                 0.56f * scale }, accent);
        return;
    }

    DrawEntityBox(AlienPartPosition(pos, forward, side, 0.0f, 0.0f,
                                    0.58f * scale),
                  (Vector3){ 1.10f * scale * armor, 0.68f * scale,
                             1.45f * scale }, body);
    for (int row = -1; row <= 1; row += 2) {
        for (int pair = -1; pair <= 1; pair += 2) {
            DrawEntityBox(AlienPartPosition(pos, forward, side,
                                            row * 0.42f * scale,
                                            pair * 0.40f * scale,
                                            0.28f * scale),
                          (Vector3){ 0.16f * scale * armor, 0.56f * scale,
                                     0.16f * scale * armor }, body);
        }
    }
    DrawEntityBox(AlienPartPosition(pos, forward, side, 0.90f * scale,
                                    0.0f, 0.82f * scale),
                  (Vector3){ 0.58f * scale, 0.54f * scale,
                             0.58f * scale }, accent);
}

void EntitiesDraw(void)
{
    for (int i = 0; i < MAX_ENTITIES; i++) {
        const Entity *entity = &entities[i];
        if (!entity->active) continue;

        if (EntityUsesEcology(entity)) {
            DrawAlienEntity(entity);
            continue;
        }

        Vector3 pos = entity->position;
        bool small = entity->type == ENTITY_CHICKEN;
        float bodyW = small ? 0.45f : 0.7f;
        float bodyH = small ? 0.4f : 0.6f;
        float bodyL = small ? 0.6f : 1.0f;
        float headSize = small ? 0.34f : 0.5f;

        Vector3 bodyCenter = { pos.x, pos.y + bodyH * 0.5f + 0.2f, pos.z };
        DrawEntityBox(bodyCenter, (Vector3){ bodyW, bodyH, bodyL }, EntityBodyColor(entity->type));

        Vector3 headCenter = { pos.x, pos.y + bodyH + 0.2f + headSize * 0.5f, pos.z };
        DrawEntityBox(headCenter, (Vector3){ headSize, headSize, headSize }, EntityHeadColor(entity->type));

        float legW = small ? 0.12f : 0.22f;
        float legH = small ? 0.35f : 0.55f;
        float legOff = small ? 0.12f : 0.22f;
        Vector3 legCenters[4] = {
            { pos.x - legOff, pos.y + legH * 0.5f, pos.z - legOff },
            { pos.x + legOff, pos.y + legH * 0.5f, pos.z - legOff },
            { pos.x - legOff, pos.y + legH * 0.5f, pos.z + legOff },
            { pos.x + legOff, pos.y + legH * 0.5f, pos.z + legOff }
        };
        Color legColor = EntityBodyColor(entity->type);
        for (int k = 0; k < 4; k++) {
            DrawEntityBox(legCenters[k], (Vector3){ legW, legH, legW }, legColor);
        }

        if (entity->type == ENTITY_CHICKEN) {
            DrawEntityBox((Vector3){ pos.x, pos.y + bodyH + 0.2f + headSize + 0.12f, pos.z },
                          (Vector3){ 0.1f, 0.12f, 0.1f }, (Color){ 214, 40, 36, 255 });
        }
    }
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

    ParticlesEmitBurst(entity->position, EntityBodyColor(entity->type), 18, 3.0f, 0.7f);
    AudioPlayBreak();
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
