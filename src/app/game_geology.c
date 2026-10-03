#include "app/game_geology.h"
#include "app/game_internal.h"
#include "app/game_debug.h"
#include "presentation/render_volcano.h"
#include "presentation/surface_render_transform.h"
#include "space/planet_surface.h"
#include "world/volcano_runtime.h"
#include "world/lava_runtime.h"
#include "world/weather.h"
#include "world/chunks.h"
#include "space/space_state.h"
#include "space/space_runtime.h"
#include "world/world_surface_snapshot.h"
#include "world/world.h"
#include "world/terrain.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static bool solidifying;
typedef struct GeologyTickContext {
    GameRuntime *game;
    VolcanoContext volcano;
    LavaContext lava;
    double weatherTime;
    unsigned activeCount;
    VolcanoDefinition active[VOLCANO_MAX_ACTIVE];
} GeologyTickContext;
static void GameGeologyTick(void *user);

static bool LocalLavaAt(int x, int y, int z, LavaCell *out)
{
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    return s.active && !WorldNetherIsActive() &&
        LavaRuntimeCellAt(s.bodyId, x + s.originX, y, z + s.originZ, out);
}
BlockType GameGeologyBlockAt(int x, int y, int z)
{
    LavaCell c;
    if (!LocalLavaAt(x, y, z, &c)) return BLOCK_AIR;
    return LavaLiquidVolume(c.thermal) > 0u ? BLOCK_LAVA : BLOCK_BASALT;
}
float GameGeologyCollisionHeightAt(int x, int y, int z)
{
    LavaCell c;
    if (!LocalLavaAt(x, y, z, &c)) return 0.0f;
    return (float)(c.thermal.volume - LavaLiquidVolume(c.thermal)) / LAVA_CAPACITY;
}
float GameGeologyLiquidHeightAt(int x, int y, int z)
{
    LavaCell c;
    if (!LocalLavaAt(x, y, z, &c) || !LavaLiquidVolume(c.thermal)) return 0.0f;
    return (float)c.thermal.volume / LAVA_CAPACITY;
}
bool GameGeologyCanSetBlock(int x, int y, int z, BlockType next)
{
    (void)next;
    LavaCell c;
    /* Partial material cannot yield a whole inventory block or be deleted.
       G3 will add explicit collection/displacement. Solidification is the
       only G2 world mutation that may replace an occupied overlay. */
    return solidifying || !LocalLavaAt(x, y, z, &c);
}

static LavaBoundary LavaBoundaryAt(int x, int y, int z, void *user)
{
    GeologyTickContext *tick = user;
    bool active = false;
    for (unsigned i = 0u; i < tick->activeCount; i++) {
        VolcanoDefinition *d = &tick->active[i];
        SurfaceMapOffset delta = SurfaceShortestMapOffset(d->mapX, d->mapZ, x, z);
        if (fabsf(delta.x) <= 96.0f && fabsf(delta.z) <= 96.0f &&
            abs(y - d->address.radial) <= 96) active = true;
    }
    if (!active) return LAVA_UNKNOWN;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    x -= s.originX; z -= s.originZ;
    if (WorldBlockRegionAt(y) != WORLD_BLOCK_REGION_SURFACE ||
        !WorldCanAccessBlockY(y)) return LAVA_BLOCKED;
    int cx, cz, lx, lz;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    const Chunk *chunk = FindChunk(cx, cz);
    if (!chunk || !chunk->loaded ||
        !ChunkTerrainSectionIsResolved(chunk, SurfaceSectionYFromBlockY(y))) return LAVA_UNKNOWN;
    return GetBlockBaseAt(x, y, z) == BLOCK_AIR ? LAVA_OPEN : LAVA_BLOCKED;
}
static double LavaAmbientAt(int x, int z, void *user)
{
    GeologyTickContext *tick = user;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    return WeatherFieldSampleAtWorldTime(x - s.originX, z - s.originZ,
                                         tick->weatherTime).temperatureK;
}
static bool LavaSolidifyAt(int x, int y, int z, void *user)
{
    if (LavaBoundaryAt(x, y, z, user) != LAVA_OPEN) return false;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    solidifying = true;
    bool ok = SetBlockNoUndoFromSource(x - s.originX, y, z - s.originZ,
                                      BLOCK_BASALT, WORLD_MUTATION_ENVIRONMENT);
    solidifying = false;
    return ok;
}
static unsigned EmitLava(const VolcanoDefinition *d, unsigned units, void *user)
{
    GeologyTickContext *tick = user;
    return LavaRuntimeDeposit(&tick->lava, d->mapX, d->address.radial, d->mapZ,
                               units, LAVA_SOURCE_K);
}
static bool HasHotLava(const VolcanoDefinition *d, void *user)
{
    GeologyTickContext *tick = user;
    return LavaRuntimeHasActiveHotNear(&tick->lava, d->mapX, d->address.radial,
                                       d->mapZ, 180.0f);
}

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
    BlockType edit;
    int x = cell.x - surface->context.originX, z = cell.z - surface->context.originZ;
    return sample.volcanicCone > 0.02f && *height > PlanetTerrainSeaLevel() &&
        GetBlockBaseAt(x, *height, z) == BLOCK_AIR &&
        !WorldGetBlockEditForCurrentDimensionAt(x, *height, z, &edit) &&
        !WorldGetBlockEditForCurrentDimensionAt(x, *height - 1, z, &edit);
}

static bool SiteReady(const VolcanoDefinition *d, void *opaque)
{
    (void)opaque;
    WorldSurfaceContext surface = WorldCurrentSurfaceContext();
    int x = d->mapX - surface.originX;
    int z = d->mapZ - surface.originZ;
    int y = d->address.radial;
    return SurfaceBlockReadyAt(x, y - 1, z) && SurfaceBlockReadyAt(x, y, z);
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
    GeologyTickContext tick = { .game = game, .volcano = ContextFor(game),
                               .weatherTime = SpaceElapsedSimulationTime() };
    if (!tick.volcano.valid) {
        VolcanoRuntimeUpdate(0.0, &tick.volcano);
        return;
    }
    LavaRuntimeAdvance(seconds, true, GameGeologyTick, &tick);
}

static void GameGeologyTick(void *user)
{
    GeologyTickContext *tick = user;
    /* Update admission without moving time, then use the same admitted set for
       source discharge and all lava cells in this 0.1-second step. */
    VolcanoRuntimeUpdate(0.0, &tick->volcano);
    tick->activeCount = 0u;
    VolcanoRecord r;
    for (unsigned i = 0u; VolcanoRuntimeAt(i, &r); i++) {
        if (r.active && tick->activeCount < VOLCANO_MAX_ACTIVE) tick->active[tick->activeCount++] = r.definition;
    }
    tick->lava = (LavaContext){ .valid = tick->volcano.valid, .bodyId = tick->volcano.bodyId,
        .boundary = LavaBoundaryAt, .ambient = LavaAmbientAt, .solidify = LavaSolidifyAt, .user = tick };
    tick->volcano.emitLava = EmitLava;
    tick->volcano.hasHotLava = HasHotLava;
    tick->volcano.userData = tick;
    VolcanoRuntimeUpdate(LAVA_STEP_SECONDS, &tick->volcano);
    LavaRuntimeStep(&tick->lava);
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
    LavaCell cell;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    for (unsigned i = 0u; LavaRuntimeAt(i, &cell); i++) {
        if (cell.bodyId != context.bodyId ||
            !SurfaceBlockReadyAt(cell.x - s.originX, cell.y, cell.z - s.originZ) ||
            GetBlockBaseAt(cell.x - s.originX, cell.y, cell.z - s.originZ) != BLOCK_AIR) continue;
        SurfaceMapOffset offset = SurfaceShortestMapOffset(context.observerMap.x,
            context.observerMap.z, cell.x, cell.z);
        if (hypotf(offset.x, offset.z) > VOLCANO_LEAVE_RADIUS) continue;
        int cx, cz, lx, lz;
        WorldToChunkLocal(cell.x - s.originX, cell.z - s.originZ, &cx, &cz, &lx, &lz);
        const Chunk *chunk = FindChunk(cx, cz);
        if (!chunk || !chunk->loaded) continue;
        Matrix transform = SurfaceChunkRenderTransform(chunk, SurfaceSectionYFromBlockY(cell.y),
            game->camera.position, s.originX, s.originZ);
        DrawLavaCell(transform,
            (Vector3){ (float)lx, (float)SurfaceSectionLocalYFromBlockY(cell.y), (float)lz },
            cell.thermal);
    }
}

static void ReplyVent(GameRuntime *game, const VolcanoRecord *r)
{
    DebugControlReply(&game->debugControl,
        "DEBUG_CONTROL geology vent id=%016" PRIx64 " body=%u position=%d,%d,%d "
        "phase=%s event=%u active=%d ticks=%" PRIu64 " pressure=%.6f reservoir=%.6f lava_physics=1\n",
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
        ok = SiteReady(&definition, NULL) &&
            GetBlockBaseAt(cell.x - surface.originX, height, cell.z - surface.originZ) == BLOCK_AIR &&
            VolcanoRuntimeForce(definition,
            game->debugControl.geologyIntensity, game->debugControl.geologyTicks);
        VolcanoRuntimeUpdate(0.0, &context);
    } else if (ok && command == DEBUG_CONTROL_COMMAND_GEOLOGY_STOP) {
        uint64_t id = game->debugControl.geologyId;
        ok = (uint32_t)(id >> 32) == context.bodyId && VolcanoRuntimeStop(id);
    } else if (ok && command == DEBUG_CONTROL_COMMAND_GEOLOGY_STEP) {
        for (unsigned i = 0u; i < game->debugControl.geologyTicks; i++) {
            GameGeologyUpdate(game, VOLCANO_STEP_SECONDS);
        }
    }
    if (!ok) {
        game->debugCommandFailed = true;
        snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure),
                 "geology_unavailable_or_invalid_state");
    }
    VolcanoStats stats = VolcanoRuntimeStats();
    DebugControlReply(&game->debugControl,
        "DEBUG_CONTROL geology %s records=%u active=%u rejected=%" PRIu64 " lava_physics=1\n",
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
    LavaStats lava = LavaRuntimeStats();
    double value;
    if (!strcmp(field, "records")) value = stats.count;
    else if (!strcmp(field, "active_count")) value = stats.active;
    else if (!strcmp(field, "rejected")) value = (double)stats.rejectedAdmissions;
    else if (!strcmp(field, "event")) value = record.state.event;
    else if (!strcmp(field, "ticks")) value = (double)record.state.activeTicks;
    else if (!strcmp(field, "pressure")) value = record.state.pressure;
    else if (!strcmp(field, "reservoir")) value = record.state.reservoir;
    else if (!strcmp(field, "lava_cells")) value = lava.records;
    else if (!strcmp(field, "hot_cells")) value = lava.hotCells;
    else if (!strcmp(field, "lava_volume")) value = (double)lava.volume / LAVA_CAPACITY;
    else if (!strcmp(field, "liquid_volume")) value = (double)lava.liquidVolume / LAVA_CAPACITY;
    else if (!strcmp(field, "solid_volume")) value = (double)lava.solidVolume / LAVA_CAPACITY;
    else if (!strcmp(field, "solidified")) value = (double)lava.solidified / LAVA_CAPACITY;
    else if (!strcmp(field, "supplied")) value = (double)lava.supplied / LAVA_CAPACITY;
    else if (!strcmp(field, "volume_error")) value =
        ((double)lava.supplied - lava.volume - lava.solidified - lava.removed) / LAVA_CAPACITY;
    else if (!strcmp(field, "energy_error")) value =
        lava.suppliedEnergy - lava.energy - lava.cooledEnergy - lava.exportedEnergy;
    else if (!strcmp(field, "lava_ticks")) value = (double)lava.ticks;
    else if (!strcmp(field, "blocked_transfers")) value = (double)lava.blockedTransfers;
    else if (!strcmp(field, "rejected_cells")) value = (double)lava.rejectedCells;
    else if (!strcmp(field, "cell_volume") || !strcmp(field, "cell_liquid") ||
             !strcmp(field, "cell_temperature") || !strcmp(field, "cell_collision_height")) {
        int x = (int)floorf(game->player.position.x);
        int y = (int)floorf(game->player.position.y);
        int z = (int)floorf(game->player.position.z);
        LavaCell cell = { 0 };
        bool exists = context.valid && LocalLavaAt(x, y, z, &cell);
        if (!strcmp(field, "cell_collision_height")) value = BlockCollisionHeightAt(x, y, z);
        else if (!strcmp(field, "cell_volume")) value = exists ? (double)cell.thermal.volume / LAVA_CAPACITY : 0.0;
        else if (!strcmp(field, "cell_liquid")) value = exists ? (double)LavaLiquidVolume(cell.thermal) / LAVA_CAPACITY : 0.0;
        else value = exists ? LavaTemperature(cell.thermal) : 0.0;
    }
    else return false;
    out->type = DEBUG_DSL_VALUE_NUMBER;
    out->as.number = value;
    return true;
}
