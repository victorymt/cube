#include "entity.h"

#include "raymath.h"
#include "world.h"
#include "terrain.h"
#include "ecology.h"
#include "space.h"
#include "particles.h"
#include "audio.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

static Entity entities[MAX_ENTITIES];

void EntitiesInit(void)
{
    for (int i = 0; i < MAX_ENTITIES; i++) entities[i].active = false;
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
    if (y < 0 || y >= SPACE_LAYER_Y) return false;
    return BlockBlocksEntity(x, y, z);
}

static int EntitySurfaceHeight(int x, int z)
{
    return PlanetWorldIsActive() ? PlanetTerrainHeight(x, z) : TerrainHeight(x, z, terrainMode);
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

static void SpawnPassive(const Player *player)
{
    if (player->position.y > 40.0f) return;

    int slot = NextFreeEntity();
    if (slot < 0) return;

    bool alienWorld = PlanetWorldIsActive();
    EntityType type = ENTITY_COW;
    if (alienWorld) {
        float faunaDensity = PlanetEcologyFaunaDensity();
        if (faunaDensity <= 0.0f || rand() % 1000 >= (int)(faunaDensity * 1000.0f)) return;
        uint32_t speciesSeed = PlanetWorldSeed();
        int species = (int)((speciesSeed + (uint32_t)(rand() % 2) * 17u) % 3u);
        type = (EntityType)(ENTITY_ALIEN_GRAZER + species);
    } else {
        EntityType types[4] = { ENTITY_COW, ENTITY_SHEEP, ENTITY_PIG, ENTITY_CHICKEN };
        type = types[rand() % 4];
    }

    float angle = (float)(rand() % 628) / 100.0f;
    float dist = alienWorld ? 12.0f + (float)(rand() % 160) / 10.0f
                            : 14.0f + (float)(rand() % 300) / 10.0f;
    int gx = (int)floorf(player->position.x + cosf(angle) * dist);
    int gz = (int)floorf(player->position.z + sinf(angle) * dist);
    if (alienWorld && !PlanetBiomeSupportsFauna(gx, gz)) return;
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
    entity->yaw = (float)(rand() % 628) / 100.0f;
    entity->moveTimer = 0.0f;
    entity->thinkTimer = 1.0f + (float)(rand() % 200) / 100.0f;
    entity->burnTimer = 0.0f;
}

static void SpawnHostile(const Player *player, float daylight)
{
    if (daylight > 0.15f) return;
    if (player->position.y > 30.0f || player->position.y < -1.0f) return;

    int slot = NextFreeEntity();
    if (slot < 0) return;

    EntityType type = (rand() % 2 == 0) ? ENTITY_ZOMBIE : ENTITY_SKELETON;
    float angle = (float)(rand() % 628) / 100.0f;
    float dist = 18.0f + (float)(rand() % 200) / 10.0f;
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
    entity->yaw = (float)(rand() % 628) / 100.0f;
    entity->moveTimer = 0.0f;
    entity->thinkTimer = 0.1f;
    entity->burnTimer = 2.0f;
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

static void UpdatePassive(Entity *entity, const Player *player, float dt)
{
    float speed = (entity->type == ENTITY_CHICKEN) ? 0.7f : 1.0f;
    if (entity->type == ENTITY_ALIEN_HOPPER) speed = 1.55f;
    else if (entity->type == ENTITY_ALIEN_STRIDER) speed = 1.20f;
    else if (entity->type == ENTITY_ALIEN_GRAZER) speed = 0.85f;
    Vector3 toPlayer = Vector3Subtract(player->position, entity->position);
    float playerDist = Vector3Length(toPlayer);

    entity->thinkTimer -= dt;
    if (entity->thinkTimer <= 0.0f) {
        entity->thinkTimer = 2.0f + (float)(rand() % 300) / 100.0f;
        if (playerDist < 5.0f) {
            entity->yaw = atan2f(-toPlayer.x, -toPlayer.z);
            entity->moveTimer = 0.8f;
        } else if (rand() % 100 < 55) {
            entity->yaw = (float)(rand() % 628) / 100.0f;
            entity->moveTimer = 1.0f + (float)(rand() % 200) / 100.0f;
        } else {
            entity->moveTimer = 0.0f;
        }
    }

    if (entity->moveTimer > 0.0f) {
        entity->moveTimer -= dt;
        float fleeSpeed = (playerDist < 5.0f) ? speed * 1.5f : speed;
        Vector3 move = { sinf(entity->yaw) * fleeSpeed, 0.0f, cosf(entity->yaw) * fleeSpeed };
        MoveEntityHorizontal(entity, move, dt);
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
    static float spawnTimer = 0.0f;
    spawnTimer -= dt;
    if (spawnTimer <= 0.0f) {
        spawnTimer = 1.5f;
        int populationCap = MAX_ENTITIES - 4;
        if (PlanetWorldIsActive()) {
            populationCap = 3 + (int)(PlanetEcologyFaunaDensity() * 18.0f);
        }
        if (GetActiveEntityCount() < populationCap) {
            if (PlanetWorldIsActive() || daylight > 0.5f) SpawnPassive(player);
            else SpawnHostile(player, daylight);
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

        float gravityScale = PlanetWorldIsActive() ? PlanetWorldGravityScale() : 1.0f;
        entity->velocity.y -= 24.0f * gravityScale * dt;
        entity->position.y += entity->velocity.y * dt;

        if (GroundBelow(entity->position)) {
            entity->position.y = floorf(entity->position.y) + 1.0f;
            entity->velocity.y = 0.0f;
        }
        if (entity->position.y < (float)NETHER_LAYER_Y) {
            entity->active = false;
            continue;
        }

        if (entity->type >= ENTITY_ZOMBIE) {
            UpdateHostile(entity, player, dt, daylight);
        } else {
            UpdatePassive(entity, player, dt);
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

static void DrawAlienEntity(const Entity *entity)
{
    Vector3 pos = entity->position;
    Vector3 forward = { sinf(entity->yaw), 0.0f, cosf(entity->yaw) };
    Vector3 side = { forward.z, 0.0f, -forward.x };
    Color body = EntityBodyColor(entity->type);
    Color accent = EntityHeadColor(entity->type);

    if (entity->type == ENTITY_ALIEN_GRAZER) {
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.0f, 0.0f, 0.92f),
                      (Vector3){ 1.10f, 0.70f, 1.55f }, body);
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.98f, 0.0f, 1.02f),
                      (Vector3){ 0.62f, 0.58f, 0.58f }, accent);
        for (int row = -1; row <= 1; row++) {
            for (int pair = -1; pair <= 1; pair += 2) {
                DrawEntityBox(AlienPartPosition(pos, forward, side, row * 0.48f,
                                                pair * 0.38f, 0.38f),
                              (Vector3){ 0.16f, 0.76f, 0.16f }, body);
            }
        }
        DrawEntityBox(AlienPartPosition(pos, forward, side, 1.02f, -0.22f, 1.48f),
                      (Vector3){ 0.10f, 0.42f, 0.10f }, accent);
        DrawEntityBox(AlienPartPosition(pos, forward, side, 1.02f, 0.22f, 1.48f),
                      (Vector3){ 0.10f, 0.42f, 0.10f }, accent);
        return;
    }

    if (entity->type == ENTITY_ALIEN_HOPPER) {
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.0f, 0.0f, 0.72f),
                      (Vector3){ 0.68f, 0.54f, 0.82f }, body);
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.52f, 0.0f, 0.91f),
                      (Vector3){ 0.42f, 0.42f, 0.42f }, accent);
        DrawEntityBox(AlienPartPosition(pos, forward, side, -0.20f, -0.38f, 0.42f),
                      (Vector3){ 0.20f, 0.84f, 0.20f }, body);
        DrawEntityBox(AlienPartPosition(pos, forward, side, -0.20f, 0.38f, 0.42f),
                      (Vector3){ 0.20f, 0.84f, 0.20f }, body);
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.58f, -0.18f, 1.30f),
                      (Vector3){ 0.08f, 0.50f, 0.08f }, accent);
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.58f, 0.18f, 1.30f),
                      (Vector3){ 0.08f, 0.50f, 0.08f }, accent);
        return;
    }

    DrawEntityBox(AlienPartPosition(pos, forward, side, 0.0f, 0.0f, 1.62f),
                  (Vector3){ 0.86f, 0.52f, 1.24f }, body);
    for (int row = -1; row <= 1; row += 2) {
        for (int pair = -1; pair <= 1; pair += 2) {
            DrawEntityBox(AlienPartPosition(pos, forward, side, row * 0.38f,
                                            pair * 0.32f, 0.76f),
                          (Vector3){ 0.15f, 1.52f, 0.15f }, body);
        }
    }
    DrawEntityBox(AlienPartPosition(pos, forward, side, 0.48f, 0.0f, 2.12f),
                  (Vector3){ 0.24f, 0.82f, 0.24f }, accent);
    DrawEntityBox(AlienPartPosition(pos, forward, side, 0.72f, 0.0f, 2.55f),
                  (Vector3){ 0.52f, 0.38f, 0.52f }, accent);
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
        Vector3 center = { entity->position.x, entity->position.y + height * 0.5f, entity->position.z };

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
