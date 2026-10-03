#include "app/game_geology.h"
#include "app/game_internal.h"
#include "app/game_debug.h"
#include "presentation/render_volcano.h"
#include "space/planet_surface.h"
#include "world/volcano_runtime.h"
#include "world/world_surface_snapshot.h"
#include "world/world.h"
#include "world/terrain.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool SampleSite(SurfaceAddress address, float *potential, int *height, void *opaque)
{
    (void)opaque;
    const WorldSurfaceSnapshot *surface = WorldCurrentSurfaceSnapshot();
    if (surface->context.kind != WORLD_SURFACE_PLANET) return false;
    float longitude, latitude;
    SurfaceAddressLatLon(address, &longitude, &latitude);
    PlanetSurfaceSample sample = PlanetSampleGlobalSurfaceBaseline(
        surface->context.seed, &surface->planetProfile, longitude, latitude);
    SurfaceMapCell cell = VolcanoMapCell(address);
    *height = WorldSurfaceHeightAt(cell.x - surface->context.originX,
                                   cell.z - surface->context.originZ) + 1;
    *potential = sample.volcanicActivity;
    return sample.volcanicCone > 0.02f && *height > PlanetTerrainSeaLevel();
}

static bool SiteReady(const VolcanoDefinition *d, void *opaque)
{
    (void)opaque;
    WorldSurfaceContext surface = WorldCurrentSurfaceContext();
    int x = d->mapX - surface.originX;
    int z = d->mapZ - surface.originZ;
    int y = d->address.radial;
    BlockType edit;
    if (!d->forced && (WorldGetBlockEditForCurrentDimensionAt(x, y, z, &edit) ||
        WorldGetBlockEditForCurrentDimensionAt(x, y - 1, z, &edit))) return false;
    return SurfaceBlockReadyAt(x, y - 1, z) && SurfaceBlockReadyAt(x, y, z) &&
        GetBlockAt(x, y, z) == BLOCK_AIR && GetBlockAt(x, y - 1, z) != BLOCK_AIR;
}

static VolcanoContext ContextFor(const GameRuntime *game)
{
    WorldSurfaceContext surface = WorldCurrentSurfaceContext();
    return (VolcanoContext){
        .valid = game && game->screen == SCREEN_PLAYING && surface.active &&
                 !WorldNetherIsActive() && !game->perfMode,
        .discover = surface.kind == WORLD_SURFACE_PLANET,
        .bodyId = surface.bodyId, .seed = surface.seed,
        .observerMap = game ? (Vector3){ game->player.position.x + surface.originX,
            game->player.position.y, game->player.position.z + surface.originZ } : (Vector3){ 0 },
        .sample = SampleSite, .ready = SiteReady
    };
}

void GameGeologyUpdate(GameRuntime *game, double seconds)
{
    VolcanoContext context = ContextFor(game);
    VolcanoRuntimeUpdate(seconds, &context);
}

void GameGeologyDraw(const GameRuntime *game, const GameFrameView *frame)
{
    VolcanoContext context = ContextFor(game);
    if (!context.valid) return;
    VolcanoVisual visuals[VOLCANO_MAX_RECORDS];
    unsigned count = 0u;
    VolcanoRecord record;
    for (unsigned i = 0u; VolcanoRuntimeAt(i, &record); i++) {
        if (record.definition.address.bodyId != context.bodyId ||
            !SiteReady(&record.definition, NULL)) continue;
        SurfaceMapOffset offset = SurfaceShortestMapOffset(context.observerMap.x,
            context.observerMap.z, record.definition.mapX, record.definition.mapZ);
        if (hypotf(offset.x, offset.z) > VOLCANO_LEAVE_RADIUS) continue;
        visuals[count++] = (VolcanoVisual){ .position = {
            game->player.position.x + offset.x, (float)record.definition.address.radial,
            game->player.position.z + offset.z }, .state = record.state };
    }
    DrawVolcanoPreviews(visuals, count, frame->weatherVisual.windAngle,
                        frame->weatherVisual.windDrift);
}

static void ReplyVent(GameRuntime *game, const VolcanoRecord *r)
{
    DebugControlReply(&game->debugControl,
        "DEBUG_CONTROL geology vent id=%016" PRIx64 " body=%u position=%d,%d,%d "
        "phase=%s event=%u active=%d ticks=%" PRIu64 " pressure=%.6f reservoir=%.3f preview=1\n",
        r->definition.id, r->definition.address.bodyId, r->definition.mapX,
        r->definition.address.radial, r->definition.mapZ, VolcanoPhaseName(r->state.phase),
        r->state.event, r->active, r->state.activeTicks, r->state.pressure, r->state.reservoir);
}

bool GameDebugGeologyDispatch(GameRuntime *game, DebugControlCommand command)
{
    if (command < DEBUG_CONTROL_COMMAND_GEOLOGY_INSPECT ||
        command > DEBUG_CONTROL_COMMAND_GEOLOGY_STEP) return false;
    VolcanoContext context = ContextFor(game);
    bool ok = context.valid;
    if (ok && command == DEBUG_CONTROL_COMMAND_GEOLOGY_FORCE) {
        SurfaceMapCell cell = SurfaceCanonicalMapCell(
            context.observerMap.x + 24.0f, context.observerMap.z);
        WorldSurfaceContext surface = WorldCurrentSurfaceContext();
        int height = WorldSurfaceHeightAt(cell.x - surface.originX,
                                          cell.z - surface.originZ) + 1;
        VolcanoDefinition definition = VolcanoDefine(context.bodyId, context.seed,
            cell.x, height, cell.z, 1.0f, true);
        ok = SiteReady(&definition, NULL) && VolcanoRuntimeForce(definition,
            game->debugControl.geologyIntensity, game->debugControl.geologyTicks);
        VolcanoRuntimeUpdate(0.0, &context);
    } else if (ok && command == DEBUG_CONTROL_COMMAND_GEOLOGY_STOP) {
        uint64_t id = game->debugControl.geologyId;
        ok = (uint32_t)(id >> 32) == context.bodyId && VolcanoRuntimeStop(id);
    } else if (ok && command == DEBUG_CONTROL_COMMAND_GEOLOGY_STEP) {
        for (unsigned i = 0u; i < game->debugControl.geologyTicks; i++) {
            VolcanoRuntimeUpdate(VOLCANO_STEP_SECONDS, &context);
        }
    }
    if (!ok) {
        game->debugCommandFailed = true;
        snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure),
                 "geology_unavailable_or_invalid_state");
    }
    VolcanoStats stats = VolcanoRuntimeStats();
    DebugControlReply(&game->debugControl,
        "DEBUG_CONTROL geology %s records=%u active=%u rejected=%" PRIu64 " preview=1\n",
        ok ? "ok" : "error", stats.count, stats.active, stats.rejectedAdmissions);
    VolcanoRecord record;
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_VENTS) {
        for (unsigned i = 0u; VolcanoRuntimeAt(i, &record); i++) ReplyVent(game, &record);
    } else if (VolcanoRuntimeNearest(context.bodyId, context.observerMap, &record)) {
        ReplyVent(game, &record);
    }
    return true;
}

bool GameDebugGeologyResolve(GameRuntime *game, const char *name, DebugDslValue *out)
{
    if (strncmp(name, "geology.", 8u) != 0) return false;
    const char *field = name + 8;
    VolcanoContext context = ContextFor(game);
    VolcanoRecord record = { 0 };
    bool present = context.valid && VolcanoRuntimeNearest(context.bodyId, context.observerMap, &record);
    if (!strcmp(field, "present") || !strcmp(field, "active")) {
        out->type = DEBUG_DSL_VALUE_BOOL;
        out->as.boolean = !strcmp(field, "present") ? present : present && record.active;
        return true;
    }
    if (!strcmp(field, "phase") || !strcmp(field, "id")) {
        out->type = DEBUG_DSL_VALUE_STRING;
        if (!strcmp(field, "phase")) {
            out->as.string = present ? VolcanoPhaseName(record.state.phase) : "none";
        } else {
            static char idText[17];
            snprintf(idText, sizeof(idText), "%016" PRIx64,
                     present ? record.definition.id : UINT64_C(0));
            out->as.string = idText;
        }
        return true;
    }
    if (!strcmp(field, "position")) {
        out->type = DEBUG_DSL_VALUE_VEC3;
        out->as.vec3 = (DebugDslVec3){ record.definition.mapX,
            record.definition.address.radial, record.definition.mapZ };
        return true;
    }
    VolcanoStats stats = VolcanoRuntimeStats();
    double value;
    if (!strcmp(field, "records")) value = stats.count;
    else if (!strcmp(field, "active_count")) value = stats.active;
    else if (!strcmp(field, "rejected")) value = (double)stats.rejectedAdmissions;
    else if (!strcmp(field, "event")) value = record.state.event;
    else if (!strcmp(field, "ticks")) value = (double)record.state.activeTicks;
    else if (!strcmp(field, "pressure")) value = record.state.pressure;
    else if (!strcmp(field, "reservoir")) value = record.state.reservoir;
    else return false;
    out->type = DEBUG_DSL_VALUE_NUMBER;
    out->as.number = value;
    return true;
}
