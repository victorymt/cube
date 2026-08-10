#include "entity.h"

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

#define ENTITY_STATE_VERSION 1u
#define ENTITY_RANDOM_FALLBACK 0x6d2b79f5u

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

static Entity entities[MAX_ENTITIES];
static uint32_t entityRandomState = ENTITY_RANDOM_FALLBACK;
static float entitySpawnTimer = 0.0f;

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

    EntityDiskStateV1 saved[MAX_ENTITIES] = { 0 };
    for (int index = 0; index < MAX_ENTITIES; index++) {
        const Entity *entity = &entities[index];
        EntityDiskStateV1 *disk = &saved[index];
        disk->active = entity->active ? 1u : 0u;
        if (!entity->active) continue;
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
        if (!EntityDiskStateValid(disk)) return false;
    }
    return fwrite(saved, sizeof(saved), 1, file) == 1;
}

bool EntitiesLoadState(FILE *file)
{
    uint32_t header[3];
    float loadedSpawnTimer = 0.0f;
    EntityDiskStateV1 saved[MAX_ENTITIES];
    if (!file || fread(header, sizeof(header), 1, file) != 1 ||
        fread(&loadedSpawnTimer, sizeof(loadedSpawnTimer), 1, file) != 1 ||
        fread(saved, sizeof(saved), 1, file) != 1) {
        return false;
    }
    if (header[0] != ENTITY_STATE_VERSION || header[1] != MAX_ENTITIES ||
        header[2] == 0u || !EntityFloatValid(loadedSpawnTimer)) {
        return false;
    }

    Entity loaded[MAX_ENTITIES] = { 0 };
    for (int index = 0; index < MAX_ENTITIES; index++) {
        const EntityDiskStateV1 *disk = &saved[index];
        if (!EntityDiskStateValid(disk)) return false;
        if (disk->active == 0u) continue;
        Entity *entity = &loaded[index];
        entity->active = true;
        entity->type = (EntityType)disk->type;
        entity->position = (Vector3){
            disk->position[0], disk->position[1], disk->position[2]
        };
        entity->velocity = (Vector3){
            disk->velocity[0], disk->velocity[1], disk->velocity[2]
        };
        entity->yaw = disk->yaw;
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
        entity->primaryBlock = (BlockType)disk->primaryBlock;
        entity->accentBlock = (BlockType)disk->accentBlock;
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
    PlanetEcologyProfile ecology = { 0 };
    if (alienWorld) {
        ecology = PlanetEcologyCurrent();
        if (!ecology.supportsFlight && player->position.y > 40.0f) return;
    } else if (player->position.y > 40.0f) {
        return;
    }
    EntityType type = ENTITY_COW;
    if (alienWorld) {
        if (ecology.faunaDensity <= 0.0f) return;
        uint32_t speciesSeed = PlanetWorldSeed();
        int species = (int)((speciesSeed +
            (uint32_t)EntityRandomBounded(2u) * 17u) % 3u);
        type = (EntityType)(ENTITY_ALIEN_GRAZER + species);
    } else {
        EntityType types[4] = { ENTITY_COW, ENTITY_SHEEP, ENTITY_PIG, ENTITY_CHICKEN };
        type = types[EntityRandomBounded(4u)];
    }

    PlanetLocalEcology localEcology = { 0 };
    float angle = (float)EntityRandomBounded(628u) / 100.0f;
    float dist = alienWorld ? 12.0f + (float)EntityRandomBounded(160u) / 10.0f
                            : 14.0f + (float)EntityRandomBounded(300u) / 10.0f;
    int gx = (int)floorf(player->position.x + cosf(angle) * dist);
    int gz = (int)floorf(player->position.z + sinf(angle) * dist);
    if (alienWorld) {
        localEcology = PlanetEcologyLocalAt(gx, gz, daylight);
        float faunaActivity = localEcology.suitability.faunaActivity;
        if (faunaActivity <= 0.0f ||
            EntityRandomBounded(1000u) >=
                (int)(faunaActivity * 1000.0f)) return;
    }
    if (alienWorld && !ecology.supportsFlight && !PlanetBiomeSupportsFauna(gx, gz) &&
        ecology.niche != PLANET_NICHE_CRYSTAL_GRAZER) return;
    int groundY = EntitySurfaceHeight(gx, gz);
    if (!alienWorld || !ecology.supportsFlight) {
        BlockType spawnAt = GetBlockAt(gx, groundY + 1, gz);
        BlockType spawnAbove = GetBlockAt(gx, groundY + 2, gz);
        if (spawnAt != BLOCK_AIR && spawnAt != BLOCK_WATER && spawnAt != BLOCK_LAVA) return;
        if (spawnAbove != BLOCK_AIR && spawnAbove != BLOCK_WATER && spawnAbove != BLOCK_LAVA) return;
    }

    Entity *entity = &entities[slot];
    entity->active = true;
    entity->type = type;
    float spawnY = (float)groundY + 1.0f;
    if (alienWorld && ecology.supportsFlight) {
        float flightBase = fmaxf(spawnY + 3.0f, player->position.y + 1.5f);
        spawnY = fminf((float)WORLD_HEIGHT - 3.0f,
                       flightBase + (float)EntityRandomBounded(45u) / 10.0f);
    }
    entity->position = (Vector3){ (float)gx + 0.5f, spawnY, (float)gz + 0.5f };
    entity->velocity = Vector3Zero();
    entity->yaw = (float)EntityRandomBounded(628u) / 100.0f;
    entity->moveTimer = 0.0f;
    entity->thinkTimer = 1.0f + (float)EntityRandomBounded(200u) / 100.0f;
    entity->burnTimer = 0.0f;
    entity->bodyPlan = alienWorld ? ecology.bodyPlan : PLANET_BODY_QUADRUPED;
    entity->chemistry = alienWorld ? ecology.chemistry : PLANET_CHEMISTRY_CARBON;
    entity->niche = alienWorld ? ecology.niche : PLANET_NICHE_GRAZER;
    entity->organismScale = alienWorld ? ecology.organismScale : 1.0f;
    entity->bodyArmor = alienWorld ? ecology.bodyArmor : 0.0f;
    entity->movementSpeed = alienWorld ? ecology.movementSpeed : 0.85f;
    entity->temperament = alienWorld ? ecology.temperament : 0.2f;
    entity->limbCount = alienWorld ? ecology.limbCount : 4;
    entity->airborne = alienWorld && ecology.bodyPlan == PLANET_BODY_FLOATING;
    entity->colony = alienWorld && ecology.bodyPlan == PLANET_BODY_COLONY;
    entity->hoverHeight = spawnY;
    entity->phase = (float)EntityRandomBounded(628u) / 100.0f;
    entity->ecologyActivity = alienWorld
        ? localEcology.suitability.faunaActivity : 1.0f;
    entity->ecologyCapacity = alienWorld
        ? localEcology.suitability.faunaCapacity : 1.0f;
    entity->ecologyWindStrength = 0.0f;
    entity->ecologyWindAngle = 0.0f;
    if (alienWorld) {
        WeatherFieldSample weather = WeatherFieldSampleAtWorld(gx, gz);
        entity->ecologyWindStrength = weather.wind;
        entity->ecologyWindAngle = WeatherWindAngleAtWorld(gx, gz);
    }
    entity->ecologySampleTimer = alienWorld
        ? 0.25f + (float)slot / (float)MAX_ENTITIES : 1.0f;
    entity->primaryBlock = alienWorld ? ecology.primaryBlock : BLOCK_GRASS;
    entity->accentBlock = alienWorld ? ecology.accentBlock : BLOCK_DIRT;
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
    entity->active = true;
    entity->type = type;
    entity->position = (Vector3){ (float)gx + 0.5f, (float)groundY + 1.0f, (float)gz + 0.5f };
    entity->velocity = Vector3Zero();
    entity->yaw = (float)EntityRandomBounded(628u) / 100.0f;
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

static PlanetHabitatChoice AlienHabitatChoiceAt(const Entity *entity,
                                                float daylight)
{
    int x = (int)floorf(entity->position.x);
    int z = (int)floorf(entity->position.z);
    const int offsets[4][2] = {
        { 0, -10 }, { 10, 0 }, { 0, 10 }, { -10, 0 }
    };
    float neighbors[4];
    for (int index = 0; index < 4; index++) {
        PlanetLocalEcology local = PlanetEcologyLocalAt(
            x + offsets[index][0], z + offsets[index][1], daylight);
        neighbors[index] = local.suitability.faunaActivity;
    }
    return PlanetEcologyChooseHabitat(entity->ecologyActivity, neighbors);
}

static void UpdatePassive(Entity *entity, const Player *player, float dt,
                          float daylight)
{
    bool alien = EntityIsAlien(entity->type);
    PlanetFaunaRuntimeState runtime = PlanetEcologyFaunaRuntime(1.0f, 1.0f);
    if (alien) {
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
            entity->ecologySampleTimer = 1.0f;
        }
        runtime = PlanetEcologyFaunaRuntime(entity->ecologyActivity,
                                             entity->ecologyCapacity);
    }

    float baseSpeed = (entity->type == ENTITY_CHICKEN) ? 0.7f : 1.0f;
    if (alien) {
        baseSpeed = entity->movementSpeed;
        if (entity->type == ENTITY_ALIEN_HOPPER) baseSpeed *= 1.25f;
        else if (entity->type == ENTITY_ALIEN_STRIDER) baseSpeed *= 1.10f;
        else if (entity->type == ENTITY_ALIEN_GRAZER) baseSpeed *= 0.92f;
    }
    Vector3 toPlayer = Vector3Subtract(player->position, entity->position);
    float playerDist = Vector3Length(toPlayer);
    bool threatened = playerDist < 5.0f;
    float movementScale = alien ? runtime.movementScale : 1.0f;
    if (alien && threatened) movementScale = fmaxf(movementScale, 0.28f);
    float speed = baseSpeed * movementScale;
    float windDrift = alien
        ? PlanetEcologyWindDrift(entity->ecologyWindStrength,
                                 entity->airborne)
        : 0.0f;
    bool seekingHabitat = false;

    entity->thinkTimer -= dt;
    if (entity->thinkTimer <= 0.0f) {
        entity->thinkTimer = 2.0f +
            (float)EntityRandomBounded(300u) / 100.0f;
        if (alien) {
            entity->thinkTimer *= 1.0f + (1.0f - runtime.activityRatio) * 1.5f;
        }
        if (threatened) {
            entity->yaw = atan2f(-toPlayer.x, -toPlayer.z);
            entity->moveTimer = 0.8f;
        } else {
            if (alien && !entity->colony && runtime.activityRatio < 0.72f) {
                PlanetHabitatChoice choice = AlienHabitatChoiceAt(entity, daylight);
                if (choice.shouldSeek) {
                    float dx = 0.0f;
                    float dz = 0.0f;
                    switch (choice.direction) {
                    case PLANET_HABITAT_NORTH: dz = -1.0f; break;
                    case PLANET_HABITAT_EAST:  dx = 1.0f; break;
                    case PLANET_HABITAT_SOUTH: dz = 1.0f; break;
                    case PLANET_HABITAT_WEST:  dx = -1.0f; break;
                    case PLANET_HABITAT_NONE:
                    default: break;
                    }
                    entity->yaw = atan2f(dx, dz);
                    entity->moveTimer = 1.15f + choice.improvement * 1.4f;
                    movementScale = fmaxf(movementScale, 0.22f);
                    speed = baseSpeed * movementScale;
                    seekingHabitat = true;
                }
            }
            if (!seekingHabitat) {
                if (entity->colony || (alien && runtime.dormant)) {
                    entity->moveTimer = 0.0f;
                } else if (EntityRandomBounded(100u) <
                           (alien ? 15 + (int)(runtime.activityRatio * 40.0f) : 55)) {
                    entity->yaw = (float)EntityRandomBounded(628u) / 100.0f;
                    entity->moveTimer = 1.0f +
                        (float)EntityRandomBounded(200u) / 100.0f;
                    if (alien) {
                        entity->moveTimer *= 0.45f + runtime.activityRatio * 0.55f;
                    }
                } else {
                    entity->moveTimer = 0.0f;
                }
            }
        }
    }

    float animationScale = alien ? runtime.animationScale : 1.0f;
    entity->phase += dt * (0.7f + baseSpeed * 0.35f) * animationScale;
    if (entity->airborne) {
        float targetY = entity->hoverHeight + sinf(entity->phase) *
                        (0.45f + entity->organismScale * 0.22f) *
                        (0.20f + animationScale * 0.80f);
        entity->position.y += (targetY - entity->position.y) * fminf(1.0f, dt * 2.2f);
    }

    if (entity->moveTimer > 0.0f && !entity->colony) {
        entity->moveTimer -= dt;
        float fleeSpeed = (playerDist < 5.0f) ? speed * (1.25f + entity->temperament) : speed;
        Vector3 move = { sinf(entity->yaw) * fleeSpeed, 0.0f, cosf(entity->yaw) * fleeSpeed };
        if (alien && !entity->airborne) {
            move.x += cosf(entity->ecologyWindAngle) * windDrift;
            move.z += sinf(entity->ecologyWindAngle) * windDrift;
        }
        if (entity->airborne) {
            entity->position.x += move.x * dt;
            entity->position.z += move.z * dt;
        } else {
            MoveEntityHorizontal(entity, move, dt);
        }
    }
    if (alien && entity->airborne && windDrift > 0.0f) {
        entity->position.x += cosf(entity->ecologyWindAngle) * windDrift * dt;
        entity->position.z += sinf(entity->ecologyWindAngle) * windDrift * dt;
    }
}

static void UpdateHostile(Entity *entity, const Player *player, float dt, float daylight)
{
    Vector3 toPlayer = Vector3Subtract(player->position, entity->position);
    toPlayer.y = 0.0f;
    float playerDist = Vector3Length(toPlayer);
    float speed = (entity->type == ENTITY_ZOMBIE) ? 1.4f : 1.2f;

    if (playerDist < 34.0f) {
        entity->yaw = atan2f(toPlayer.x, toPlayer.z);
        Vector3 move = { sinf(entity->yaw) * speed, 0.0f, cosf(entity->yaw) * speed };
        MoveEntityHorizontal(entity, move, dt);
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
        if (PlanetWorldIsActive()) {
            int playerX = (int)floorf(player->position.x);
            int playerZ = (int)floorf(player->position.z);
            float localFauna = PlanetEcologyFaunaDensityAt(
                playerX, playerZ, daylight);
            populationCap = localFauna > 0.0f
                ? 1 + (int)(localFauna * 20.0f) : 0;
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

        if (!entity->airborne) {
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
            UpdatePassive(entity, player, dt, daylight);
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

        if (EntityIsAlien(entity->type)) {
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
        if (EntityIsAlien(entity->type)) {
            float scale = entity->organismScale > 0.1f ? entity->organismScale : 1.0f;
            radius = 0.62f * scale;
            height = 1.55f * scale;
            if (entity->bodyPlan == PLANET_BODY_FLOATING) {
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
        if (EntityIsAlien(entity->type) && entity->bodyPlan == PLANET_BODY_FLOATING) {
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

void EntityKill(int index)
{
    if (index < 0 || index >= MAX_ENTITIES) return;
    Entity *entity = &entities[index];
    if (!entity->active) return;

    ParticlesEmitBurst(entity->position, EntityBodyColor(entity->type), 18, 3.0f, 0.7f);
    AudioPlayBreak();
    entity->active = false;
}
