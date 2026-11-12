#include "app/game_debug_topology.h"

#include "app/game_runtime.h"
#include "core/debug_control.h"
#include "gameplay/player.h"
#include "world/chunks.h"
#include "world/surface_topology.h"
#include "presentation/homeworld_map.h"
#include "world/world_state.h"
#include "app/game_world_transition.h"
#include "app/game_internal.h"
#include "world/world_surface_snapshot.h"
#include "world/surface_contract.h"
#include "world/surface_cube.h"
#include "world/surface_pose.h"

#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

typedef struct GameDebugTopologyInfo {
    uint32_t bodyId;
    Vector2 canonical;
    SurfaceMapProjection projection;
    SurfaceMapCell canonicalCell;
    bool longitudeAlias;
    bool northPoleAlias;
    bool southPoleAlias;
    ChunkCanonicalIdentityStats chunks;
    WorldSurfaceRebaseEvent lastRebase;
} GameDebugTopologyInfo;

static bool SurfaceAliasMatches(uint32_t bodyId, Vector2 canonical,
                                float aliasX, float aliasZ)
{
    SurfaceSpatialKey expected = SurfaceSpatialKeyFromMapCoordinates(
        bodyId, canonical.x, canonical.y, 0);
    SurfaceSpatialKey alias = SurfaceSpatialKeyFromMapCoordinates(
        bodyId, aliasX, aliasZ, 0);
    return SurfaceSpatialKeyEqual(expected, alias);
}

static GameDebugTopologyInfo TopologyAt(const GameRuntime *game)
{
    float mapX = (float)WorldSurfaceMapOriginX() + game->player.position.x;
    float mapZ = (float)WorldSurfaceMapOriginZ() + game->player.position.z;
    SurfaceMapProjection projection = SurfaceProjectMapCoordinates(mapX, mapZ);
    Vector2 canonical = SurfaceCanonicalMapPosition(mapX, mapZ, NULL);
    SurfaceMapCell canonicalCell = SurfaceCanonicalMapCell(mapX, mapZ);
    const float halfEquator = (float)SURFACE_EQUATOR_BLOCKS * 0.5f;
    const float poleToPole = (float)SURFACE_POLE_TO_POLE_BLOCKS;
    uint32_t bodyId = WorldCurrentSurfaceId();
    WorldSurfaceRebaseEvent lastRebase = WorldLastSurfaceRebaseEvent();
    if (lastRebase.bodyId != bodyId) lastRebase.valid = false;
    return (GameDebugTopologyInfo){
        .bodyId = bodyId,
        .canonical = canonical,
        .projection = projection,
        .canonicalCell = canonicalCell,
        .longitudeAlias = SurfaceAliasMatches(
            bodyId, canonical,
            canonical.x + (float)SURFACE_EQUATOR_BLOCKS, canonical.y),
        .northPoleAlias = SurfaceAliasMatches(
            bodyId, canonical, canonical.x + halfEquator,
            poleToPole - canonical.y),
        .southPoleAlias = SurfaceAliasMatches(
            bodyId, canonical, canonical.x + halfEquator,
            -poleToPole - canonical.y),
        .chunks = ChunksGetCanonicalIdentityStats(),
        .lastRebase = lastRebase
    };
}

static bool DslBool(DebugDslValue *outValue, bool value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_BOOL,
        .as.boolean = value
    };
    return true;
}

static bool DslNumber(DebugDslValue *outValue, double value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_NUMBER,
        .as.number = value
    };
    return true;
}

static bool DslVec3(DebugDslValue *outValue, Vector3 value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_VEC3,
        .as.vec3 = { value.x, value.y, value.z }
    };
    return true;
}

void GameDebugTopologyReply(GameRuntime *game)
{
    GameDebugTopologyInfo info = TopologyAt(game);
    if (!info.lastRebase.valid) {
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL world topology ok body=%u canonical=%.6f,%.6f "
            "longitude=%.9f latitude=%.9f north_sign=%.0f "
            "aliases=longitude:%d,north:%d,south:%d "
            "loaded_canonical_chunks=%d duplicate_canonical_chunks=%d "
            "last_rebase=none\n",
            info.bodyId, info.canonical.x, info.canonical.y,
            info.projection.longitude, info.projection.latitude,
            info.projection.northDirection,
            info.longitudeAlias ? 1 : 0, info.northPoleAlias ? 1 : 0,
            info.southPoleAlias ? 1 : 0, info.chunks.unique,
            info.chunks.duplicates);
        return;
    }
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL world topology ok body=%u canonical=%.6f,%.6f "
        "longitude=%.9f latitude=%.9f north_sign=%.0f "
        "aliases=longitude:%d,north:%d,south:%d "
        "loaded_canonical_chunks=%d duplicate_canonical_chunks=%d "
        "last_rebase=sequence:%llu,from:%.6f,%.6f,to:%.6f,%.6f,north:%.0f\n",
        info.bodyId, info.canonical.x, info.canonical.y,
        info.projection.longitude, info.projection.latitude,
        info.projection.northDirection,
        info.longitudeAlias ? 1 : 0, info.northPoleAlias ? 1 : 0,
        info.southPoleAlias ? 1 : 0, info.chunks.unique,
        info.chunks.duplicates,
        (unsigned long long)info.lastRebase.sequence,
        info.lastRebase.previous.x, info.lastRebase.previous.y,
        info.lastRebase.canonical.x, info.lastRebase.canonical.y,
        info.lastRebase.northDirection);
}

bool GameDebugTopologyDslResolve(const GameRuntime *game, const char *name,
                                 DebugDslValue *outValue)
{
    if (!game || !name || !outValue ||
        (strncmp(name, "world.", 6u) != 0 &&
         strncmp(name, "weather.", 8u) != 0 &&
         strncmp(name, "ecology.", 8u) != 0)) {
        return false;
    }
    if (strcmp(name, "world.surface_topology") == 0 ||
        strcmp(name, "world.surface_topology_version") == 0) {
        SurfaceTopologyVersion topology = WorldCurrentSurfaceContext().topology;
        // Nether is backed by Home terrain but is not a surface dimension.
        if (!WorldIsSurfaceDimension(WorldCurrentDimension())) {
            topology = (SurfaceTopologyVersion){ SURFACE_TOPOLOGY_NONE, 0u };
        }
        if (strcmp(name, "world.surface_topology_version") == 0) {
            return DslNumber(outValue, topology.version);
        }
        *outValue = (DebugDslValue){ .type = DEBUG_DSL_VALUE_STRING };
        outValue->as.string = SurfaceTopologyName(topology);
        return true;
    }
    GameDebugTopologyInfo topology = TopologyAt(game);
    if (strcmp(name, "weather.canonical_cell") == 0 ||
        strcmp(name, "ecology.canonical_cell") == 0) {
        return DslVec3(outValue, (Vector3){
            (float)topology.canonicalCell.x, game->player.position.y,
            (float)topology.canonicalCell.z
        });
    }
    if (strcmp(name, "ecology.region_x") == 0) {
        return DslNumber(outValue, floorf(
            (float)topology.canonicalCell.x / 64.0f));
    }
    if (strcmp(name, "ecology.region_z") == 0) {
        return DslNumber(outValue, floorf(
            (float)topology.canonicalCell.z / 64.0f));
    }
    if (strcmp(name, "world.surface_body") == 0) {
        return DslNumber(outValue, topology.bodyId);
    }
    if (strcmp(name, "world.longitude") == 0) {
        return DslNumber(outValue, topology.projection.longitude);
    }
    if (strcmp(name, "world.latitude") == 0) {
        return DslNumber(outValue, topology.projection.latitude);
    }
    if (strcmp(name, "world.north_sign") == 0) {
        return DslNumber(outValue, topology.projection.northDirection);
    }
    if (strcmp(name, "world.canonical_position") == 0) {
        return DslVec3(outValue,
                       (Vector3){ topology.canonical.x,
                                  game->player.position.y,
                                  topology.canonical.y });
    }
    if (strcmp(name, "world.loaded_canonical_chunks") == 0) {
        return DslNumber(outValue, topology.chunks.unique);
    }
    if (strcmp(name, "world.duplicate_canonical_chunks") == 0) {
        return DslNumber(outValue, topology.chunks.duplicates);
    }
    if (strcmp(name, "world.longitude_alias") == 0) {
        return DslBool(outValue, topology.longitudeAlias);
    }
    if (strcmp(name, "world.north_pole_alias") == 0) {
        return DslBool(outValue, topology.northPoleAlias);
    }
    if (strcmp(name, "world.south_pole_alias") == 0) {
        return DslBool(outValue, topology.southPoleAlias);
    }
    if (strcmp(name, "world.last_rebase") == 0) {
        return DslBool(outValue, topology.lastRebase.valid);
    }
    if (strcmp(name, "world.last_rebase_from") == 0) {
        Vector2 position = topology.lastRebase.previous;
        return DslVec3(outValue, (Vector3){ position.x, 0.0f, position.y });
    }
    if (strcmp(name, "world.last_rebase_to") == 0) {
        Vector2 position = topology.lastRebase.canonical;
        return DslVec3(outValue, (Vector3){ position.x, 0.0f, position.y });
    }
    if (strcmp(name, "world.last_rebase_north_sign") == 0) {
        return DslNumber(outValue, topology.lastRebase.northDirection);
    }
    if (strcmp(name, "world.rebase_count") == 0) {
        return DslNumber(outValue, topology.lastRebase.valid
            ? topology.lastRebase.sequence : 0u);
    }
    return false;
}

bool GameDebugEnterCubeWorld(struct GameRuntime *game)
{
    if (!game) return false;
    Vector3 previousPosition = game->player.position;
    float previousYaw = game->player.yaw;
    float previousPitch = game->player.pitch;
    int previousOriginX = WorldSurfaceMapOriginX();
    int previousOriginZ = WorldSurfaceMapOriginZ();
    bool previousGate = WorldCubeRuntimeEnabled();
    WorldSurfaceTopologyPolicy previousPolicy = WorldCurrentTopologyPolicy();
    WorldSetCubeRuntimeEnabled(true);
    WorldSurfaceTopologyPolicy mixedPolicy = {
        { SURFACE_TOPOLOGY_CUBE_GRID, SURFACE_CUBE_TOPOLOGY_VERSION },
        SURFACE_TOPOLOGY_MAP_V1
    };
    if (!WorldInstallTopologyPolicy(mixedPolicy)) {
        WorldSetCubeRuntimeEnabled(previousGate);
        return false;
    }
    HomeWorldMapClose();
    if (!GameWorldTransitionDebugSurface(
            &game->player, true, SOLAR_STYLE_TEMPERATE,
            game->debugControl.surfaceDebugSeed)) {
        /* Restore the exact policy this debug command replaced. */
        (void)WorldInstallTopologyPolicy(previousPolicy);
        WorldSetCubeRuntimeEnabled(previousGate);
        return false;
    }
    SurfaceMapProjection previousProjection = SurfaceProjectMapCoordinates(
        (float)previousOriginX + previousPosition.x,
        (float)previousOriginZ + previousPosition.z);
    SurfacePose restoredPose;
    Vector3 restoredLocal;
    if (SurfacePoseFromGeographic(
            WorldCurrentSurfaceId(), previousProjection.longitude,
            previousProjection.latitude, previousPosition.y,
            previousYaw, previousPitch, &restoredPose) &&
        PlayerLocalFromSurfacePose(&restoredPose, &restoredLocal)) {
        game->player.position = restoredLocal;
        game->player.velocity = (Vector3){ 0 };
        game->player.yaw = previousYaw;
        game->player.pitch = previousPitch;
        game->player.onGround = false;
        PlayerResetRuntimeState(&game->player);
    }
    game->landingTransition = (LandingTransition){ 0 };
    game->wasInSpace = false;
    game->entitiesWorldActive = true;
    game->entitiesWorldDimension = WorldCurrentSurfaceId();
    game->cursorReleased = false;
    DisableCursor();
    return true;
}

bool GameDebugTransitionSurface(GameRuntime *game, bool homeWorld,
                                SolarBodyStyle style, uint32_t seed)
{
    if (!game) return false;
    WorldSurfaceTopologyPolicy previousPolicy = WorldCurrentTopologyPolicy();
    bool previousGate = WorldCubeRuntimeEnabled();
    WorldSurfaceTopologyPolicy nextPolicy = previousPolicy;
    if (homeWorld) {
        /* Homeworld has no legacy backend anymore. */
        nextPolicy.home =
            (SurfaceTopologyVersion)SURFACE_TOPOLOGY_CUBE_V1;
        WorldSetCubeRuntimeEnabled(true);
    } else {
        nextPolicy.planets = (SurfaceTopologyVersion)SURFACE_TOPOLOGY_MAP_V1;
    }
    if (!WorldInstallTopologyPolicy(nextPolicy)) {
        WorldSetCubeRuntimeEnabled(previousGate);
        return false;
    }
    HomeWorldMapClose();
    if (!GameWorldTransitionDebugSurface(&game->player, homeWorld, style, seed)) {
        (void)WorldInstallTopologyPolicy(previousPolicy);
        WorldSetCubeRuntimeEnabled(previousGate);
        return false;
    }
    return true;
}

bool GameDebugSetSurfacePose(GameRuntime *game)
{
    if (!game || !WorldIsSurfaceActive()) return false;
    const DebugControl *control = &game->debugControl;
    SurfacePose pose;
    if (!SurfacePoseFromGeographic(
            WorldCurrentSurfaceId(), control->surfacePoseLongitude,
            control->surfacePoseLatitude, control->surfacePoseRadial,
            control->surfacePoseYaw, control->surfacePosePitch, &pose)) {
        return false;
    }
    Vector3 local;
    if (!PlayerLocalFromSurfacePose(&pose, &local)) return false;
    game->player.position = local;
    game->player.velocity = (Vector3){ 0 };
    game->player.yaw = (float)control->surfacePoseYaw;
    game->player.pitch = (float)control->surfacePosePitch;
    game->player.onGround = false;
    PlayerResetRuntimeState(&game->player);
    game->scriptedInputFrames = 0u;
    game->scriptedInputFirstFrame = false;
    GameDebugTraceEvent(game, "surface-pose");
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL surface pose ok position=%.6f,%.6f,%.6f "
        "longitude=%.9f latitude=%.9f radial=%.6f\n",
        local.x, local.y, local.z, control->surfacePoseLongitude,
        control->surfacePoseLatitude, control->surfacePoseRadial);
    return true;
}

bool GameDebugSetSurfaceAddress(GameRuntime *game)
{
    if (!game || !WorldIsSurfaceActive()) return false;
    const SurfaceContract *contract = WorldCurrentSurfaceContract();
    const DebugControl *control = &game->debugControl;
    if (!contract || strcmp(SurfaceContractBackendName(contract), "cube-grid") != 0 ||
        control->surfaceAddressFace < 0 ||
        control->surfaceAddressFace >= SURFACE_FACE_COUNT ||
        control->surfaceAddressU < 0 ||
        control->surfaceAddressU >= SURFACE_FACE_BLOCKS ||
        control->surfaceAddressV < 0 ||
        control->surfaceAddressV >= SURFACE_FACE_BLOCKS ||
        !isfinite(control->surfaceAddressAltitude)) return false;
    SurfaceAddress address = {
        WorldCurrentSurfaceId(), (SurfaceFace)control->surfaceAddressFace,
        control->surfaceAddressU, control->surfaceAddressV, 0
    };
    SurfaceContractSample sample;
    if (!SurfaceContractSampleAt(contract, address, &sample)) return false;
    double absoluteRadial = (double)sample.elevation +
        control->surfaceAddressAltitude;
    if (!isfinite(absoluteRadial) ||
        absoluteRadial < INT_MIN || absoluteRadial > INT_MAX) return false;
    address.radial = (int)floor(absoluteRadial);
    SurfaceChunkHandle chunk;
    if (!SurfaceContractChunkFromAddress(contract, address, &chunk)) return false;
    SurfacePose pose;
    if (!SurfacePoseFromAddress(address, control->surfaceAddressYaw,
                                control->surfaceAddressPitch, &pose)) return false;
    Vector3 local;
    if (!PlayerLocalFromSurfacePose(&pose, &local)) return false;
    game->player.position = local;
    game->player.velocity = (Vector3){ 0 };
    game->player.yaw = (float)control->surfaceAddressYaw;
    game->player.pitch = (float)control->surfaceAddressPitch;
    game->player.onGround = false;
    PlayerResetRuntimeState(&game->player);
    game->scriptedInputFrames = 0u;
    game->scriptedInputFirstFrame = false;
    GameDebugTraceEvent(game, "surface-address");
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL surface address ok face=%d u=%d v=%d radial=%d "
        "altitude=%.6f sample_elevation=%.6f position=%.6f,%.6f,%.6f\n",
        control->surfaceAddressFace, control->surfaceAddressU,
        control->surfaceAddressV, address.radial,
        control->surfaceAddressAltitude, sample.elevation,
        local.x, local.y, local.z);
    return true;
}
