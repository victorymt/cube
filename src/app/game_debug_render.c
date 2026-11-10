#include "app/game_debug_render.h"

#include "app/game_runtime.h"
#include "gameplay/interaction.h"
#include "world/flora_taxa.h"
#include "world/block_edits.h"
#include "world/block_types.h"
#include "world/chunks.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void RenderPickError(GameRuntime *game, const char *reason)
{
    game->debugCommandFailed = true;
    snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure),
             "%s", reason);
}

static float RenderPickCenterDistance(Vector3 origin, Vector3 direction,
                                      const HitResult *hit)
{
    Vector3 center = {
        (float)hit->x + 0.5f,
        (float)hit->y + 0.5f,
        (float)hit->z + 0.5f
    };
    float dx = center.x - origin.x;
    float dy = center.y - origin.y;
    float dz = center.z - origin.z;
    float distance = dx * direction.x + dy * direction.y + dz * direction.z;
    return isfinite(distance) && distance > 0.0f ? distance : 0.0f;
}

static const char *RenderFloraKindName(FloraStructureKind kind)
{
    switch (kind) {
    case FLORA_STRUCTURE_ALIEN_CANOPY: return "alien_canopy";
    case FLORA_STRUCTURE_CRYSTAL: return "crystal";
    case FLORA_STRUCTURE_SPORE: return "spore";
    case FLORA_STRUCTURE_THERMAL_VENT: return "thermal_vent";
    case FLORA_STRUCTURE_HOME_TREE: return "home_tree";
    }
    return "unknown";
}

static const char *RenderFloraTaxonName(FloraTaxonId id)
{
    const FloraTaxon *taxon = FloraTaxonAt(id);
    return taxon ? taxon->commonName : "none";
}

static const Chunk *RenderFloraOwnerChunk(int x, int y, int z,
                                          BlockType block, int *outOwner)
{
    if (outOwner) *outOwner = -1;
    const Chunk *chunks = ChunksView();
    for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
        const Chunk *chunk = &chunks[index];
        if (!chunk->loaded) continue;
        int owner = ChunkFloraStructureOwnerAtBlock(chunk, x, y, z, block);
        if (owner < 0) continue;
        if (outOwner) *outOwner = owner;
        return chunk;
    }
    return NULL;
}

static void RenderPick(GameRuntime *game)
{
    int screenX = game->debugControl.renderPickX;
    int screenY = game->debugControl.renderPickY;
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    game->renderPickActive = true;
    game->renderPickHit = false;
    game->renderPickLiquid = false;
    game->renderPickBlock = BLOCK_AIR;
    game->renderPickFlora = false;
    game->renderPickFloraRoot = (Vector3){ 0 };
    game->renderPickFloraKind = FLORA_STRUCTURE_ALIEN_CANOPY;
    game->renderPickFloraTaxon = FLORA_TAXON_COUNT;
    game->renderPickPosition = (Vector3){ 0 };
    game->renderPickNormal = (Vector3){ 0 };
    game->renderPickDistance = 0.0f;
    game->renderPickScreenX = screenX;
    game->renderPickScreenY = screenY;
    if (screenX < 0 || screenY < 0 || screenX >= screenWidth ||
        screenY >= screenHeight) {
        RenderPickError(game, "render_pick_out_of_bounds");
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL render pick error reason=out_of_bounds "
            "screen=%d,%d viewport=%d,%d\n",
            screenX, screenY, screenWidth, screenHeight);
        return;
    }

    Ray ray = GetScreenToWorldRay((Vector2){ (float)screenX, (float)screenY },
                                  game->camera);
    float maxDistance = (float)(MAX_RENDER_DISTANCE_CHUNKS * CHUNK_SIZE +
                                CHUNK_SIZE * 2);
    HitResult hit = RaycastBlocksFiltered(
        ray.position, ray.direction, maxDistance, RAYCAST_BLOCK_ALL);
    game->renderPickRayOrigin = ray.position;
    game->renderPickRayDirection = ray.direction;
    game->renderPickHit = hit.hit;
    if (hit.hit) {
        game->renderPickPosition = (Vector3){
            (float)hit.x, (float)hit.y, (float)hit.z
        };
        game->renderPickNormal = (Vector3){
            (float)hit.nx, (float)hit.ny, (float)hit.nz
        };
        game->renderPickDistance = RenderPickCenterDistance(
            ray.position, ray.direction, &hit);
        game->renderPickBlock = GetBlockAt(hit.x, hit.y, hit.z);
        game->renderPickLiquid = IsLiquidBlock(game->renderPickBlock);
        int owner = -1;
        const Chunk *chunk = RenderFloraOwnerChunk(
            hit.x, hit.y, hit.z, game->renderPickBlock, &owner);
        if (chunk && owner >= 0) {
            const FloraStructureInstance *structure =
                &chunk->floraStructures[owner];
            game->renderPickFlora = true;
            game->renderPickFloraRoot = (Vector3){
                (float)structure->rootX,
                (float)structure->groundY,
                (float)structure->rootZ
            };
            game->renderPickFloraKind = structure->kind;
            game->renderPickFloraTaxon = (FloraTaxonId)structure->taxonId;
        }
    }
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL render pick ok screen=%d,%d viewport=%d,%d "
        "ray_origin=%.3f,%.3f,%.3f ray_direction=%.6f,%.6f,%.6f "
        "hit=%d position=%d,%d,%d normal=%d,%d,%d distance=%.3f "
        "block=\"%s\" block_id=%d liquid=%d flora=%d flora_kind=%s "
        "flora_taxon=\"%s\" flora_root=%.3f,%.3f,%.3f max_distance=%.1f\n",
        screenX, screenY, screenWidth, screenHeight,
        ray.position.x, ray.position.y, ray.position.z,
        ray.direction.x, ray.direction.y, ray.direction.z,
        hit.hit ? 1 : 0,
        hit.x, hit.y, hit.z, hit.nx, hit.ny, hit.nz,
        game->renderPickDistance, BlockName(game->renderPickBlock),
        (int)game->renderPickBlock, game->renderPickLiquid ? 1 : 0,
        game->renderPickFlora ? 1 : 0,
        RenderFloraKindName(game->renderPickFloraKind),
        game->renderPickFlora ?
            RenderFloraTaxonName(game->renderPickFloraTaxon) : "none",
        game->renderPickFloraRoot.x, game->renderPickFloraRoot.y,
        game->renderPickFloraRoot.z,
        maxDistance);
}

bool GameDebugRenderDispatch(GameRuntime *game, DebugControlCommand command)
{
    if (!game) return false;
    if (command == DEBUG_CONTROL_COMMAND_RENDER_PICK) {
        RenderPick(game);
        return true;
    }
    return false;
}

static bool RenderDslNumber(DebugDslValue *outValue, double value)
{
    *outValue = (DebugDslValue){ .type = DEBUG_DSL_VALUE_NUMBER,
                                 .as.number = value };
    return true;
}

static bool RenderDslBool(DebugDslValue *outValue, bool value)
{
    *outValue = (DebugDslValue){ .type = DEBUG_DSL_VALUE_BOOL,
                                 .as.boolean = value };
    return true;
}

static bool RenderDslString(DebugDslValue *outValue, const char *value)
{
    *outValue = (DebugDslValue){ .type = DEBUG_DSL_VALUE_STRING,
                                 .as.string = value ? value : "none" };
    return true;
}

static bool RenderDslVec3(DebugDslValue *outValue, Vector3 value)
{
    *outValue = (DebugDslValue){ .type = DEBUG_DSL_VALUE_VEC3,
                                 .as.vec3 = { value.x, value.y, value.z } };
    return true;
}

bool GameDebugRenderDslResolve(GameRuntime *game, const char *name,
                               DebugDslValue *outValue)
{
    if (!game || !name || !outValue) return false;
    if (strcmp(name, "render.pick.active") == 0) {
        return RenderDslBool(outValue, game->renderPickActive);
    }
    if (strcmp(name, "render.pick.hit") == 0) {
        return RenderDslBool(outValue, game->renderPickActive &&
                             game->renderPickHit);
    }
    if (strcmp(name, "render.pick.screen") == 0) {
        return RenderDslVec3(outValue, (Vector3){
            (float)game->renderPickScreenX,
            (float)game->renderPickScreenY,
            0.0f
        });
    }
    if (strcmp(name, "render.pick.ray_origin") == 0) {
        return RenderDslVec3(outValue, game->renderPickRayOrigin);
    }
    if (strcmp(name, "render.pick.ray_direction") == 0) {
        return RenderDslVec3(outValue, game->renderPickRayDirection);
    }
    if (strcmp(name, "render.pick.position") == 0) {
        return RenderDslVec3(outValue, game->renderPickPosition);
    }
    if (strcmp(name, "render.pick.normal") == 0) {
        return RenderDslVec3(outValue, game->renderPickNormal);
    }
    if (strcmp(name, "render.pick.distance") == 0) {
        return RenderDslNumber(outValue, game->renderPickActive &&
                               game->renderPickHit ?
                               game->renderPickDistance : -1.0);
    }
    if (strcmp(name, "render.pick.block") == 0) {
        return RenderDslString(outValue, game->renderPickActive ?
                               BlockName(game->renderPickBlock) : "air");
    }
    if (strcmp(name, "render.pick.liquid") == 0) {
        return RenderDslBool(outValue, game->renderPickActive &&
                             game->renderPickLiquid);
    }
    if (strcmp(name, "render.pick.flora") == 0) {
        return RenderDslBool(outValue, game->renderPickActive &&
                             game->renderPickFlora);
    }
    if (strcmp(name, "render.pick.flora_kind") == 0) {
        return RenderDslString(outValue, game->renderPickActive &&
                               game->renderPickFlora ?
                               RenderFloraKindName(game->renderPickFloraKind) :
                               "none");
    }
    if (strcmp(name, "render.pick.flora_taxon") == 0) {
        return RenderDslString(outValue, game->renderPickFlora ?
                               RenderFloraTaxonName(
                                   game->renderPickFloraTaxon) : "none");
    }
    if (strcmp(name, "render.pick.flora_root") == 0) {
        return RenderDslVec3(outValue, game->renderPickFlora ?
                             game->renderPickFloraRoot : (Vector3){ 0 });
    }
    return false;
}
