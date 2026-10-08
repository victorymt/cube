#include "app/game_debug_volcano_mountain.h"
#include "app/game_debug_deposition.h"
#include "app/game_geology.h"
#include "app/game_internal.h"
#include "app/game_debug.h"
#include "gameplay/inventory.h"
#include "presentation/render_volcano.h"
#include "presentation/surface_render_transform.h"
#include "space/planet_surface.h"
#include "world/volcano_runtime.h"
#include "world/lava_runtime.h"
#include "world/ash_runtime.h"
#include "world/fluid.h"
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
    AshContext ash;
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
static bool LocalAshAt(int x, int y, int z, AshCell *out)
{
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    return s.active && !WorldNetherIsActive() &&
        AshRuntimeCellAt(s.bodyId, x + s.originX, y, z + s.originZ, out);
}
static BlockType RockBlock(LavaRock rock)
{
    switch (rock) {
    case LAVA_ROCK_OBSIDIAN: return BLOCK_OBSIDIAN;
    case LAVA_ROCK_SCORIA: return BLOCK_SCORIA;
    case LAVA_ROCK_PUMICE: return BLOCK_PUMICE;
    case LAVA_ROCK_RHYOLITE: return BLOCK_RHYOLITE;
    default: return BLOCK_BASALT;
    }
}
BlockType GameGeologyBlockAt(int x, int y, int z)
{
    LavaCell c;
    if (LocalLavaAt(x, y, z, &c)) return LavaLiquidVolume(c.thermal) > 0u ? BLOCK_LAVA :
        RockBlock(LavaRockForMaterial(c.material, c.thermal.volume));
    AshCell ash;
    return LocalAshAt(x, y, z, &ash) ? BLOCK_VOLCANIC_ASH : BLOCK_AIR;
}
float GameGeologyCollisionHeightAt(int x, int y, int z)
{
    LavaCell c;
    if (!LocalLavaAt(x, y, z, &c)) {
        AshCell ash;
        return LocalAshAt(x, y, z, &ash) ? (float)ash.volume / ASH_CAPACITY : 0.0f;
    }
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
    /* Ordinary edits cannot erase deposits. Collection has its own volume
       transaction, independent of the base-block undo history.
       Solidification may replace an occupied overlay. */
    AshCell ash;
    return solidifying || (!LocalLavaAt(x, y, z, &c) && !LocalAshAt(x, y, z, &ash));
}

typedef struct CollectDeposit {
    uint32_t body;
    int x, y, z;
    unsigned units;
    bool ash;
    LavaRock rock;
} CollectDeposit;
static bool RemoveDeposit(void *user)
{
    CollectDeposit *c = user;
    return c->ash ? AshRuntimeCollect(c->body, c->x, c->y, c->z, c->units) :
        LavaRuntimeCollect(c->body, c->x, c->y, c->z, c->units, c->rock);
}
GameGeologyCollection GameGeologyCollectAt(int x, int y, int z, BlockType expectedType)
{
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    if (!s.active || WorldNetherIsActive() || WorldBlockRegionAt(y) != WORLD_BLOCK_REGION_SURFACE)
        return GAME_GEOLOGY_NO_DEPOSIT;
    LavaCell lava; AshCell ash;
    bool hasLava = LocalLavaAt(x, y, z, &lava), hasAsh = LocalAshAt(x, y, z, &ash);
    if (!hasLava && !hasAsh) return GAME_GEOLOGY_NO_DEPOSIT;
    /* Never excavate parked/hidden records, or solid crust beneath liquid. */
    if (!SurfaceBlockReadyAt(x, y, z) || GetBlockBaseAt(x, y, z) != BLOCK_AIR ||
        (hasLava && (!lava.cooled || LavaLiquidVolume(lava.thermal)))) return GAME_GEOLOGY_COLLECTION_BLOCKED;
    LavaRock rock = hasLava ? LavaRockForMaterial(lava.material, lava.thermal.volume) : LAVA_ROCK_BASALT;
    BlockType type = hasLava ? RockBlock(rock) : BLOCK_VOLCANIC_ASH;
    if (expectedType != type) return GAME_GEOLOGY_COLLECTION_BLOCKED;
    CollectDeposit source = { .body = s.bodyId, .x = x + s.originX, .y = y, .z = z + s.originZ,
        .units = hasLava ? lava.thermal.volume : ash.volume, .ash = !hasLava, .rock = rock };
    if (!InventoryCollectVolume(type, source.units, RemoveDeposit, &source)) return GAME_GEOLOGY_COLLECTION_BLOCKED;
    /* Overlays draw directly; only neighboring sleeping water needs waking. */
    FluidOnBlockChanged(x, y, z, type, BLOCK_AIR);
    return GAME_GEOLOGY_COLLECTED;
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
    AshCell ash;
    if (LocalAshAt(x, y, z, &ash)) return LAVA_BLOCKED;
    return GetBlockBaseAt(x, y, z) == BLOCK_AIR ? LAVA_OPEN : LAVA_BLOCKED;
}
static double LavaAmbientAt(int x, int z, void *user)
{
    GeologyTickContext *tick = user;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    return WeatherFieldSampleAtWorldTime(x - s.originX, z - s.originZ,
                                         tick->weatherTime).temperatureK;
}
static bool LavaWaterAt(int x, int y, int z, unsigned *volume, void *user)
{
    if (LavaBoundaryAt(x, y, z, user) == LAVA_UNKNOWN) return false;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    *volume = FluidGetVolumeAt(x - s.originX, y, z - s.originZ);
    return true;
}
static bool LavaEvaporateAt(int x, int y, int z, unsigned expected, unsigned units,
                            bool *reservoir, void *user)
{
    unsigned current = 0u;
    if (FLUID_CAPACITY != LAVA_WATER_CAPACITY || units > FLUID_CAPACITY ||
        !LavaWaterAt(x, y, z, &current, user) || current != expected) return false;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    return FluidTryEvaporateAt(x - s.originX, y, z - s.originZ,
                               (uint8_t)expected, (uint8_t)units, reservoir);
}
static bool LavaSolidifyAt(int x, int y, int z, void *user)
{
    if (LavaBoundaryAt(x, y, z, user) != LAVA_OPEN) return false;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    LavaCell cell;
    if (!LavaRuntimeCellAt(s.bodyId, x, y, z, &cell)) return false;
    solidifying = true;
    bool ok = SetBlockNoUndoFromSource(x - s.originX, y, z - s.originZ,
                                      RockBlock(LavaRockForMaterial(cell.material, cell.thermal.volume)),
                                      WORLD_MUTATION_ENVIRONMENT);
    solidifying = false;
    if (ok) VolcanoRuntimeSolidifiedSource(s.bodyId, x, y, z);
    return ok;
}
static unsigned EmitLava(const VolcanoDefinition *d, unsigned units, void *user)
{
    GeologyTickContext *tick = user;
    /* Stable source archetypes; forced legacy vents remain dense/mafic. */
    uint32_t profile = (uint32_t)d->id ^ (uint32_t)(d->id >> 32) ^ d->seed;
    profile ^= profile >> 16; profile *= 0x7feb352du;
    profile ^= profile >> 15; profile *= 0x846ca68bu; profile ^= profile >> 16;
    double silica = !d->forced && (profile & 1u) ? 1.0 : 0.0;
    double vesicles = !d->forced && (profile & 2u) ? 1.0 : 0.0;
    unsigned accepted = 0;
    /* The conduit stays at the crater center. Stack only above FULL lava
       cells; unknown terrain and player/ordinary solid caps stop injection. */
    for (int dy = 0; dy < 64 && accepted < units; dy++) {
        int y = d->address.radial + dy;
        if (LavaBoundaryAt(d->mapX, y, d->mapZ, user) != LAVA_OPEN) break;
        unsigned remaining = units - accepted;
        if (remaining > LAVA_CAPACITY) remaining = LAVA_CAPACITY;
        accepted += LavaRuntimeDepositMaterial(&tick->lava, d->mapX, y, d->mapZ,
                                               remaining, LAVA_SOURCE_K, silica, vesicles);
        LavaCell cell;
        if (!LavaRuntimeCellAt(d->address.bodyId, d->mapX, y, d->mapZ, &cell) ||
            cell.thermal.volume != LAVA_CAPACITY) break;
    }
    return accepted;
}

static AshBoundary AshBoundaryAt(int x, int y, int z, void *user)
{
    GeologyTickContext *tick = user;
    SurfaceMapOffset offset = SurfaceShortestMapOffset(tick->volcano.observerMap.x,
        tick->volcano.observerMap.z, x, z);
    if (fabsf(offset.x) > 96 || fabsf(offset.z) > 96 ||
        fabsf(y - tick->volcano.observerMap.y) > 96) return ASH_UNKNOWN;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    x -= s.originX; z -= s.originZ;
    if (!WorldCanAccessBlockY(y) || WorldBlockRegionAt(y) != WORLD_BLOCK_REGION_SURFACE) return ASH_BLOCKED;
    int cx, cz, lx, lz; WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    const Chunk *chunk = FindChunk(cx, cz);
    if (!chunk || !chunk->loaded || !ChunkTerrainSectionIsResolved(chunk, SurfaceSectionYFromBlockY(y))) return ASH_UNKNOWN;
    LavaCell lava;
    return GetBlockBaseAt(x, y, z) == BLOCK_AIR && !LocalLavaAt(x, y, z, &lava) ? ASH_OPEN : ASH_BLOCKED;
}
static double AshWetnessAt(int x, int y, int z, void *user)
{
    GeologyTickContext *tick = user;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    /* Only exposed top layers weather. The same bounded 12-block canopy
       probe used by fallout rejects solid roofs and unknown space. */
    for (int dy = 1; dy <= 12; dy++) {
        AshCell cover;
        if (AshBoundaryAt(x, y + dy, z, user) != ASH_OPEN ||
            AshRuntimeCellAt(s.bodyId, x, y + dy, z, &cover)) return 0;
    }
    WeatherFieldSample weather = WeatherFieldSampleAtWorldTime(x - s.originX, z - s.originZ, tick->weatherTime);
    return fmax(weather.rain, weather.drizzle);
}
static unsigned EmitAsh(const VolcanoDefinition *d, unsigned units, uint64_t activeTick, void *user)
{
    GeologyTickContext *tick = user;
    for (int dy = 1; dy <= 12; dy++) {
        if (AshBoundaryAt(d->mapX, d->address.radial + dy, d->mapZ, user) != ASH_OPEN) return 0;
    }
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    WeatherFieldSample weather = WeatherFieldSampleAtWorldTime(d->mapX - s.originX,
        d->mapZ - s.originZ, tick->weatherTime);
    uint32_t sequence = (uint32_t)activeTick + (uint32_t)(d->id ^ d->seed);
    int dx = ((int)(sequence % 7u) - 3) * 2 + (int)lroundf(cosf(weather.windAngle) * weather.wind * 4);
    int dz = ((int)((sequence / 7u) % 7u) - 3) * 2 + (int)lroundf(sinf(weather.windAngle) * weather.wind * 4);
    SurfaceMapCell map = SurfaceCanonicalMapCell(d->mapX + dx, d->mapZ + dz);
    int bottom = d->address.radial - 24;
    for (int y = d->address.radial + 12; y >= bottom; y--) {
        AshBoundary boundary = AshBoundaryAt(map.x, y, map.z, user);
        if (boundary == ASH_UNKNOWN) return 0;
        AshCell ash;
        if (boundary == ASH_OPEN && AshRuntimeCellAt(s.bodyId, map.x, y, map.z, &ash)) {
            return AshRuntimeDeposit(&tick->ash, map.x, ash.volume == ASH_CAPACITY ? y + 1 : y, map.z, units);
        }
        if (boundary == ASH_BLOCKED) {
            int x = map.x - s.originX, z = map.z - s.originZ;
            if (BlockCollisionHeightAt(x, y, z) < 1.0f) return 0;
            return AshRuntimeDeposit(&tick->ash, map.x, y + 1, map.z, units);
        }
    }
    return 0;
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
    VolcanoTerrainDefinition mountain;
    if (!VolcanoTerrainDefine(surface->context.seed, surface->planetProfile.volcanicActivity,
                               address, &mountain)) return false;
    SurfaceMapCell cell = VolcanoMapCell(address);
    *height = WorldSurfaceHeightAt(cell.x - surface->context.originX,
                                   cell.z - surface->context.originZ) + 1;
    *potential = mountain.potential;
    BlockType edit;
    int x = cell.x - surface->context.originX, z = cell.z - surface->context.originZ;
    return *height > PlanetTerrainSeaLevel() &&
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
    /* Keep saved admission, inventories and fractional clocks unchanged. The
       setting gates simulation only; rendering/queries keep their own context. */
    if (!game || !game->settings.geologyEnabled) return;
    GeologyTickContext tick = { .game = game, .volcano = ContextFor(game),
                               .weatherTime = SpaceElapsedSimulationTime() };
    if (!tick.volcano.valid) {
        VolcanoRuntimeUpdate(0.0, &tick.volcano);
        return;
    }
    LavaRuntimeAdvance(seconds, true, GameGeologyTick, &tick);
}

static void PrepareTick(GeologyTickContext *tick)
{
    /* Update admission without moving time, then use the same admitted set for
       source discharge and all lava cells in this 0.1-second step. */
    VolcanoRuntimeUpdate(0.0, &tick->volcano);
    tick->activeCount = 0u;
    VolcanoRecord r;
    for (unsigned i = 0u; VolcanoRuntimeAt(i, &r); i++) {
        if (r.active && tick->activeCount < VOLCANO_MAX_ACTIVE) tick->active[tick->activeCount++] = r.definition;
    }
    tick->lava = (LavaContext){ .valid = tick->volcano.valid, .bodyId = tick->volcano.bodyId,
        .boundary = LavaBoundaryAt, .ambient = LavaAmbientAt, .solidify = LavaSolidifyAt, .user = tick,
        .water = LavaWaterAt, .evaporate = LavaEvaporateAt };
    tick->ash = (AshContext){ .valid = tick->volcano.valid, .bodyId = tick->volcano.bodyId,
        .boundary = AshBoundaryAt, .wetness = AshWetnessAt, .user = tick };
    tick->volcano.emitLava = EmitLava;
    tick->volcano.emitAsh = EmitAsh;
    tick->volcano.hasHotLava = HasHotLava;
    tick->volcano.userData = tick;
}
static void GameGeologyTick(void *user)
{
    GeologyTickContext *tick = user;
    PrepareTick(tick);
    VolcanoRuntimeUpdate(LAVA_STEP_SECONDS, &tick->volcano);
    LavaRuntimeStep(&tick->lava);
    AshRuntimeStep(&tick->ash);
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
    unsigned steamCells = 0u;
    uint64_t lavaTicks = LavaRuntimeStats().ticks;
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
            cell.thermal, LavaRockForMaterial(cell.material, cell.thermal.volume));
        if (cell.steam >= 0.05 && steamCells < LAVA_STEAM_MAX_CELLS) {
            DrawLavaSteam(transform,
                (Vector3){ lx + 0.5f, SurfaceSectionLocalYFromBlockY(cell.y) +
                    (float)cell.thermal.volume / LAVA_CAPACITY, lz + 0.5f },
                cell.steam, lavaTicks, frame->weatherVisual.windAngle, frame->weatherVisual.windDrift);
            steamCells++;
        }
    }
    AshCell ash;
    for (unsigned i = 0; AshRuntimeAt(i, &ash); i++) {
        int x = ash.x - s.originX, z = ash.z - s.originZ;
        if (ash.bodyId != s.bodyId || !SurfaceBlockReadyAt(x, ash.y, z) || GetBlockBaseAt(x, ash.y, z) != BLOCK_AIR) continue;
        int cx, cz, lx, lz; WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
        const Chunk *chunk = FindChunk(cx, cz);
        if (!chunk || !chunk->loaded) continue;
        Matrix transform = SurfaceChunkRenderTransform(chunk, SurfaceSectionYFromBlockY(ash.y),
            game->camera.position, s.originX, s.originZ);
        DrawAshCell(transform, (Vector3){ lx, SurfaceSectionLocalYFromBlockY(ash.y), lz }, ash.volume);
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
static const char *collectionNames[] = { "ash", "basalt", "obsidian", "scoria", "pumice", "rhyolite" };
static const BlockType collectionTypes[] = { BLOCK_VOLCANIC_ASH, BLOCK_BASALT, BLOCK_OBSIDIAN,
    BLOCK_SCORIA, BLOCK_PUMICE, BLOCK_RHYOLITE };

bool GameDebugGeologyDispatch(GameRuntime *game, DebugControlCommand command)
{
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_MOUNTAIN) {
        GameDebugVolcanoMountain(game);
        return true;
    }
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_DEPOSITION_TEST) {
        if (!GameDebugDepositionRun(&game->debugControl)) {
            game->debugCommandFailed = true;
            snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure), "deposition_fixture_failed");
        }
        return true;
    }
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_FIND) {
        DebugControl *c = &game->debugControl;
        const WorldSurfaceSnapshot *s = WorldCurrentSurfaceSnapshot();
        c->geologyTerrainFound = false;
        if (s->context.kind != WORLD_SURFACE_PLANET || !ContextFor(game).valid) {
            game->debugCommandFailed = true;
            snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure), "geology_planet_required");
            return true;
        }
        float best = INFINITY;
        Vector3 observer = ContextFor(game).observerMap;
        for (SurfaceFace face = 0; face < SURFACE_FACE_COUNT; face++) {
            for (int v = VOLCANO_TERRAIN_OFFSET; v < SURFACE_FACE_BLOCKS; v += VOLCANO_TERRAIN_SPACING) {
                for (int u = VOLCANO_TERRAIN_OFFSET; u < SURFACE_FACE_BLOCKS; u += VOLCANO_TERRAIN_SPACING) {
                    VolcanoTerrainDefinition d;
                    if (!VolcanoTerrainDefine(s->context.seed, s->planetProfile.volcanicActivity,
                            (SurfaceAddress){s->context.bodyId,face,u,v,0}, &d)) continue;
                    SurfaceMapCell cell = VolcanoMapCell(d.center);
                    SurfaceMapOffset delta = SurfaceShortestMapOffset(observer.x, observer.z, cell.x, cell.z);
                    float distance = hypotf(delta.x, delta.z);
                    if (distance >= best) continue;
                    int height = WorldSurfaceHeightAt(cell.x - s->context.originX, cell.z - s->context.originZ) + 1;
                    if (height <= PlanetTerrainSeaLevel()) continue;
                    best = distance;
                    c->geologyTerrainFound = true;
                    c->geologyTerrainBody = s->context.bodyId; c->geologyTerrainSeed = s->context.seed;
                    c->geologyTerrainX = cell.x; c->geologyTerrainY = height; c->geologyTerrainZ = cell.z;
                    c->geologyTerrainRadius = d.radius; c->geologyTerrainHeight = d.height;
                    c->geologyCraterRadius = d.craterRadius;
                }
            }
        }
        DebugControlReply(c, "DEBUG_CONTROL geology find found=%d position=%d,%d,%d radius=%.3f height=%.3f crater_radius=%.3f\n",
            c->geologyTerrainFound, c->geologyTerrainX, c->geologyTerrainY, c->geologyTerrainZ,
            c->geologyTerrainRadius, c->geologyTerrainHeight, c->geologyCraterRadius);
        return true;
    }
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_ENABLED) {
        GameSettings settings = game->settings;
        settings.geologyEnabled = game->debugControl.geologyEnabled;
        bool saved = GameSettingsSave(&settings);
        if (saved) game->settings = settings;
        else {
            game->debugCommandFailed = true;
            snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure),
                     "geology_settings_save_failed");
        }
        DebugControlReply(&game->debugControl,
            "DEBUG_CONTROL geology enabled=%d saved=%d\n",
            game->settings.geologyEnabled, saved);
        return true;
    }
    if (command < DEBUG_CONTROL_COMMAND_GEOLOGY_INSPECT ||
        command > DEBUG_CONTROL_COMMAND_GEOLOGY_INVENTORY) return false;
    VolcanoContext context = ContextFor(game);
    bool ok = context.valid;
    if (!game->settings.geologyEnabled &&
        (command == DEBUG_CONTROL_COMMAND_GEOLOGY_FORCE ||
         command == DEBUG_CONTROL_COMMAND_GEOLOGY_STOP ||
         command == DEBUG_CONTROL_COMMAND_GEOLOGY_DEPOSIT)) ok = false;
    if (ok && command == DEBUG_CONTROL_COMMAND_GEOLOGY_INVENTORY) {
        unsigned kind = game->debugControl.geologyMaterial;
        if (kind >= 6) ok = false;
        else {
            BlockType type = collectionTypes[kind];
            int desired = (int)game->debugControl.geologyUnits, current = InventoryCount(type);
            if (desired > current) ok = InventoryAdd(type, desired - current) == desired - current;
            if (desired < current) ok = InventoryConsume(type, current - desired);
            DebugControlReply(&game->debugControl, "DEBUG_CONTROL geology inventory %s blocks=%d remainder=%u\n",
                collectionNames[kind], InventoryCount(type), InventoryRemainder(type));
        }
    } else if (ok && command == DEBUG_CONTROL_COMMAND_GEOLOGY_FORCE) {
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
    } else if (ok && command == DEBUG_CONTROL_COMMAND_GEOLOGY_DEPOSIT) {
        GeologyTickContext tick = { .game = game, .volcano = context,
            .weatherTime = SpaceElapsedSimulationTime() };
        PrepareTick(&tick);
        WorldSurfaceContext s = WorldCurrentSurfaceContext();
        DebugControl *c = &game->debugControl;
        int x = c->geologyX + s.originX, z = c->geologyZ + s.originZ;
        unsigned kind = c->geologyMaterial;
        unsigned accepted = kind == 0 ? AshRuntimeDeposit(&tick.ash, x, c->geologyY, z, c->geologyUnits) :
            LavaRuntimeDepositMaterial(&tick.lava, x, c->geologyY, z, c->geologyUnits,
                LAVA_SOURCE_K, kind == 2 || kind == 4, kind == 3 || kind == 4);
        ok = accepted > 0;
        DebugControlReply(c, "DEBUG_CONTROL geology deposit accepted=%u requested=%u\n", accepted, c->geologyUnits);
    }
    if (!ok) {
        game->debugCommandFailed = true;
        snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure),
                 "geology_unavailable_or_invalid_state");
    }
    VolcanoStats stats = VolcanoRuntimeStats();
    LavaStats lava = LavaRuntimeStats();
    AshStats ash = AshRuntimeStats();
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_INSPECT) {
        DebugDslValue radius;
        GameDebugGeologyResolve(game, "geology.flow_radius", &radius);
        DebugControlReply(&game->debugControl,
            "DEBUG_CONTROL geology flow radius=%.3f supplied=%.3f solidified=%.3f hot=%u records=%u\n",
            radius.as.number, (double)lava.supplied / LAVA_CAPACITY,
            (double)lava.solidified / LAVA_CAPACITY, lava.hotCells, lava.records);
    }
    DebugControlReply(&game->debugControl,
        "DEBUG_CONTROL geology %s records=%u active=%u rejected=%" PRIu64 " lava_physics=1 enabled=%d\n",
        ok ? "ok" : "error", stats.count, stats.active, stats.rejectedAdmissions,
        game->settings.geologyEnabled);
    DebugControlReply(&game->debugControl,
        "DEBUG_CONTROL geology ash cells=%u volume=%" PRIu64 " supplied=%" PRIu64
        " washed=%" PRIu64 " collected=%" PRIu64 " volume_error=%.0f\n", ash.cells, ash.volume, ash.supplied,
        ash.washed, ash.collected, (double)ash.supplied - ash.volume - ash.washed - ash.collected);
    DebugControlReply(&game->debugControl,
        "DEBUG_CONTROL geology water evaporated=%" PRIu64 " reservoir=%" PRIu64
        " heat=%.6f energy_error=%.9f\n", lava.evaporatedWater, lava.reservoirWater,
        lava.waterEnergy, lava.suppliedEnergy - lava.energy - lava.cooledEnergy -
            lava.exportedEnergy - lava.waterEnergy);
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_INSPECT) {
        LavaCell cell = { 0 };
        LocalLavaAt((int)floorf(game->player.position.x), (int)floorf(game->player.position.y),
                    (int)floorf(game->player.position.z), &cell);
        double credit = 0.0;
        for (unsigned face = 0u; face < 6u; face++) credit += cell.contactCredit[face];
        DebugControlReply(&game->debugControl,
            "DEBUG_CONTROL geology cell volume=%u temperature=%.6f credit=%.6f steam=%.6f\n",
            cell.thermal.volume, LavaTemperature(cell.thermal), credit, cell.steam);
    }
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
    if (!strncmp(field, "deposition_", 11)) return GameDebugDepositionResolve(&game->debugControl, field, out);
    if (!strncmp(field, "terrain_", 8)) {
        const DebugControl *c = &game->debugControl;
        WorldSurfaceContext s = WorldCurrentSurfaceContext();
        bool found = c->geologyTerrainFound && s.kind == WORLD_SURFACE_PLANET &&
            s.bodyId == c->geologyTerrainBody && s.seed == c->geologyTerrainSeed;
        if (!strcmp(field, "terrain_found")) {
            out->type = DEBUG_DSL_VALUE_BOOL; out->as.boolean = found;
        } else if (!strcmp(field, "terrain_position")) {
            out->type = DEBUG_DSL_VALUE_VEC3;
            out->as.vec3 = found ? (DebugDslVec3){ c->geologyTerrainX - s.originX,
                c->geologyTerrainY, c->geologyTerrainZ - s.originZ } : (DebugDslVec3){0};
        } else {
            out->type = DEBUG_DSL_VALUE_NUMBER;
            if (!strcmp(field, "terrain_radius")) out->as.number = found ? c->geologyTerrainRadius : 0;
            else if (!strcmp(field, "terrain_height")) out->as.number = found ? c->geologyTerrainHeight : 0;
            else if (!strcmp(field, "terrain_crater_radius")) out->as.number = found ? c->geologyCraterRadius : 0;
            else if (!strcmp(field, "terrain_ground")) out->as.number = WorldSurfaceHeightAt(
                (int)floorf(game->player.position.x), (int)floorf(game->player.position.z));
            else return false;
        }
        return true;
    }
    if (!strcmp(field, "enabled")) {
        out->type = DEBUG_DSL_VALUE_BOOL;
        out->as.boolean = game->settings.geologyEnabled;
        return true;
    }
    if (!strncmp(field, "inventory_", 10) || !strncmp(field, "remainder_", 10)) {
        for (unsigned i = 0; i < 6; i++) {
            if (strcmp(field + 10, collectionNames[i])) continue;
            out->type = DEBUG_DSL_VALUE_NUMBER;
            out->as.number = !strncmp(field, "inventory_", 10) ?
                (double)InventoryCount(collectionTypes[i]) : (double)InventoryRemainder(collectionTypes[i]);
            return true;
        }
        return false;
    }
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
    if (!strcmp(field, "cell_rock")) {
        int x = (int)floorf(game->player.position.x), y = (int)floorf(game->player.position.y);
        int z = (int)floorf(game->player.position.z);
        LavaCell cell;
        out->type = DEBUG_DSL_VALUE_STRING;
        out->as.string = "none";
        if (context.valid && LocalLavaAt(x, y, z, &cell)) {
            out->as.string = LavaRockName(LavaRockForMaterial(cell.material, cell.thermal.volume));
        } else if (context.valid) {
            BlockType base = GetBlockAt(x, y, z);
            for (LavaRock rock = LAVA_ROCK_BASALT; rock <= LAVA_ROCK_RHYOLITE; rock++)
                if (base == RockBlock(rock)) out->as.string = LavaRockName(rock);
        }
        return true;
    }
    VolcanoStats stats = VolcanoRuntimeStats();
    LavaStats lava = LavaRuntimeStats();
    AshStats ash = AshRuntimeStats();
    double value;
    if (!strcmp(field, "flow_radius")) {
        value = 0;
        LavaCell cell;
        for (unsigned i = 0; present && LavaRuntimeAt(i, &cell); i++) {
            if (cell.bodyId != context.bodyId || !LavaLiquidVolume(cell.thermal)) continue;
            SurfaceMapOffset delta = SurfaceShortestMapOffset(record.definition.mapX,
                record.definition.mapZ, cell.x, cell.z);
            value = fmax(value, hypot(delta.x, delta.z));
        }
    }
    else if (!strcmp(field, "records")) value = stats.count;
    else if (!strcmp(field, "active_count")) value = stats.active;
    else if (!strcmp(field, "rejected")) value = (double)stats.rejectedAdmissions;
    else if (!strcmp(field, "event")) value = record.state.event;
    else if (!strcmp(field, "ticks")) value = (double)record.state.activeTicks;
    else if (!strcmp(field, "pressure")) value = record.state.pressure;
    else if (!strcmp(field, "reservoir")) value = record.state.reservoir;
    else if (!strcmp(field, "ash_cells")) value = ash.cells;
    else if (!strcmp(field, "ash_volume")) value = (double)ash.volume / ASH_CAPACITY;
    else if (!strcmp(field, "ash_supplied")) value = (double)ash.supplied / ASH_CAPACITY;
    else if (!strcmp(field, "ash_washed")) value = (double)ash.washed / ASH_CAPACITY;
    else if (!strcmp(field, "ash_collected")) value = (double)ash.collected / ASH_CAPACITY;
    else if (!strcmp(field, "ash_volume_error")) value = ((double)ash.supplied - ash.volume - ash.washed - ash.collected) / ASH_CAPACITY;
    else if (!strcmp(field, "ash_ticks")) value = (double)ash.ticks;
    else if (!strcmp(field, "cell_ash_height")) {
        AshCell cell;
        bool exists = context.valid && LocalAshAt((int)floorf(game->player.position.x),
            (int)floorf(game->player.position.y), (int)floorf(game->player.position.z), &cell);
        value = exists ? (double)cell.volume / ASH_CAPACITY : 0;
    }
    else if (!strcmp(field, "lava_cells")) value = lava.records;
    else if (!strcmp(field, "hot_cells")) value = lava.hotCells;
    else if (!strcmp(field, "lava_volume")) value = (double)lava.volume / LAVA_CAPACITY;
    else if (!strcmp(field, "liquid_volume")) value = (double)lava.liquidVolume / LAVA_CAPACITY;
    else if (!strcmp(field, "solid_volume")) value = (double)lava.solidVolume / LAVA_CAPACITY;
    else if (!strcmp(field, "solidified")) value = (double)lava.solidified / LAVA_CAPACITY;
    else if (!strcmp(field, "collected")) value = (double)lava.removed / LAVA_CAPACITY;
    else if (!strcmp(field, "supplied")) value = (double)lava.supplied / LAVA_CAPACITY;
    else if (!strcmp(field, "volume_error")) value =
        ((double)lava.supplied - lava.volume - lava.solidified - lava.removed) / LAVA_CAPACITY;
    else if (!strcmp(field, "energy_error")) value =
        lava.suppliedEnergy - lava.energy - lava.cooledEnergy - lava.exportedEnergy - lava.waterEnergy;
    else if (!strcmp(field, "evaporated_units")) value = (double)lava.evaporatedWater;
    else if (!strcmp(field, "evaporated_water")) value = (double)lava.evaporatedWater / FLUID_CAPACITY;
    else if (!strcmp(field, "reservoir_units")) value = (double)lava.reservoirWater;
    else if (!strcmp(field, "water_energy")) value = lava.waterEnergy;
    else if (!strcmp(field, "lava_ticks")) value = (double)lava.ticks;
    else if (!strcmp(field, "lava_accumulator")) value = lava.accumulator;
    else if (!strcmp(field, "vent_accumulator")) value = record.accumulator;
    else if (!strcmp(field, "blocked_transfers")) value = (double)lava.blockedTransfers;
    else if (!strcmp(field, "rejected_cells")) value = (double)lava.rejectedCells;
    else if (!strcmp(field, "cell_volume") || !strcmp(field, "cell_liquid") ||
             !strcmp(field, "cell_temperature") || !strcmp(field, "cell_collision_height") ||
             !strcmp(field, "cell_contact_credit") || !strcmp(field, "cell_steam") ||
             !strcmp(field, "cell_silica") || !strcmp(field, "cell_vesicles") || !strcmp(field, "cell_quenched")) {
        int x = (int)floorf(game->player.position.x);
        int y = (int)floorf(game->player.position.y);
        int z = (int)floorf(game->player.position.z);
        LavaCell cell = { 0 };
        bool exists = context.valid && LocalLavaAt(x, y, z, &cell);
        if (!strcmp(field, "cell_collision_height")) value = BlockCollisionHeightAt(x, y, z);
        else if (!strcmp(field, "cell_volume")) value = exists ? (double)cell.thermal.volume / LAVA_CAPACITY : 0.0;
        else if (!strcmp(field, "cell_liquid")) value = exists ? (double)LavaLiquidVolume(cell.thermal) / LAVA_CAPACITY : 0.0;
        else if (!strcmp(field, "cell_steam")) value = exists ? cell.steam : 0.0;
        else if (!strcmp(field, "cell_silica")) value = exists ? cell.material.silica / cell.thermal.volume : 0;
        else if (!strcmp(field, "cell_vesicles")) value = exists ? cell.material.vesicles / cell.thermal.volume : 0;
        else if (!strcmp(field, "cell_quenched")) value = exists ? cell.material.quenched / cell.thermal.volume : 0;
        else if (!strcmp(field, "cell_contact_credit")) {
            value = 0.0;
            if (exists) for (unsigned face = 0u; face < 6u; face++) value += cell.contactCredit[face];
        }
        else value = exists ? LavaTemperature(cell.thermal) : 0.0;
    }
    else return false;
    out->type = DEBUG_DSL_VALUE_NUMBER;
    out->as.number = value;
    return true;
}
