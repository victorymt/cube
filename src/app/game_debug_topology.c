#include "app/game_debug_topology.h"

#include "app/game_runtime.h"
#include "core/debug_control.h"
#include "world/chunks.h"
#include "world/surface_topology.h"
#include "world/world.h"

#include <stdint.h>
#include <string.h>

typedef struct GameDebugTopologyInfo {
    uint32_t bodyId;
    Vector2 canonical;
    SurfaceMapProjection projection;
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
    const float halfEquator = (float)SURFACE_EQUATOR_BLOCKS * 0.5f;
    const float poleToPole = (float)SURFACE_POLE_TO_POLE_BLOCKS;
    uint32_t bodyId = WorldCurrentSurfaceId();
    WorldSurfaceRebaseEvent lastRebase = WorldLastSurfaceRebaseEvent();
    if (lastRebase.bodyId != bodyId) lastRebase.valid = false;
    return (GameDebugTopologyInfo){
        .bodyId = bodyId,
        .canonical = canonical,
        .projection = projection,
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
    if (!game || !name || !outValue || strncmp(name, "world.", 6u) != 0) {
        return false;
    }
    GameDebugTopologyInfo topology = TopologyAt(game);
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
