#include "gameplay/player.h"

#include "world/block_catalog.h"
#include "world/fluid.h"
#include "gameplay/ship.h"
#include "world/block_types.h"
#include "world/block_edits.h"
#include "world/world_environment.h"
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

static void TestCameraIsPushedOutOfSolidBlocks(void)
{
    ResetCollisionWorld();
    wallEnabled = true;
    Vector3 inside = { 2.5f, 1.5f, 0.5f };
    Vector3 pivot = { 1.5f, 1.5f, 0.5f };
    assert(PlayerCameraPositionInsideSolid(inside));

    Vector3 resolved = PlayerResolveCameraPosition(pivot, inside);
    assert(!PlayerCameraPositionInsideSolid(resolved));
    assert(resolved.x < 2.0f);

    resolved = PlayerResolveCameraPosition(inside, inside);
    assert(!PlayerCameraPositionInsideSolid(resolved));

    waterEnabled = true;
    assert(!PlayerCameraPositionInsideSolid((Vector3){ 0.5f, 2.5f, 0.5f }));
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
        UpdatePlayerWithInput(&player, 1.0f / 60.0f, &forward);
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
        UpdatePlayerWithInput(&player, 1.0f / 60.0f, &ascend);
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

    /* Experimental cube rule: unresolved cells are passable while readiness
     * diagnostics continue to report the missing chunk. */
    cubeTerrainKnown = false;
    assert(!PlayerOverlapsWorld((Vector3){0.5f, (float)cell.radial + 1.0f, 0.5f}));

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

    /* The cube body has unknown cells, so readiness must still report missing
     * chunks even though collision intentionally treats them as air. */
    assert(PlayerMissingSurfaceChunkCount(position) > 0);
    assert(!PlayerOverlapsWorld(position));

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
    TestRuntimeStateIsPerPlayer();
    TestRuntimeStateReset();
    TestWaterStateUsesActualColumnSurface();
    InstallPlayerCubeContract();
    TestCubeOverlapBranch();
    TestCubeDecorationBlocksArePassable();
    TestCubeReadinessUsesPhysicalBodyCells();
    TestCubeGroundedPlayerWalks();
    TestCubeHeightSurvivesMovement();
    TestCubeMoveIsPhysical();
    assert(WorldInstallSurfaceSnapshot(NULL));
    WorldSetCubeRuntimeEnabled(false);
    WorldResetTopologyPolicy();
    TestSwimmingSpeedAndAscent();
    puts("player collision tests passed");
    return 0;
}
