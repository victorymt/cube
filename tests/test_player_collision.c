#include "gameplay/player.h"
#include "gameplay/camera.h"
#include "gameplay/camera_world.h"
#include "gameplay/player_input.h"
#include "gameplay/player_motion.h"
#include "gameplay/player_sensors.h"

#include "world/block_catalog.h"
#include "world/fluid.h"
#include "gameplay/ship.h"
#include "world/block_types.h"
#include "world/block_edits.h"
#include "world/environment_query.h"
#include "world/world_environment.h"
#include "world/world_frame.h"
#include "space/space_types.h"
#include "world/surface_contract_internal.h"
#include "world/surface_cube.h"
#include "world/world_surface_snapshot.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "raymath.h"

static bool wallEnabled = false;
static float partialSolidHeight;
static bool ceilingEnabled = false;
static bool stairsEnabled = false;
static bool waterEnabled = false;
static bool groundEnabled = false;
static BlockType ecologyPlant = BLOCK_AIR;
static bool proceduralWaterEnabled = false;
static bool surfaceReady = true;
static bool shipDriving = false;
static float proceduralWaterSurface = 0.0f;
static int waterMinY = 1;
static int waterMaxY = 5;

BlockType GetBlockAt(int x, int y, int z)
{
    if (partialSolidHeight > 0.0f && x == 0 && y == 1 && z == 0) return BLOCK_BASALT;
    if (waterEnabled && x >= -16 && x <= 16 && z >= -16 && z <= 16 &&
        y >= waterMinY && y <= waterMaxY) {
        return BLOCK_WATER;
    }
    if (groundEnabled && y == 0) return BLOCK_STONE;
    if (wallEnabled && x == 2 && y >= 0 && y < 4 && z == 0) {
        return BLOCK_STONE;
    }
    if (ceilingEnabled && x == 0 && y == 3 && z == 0) {
        return BLOCK_STONE;
    }
    if (stairsEnabled && x == 2 && y == 1 && z == 0) {
        return BLOCK_STONE_STAIRS;
    }
    if (ecologyPlant != BLOCK_AIR && x == 2 && y == 1 && z == 0) {
        return ecologyPlant;
    }
    return BLOCK_AIR;
}

bool IsLiquidBlock(BlockType type)
{
    return type == BLOCK_WATER || type == BLOCK_LAVA;
}
float BlockLiquidHeightAt(int x, int y, int z)
{
    return IsLiquidBlock(GetBlockAt(x, y, z)) ? 1.0f : 0.0f;
}

bool IsWaterBlock(BlockType type)
{
    return type == BLOCK_WATER;
}

bool WorldIsSpaceActive(void) { return false; }
int SpaceOriginX(void) { return 0; }
int SpaceOriginZ(void) { return 0; }
int SpaceSystemGlobalCoordinate(int anchor)
{
    return anchor * STAR_SYSTEM_SPACING;
}
int SpaceAnchorForLocalCoordinate(float local, int origin)
{
    int64_t global = (int64_t)llroundf(local) + origin;
    int64_t halfSpacing = STAR_SYSTEM_SPACING / 2;
    return (int)(global >= 0 ? (global + halfSpacing) / STAR_SYSTEM_SPACING
                             : (global - halfSpacing) / STAR_SYSTEM_SPACING);
}
float WorldGravityScale(void) { return 1.0f; }
bool PlanetSurfaceAt(Vector3 position, Vector3 *gravityDirection,
                     float *surfaceDistance, float *gravityScale)
{
    (void)position;
    (void)gravityDirection;
    (void)surfaceDistance;
    (void)gravityScale;
    return false;
}
float BlockCollisionHeightAt(int x, int y, int z)
{
    if (partialSolidHeight > 0.0f && x == 0 && y == 1 && z == 0) return partialSolidHeight;
    BlockType block = GetBlockAt(x, y, z);
    return block == BLOCK_AIR || block == BLOCK_WATER ||
           block == BLOCK_TALL_GRASS || block == BLOCK_MOSS_CARPET
        ? 0.0f : 1.0f;
}

float BlockCollisionHeight(BlockType type)
{
    return type == BLOCK_AIR || type == BLOCK_WATER ||
           type == BLOCK_TALL_GRASS || type == BLOCK_MOSS_CARPET
        ? 0.0f : 1.0f;
}

WorldBlockRegion WorldBlockRegionAt(int y)
{
    (void)y;
    return WORLD_BLOCK_REGION_SURFACE;
}

bool WorldIsSurfaceActive(void)
{
    return true;
}

/* Minimal surface context so geometry-driven code can run: a Home world whose
 * map origin is the legacy local origin and whose body id is 0. */
static bool cubeSurface = false;
static int cubeGroundRadial = 0;
static bool cubeTerrainKnown = true;
static bool cubeOverrideEnabled = false;
static SurfaceAddress cubeOverrideAddress;
static BlockType cubeOverrideBlock = BLOCK_AIR;
static unsigned cubePointQueries = 0;

WorldSurfaceContext WorldCurrentSurfaceContext(void)
{
    return (WorldSurfaceContext){
        .active = true,
        .kind = cubeSurface ? WORLD_SURFACE_HOME : WORLD_SURFACE_PLANET,
        .topology = cubeSurface
            ? (SurfaceTopologyVersion)SURFACE_TOPOLOGY_CUBE_V1
            : (SurfaceTopologyVersion)SURFACE_TOPOLOGY_MAP_V1,
        .bodyId = cubeSurface ? 0u : 1u,
        .seed = 1234u,
        .originX = 0,
        .originZ = 0
    };
}

/* Cube stub: everything unknown when the fixture says so, otherwise solid at
 * or below the configured ground radial. */
bool WorldCubeBlockAt(WorldBlockAddress address, BlockType *outType)
{
    if (!outType) return false;
    if (!cubeTerrainKnown) return false;
    if (cubeOverrideEnabled &&
        SurfaceAddressEqual(address.at.surface, cubeOverrideAddress)) {
        *outType = cubeOverrideBlock;
        return true;
    }
    *outType = address.at.surface.radial <= cubeGroundRadial
        ? BLOCK_STONE : BLOCK_AIR;
    return true;
}

bool WorldCubeBlockAtPoint(uint32_t bodyId, SurfaceVector point,
                           BlockType *outType)
{
    cubePointQueries++;
    SurfaceAddress address;
    if (!outType || !SurfaceCellFromPoint(bodyId, point, &address)) return false;
    return WorldCubeBlockAt((WorldBlockAddress){
        .dimension = bodyId == 0 ? WORLD_DIMENSION_HOME
                                 : WORLD_DIMENSION_PLANET,
        .at.surface = address
    }, outType);
}

static bool TestPlayerContractCellRead(const SurfaceContract *contract,
                                       SurfaceAddress address,
                                       WorldCell *outCell)
{
    BlockType block;
    if (!contract || !outCell || address.bodyId != contract->bodyId ||
        !WorldCubeBlockAt((WorldBlockAddress){
            .dimension = WORLD_DIMENSION_HOME, .at.surface = address },
            &block)) return false;
    *outCell = WorldCellFromLegacyBlock(block);
    return true;
}

static bool TestPlayerContractNeighbor(
    const SurfaceContract *contract, SurfaceAddress address,
    SurfaceDirection direction, SurfaceAddress *outAddress,
    SurfaceDirection *outReturnDirection)
{
    if (!contract || !outAddress || address.bodyId != contract->bodyId) {
        return false;
    }
    SurfaceAddress next = address;
    if (direction <= SURFACE_DIRECTION_V_PLUS) {
        SurfaceCubeEdge back;
        if (!SurfaceCubeCellNeighbor(address, (SurfaceCubeEdge)direction,
                                     &next, &back)) return false;
        if (outReturnDirection) *outReturnDirection = (SurfaceDirection)back;
    } else if (direction == SURFACE_DIRECTION_RADIAL_MINUS) {
        if (address.radial == INT_MIN) return false;
        next.radial--;
        if (outReturnDirection) *outReturnDirection =
            SURFACE_DIRECTION_RADIAL_PLUS;
    } else if (direction == SURFACE_DIRECTION_RADIAL_PLUS) {
        if (address.radial == INT_MAX) return false;
        next.radial++;
        if (outReturnDirection) *outReturnDirection =
            SURFACE_DIRECTION_RADIAL_MINUS;
    } else {
        return false;
    }
    *outAddress = next;
    return true;
}

static void TestPlayerContractRelease(SurfaceContract *contract)
{
    (void)contract;
}

static const SurfaceContractOps playerContractOps = {
    .cell_read = TestPlayerContractCellRead,
    .neighbor = TestPlayerContractNeighbor,
    .supports_physical_pose = true,
    .release = TestPlayerContractRelease
};
static SurfaceContract playerContract;

static void InstallPlayerCubeContract(void)
{
    WorldSetCubeRuntimeEnabled(true);
    WorldSurfaceTopologyPolicy policy = {
        { SURFACE_TOPOLOGY_CUBE_GRID, 1u },
        { SURFACE_TOPOLOGY_CUBE_GRID, 1u }
    };
    assert(WorldInstallTopologyPolicy(policy));
    WorldSurfaceSnapshot snapshot = {
        .context = {
            .active = true,
            .kind = WORLD_SURFACE_HOME,
            .topology = policy.home,
            .bodyId = 0u,
            .seed = 1234u
        }
    };
    playerContract = (SurfaceContract){
        .ops = &playerContractOps,
        .implementation = &playerContract,
        .topology = policy.home,
        .bodyId = 0u
    };
    assert(WorldInstallSurfaceSnapshotWithContract(&snapshot, &playerContract));
}
int WorldSurfaceMapOriginX(void) { return 0; }
int WorldSurfaceMapOriginZ(void) { return 0; }
uint32_t WorldCurrentSurfaceId(void) { return cubeSurface ? 0u : 1u; }

bool WorldProceduralWaterSurfaceAt(int x, int z, float *outSurfaceY)
{
    (void)x;
    (void)z;
    if (!proceduralWaterEnabled || !outSurfaceY) return false;
    *outSurfaceY = proceduralWaterSurface;
    return true;
}

uint8_t FluidGetVolumeAt(int x, int y, int z)
{
    return GetBlockAt(x, y, z) == BLOCK_WATER ? FLUID_CAPACITY : 0u;
}

FluidSample FluidSampleAt(Vector3 position)
{
    int x = (int)floorf(position.x);
    int y = (int)floorf(position.y);
    int z = (int)floorf(position.z);
    uint8_t volume = FluidGetVolumeAt(x, y, z);
    return (FluidSample){
        .volume = volume,
        .surfaceY = (float)y + (float)volume / (float)FLUID_CAPACITY
    };
}

bool SpaceBlockReadyAt(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
    return true;
}

bool SurfaceBlockReadyAt(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
    return surfaceReady;
}

bool ShipIsDriving(void)
{
    return shipDriving;
}

static void ResetCollisionWorld(void)
{
    wallEnabled = false;
    ceilingEnabled = false;
    stairsEnabled = false;
    waterEnabled = false;
    groundEnabled = false;
    ecologyPlant = BLOCK_AIR;
    proceduralWaterEnabled = false;
    surfaceReady = true;
    shipDriving = false;
    proceduralWaterSurface = 0.0f;
    waterMinY = 1;
    waterMaxY = 5;
}

static void TestSurfaceStreamingFrontierBlocksWalkingPlayer(void)
{
    ResetCollisionWorld();
    surfaceReady = false;
    Player player = {
        .position = { 0.5f, 10.0f, 0.5f },
        .velocity = { 4.0f, -4.0f, 0.0f },
        .floating = true
    };

    assert(PlayerOverlapsWorld(player.position));
    assert(PlayerMissingSurfaceChunkCount(player.position) == 1);
    MovePlayer(&player, (Vector3){ 2.0f, -2.0f, 0.0f });
    assert(player.position.x == 0.5f);
    assert(player.position.y == 10.0f);
    assert(player.velocity.x == 0.0f);
    assert(player.velocity.y == 0.0f);

    surfaceReady = true;
    assert(PlayerMissingSurfaceChunkCount(player.position) == 0);
    MovePlayer(&player, (Vector3){ 1.0f, -1.0f, 0.0f });
    assert(player.position.x > 0.5f);
    assert(player.position.y < 10.0f);
}

static void TestShipMayCrossSurfaceStreamingFrontier(void)
{
    ResetCollisionWorld();
    surfaceReady = false;
    shipDriving = true;
    Player player = {
        .position = { 0.5f, 10.0f, 0.5f },
        .floating = true
    };

    assert(!PlayerOverlapsWorld(player.position));
    MovePlayer(&player, (Vector3){ 1.0f, -1.0f, 0.0f });
    assert(player.position.x > 0.5f);
    assert(player.position.y < 10.0f);
}

static void TestHighSpeedHorizontalMovementStopsAtWall(void)
{
    ResetCollisionWorld();
    wallEnabled = true;
    Player player = {
        .position = { 0.5f, 1.0f, 0.5f },
        .velocity = { 8.0f, 0.0f, 0.0f },
        .onGround = false,
        .floating = true
    };

    MovePlayer(&player, (Vector3){ 4.0f, 0.0f, 0.0f });

    assert(isfinite(player.position.x));
    assert(player.position.x + PLAYER_RADIUS <= 2.0001f);
    assert(player.position.x > 1.0f);
    assert(player.velocity.x == 0.0f);
}

static void TestHighSpeedVerticalMovementStopsAtCeiling(void)
{
    ResetCollisionWorld();
    ceilingEnabled = true;
    Player player = {
        .position = { 0.5f, 0.0f, 0.5f },
        .velocity = { 0.0f, 8.0f, 0.0f },
        .onGround = true,
        .floating = true
    };

    MovePlayer(&player, (Vector3){ 0.0f, 5.0f, 0.0f });

    assert(player.position.y + PLAYER_HEIGHT <= 3.0001f);
    assert(player.velocity.y == 0.0f);
}

static void TestHugeMovementIsBounded(void)
{
    ResetCollisionWorld();
    Player player = {
        .position = { 0.5f, 10.0f, 0.5f },
        .velocity = { 0.0f, 0.0f, 0.0f },
        .onGround = false,
        .floating = true
    };

    MovePlayer(&player, (Vector3){ 1000000.0f, 0.0f, 0.0f });

    assert(isfinite(player.position.x));
    assert(player.position.x <= 16.5001f);
}

static void TestSubstepsPreserveStepUp(void)
{
    ResetCollisionWorld();
    stairsEnabled = true;
    Player player = {
        .position = { 0.5f, 1.0f, 0.1f },
        .velocity = { 5.0f, 0.0f, 0.0f },
        .onGround = true,
        .floating = false
    };

    MovePlayer(&player, (Vector3){ 2.5f, 0.0f, 0.0f });

    assert(player.position.x > 2.0f);
    assert(player.position.y > 1.0f);
    assert(player.position.y <= 1.34f);
}

static void TestNonFiniteMovementIsIgnored(void)
{
    ResetCollisionWorld();
    Player player = {
        .position = { 0.5f, 2.0f, 0.5f },
        .velocity = { 1.0f, 2.0f, 3.0f },
        .onGround = true,
        .floating = false
    };
    Player before = player;

    MovePlayer(&player, (Vector3){ NAN, 0.0f, 0.0f });
    assert(memcmp(&player, &before, sizeof(player)) == 0);
    player.position.x = INFINITY;
    before = player;
    MovePlayer(&player, (Vector3){ 1.0f, 0.0f, 0.0f });
    assert(memcmp(&player, &before, sizeof(player)) == 0);
}

static void TestEcologyPlantsDoNotBlockMovement(void)
{
    const BlockType plants[] = { BLOCK_TALL_GRASS, BLOCK_MOSS_CARPET };
    for (size_t index = 0; index < sizeof(plants) / sizeof(plants[0]); index++) {
        ResetCollisionWorld();
        ecologyPlant = plants[index];
        Player player = {
            .position = { 0.5f, 1.0f, 0.5f },
            .velocity = { 5.0f, 0.0f, 0.0f },
            .floating = true
        };
        MovePlayer(&player, (Vector3){ 3.0f, 0.0f, 0.0f });
        assert(player.position.x > 3.0f);
    }
}

static void TestInvalidCollisionPositionsAreBlocked(void)
{
    ResetCollisionWorld();
    assert(PlayerOverlapsWorld((Vector3){ NAN, 2.0f, 0.0f }));
    assert(PlayerOverlapsWorld((Vector3){ 0x1p31f, 2.0f, 0.0f }));
    assert(PlayerOverlapsWorld((Vector3){ 0.0f, 0x1p31f, 0.0f }));
    assert(!PlayerOverlapsWorld(
        (Vector3){ 0.0f, (float)SURFACE_MIN_Y, 0.0f }));
    assert(PlayerOverlapsWorld(
        (Vector3){ 0.0f, (float)SURFACE_MIN_Y - 0.25f, 0.0f }));
    assert(PlayerOverlapsWorld((Vector3){
        0.0f, (float)SURFACE_MAX_Y_EXCLUSIVE - 1.0f, 0.0f
    }));
}

static bool TestCameraPointInsideSolid(void *user, Vector3 position)
{
    (void)user;
    return PlayerCameraPositionInsideSolid(position);
}

static void TestCameraIsPushedOutOfSolidBlocks(void)
{
    ResetCollisionWorld();
    wallEnabled = true;
    Vector3 inside = { 2.5f, 1.5f, 0.5f };
    Vector3 pivot = { 1.5f, 1.5f, 0.5f };
    assert(PlayerCameraPositionInsideSolid(inside));
    const CameraWorldOps ops = { .pointInsideSolid = TestCameraPointInsideSolid };

    Vector3 resolved = CameraResolvePosition(&ops, pivot, inside);
    assert(!PlayerCameraPositionInsideSolid(resolved));
    assert(resolved.x < 2.0f);

    resolved = CameraResolvePosition(&ops, inside, inside);
    assert(!PlayerCameraPositionInsideSolid(resolved));

    waterEnabled = true;
    assert(!PlayerCameraPositionInsideSolid((Vector3){ 0.5f, 2.5f, 0.5f }));
}

static void TestNeutralActorMotionSolver(void)
{
    ResetCollisionWorld();
    ActorBody body = {
        .position = { 0.5f, 2.0f, 0.5f },
        .velocity = { 2.0f, 0.0f, 0.0f },
        .yaw = 0.75f,
        .pitch = -0.2f,
        .onGround = true,
        .floating = false
    };
    const ActorBody before = body;
    const ActorMotionRequest request = {
        .delta = { 1.0f, 0.0f, 0.0f },
        .allowUnready = false,
        .canStepUp = true
    };
    const ActorCollider collider = {
        .radius = PLAYER_RADIUS,
        .height = PLAYER_HEIGHT,
        .stepHeight = 0.5f
    };
    ActorMotionResult result;
    assert(WorldActorResolveMotion(&body, &collider, &request, &result));
    assert(result.moved);
    assert(fabsf(result.body.position.x - 1.5f) < 1e-5f);
    assert(result.body.yaw == before.yaw && result.body.pitch == before.pitch);
    assert(Vector3Distance(body.position, before.position) == 0.0f);
}

static void TestRuntimeStateIsPerPlayer(void)
{
    Player first = { 0 };
    Player second = { 0 };

    first.wasInWater = true;
    first.stepTimer = 1.25f;
    assert(!second.wasInWater);
    assert(second.stepTimer == 0.0f);

    second.wasInWater = true;
    second.stepTimer = 2.5f;
    assert(first.wasInWater);
    assert(first.stepTimer == 1.25f);
}

static void TestRuntimeStateReset(void)
{
    Player player = {
        .wasInWater = true,
        .stepTimer = NAN
    };

    PlayerResetRuntimeState(&player);
    assert(!player.wasInWater);
    assert(player.stepTimer == 0.0f);
    PlayerResetRuntimeState(NULL);
}

static void TestWaterStateUsesActualColumnSurface(void)
{
    ResetCollisionWorld();
    waterEnabled = true;
    PlayerWaterState water = PlayerWaterStateAt(
        (Vector3){ 0.5f, 1.01f, 0.5f });
    assert(water.feetSubmerged);
    assert(water.bodySubmerged);
    assert(water.eyesSubmerged);
    assert(fabsf(water.surfaceY - 6.0f) < 0.0001f);
    assert(fabsf(water.eyeDepth - (6.0f - 1.01f - EYE_HEIGHT)) < 0.0001f);

    proceduralWaterEnabled = true;
    proceduralWaterSurface = 81.0f;
    water = PlayerWaterStateAt((Vector3){ 0.5f, 1.01f, 0.5f });
    assert(fabsf(water.surfaceY - 6.0f) < 0.0001f);

    waterMinY = -2000;
    waterMaxY = 80;
    water = PlayerWaterStateAt((Vector3){ 0.5f, -1000.0f, 0.5f });
    assert(fabsf(water.surfaceY - 81.0f) < 0.0001f);
    assert(fabsf(water.eyeDepth - (81.0f + 1000.0f - EYE_HEIGHT)) <
           0.0001f);
    proceduralWaterEnabled = false;
    waterMinY = 1;
    waterMaxY = 5;

    water = PlayerWaterStateAt((Vector3){ 0.5f, 5.20f, 0.5f });
    assert(water.feetSubmerged);
    assert(!water.bodySubmerged);
    assert(!water.eyesSubmerged);
    assert(water.eyeDepth == 0.0f);
}

static void TestEnvironmentQueryMatchesPlayerAdapters(void)
{
    ResetCollisionWorld();
    waterEnabled = true;
    WorldWaterSample neutral = WorldWaterSampleAt(
        (Vector3){ 0.5f, 1.01f, 0.5f }, EYE_HEIGHT, PLAYER_HEIGHT);
    PlayerWaterState player = PlayerWaterStateAt(
        (Vector3){ 0.5f, 1.01f, 0.5f });
    assert(neutral.feetSubmerged == player.feetSubmerged);
    assert(neutral.bodySubmerged == player.bodySubmerged);
    assert(neutral.eyesSubmerged == player.eyesSubmerged);
    assert(neutral.surfaceY == player.surfaceY);
    assert(neutral.eyeDepth == player.eyeDepth);

    WorldEnvironmentSample environment = WorldEnvironmentSampleAt(
        (Vector3){ 0.5f, 1.01f, 0.5f }, EYE_HEIGHT, PLAYER_HEIGHT, 12.5);
    assert(environment.simulationTime == 12.5);
    assert(environment.water.surfaceY == neutral.surfaceY);
    assert(environment.gravity.gravityScale == 1.0f);
    assert(environment.gravity.acceleration.y == -1.0f);
    assert(environment.gravity.ruleCount == 0u);
    assert(!environment.gravity.inSpace);

    WorldRuleSet rules;
    WorldRuleSetInit(&rules);
    assert(WorldRuleSetAdd(&rules, &(WorldRule){
        .stage = WORLD_RULE_STAGE_GRAVITY,
        .priority = 0,
        .scope = { WORLD_RULE_SCOPE_BODY, 7u, 0u },
        .operation = WORLD_RULE_ADD,
        .value = { 0.0f, 0.0f, 0.25f }
    }) > 0);
    environment = WorldEnvironmentSampleAtWithRules(
        (Vector3){ 0.5f, 1.01f, 0.5f }, EYE_HEIGHT, PLAYER_HEIGHT, 12.5,
        7u, 0u, &rules);
    assert(environment.gravity.ruleCount == 1u);
    assert(fabsf(environment.gravity.acceleration.z - 0.25f) < 1e-6f);
    environment = WorldEnvironmentSampleAtWithRules(
        (Vector3){ 0.5f, 1.01f, 0.5f }, EYE_HEIGHT, PLAYER_HEIGHT, 12.5,
        8u, 0u, &rules);
    assert(environment.gravity.ruleCount == 0u);
}

static void TestWorldFrameSurfaceRoundTrip(void)
{
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    WorldFramePoint local = {
        .frame = { WORLD_FRAME_LOCAL, context.bodyId },
        .x = 0.5,
        .y = 12.5,
        .z = 0.5
    };
    WorldFramePoint body;
    assert(WorldFrameTransformPoint(&local, local.frame, 8.0, &body));
    assert(body.frame.kind == WORLD_FRAME_LOCAL &&
           body.x == local.x && body.y == local.y && body.z == local.z);
    assert(WorldFrameTransformPoint(
        &local, (WorldFrameId){ .kind = WORLD_FRAME_BODY_CENTERED,
                                .bodyId = context.bodyId }, 8.0, &body));
    assert(body.frame.kind == WORLD_FRAME_BODY_CENTERED);
    assert(body.frame.bodyId == context.bodyId);
    WorldFramePoint roundTrip;
    assert(WorldFrameTransformPoint(&body, local.frame, 8.1, &roundTrip));
    assert(fabs(roundTrip.x - local.x) < 1e-3);
    assert(fabs(roundTrip.y - local.y) < 1e-3);
    assert(fabs(roundTrip.z - local.z) < 1e-3);
    assert(WorldFrameCanTransform(
        local.frame, (WorldFrameId){ .kind = WORLD_FRAME_SYSTEM,
                                    .bodyId = context.bodyId }));
    local.x = NAN;
    assert(!WorldFrameTransformPoint(
        &local, (WorldFrameId){ .kind = WORLD_FRAME_BODY_CENTERED,
                                .bodyId = context.bodyId }, 8.0, &body));

    local = (WorldFramePoint){
        .frame = { WORLD_FRAME_LOCAL, context.bodyId },
        .x = 0.5,
        .y = 12.5,
        .z = 0.5
    };
    WorldFrameId systemFrame = WorldFrameSystemId(0, 0);
    systemFrame.bodyId = context.bodyId;
    WorldFramePoint system;
    assert(WorldFrameTransformPoint(&local, systemFrame, 8.0, &system));
    assert(system.frame.kind == WORLD_FRAME_SYSTEM);
    WorldFramePoint systemRoundTrip;
    assert(WorldFrameTransformPoint(&system, local.frame, 8.1,
                                    &systemRoundTrip));
    assert(fabs(systemRoundTrip.x - local.x) < 1e-3);
    assert(fabs(systemRoundTrip.y - local.y) < 1e-3);
    assert(fabs(systemRoundTrip.z - local.z) < 1e-3);
    WorldFramePoint savePoint;
    assert(WorldFrameTransformPoint(
        &system, WorldFrameSaveId(context.bodyId), 8.15, &savePoint));
    assert(savePoint.frame.kind == WORLD_FRAME_LOCAL);
    assert(fabs(savePoint.x - local.x) < 1e-3);
    assert(fabs(savePoint.y - local.y) < 1e-3);
    assert(fabs(savePoint.z - local.z) < 1e-3);

    Vector3 cameraLocal = { 2.0f, 3.0f, 4.0f };
    WorldFramePoint cameraPoint;
    assert(WorldFrameTransformPointToCamera(&local, cameraLocal, 8.2,
                                             &cameraPoint));
    assert(cameraPoint.frame.kind == WORLD_FRAME_RENDER_CAMERA);
    assert(fabs(cameraPoint.x + 1.5) < 1e-6);
    assert(fabs(cameraPoint.y - 9.5) < 1e-6);
    assert(fabs(cameraPoint.z + 3.5) < 1e-6);
    WorldFramePoint cameraRoundTrip;
    assert(WorldFrameTransformPointFromCamera(&cameraPoint, cameraLocal, 8.3,
                                               &cameraRoundTrip));
    assert(fabs(cameraRoundTrip.x - local.x) < 1e-3);
    assert(fabs(cameraRoundTrip.y - local.y) < 1e-3);
    assert(fabs(cameraRoundTrip.z - local.z) < 1e-3);
}

static void TestRightVectorMatchesCameraFrame(void)
{
    const float yaws[] = {0.0f, PI * 0.25f, PI * 0.5f, PI,
                          -PI * 0.5f, 2.0f};
    for (size_t index = 0; index < sizeof(yaws) / sizeof(yaws[0]); index++) {
        Vector3 forward = FlatForward(yaws[index]);
        Vector3 expected = Vector3Normalize(Vector3CrossProduct(
            forward, (Vector3){0.0f, 1.0f, 0.0f}));
        Vector3 right = RightFromYaw(yaws[index]);
        assert(Vector3Distance(right, expected) < 1.0e-6f);
        assert(fabsf(Vector3DotProduct(right, forward)) < 1.0e-6f);
    }
}

static bool TestPlayerWorldMove(void *user, ActorBody *body,
                                const ActorMotionRequest *request)
{
    (void)user;
    ActorMotionResult result;
    if (!WorldActorResolveMotion(body, PlayerMotionCollider(), request,
                                 &result)) return false;
    *body = result.body;
    return true;
}

static bool TestPlayerWorldOverlaps(void *user, Vector3 position,
                                    bool allowUnready)
{
    (void)user;
    return PlayerOverlapsWorldWithOptions(position, allowUnready);
}

static PlayerWaterState TestPlayerWorldWater(void *user, Vector3 position,
                                             double simulationTime)
{
    (void)user;
    (void)simulationTime;
    return PlayerWaterStateAt(position);
}

static bool TestPlayerWorldGravity(void *user, Vector3 position,
                                   double simulationTime,
                                   PlayerGravitySample *out)
{
    (void)user;
    (void)position;
    (void)simulationTime;
    *out = (PlayerGravitySample){ .gravityScale = 1.0f };
    return true;
}

static const PlayerWorldOps testPlayerWorldOps = {
    .move = TestPlayerWorldMove,
    .overlaps = TestPlayerWorldOverlaps,
    .sampleWater = TestPlayerWorldWater,
    .sampleGravity = TestPlayerWorldGravity
};

static void TestSwimmingSpeedAndAscent(void)
{
    ResetCollisionWorld();
    waterEnabled = true;
    groundEnabled = true;
    Player player = {
        .position = { 0.5f, 1.01f, 0.5f },
        .yaw = 0.0f,
        .onGround = true
    };
    PlayerInput forward = { .forward = 1.0f };
    for (int frame = 0; frame < 120; frame++) {
        UpdatePlayerWithWorld(&player, 1.0f / 60.0f, (double)frame / 60.0,
                              &forward, &testPlayerWorldOps);
    }
    float distance = player.position.z - 0.5f;
    assert(distance > 5.0f && distance < 6.1f);

    player = (Player){
        .position = { 0.5f, 1.01f, 0.5f },
        .yaw = 0.0f,
        .onGround = true
    };
    PlayerInput ascend = { .vertical = 1.0f, .jumpPressed = true };
    for (int frame = 0; frame < 120; frame++) {
        ascend.jumpPressed = frame == 0;
        UpdatePlayerWithWorld(&player, 1.0f / 60.0f, (double)frame / 60.0,
                              &ascend, &testPlayerWorldOps);
    }
    assert(player.position.y > 3.01f);
}

/* Cube branch: the body box must collide with solid terrain and must treat
 * ungenerated terrain as a wall for a walking player. */
static void TestCubeOverlapBranch(void)
{
    /* Find the ground cell under the origin once, then stand two cells up. */
    SurfacePose probe;
    assert(SurfacePoseFromGeographic(0, 0.0, 0.0, 0.0, 0.0, 0.0, &probe));
    SurfaceAddress cell;
    assert(SurfaceCellFromPoint(0, probe.position, &cell));

    cubeSurface = true;
    cubeTerrainKnown = true;
    cubeGroundRadial = cell.radial;

    /* Feet on the ground top: the body sits above the solid cell. */
    assert(!PlayerOverlapsWorld((Vector3){0.5f, (float)cell.radial + 1.0f, 0.5f}));

    /* Body sunk into the ground cell collides. */
    assert(PlayerOverlapsWorld((Vector3){0.5f, (float)cell.radial - 0.5f, 0.5f}));

    /* Ungenerated terrain blocks a walking player. */
    cubeTerrainKnown = false;
    assert(PlayerOverlapsWorld((Vector3){0.5f, (float)cell.radial + 1.0f, 0.5f}));

    cubeTerrainKnown = true;
    cubeSurface = false;
}

static void TestCubeDecorationBlocksArePassable(void)
{
    cubeSurface = true;
    cubeTerrainKnown = true;
    cubeGroundRadial = -100000;

    SurfacePose pose;
    assert(SurfacePoseFromGeographic(0, 0.0, 0.0, 40.0, 0.0, 0.0, &pose));
    Vector3 position;
    assert(PlayerLocalFromSurfacePose(&pose, &position));
    assert(SurfaceCellFromPoint(0, pose.position, &cubeOverrideAddress));
    cubeOverrideEnabled = true;

    const BlockType passable[] = {
        BLOCK_FLOWER, BLOCK_TALL_GRASS, BLOCK_FERN, BLOCK_REED,
        BLOCK_COMMON_REED, BLOCK_MOSS_CARPET, BLOCK_LEAF_LITTER,
        BLOCK_SPHAGNUM, BLOCK_FENCE_GATE_OPEN, BLOCK_LAVA
    };
    for (size_t index = 0; index < sizeof(passable) / sizeof(passable[0]); index++) {
        cubeOverrideBlock = passable[index];
        assert(BlockCatalogGet(cubeOverrideBlock)->collisionHeight <= 0.0f);
        assert(!PlayerOverlapsWorld(position));
    }

    cubeOverrideBlock = BLOCK_STONE;
    assert(PlayerOverlapsWorld(position));

    cubeOverrideEnabled = false;
    cubeSurface = false;
}

static void TestCubeReadinessUsesPhysicalBodyCells(void)
{
    cubeSurface = true;
    cubeTerrainKnown = false;
    surfaceReady = true; /* Stub says the legacy map column is ready. */

    SurfacePose pose;
    assert(SurfacePoseFromGeographic(0, 0.0, 0.0, 40.0, 0.0, 0.0, &pose));
    Vector3 position;
    assert(PlayerLocalFromSurfacePose(&pose, &position));

    /* The cube body has unknown cells, so readiness must agree with collision
     * even though the legacy SurfaceBlockReadyAt stub returns true. */
    assert(PlayerMissingSurfaceChunkCount(position) > 0);
    assert(PlayerOverlapsWorld(position));

    cubeTerrainKnown = true;
    cubeGroundRadial = -100000;
    assert(PlayerMissingSurfaceChunkCount(position) == 0);
    assert(!PlayerOverlapsWorld(position));
    cubeOverrideEnabled = false;
    cubeSurface = false;
}

/* Cube movement must be physical: the same input travels the same distance in
 * blocks regardless of latitude. Stepping in map cells would make it shrink
 * with latitude, which is exactly the defect this path replaces. */
static double CubeTravel(Vector3 from, Vector3 to)
{
    Vector3 a = SurfaceDirectionFromMapCoordinates(from.x, from.z);
    Vector3 b = SurfaceDirectionFromMapCoordinates(to.x, to.z);
    double dot = fmax(-1.0, fmin(1.0, (double)Vector3DotProduct(a, b)));
    return acos(dot) * (double)SURFACE_RADIUS_BLOCKS;
}

/* The two conversions must agree with PlayerOverlapsCubeWorld's own convention:
 * legacy Y is height above the mean sphere, so a standing player's Y survives a
 * move unchanged when the move is purely horizontal. A radial offset of one
 * sphere radius would move the body by thousands of blocks. */
static void TestCubeHeightSurvivesMovement(void)
{
    cubeSurface = true;
    cubeTerrainKnown = true;
    cubeGroundRadial = -100000;

    Player player = {0};
    const float startY = 70.0f;
    player.position = (Vector3){0.5f, startY, 0.5f};
    for (int i = 0; i < 4; i++)
        MovePlayer(&player, (Vector3){0.5f, 0.0f, 0.0f});
    /* Height must be preserved to within float noise, not shifted by R. */
    assert(fabsf(player.position.y - startY) < 1.0f);
    cubeSurface = false;
}

/* A grounded player must still be able to walk. Gravity contributes a small
 * negative delta.y every frame; if the whole displacement is tested at once,
 * that term pushes the endpoint into the floor and vetoes horizontal motion. */
static void TestCubeGroundedPlayerWalks(void)
{
    cubeSurface = true;
    cubeTerrainKnown = true;
    /* Solid at or below radial 40, air above. */
    cubeGroundRadial = 40;

    Player player = {0};
    /* Feet rest on top of the solid cell: solid covers radial <= 40, so the
     * body starts at 41 and the first small fall reaches the contact height. */
    player.position = (Vector3){0.5f, 41.0f, 0.5f};
    player.onGround = false;

    /* Fall until the ground stops the player; the blocked downward move is
     * what marks it grounded. */
    for (int i = 0; i < 40 && !player.onGround; i++)
        MovePlayer(&player, (Vector3){0.0f, -0.05f, 0.0f});
    assert(player.onGround);

    /* Now walk. The horizontal component must survive the blocked gravity. */
    float startZ = player.position.z;
    for (int i = 0; i < 20; i++)
        MovePlayer(&player, (Vector3){0.0f, -0.01f, 0.15f});
    assert(fabsf(player.position.z - startZ) > 0.5f);
    cubeSurface = false;
}

static void TestCubeMoveIsPhysical(void)
{
    cubeSurface = true;
    cubeTerrainKnown = true;
    cubeGroundRadial = -100000; /* no ground: nothing collides */

    const float step = 0.5f;
    const int steps = 4;
    double travelled[3] = {0};
    /* Three latitudes, all at the same physical height. */
    const float mapZ[3] = {0.5f, 1600.5f, 2600.5f};
    for (int pass = 0; pass < 3; pass++) {
        Player player = {0};
        /* Legacy Y is height above the mean sphere, so 40 is 40 blocks up. */
        player.position = (Vector3){0.5f, 40.0f, mapZ[pass]};
        Vector3 before = player.position;
        for (int i = 0; i < steps; i++)
            MovePlayer(&player, (Vector3){step, 0.0f, 0.0f});
        travelled[pass] = CubeTravel(before, player.position);
    }
    /* Each pass should travel steps*step blocks. Allow a small tolerance for
     * the float32 projection, but reject latitude-dependent shrinking. */
    for (int pass = 0; pass < 3; pass++) {
        double expected = steps * step;
        if (!(travelled[pass] > expected * 0.5 &&
              travelled[pass] < expected * 1.5)) {
            fprintf(stderr, "pass=%d travelled=%.4f expected~%.1f\n",
                    pass, travelled[pass], expected);
            assert(0 && "cube movement is not physical");
        }
    }
    cubeSurface = false;
}

static void TestCameraCubePointUsesPhysicalCell(void)
{
    cubeSurface = true;
    cubeTerrainKnown = true;
    cubeGroundRadial = 40;
    cubePointQueries = 0;

    assert(PlayerCameraPositionInsideSolid((Vector3){ 0.5f, 40.5f, 0.5f }));
    assert(!PlayerCameraPositionInsideSolid((Vector3){ 0.5f, 41.5f, 0.5f }));
    assert(cubePointQueries >= 2u);

    /* The pole is a topology seam: the point query must canonicalize through
     * the physical cube cell instead of relying on the legacy map grid. */
    float poleZ = (float)SURFACE_POLE_TO_POLE_BLOCKS * 0.5f - 0.25f;
    assert(PlayerCameraPositionInsideSolid(
        (Vector3){ 0.5f, 40.5f, poleZ }));
    assert(cubePointQueries >= 3u);
    cubeSurface = false;
}

int main(void)
{
    ResetCollisionWorld();
    TestRightVectorMatchesCameraFrame();
    partialSolidHeight = 0.25f;
    assert(PlayerOverlapsWorld((Vector3){ 0.5f, 1.1f, 0.5f }));
    assert(!PlayerOverlapsWorld((Vector3){ 0.5f, 1.26f, 0.5f }));
    assert(PlayerCameraPositionInsideSolid((Vector3){ 0.5f, 1.1f, 0.5f }));
    assert(!PlayerCameraPositionInsideSolid((Vector3){ 0.5f, 1.5f, 0.5f }));
    Player partial = { .position = { 0.5f, 2.0f, 0.5f }, .floating = false };
    MovePlayer(&partial, (Vector3){ 0.0f, -2.0f, 0.0f });
    assert(partial.position.y >= 1.25f && partial.position.y < 1.26f);
    partialSolidHeight = 0.0f;
    TestHighSpeedHorizontalMovementStopsAtWall();
    TestHighSpeedVerticalMovementStopsAtCeiling();
    TestSurfaceStreamingFrontierBlocksWalkingPlayer();
    TestShipMayCrossSurfaceStreamingFrontier();
    TestHugeMovementIsBounded();
    TestSubstepsPreserveStepUp();
    TestNonFiniteMovementIsIgnored();
    TestEcologyPlantsDoNotBlockMovement();
    TestInvalidCollisionPositionsAreBlocked();
    TestCameraIsPushedOutOfSolidBlocks();
    TestNeutralActorMotionSolver();
    TestRuntimeStateIsPerPlayer();
    TestRuntimeStateReset();
    TestWaterStateUsesActualColumnSurface();
    TestEnvironmentQueryMatchesPlayerAdapters();
    TestWorldFrameSurfaceRoundTrip();
    InstallPlayerCubeContract();
    TestCubeOverlapBranch();
    TestCubeDecorationBlocksArePassable();
    TestCubeReadinessUsesPhysicalBodyCells();
    TestCubeGroundedPlayerWalks();
    TestCubeHeightSurvivesMovement();
    TestCubeMoveIsPhysical();
    TestCameraCubePointUsesPhysicalCell();
    assert(WorldInstallSurfaceSnapshot(NULL));
    WorldSetCubeRuntimeEnabled(false);
    WorldResetTopologyPolicy();
    TestSwimmingSpeedAndAscent();
    puts("player collision tests passed");
    return 0;
}
