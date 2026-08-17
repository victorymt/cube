#include "ecology/entity_internal.h"

#include "ecology/ecology.h"
#include "space/space_state.h"
#include "world/terrain.h"
#include "world/weather.h"
#include "world/world.h"
#include "world/world_environment.h"

#include "raymath.h"

#include <math.h>
#include <string.h>

#define entities entityStore

typedef struct EntityPassiveSpawn {
    int slot;
    bool alienWorld;
    bool homeWorld;
    bool evolvable;
    EntityType type;
    EvolutionArchetype archetype;
    PlanetEcologyProfile ecology;
    PlanetLocalEcology localEcology;
    CreatureGenome sampledGenome;
    uint32_t sampledLineage;
    uint32_t sampledSpecies;
    uint32_t organismSeed;
    bool haveSampledGenome;
    int x;
    int z;
    int groundY;
    int waterY;
} EntityPassiveSpawn;

static EvolutionArchetype EntityEvolutionArchetypeForType(EntityType type)
{
    if (type == ENTITY_ALIEN_HOPPER) return EVOLUTION_ARCHETYPE_FLIGHT;
    if (type == ENTITY_ALIEN_STRIDER) return EVOLUTION_ARCHETYPE_AQUATIC;
    return EVOLUTION_ARCHETYPE_GROUND;
}

static bool EntityAnyEvolvable(void)
{
    for (int index = 0; index < MAX_ENTITIES; index++) {
        if (entities[index].active && entities[index].evolvable) return true;
    }
    return false;
}

static bool PlanetBiomeSupportsFauna(int x, int z)
{
    PlanetBiome biome = PlanetBiomeAt(x, z);
    return biome != PLANET_BIOME_OCEAN && biome != PLANET_BIOME_LAVA_SEA &&
           biome != PLANET_BIOME_STORM_BANDS &&
           biome != PLANET_BIOME_VOLCANIC_RIDGE;
}

static bool EntitySelectPassiveSpawn(EntityPassiveSpawn *spawn,
                                     const Player *player)
{
    spawn->slot = EntityNextFreeSlot();
    if (spawn->slot < 0) return false;

    spawn->alienWorld = PlanetWorldIsActive();
    spawn->homeWorld = !spawn->alienWorld &&
        WorldBlockRegionAt((int)floorf(player->position.y)) ==
        WORLD_BLOCK_REGION_SURFACE;
    if (spawn->alienWorld) {
        spawn->ecology = PlanetEcologyCurrent();
        if (!spawn->ecology.supportsFlight && player->position.y > 40.0f) {
            return false;
        }
    } else if (!spawn->homeWorld && player->position.y > 40.0f) {
        return false;
    }

    spawn->type = ENTITY_COW;
    spawn->archetype = EVOLUTION_ARCHETYPE_GROUND;
    spawn->evolvable = spawn->alienWorld ||
        (spawn->homeWorld && (!EntityAnyEvolvable() ||
                              EntityRandomBounded(100u) < 45));
    if (spawn->evolvable) {
        spawn->ecology = PlanetEcologyCurrent();
        if (spawn->ecology.faunaDensity <= 0.0f) return false;
        uint32_t speciesSeed = PlanetWorldSeed();
        if (spawn->alienWorld) {
            int species = (int)((speciesSeed +
                (uint32_t)EntityRandomBounded(3u) * 17u) % 3u);
            spawn->type = (EntityType)(ENTITY_ALIEN_GRAZER + species);
            spawn->archetype = EntityEvolutionArchetypeForType(spawn->type);
            if (spawn->archetype == EVOLUTION_ARCHETYPE_FLIGHT &&
                !spawn->ecology.supportsFlight) {
                spawn->type = ENTITY_ALIEN_GRAZER;
                spawn->archetype = EVOLUTION_ARCHETYPE_GROUND;
            }
        }
    } else {
        EntityType types[4] = {
            ENTITY_COW, ENTITY_SHEEP, ENTITY_PIG, ENTITY_CHICKEN
        };
        spawn->type = types[EntityRandomBounded(4u)];
    }
    return true;
}

static bool EntitySamplePassiveSpawn(EntityPassiveSpawn *spawn,
                                     const Player *player, float daylight)
{
    float angle = (float)EntityRandomBounded(628u) / 100.0f;
    float distance = spawn->evolvable
        ? 12.0f + (float)EntityRandomBounded(160u) / 10.0f
        : 14.0f + (float)EntityRandomBounded(300u) / 10.0f;
    spawn->x = (int)floorf(
        player->position.x + cosf(angle) * distance);
    spawn->z = (int)floorf(
        player->position.z + sinf(angle) * distance);
    if (spawn->evolvable) {
        spawn->localEcology = PlanetEcologyLocalAt(
            spawn->x, spawn->z, daylight);
        float faunaActivity = spawn->localEcology.suitability.faunaActivity;
        if (!PlanetFaunaSpawnAccepted(
                faunaActivity, (uint32_t)EntityRandomBounded(1000u))) {
            return false;
        }
        spawn->organismSeed = EntityMix(
            (PlanetWorldIsActive() ? PlanetWorldSeed() : WorldGetSeed()) ^
            (uint32_t)spawn->x * 0x9e3779b9u ^
            (uint32_t)spawn->z * 0x85ebca6bu ^ EntityRandomNext());
        if (spawn->organismSeed == 0u) spawn->organismSeed = 1u;
        spawn->haveSampledGenome = PlanetEcologySampleGenome(
            spawn->x, spawn->z, daylight, spawn->organismSeed,
            &spawn->sampledGenome, &spawn->sampledLineage,
            &spawn->sampledSpecies);
        if (spawn->haveSampledGenome) {
            CreaturePhenotype sampledPhenotype = EvolutionDevelop(
                &spawn->sampledGenome);
            spawn->haveSampledGenome = sampledPhenotype.valid;
            if (spawn->haveSampledGenome) {
                spawn->archetype = sampledPhenotype.locomotion ==
                    CREATURE_LOCOMOTION_FLIGHT ? EVOLUTION_ARCHETYPE_FLIGHT :
                    sampledPhenotype.locomotion ==
                    CREATURE_LOCOMOTION_AQUATIC ?
                    EVOLUTION_ARCHETYPE_AQUATIC : EVOLUTION_ARCHETYPE_GROUND;
                spawn->type = EntityEvolutionTypeForArchetype(
                    spawn->archetype, spawn->alienWorld);
            }
        }
    }
    return true;
}

static bool EntityValidatePassiveSpawn(EntityPassiveSpawn *spawn,
                                       const Player *player)
{
    if (spawn->evolvable &&
        spawn->archetype == EVOLUTION_ARCHETYPE_FLIGHT &&
        !spawn->ecology.supportsFlight) {
        return false;
    }
    if (spawn->evolvable &&
        spawn->archetype == EVOLUTION_ARCHETYPE_GROUND &&
        !PlanetBiomeSupportsFauna(spawn->x, spawn->z) &&
        spawn->ecology.niche != PLANET_NICHE_CRYSTAL_GRAZER) {
        return false;
    }
    spawn->groundY = EntitySurfaceHeight(spawn->x, spawn->z);
    int playerY = (int)floorf(player->position.y);
    bool playerUnderwater = GetBlockAt(
        (int)floorf(player->position.x), playerY,
        (int)floorf(player->position.z)) == BLOCK_WATER;
    if (spawn->evolvable &&
        spawn->archetype == EVOLUTION_ARCHETYPE_FLIGHT && playerUnderwater) {
        return false;
    }
    if (spawn->evolvable &&
        spawn->archetype == EVOLUTION_ARCHETYPE_AQUATIC) {
        int seaLevel = spawn->homeWorld ? HOME_SEA_LEVEL : -1;
        int verticalJitter = (int)EntityRandomBounded(13u) - 6;
        int preferredY = playerY + verticalJitter;
        int fallbackY = seaLevel >= 0 ? seaLevel : spawn->groundY + 2;
        if (!EntityFindAquaticSpawnY(
                spawn->x, spawn->z, preferredY, fallbackY,
                &spawn->waterY)) {
            return false;
        }
    }
    if (!spawn->evolvable ||
        spawn->archetype == EVOLUTION_ARCHETYPE_GROUND) {
        if (fabsf((float)spawn->groundY + 1.0f - player->position.y) >
            ENTITY_GROUND_SPAWN_VERTICAL_RANGE) {
            return false;
        }
        BlockType spawnAt = GetBlockAt(
            spawn->x, spawn->groundY + 1, spawn->z);
        BlockType spawnAbove = GetBlockAt(
            spawn->x, spawn->groundY + 2, spawn->z);
        if (spawn->evolvable) {
            if (spawnAt != BLOCK_AIR || spawnAbove != BLOCK_AIR) return false;
        } else {
            if (spawnAt != BLOCK_AIR && spawnAt != BLOCK_WATER &&
                spawnAt != BLOCK_LAVA) {
                return false;
            }
            if (spawnAbove != BLOCK_AIR && spawnAbove != BLOCK_WATER &&
                spawnAbove != BLOCK_LAVA) {
                return false;
            }
        }
    }
    return true;
}

static void EntityInitializePassiveSpawn(EntityPassiveSpawn *spawn,
                                         const Player *player)
{
    Entity *entity = &entities[spawn->slot];
    memset(entity, 0, sizeof(*entity));
    entity->active = true;
    entity->type = spawn->type;
    float spawnY = spawn->archetype == EVOLUTION_ARCHETYPE_AQUATIC
        ? (float)spawn->waterY + 0.35f
        : (float)spawn->groundY + 1.0f;
    if (spawn->evolvable &&
        spawn->archetype == EVOLUTION_ARCHETYPE_FLIGHT) {
        float flightBase = fmaxf(spawnY + 3.0f, player->position.y + 1.5f);
        float flightCeiling = spawn->homeWorld
            ? (float)SURFACE_MAX_Y_EXCLUSIVE - 3.0f
            : (float)SURFACE_GENERATION_MAX_Y_EXCLUSIVE - 3.0f;
        spawnY = fminf(flightCeiling,
                       flightBase + (float)EntityRandomBounded(45u) / 10.0f);
    }
    entity->position = (Vector3){
        (float)spawn->x + 0.5f, spawnY, (float)spawn->z + 0.5f
    };
    entity->velocity = Vector3Zero();
    entity->yaw = (float)EntityRandomBounded(628u) / 100.0f;
    entity->motionTargetYaw = entity->yaw;
    entity->moveTimer = 0.0f;
    entity->thinkTimer = 1.0f + (float)EntityRandomBounded(200u) / 100.0f;
    entity->burnTimer = 0.0f;
    entity->bodyPlan = spawn->evolvable
        ? spawn->ecology.bodyPlan : PLANET_BODY_QUADRUPED;
    entity->chemistry = spawn->evolvable
        ? spawn->ecology.chemistry : PLANET_CHEMISTRY_CARBON;
    entity->niche = spawn->evolvable
        ? spawn->ecology.niche : PLANET_NICHE_GRAZER;
    entity->organismScale = spawn->evolvable
        ? spawn->ecology.organismScale : 1.0f;
    entity->bodyArmor = spawn->evolvable ? spawn->ecology.bodyArmor : 0.0f;
    entity->movementSpeed = spawn->evolvable
        ? spawn->ecology.movementSpeed : 0.85f;
    entity->temperament = spawn->evolvable
        ? spawn->ecology.temperament : 0.2f;
    entity->limbCount = spawn->evolvable ? spawn->ecology.limbCount : 4;
    entity->airborne = spawn->evolvable &&
        spawn->ecology.bodyPlan == PLANET_BODY_FLOATING;
    entity->colony = spawn->evolvable &&
        spawn->ecology.bodyPlan == PLANET_BODY_COLONY;
    if (spawn->evolvable) {
        if (spawn->organismSeed == 0u) {
            spawn->organismSeed = EntityMix(
                (PlanetWorldIsActive() ? PlanetWorldSeed() : WorldGetSeed()) ^
                (uint32_t)spawn->x * 0x9e3779b9u ^
                (uint32_t)spawn->z * 0x85ebca6bu ^ EntityRandomNext());
            if (spawn->organismSeed == 0u) spawn->organismSeed = 1u;
        }
        EntityInitializeEvolution(entity, spawn->archetype,
                                  spawn->organismSeed, false);
        if (spawn->haveSampledGenome) {
            entity->genome = spawn->sampledGenome;
            entity->lineageId = spawn->sampledLineage;
            entity->speciesId = spawn->sampledSpecies;
            EntityApplyEvolutionPhenotype(entity);
        }
    }
    entity->hoverHeight = spawnY;
    entity->phase = (float)EntityRandomBounded(628u) / 100.0f;
    entity->ecologyActivity = spawn->evolvable
        ? spawn->localEcology.suitability.faunaActivity : 1.0f;
    entity->ecologyCapacity = spawn->evolvable
        ? spawn->localEcology.suitability.faunaCapacity : 1.0f;
    entity->ecologyWindStrength = 0.0f;
    entity->ecologyWindAngle = 0.0f;
    EntityInitializeBehaviorState(entity);
    if (spawn->evolvable) {
        WeatherFieldSample weather = WeatherFieldSampleAtWorld(
            spawn->x, spawn->z);
        entity->ecologyWindStrength = weather.wind;
        entity->ecologyWindAngle = WeatherWindAngleAtWorld(
            spawn->x, spawn->z);
        EntityApplyLocalBehaviorEnvironment(
            entity, &spawn->localEcology, weather);
    }
    entity->ecologySampleTimer = spawn->evolvable
        ? 0.25f + (float)spawn->slot / (float)MAX_ENTITIES : 1.0f;
    entity->primaryBlock = spawn->evolvable
        ? spawn->ecology.primaryBlock : BLOCK_GRASS;
    entity->accentBlock = spawn->evolvable
        ? spawn->ecology.accentBlock : BLOCK_DIRT;
}

void EntitySpawnPassive(const Player *player, float daylight)
{
    EntityPassiveSpawn spawn = { 0 };
    if (!EntitySelectPassiveSpawn(&spawn, player)) return;
    if (!EntitySamplePassiveSpawn(&spawn, player, daylight)) return;
    if (!EntityValidatePassiveSpawn(&spawn, player)) return;
    EntityInitializePassiveSpawn(&spawn, player);
}

void EntitySpawnHostile(const Player *player, float daylight)
{
    if (daylight > 0.15f) return;
    if (player->position.y > 30.0f || player->position.y < -1.0f) return;

    int slot = EntityNextFreeSlot();
    if (slot < 0) return;

    EntityType type = EntityRandomBounded(2u) == 0
        ? ENTITY_ZOMBIE : ENTITY_SKELETON;
    float angle = (float)EntityRandomBounded(628u) / 100.0f;
    float distance = 18.0f + (float)EntityRandomBounded(200u) / 10.0f;
    int x = (int)floorf(player->position.x + cosf(angle) * distance);
    int z = (int)floorf(player->position.z + sinf(angle) * distance);
    int groundY = EntitySurfaceHeight(x, z);
    BlockType spawnAt = GetBlockAt(x, groundY + 1, z);
    BlockType spawnAbove = GetBlockAt(x, groundY + 2, z);
    if (spawnAt != BLOCK_AIR && spawnAt != BLOCK_WATER &&
        spawnAt != BLOCK_LAVA) {
        return;
    }
    if (spawnAbove != BLOCK_AIR && spawnAbove != BLOCK_WATER &&
        spawnAbove != BLOCK_LAVA) {
        return;
    }

    Entity *entity = &entities[slot];
    memset(entity, 0, sizeof(*entity));
    entity->active = true;
    entity->type = type;
    entity->position = (Vector3){
        (float)x + 0.5f, (float)groundY + 1.0f, (float)z + 0.5f
    };
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
