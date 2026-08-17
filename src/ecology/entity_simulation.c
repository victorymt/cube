#include "ecology/entity_internal.h"

#include "core/game_effects.h"
#include "ecology/ecology.h"
#include "ecology/fauna_motion.h"
#include "space/space_state.h"
#include "world/fluid.h"
#include "world/terrain.h"
#include "world/weather.h"
#include "world/world.h"
#include "world/world_environment.h"

#include "raymath.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define entities entityStore

int EntityNextFreeSlot(void)
{
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if (!entities[i].active) return i;
    }
    return -1;
}

static bool BlockBlocksEntity(int x, int y, int z)
{
    BlockType type = GetBlockAt(x, y, z);
    return BlockCollisionHeight(type) > 0.0f;
}

static bool GroundBelow(Vector3 position)
{
    int x = (int)floorf(position.x);
    int z = (int)floorf(position.z);
    int y = (int)floorf(position.y - 0.1f);
    if (WorldBlockRegionAt(y) != WORLD_BLOCK_REGION_SURFACE) return false;
    return BlockBlocksEntity(x, y, z);
}

int EntitySurfaceHeight(int x, int z)
{
    return WorldSurfaceHeightAt(x, z);
}

bool EntityIsAlien(EntityType type)
{
    return type >= ENTITY_ALIEN_GRAZER && type <= ENTITY_ALIEN_STRIDER;
}

bool EntityUsesEcology(const Entity *entity)
{
    return entity && (entity->evolvable || EntityIsAlien(entity->type));
}

float EntityEvolutionGrowthScale(const Entity *entity)
{
    if (!entity || !entity->evolvable || entity->maturityAgeDays <= 0.0f) {
        return 1.0f;
    }
    float maturity = fminf(fmaxf(
        entity->ageDays / entity->maturityAgeDays, 0.0f), 1.0f);
    return 0.35f + maturity * 0.65f;
}

Color EntityParticleColor(EntityType type)
{
    switch (type) {
    case ENTITY_COW: return (Color){ 138, 96, 62, 255 };
    case ENTITY_SHEEP: return (Color){ 238, 236, 228, 255 };
    case ENTITY_PIG: return (Color){ 236, 176, 168, 255 };
    case ENTITY_CHICKEN: return (Color){ 240, 236, 222, 255 };
    case ENTITY_ALIEN_GRAZER:
    case ENTITY_ALIEN_HOPPER:
    case ENTITY_ALIEN_STRIDER:
        return ColorPalette256((int)(PlanetWorldSeed() % 216u) + 20);
    case ENTITY_ZOMBIE: return (Color){ 110, 150, 84, 255 };
    case ENTITY_SKELETON: return (Color){ 226, 226, 224, 255 };
    default: return MAGENTA;
    }
}

EntityType EntityEvolutionTypeForArchetype(
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

void EntityApplyLocalBehaviorEnvironment(
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
        float dy = entity->position.y - player->position.y;
        float dz = entity->position.z - player->position.z;
        float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (distanceSquared > farthestDistanceSquared) {
            farthestDistanceSquared = distanceSquared;
            farthest = index;
        }
    }
    if (farthest >= 0) entities[farthest].active = false;
}

static bool EntityFindWaterNearY(int x, int z, int centerY, int radius,
                                 int *outY)
{
    if (!outY || radius < 0) return false;
    for (int offset = 0; offset <= radius; offset++) {
        int above = centerY + offset;
        if (above >= SURFACE_MIN_Y && above < SURFACE_MAX_Y_EXCLUSIVE &&
            GetBlockAt(x, above, z) == BLOCK_WATER) {
            *outY = above;
            return true;
        }
        int below = centerY - offset;
        if (offset > 0 && below >= SURFACE_MIN_Y &&
            below < SURFACE_MAX_Y_EXCLUSIVE &&
            GetBlockAt(x, below, z) == BLOCK_WATER) {
            *outY = below;
            return true;
        }
    }
    return false;
}

bool EntityFindAquaticSpawnY(int x, int z, int preferredY,
                             int fallbackY, int *outY)
{
    if (EntityFindWaterNearY(
            x, z, preferredY, ENTITY_AQUATIC_SPAWN_SEARCH_RADIUS, outY)) {
        return true;
    }
    if (fallbackY == preferredY) return false;
    return EntityFindWaterNearY(
        x, z, fallbackY, ENTITY_AQUATIC_SPAWN_SEARCH_RADIUS, outY);
}

#ifdef ENTITY_TESTING
bool EntityTestFindAquaticSpawnY(int x, int preferredY, int z,
                                 int fallbackY, int *outY)
{
    return EntityFindAquaticSpawnY(x, z, preferredY, fallbackY, outY);
}

bool EntityTestBlockTypeBlocks(BlockType type)
{
    return BlockCollisionHeight(type) > 0.0f;
}
#endif

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

void EntityBecomeCorpse(Entity *entity)
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
    int slot = EntityNextFreeSlot();
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

typedef struct PassiveUpdateContext {
    Entity *entity;
    int entityIndex;
    const Player *player;
    float dt;
    float daylight;
    bool ecological;
    bool threatened;
    PlanetFaunaRuntimeState runtime;
    float baseSpeed;
    float movementScale;
    float windDrift;
    float horizontalSpeed;
    Vector3 toPlayer;
    FaunaMotionProfile motionProfile;
    FaunaNeedInput needInput;
} PassiveUpdateContext;

static void PassiveSampleEnvironment(PassiveUpdateContext *context)
{
    Entity *entity = context->entity;
    context->ecological = EntityUsesEcology(entity);
    context->runtime = PlanetEcologyFaunaRuntime(1.0f, 1.0f);
    if (!context->ecological) return;

    entity->ecologySampleTimer -= context->dt;
    if (entity->ecologySampleTimer <= 0.0f) {
        int x = (int)floorf(entity->position.x);
        int z = (int)floorf(entity->position.z);
        PlanetLocalEcology local = PlanetEcologyLocalAt(
            x, z, context->daylight);
        entity->ecologyActivity = local.suitability.faunaActivity;
        entity->ecologyCapacity = local.suitability.faunaCapacity;
        WeatherFieldSample weather = WeatherFieldSampleAtWorld(x, z);
        entity->ecologyWindStrength = weather.wind;
        entity->ecologyWindAngle = WeatherWindAngleAtWorld(x, z);
        EntityApplyLocalBehaviorEnvironment(entity, &local, weather);
        entity->ecologySampleTimer = 1.0f;
    }
    context->runtime = PlanetEcologyFaunaRuntime(
        entity->ecologyActivity, entity->ecologyCapacity);
}

static void PassiveAdvanceNeeds(PassiveUpdateContext *context)
{
    Entity *entity = context->entity;
    context->baseSpeed = entity->type == ENTITY_CHICKEN ? 0.7f : 1.0f;
    if (context->ecological) {
        context->baseSpeed = entity->movementSpeed;
        if (entity->airborne) context->baseSpeed *= 1.25f;
        else if (entity->aquatic) context->baseSpeed *= 1.10f;
        else context->baseSpeed *= 0.92f;
    }
    context->toPlayer = Vector3Subtract(
        context->player->position, entity->position);
    context->threatened = Vector3Length(context->toPlayer) < 5.0f;
    context->movementScale = context->ecological
        ? context->runtime.movementScale : 1.0f;
    if (context->ecological && context->threatened) {
        context->movementScale = fmaxf(context->movementScale, 0.28f);
    }
    context->windDrift = context->ecological
        ? PlanetEcologyWindDrift(entity->ecologyWindStrength,
                                 entity->airborne)
        : 0.0f;
    context->motionProfile = EntityMotionProfile(
        entity, context->baseSpeed);
    context->horizontalSpeed = sqrtf(
        entity->velocity.x * entity->velocity.x +
        entity->velocity.z * entity->velocity.z);
    bool actionActive = entity->moveTimer > 0.0f;
    bool filterFeeding = entity->niche == PLANET_NICHE_FILTER_FEEDER &&
                         entity->behavior == FAUNA_ACTION_SEEK_FOOD;
    context->needInput = (FaunaNeedInput){
        .activityRatio = context->runtime.activityRatio,
        .movementRatio = context->motionProfile.sprintSpeed > 0.0001f
            ? context->horizontalSpeed / context->motionProfile.sprintSpeed
            : 0.0f,
        .foodAvailability = entity->ecologyFoodAvailability,
        .waterAvailability = entity->ecologyWaterAvailability,
        .shelterAvailability = entity->ecologyShelterAvailability,
        .stormPressure = entity->ecologyStormPressure,
        .temperatureStress = entity->ecologyTemperatureStress,
        .moving = actionActive && FaunaBehaviorActionMoves(entity->behavior),
        .threatened = context->threatened,
        .feeding = actionActive &&
            (entity->behavior == FAUNA_ACTION_FORAGE || filterFeeding),
        .drinking = actionActive && entity->behavior == FAUNA_ACTION_DRINK,
        .resting = actionActive && entity->behavior == FAUNA_ACTION_REST
    };
    entity->needs = FaunaNeedsAdvance(
        &entity->needs, &context->needInput, context->dt);
}

static void PassiveChooseBehavior(PassiveUpdateContext *context)
{
    Entity *entity = context->entity;
    entity->thinkTimer -= context->dt;
    if (context->threatened && entity->behavior != FAUNA_ACTION_FLEE) {
        entity->thinkTimer = 0.0f;
    }
    if (entity->thinkTimer <= 0.0f) {
        float baseThinkInterval = 2.0f +
            (float)EntityRandomBounded(300u) / 100.0f;
        EntityBehaviorDirections directions = { 0 };
        if (context->ecological && !context->threatened && !entity->colony) {
            directions = AlienBehaviorDirectionsAt(entity, context->daylight);
        }
        FaunaBehaviorInput behaviorInput = {
            .needs = entity->needs,
            .environment = context->needInput,
            .food = directions.food,
            .water = directions.water,
            .shelter = directions.shelter,
            .habitat = directions.habitat,
            .foodDependence = EntityFoodDependence(entity->niche),
            .waterDependence = EntityWaterDependence(entity->chemistry),
            .fleeYaw = atan2f(-context->toPlayer.x, -context->toPlayer.z),
            .baseThinkInterval = baseThinkInterval,
            .wanderRoll = (unsigned)EntityRandomBounded(100u),
            .wanderYaw = (float)EntityRandomBounded(628u) / 100.0f,
            .baseWanderDuration = 1.0f +
                (float)EntityRandomBounded(200u) / 100.0f,
            .currentAction = entity->behavior,
            .colony = entity->colony,
            .dormant = context->ecological && context->runtime.dormant
        };
        FaunaBehaviorDecision decision = FaunaBehaviorEvaluate(&behaviorInput);
        int evolutionTarget = -1;
        FaunaBehaviorAction evolutionAction = FAUNA_ACTION_IDLE;
        if (entity->evolvable && !entity->pregnant &&
            !context->threatened) {
            if (entity->phenotype.diet >= 0.36f && entity->needs.energy < 0.78f) {
                evolutionTarget = EntityFindEvolutionTarget(
                    context->entityIndex, FAUNA_ACTION_SCAVENGE);
                evolutionAction = FAUNA_ACTION_SCAVENGE;
            }
            if (evolutionTarget < 0 && entity->phenotype.diet >= 0.58f &&
                entity->needs.energy < 0.70f) {
                evolutionTarget = EntityFindEvolutionTarget(
                    context->entityIndex, FAUNA_ACTION_HUNT);
                evolutionAction = FAUNA_ACTION_HUNT;
            }
            if (evolutionTarget < 0 && EntityAdult(entity) &&
                entity->reproductionCooldownDays <= 0.0f) {
                evolutionTarget = EntityFindEvolutionTarget(
                    context->entityIndex, FAUNA_ACTION_MATE);
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
}

static void PassiveInteractWithTarget(PassiveUpdateContext *context)
{
    Entity *entity = context->entity;
    if (entity->evolvable && entity->targetEntity >= 0) {
        Entity *target = &entities[entity->targetEntity];
        Vector3 toTarget = Vector3Subtract(target->position, entity->position);
        float targetDistance = Vector3Length(toTarget);
        entity->motionTargetYaw = atan2f(toTarget.x, toTarget.z);
        if (targetDistance <= entity->phenotype.bodyRadius +
                              target->phenotype.bodyRadius + 0.55f) {
            if (entity->behavior == FAUNA_ACTION_SCAVENGE && target->corpse) {
                float consumed = fminf(
                    target->corpseEnergy, context->dt * 0.10f);
                target->corpseEnergy -= consumed;
                entity->needs.energy = fminf(1.0f,
                    entity->needs.energy + consumed * 0.85f);
                if (target->corpseEnergy <= 0.0f) {
                    target->active = false;
                    entity->targetEntity = -1;
                }
            } else if (entity->behavior == FAUNA_ACTION_HUNT &&
                       !target->corpse) {
                float damage = context->dt *
                    (0.08f + entity->phenotype.attack * 0.035f) /
                    fmaxf(0.5f, target->phenotype.defense * 0.22f);
                target->health -= damage;
                if (target->health <= 0.0f) {
                    PlanetEcologyRecordEvolutionEvent(
                        (int)floorf(target->position.x),
                        (int)floorf(target->position.z), context->daylight,
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
}

static void PassiveAdvanceController(PassiveUpdateContext *context)
{
    Entity *entity = context->entity;
    bool actionActive = entity->moveTimer > 0.0f;
    float animationScale = context->ecological
        ? context->runtime.animationScale : 1.0f;
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
        context->movementScale *= fminf(fmaxf(
            1.0f + controllerOutputs[1] * 0.16f, 0.82f), 1.18f);
    }
    float neuralPhaseScale = 1.0f + controllerOutputs[2] * 0.15f;
    entity->phase += context->dt *
        (0.7f + context->baseSpeed * 0.35f) * animationScale *
        neuralPhaseScale;
    if (entity->airborne && !entity->aquatic) {
        int groundY = EntitySurfaceHeight(
            (int)floorf(entity->position.x),
            (int)floorf(entity->position.z));
        float flightCeiling = PlanetWorldIsActive()
            ? (float)SURFACE_GENERATION_MAX_Y_EXCLUSIVE - 3.0f
            : (float)SURFACE_MAX_Y_EXCLUSIVE - 3.0f;
        float terrainHover = fminf(
            flightCeiling,
            (float)groundY + 1.0f + context->motionProfile.hoverClearance);
        entity->hoverHeight += (terrainHover - entity->hoverHeight) *
            fminf(1.0f, context->dt * 0.75f);
        float targetY = entity->hoverHeight + sinf(entity->phase) *
                        (0.45f + entity->organismScale * 0.22f) *
                        (0.20f + animationScale * 0.80f);
        entity->position.y += (targetY - entity->position.y) *
            fminf(1.0f, context->dt * 2.2f);
    }
}

static void PassiveIntegrateMotion(PassiveUpdateContext *context)
{
    Entity *entity = context->entity;
    bool moving = entity->moveTimer > 0.0f && !entity->colony &&
                  FaunaBehaviorActionMoves(entity->behavior);
    if (entity->moveTimer > 0.0f) entity->moveTimer -= context->dt;

    FaunaMotionInput motionInput = {
        .profile = context->motionProfile,
        .currentYaw = entity->yaw,
        .targetYaw = entity->motionTargetYaw,
        .currentSpeed = context->horizontalSpeed,
        .movementScale = fmaxf(
            context->movementScale,
            EntityBehaviorMovementFloor(entity->behavior)),
        .deltaTime = context->dt,
        .moving = moving,
        .sprinting = entity->behavior == FAUNA_ACTION_FLEE
    };
    float lookahead = context->motionProfile.bodyRadius + 0.38f +
        fminf(motionInput.currentSpeed * 0.25f, 0.45f);
    if (moving) {
        EntityMotionCandidates(entity, &context->motionProfile,
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
        entity->position.y += current.y * context->dt;
    }
    if (context->ecological && context->windDrift > 0.0f) {
        float coupledDrift = context->windDrift *
            context->motionProfile.windCoupling;
        move.x += cosf(entity->ecologyWindAngle) * coupledDrift;
        move.z += sinf(entity->ecologyWindAngle) * coupledDrift;
    }
    if (motion.speed > 0.0f || fabsf(current.x) > 0.0001f ||
        fabsf(current.z) > 0.0001f ||
        (context->ecological && entity->airborne &&
         context->windDrift > 0.0f)) {
        if (entity->airborne || entity->aquatic) {
            MoveEntityAirborne(
                entity, &context->motionProfile, move, context->dt);
        } else {
            MoveEntityGrounded(
                entity, &context->motionProfile, move, context->dt);
        }
    }
}

static void UpdatePassive(Entity *entity, int entityIndex,
                          const Player *player, float dt,
                          float daylight)
{
    EntityUpdateEvolutionLifecycle(entity, entityIndex, dt, daylight);
    if (!entity->active || entity->corpse) return;
    PassiveUpdateContext context = {
        .entity = entity,
        .entityIndex = entityIndex,
        .player = player,
        .dt = dt,
        .daylight = daylight
    };
    PassiveSampleEnvironment(&context);
    PassiveAdvanceNeeds(&context);
    PassiveChooseBehavior(&context);
    PassiveInteractWithTarget(&context);
    PassiveAdvanceController(&context);
    PassiveIntegrateMotion(&context);
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
            GameEffectsEmitParticleBurst(
                entity->position, (Color){ 255, 140, 40, 255 },
                4, 1.5f, 0.5f);
        }
        if (entity->burnTimer <= 0.0f) {
            GameEffectsEmitParticleBurst(
                entity->position, (Color){ 255, 170, 60, 255 },
                14, 2.5f, 0.6f);
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
            if (PlanetWorldIsActive() || daylight > 0.5f) {
                EntitySpawnPassive(player, daylight);
            } else {
                EntitySpawnHostile(player, daylight);
            }
        } else if (PlanetWorldIsActive() && activeCount > populationCap) {
            DespawnDistantAlien(player);
        }
    }

    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *entity = &entities[i];
        if (!entity->active) continue;

        float dx = entity->position.x - player->position.x;
        float dy = entity->position.y - player->position.y;
        float dz = entity->position.z - player->position.z;
        if (dx * dx + dy * dy + dz * dz >
            ENTITY_DESPAWN_DISTANCE * ENTITY_DESPAWN_DISTANCE) {
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
        float minimumY =
            WorldBlockRegionAt(NETHER_LAYER_Y) == WORLD_BLOCK_REGION_NETHER
            ? (float)NETHER_LAYER_Y : (float)SURFACE_MIN_Y;
        if (WorldIsSurfaceActive() && entity->position.y < minimumY) {
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
