#include "app/game_inventory.h"
#include "raylib.h"
#include "raymath.h"

#include "app/game_debug.h"
#include "app/game_debug_player.h"
#include "app/game_debug_cga.h"
#include "app/game_geology.h"
#include "app/game_debug_landform.h"
#include "presentation/biology_ui.h"
#include "app/game_debug_status.h"
#include "app/game_debug_block.h"
#include "app/game_debug_flora.h"
#include "app/game_debug_gate.h"
#include "app/game_debug_lod.h"
#include "app/game_debug_diagnostics.h"
#include "app/game_debug_topology.h"
#include "app/game_debug_trace.h"
#include "app/game_debug_weather.h"
#include "app/game_debug_hydrology.h"
#include "app/game_debug_wildfire.h"
#include "app/game_debug_world.h"
#include "app/game_coverage_evidence.h"
#include "app/game_save.h"
#include "world/chunks.h"
#include "core/debug_control.h"
#include "core/debug_dsl.h"
#include "core/game_notice.h"
#include "core/perf.h"
#include "core/simulation_time.h"
#include "ecology/ecology.h"
#include "ecology/entity.h"
#include "ecology/evolution.h"
#include "ecology/evolution_catalog.h"
#include "world/fluid.h"
#include "app/game_interaction.h"
#include "app/game_internal.h"
#include "app/game_runtime.h"
#include "app/game_stream_audit.h"
#include "app/game_world_transition.h"
#include "gameplay/interaction.h"
#include "gameplay/map_markers.h"
#include "gameplay/player.h"
#include "gameplay/ship.h"
#include "presentation/homeworld_map.h"
#include "presentation/import_dialog_ui.h"
#include "presentation/render.h"
#include "presentation/weather_presentation.h"
#include "space/space_runtime.h"
#include "space/space_state.h"
#include "world/terrain.h"
#include "world/surface_topology.h"
#include "world/weather.h"
#include "world/weather_impact.h"
#include "world/tornado.h"
#include "world/world_state.h"
#include "world/block_types.h"
#include "world/block_edits.h"
#include "world/world_environment.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
void GameDebugMarkCommandError(GameRuntime *game, const char *reason)
{
    if (!game) return;
    game->debugCommandFailed = true;
    snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure),
             "%s", reason ? reason : "command_failed");
}
static void GameDebugToggleSurfaceMap(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING || !WorldIsSurfaceActive()) {
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL map error reason=no_active_surface\n");
        return;
    }
    if (HomeWorldMapIsOpen()) {
        HomeWorldMapClose();
        game->cursorReleased = false;
        DisableCursor();
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL map closed\n");
        return;
    }

    float daylight = 0.0f;
    float sunset = 0.0f;
    PlanetLightState light = { 0 };
    if (PlanetWorldLightStateAt(game->player.position, &light)) {
        daylight = light.daylight;
    } else {
        DayNightFactors(game->dayTime, &daylight, &sunset);
    }
    HomeWorldMapOpen(game->player.position, daylight);
    game->player.velocity = Vector3Zero();
    game->cursorReleased = true;
    EnableCursor();
    DebugControlReply(&game->debugControl, "DEBUG_CONTROL map open\n");
}
static MapMarkerSurface GameDebugMarkerSurface(void)
{
    return (MapMarkerSurface){
        .dimension = WorldCurrentDimension(),
        .surfaceId = WorldCurrentSurfaceId()
    };
}

static bool GameDebugMarkerColor(const char *name, MapMarkerColor *out)
{
    static const char *names[MAP_MARKER_COLOR_COUNT] = {
        "red", "amber", "green", "cyan", "blue", "magenta"
    };
    if (!name || !out) return false;
    for (int i = 0; i < MAP_MARKER_COLOR_COUNT; i++) {
        if (strcasecmp(name, names[i]) == 0) {
            *out = (MapMarkerColor)i;
            return true;
        }
    }
    return false;
}

static const char *GameDebugMarkerColorName(MapMarkerColor color)
{
    static const char *names[MAP_MARKER_COLOR_COUNT] = {
        "red", "amber", "green", "cyan", "blue", "magenta"
    };
    return color >= 0 && color < MAP_MARKER_COLOR_COUNT
        ? names[color] : "invalid";
}

static bool GameDebugMarkerAvailable(GameRuntime *game)
{
    if (game->screen == SCREEN_PLAYING && WorldIsSurfaceActive()) return true;
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL marker error reason=no_active_surface\n");
    return false;
}

static void GameDebugMarkerAdd(GameRuntime *game)
{
    if (!GameDebugMarkerAvailable(game)) return;
    MapMarkerColor color = MAP_MARKER_RED;
    if (!GameDebugMarkerColor(game->debugControl.marker.color, &color)) {
        GameDebugMarkCommandError(game, "marker_invalid_color");
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL marker add error reason=invalid_color\n");
        return;
    }
    uint32_t id = 0u;
    if (!MapMarkersCreate(GameDebugMarkerSurface(),
                          game->debugControl.marker.x,
                          game->debugControl.marker.z,
                          game->debugControl.marker.name, color, &id)) {
        GameDebugMarkCommandError(game, "marker_invalid_or_limit");
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL marker add error reason=invalid_or_limit\n");
        return;
    }
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL marker add ok id=%u dimension=%s surface=%u "
        "x=%.3f z=%.3f color=%s name=%s\n",
        id, WorldDimensionName(WorldCurrentDimension()),
        WorldCurrentSurfaceId(), game->debugControl.marker.x,
        game->debugControl.marker.z, GameDebugMarkerColorName(color),
        game->debugControl.marker.name);
}

static void GameDebugMarkerList(GameRuntime *game)
{
    if (!GameDebugMarkerAvailable(game)) return;
    MapMarker markers[MAP_MARKERS_PER_SURFACE];
    int count = MapMarkersCollect(GameDebugMarkerSurface(), markers,
                                  MAP_MARKERS_PER_SURFACE);
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL marker list ok dimension=%s surface=%u count=%d "
        "target=%u\n",
        WorldDimensionName(WorldCurrentDimension()), WorldCurrentSurfaceId(),
        count, MapMarkersTargetId());
    for (int i = 0; i < count; i++) {
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL marker item id=%u x=%.3f z=%.3f color=%s "
            "target=%d name=%s\n",
            markers[i].id, markers[i].x, markers[i].z,
            GameDebugMarkerColorName(markers[i].color),
            markers[i].id == MapMarkersTargetId() ? 1 : 0,
            markers[i].name);
    }
}

static void GameDebugMarkerTarget(GameRuntime *game)
{
    if (!GameDebugMarkerAvailable(game)) return;
    uint32_t id = game->debugControl.marker.id;
    if (!MapMarkersSetTarget(id)) {
        GameDebugMarkCommandError(game, "marker_target_not_found");
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL marker target error reason=not_found id=%u\n",
                          id);
        return;
    }
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL marker target ok id=%u\n", id);
}

static void GameDebugMarkerRemove(GameRuntime *game)
{
    if (!GameDebugMarkerAvailable(game)) return;
    uint32_t id = game->debugControl.marker.id;
    if (!MapMarkersRemove(id)) {
        GameDebugMarkCommandError(game, "marker_remove_not_found");
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL marker remove error reason=not_found id=%u\n",
                          id);
        return;
    }
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL marker remove ok id=%u\n", id);
}

static void GameDebugInspectFluid(GameRuntime *game)
{
    int x = game->debugControl.fluidUsePlayerPosition
                ? (int)floorf(game->player.position.x)
                : game->debugControl.fluidX;
    int y = game->debugControl.fluidUsePlayerPosition
                ? (int)floorf(game->player.position.y)
                : game->debugControl.fluidY;
    int z = game->debugControl.fluidUsePlayerPosition
                ? (int)floorf(game->player.position.z)
                : game->debugControl.fluidZ;
    FluidSample sample = FluidSampleAt(
        (Vector3){ (float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f });
    FluidStats stats = FluidGetStats();
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL fluid inspect ok position=%d,%d,%d "
        "volume=%u surface=%.6f flow=%.6f,%.6f,%.6f "
        "queue=%u processed=%u edits=%u total=%llu overflows=%u\n",
        x, y, z, (unsigned)sample.volume, sample.surfaceY, sample.velocity.x,
        sample.velocity.y, sample.velocity.z, stats.activeCells,
        stats.lastProcessedCells, stats.editCount,
        (unsigned long long)FluidLoadedVolume(), stats.queueOverflows);
}

static void GameDebugSetFluid(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING || !WorldIsSurfaceActive()) {
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL fluid set error reason=no_active_surface\n");
    } else if (!FluidSetVolumeAt(game->debugControl.fluidX,
                                 game->debugControl.fluidY,
                                 game->debugControl.fluidZ,
                                 (uint8_t)game->debugControl.fluidVolume)) {
        GameDebugMarkCommandError(game, "fluid_cell_unavailable");
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL fluid set error reason=cell_unavailable\n");
    } else {
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL fluid set ok position=%d,%d,%d volume=%u\n",
            game->debugControl.fluidX, game->debugControl.fluidY,
            game->debugControl.fluidZ, game->debugControl.fluidVolume);
    }
}

static void GameDebugStepFluid(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING || !WorldIsSurfaceActive()) {
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL fluid step error reason=no_active_surface\n");
        return;
    }
    FluidStepTicks(game->debugControl.fluidTicks);
    FluidStats stats = FluidGetStats();
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL fluid step ok ticks=%u queue=%u "
                      "processed=%u total=%llu\n",
                      game->debugControl.fluidTicks, stats.activeCells,
                      stats.lastProcessedCells,
                      (unsigned long long)FluidLoadedVolume());
}

static void GameDebugApplyInput(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL input error reason=not_playing\n");
        return;
    }
    game->scriptedPlayerInput = (PlayerInput){
        .forward = game->debugControl.playerInput.forward,
        .strafe = game->debugControl.playerInput.strafe,
        .vertical = game->debugControl.playerInput.vertical,
        .sprint = game->debugControl.playerInput.sprint
    };
    game->scriptedInputFrames = game->debugControl.playerInput.frames;
    game->scriptedInputFirstFrame = true;
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL input ok forward=%.3f strafe=%.3f "
        "vertical=%.3f sprint=%d frames=%u\n",
        game->scriptedPlayerInput.forward, game->scriptedPlayerInput.strafe,
        game->scriptedPlayerInput.vertical,
        game->scriptedPlayerInput.sprint ? 1 : 0, game->scriptedInputFrames);
}

static void GameDebugEnterShip(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ship enter error reason=not_playing\n");
        return;
    }
    if (ShipIsDriving()) {
        GameDebugMarkCommandError(game, "ship_already_driving");
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ship enter ignored reason=already_driving\n");
        return;
    }

    ShipLocatorTarget target = { 0 };
    if (!ShipLocatorTargetAt(game->player.position, &target)) {
        GameDebugMarkCommandError(game, "ship_no_recorded_ship");
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ship enter error reason=no_recorded_ship\n");
        return;
    }
    if (target.status != SHIP_LOCATOR_TARGET_LOCAL) {
        GameDebugMarkCommandError(game, "ship_not_local");
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ship enter error reason=ship_not_local\n");
        return;
    }
    if (!ShipTryEnter(target.blockX, target.blockY, target.blockZ,
                      &game->player)) {
        GameDebugMarkCommandError(game, "ship_unavailable");
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ship enter error reason=unavailable\n");
        return;
    }
    game->scriptedShipInput = (ShipControlInput){ 0 };
    game->scriptedShipInputFrames = 0u;
    game->scriptedShipInputFrameCarry = 0.0f;
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL ship enter ok block=%d,%d,%d\n",
                      target.blockX, target.blockY, target.blockZ);
}

static void GameDebugBeginShip(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ship begin error reason=not_playing\n");
        return;
    }
    if (!ShipBeginDebugFlight(&game->player)) {
        GameDebugMarkCommandError(game, "ship_already_driving");
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ship begin error reason=already_driving\n");
        return;
    }
    game->scriptedShipInput = (ShipControlInput){ 0 };
    game->scriptedShipInputFrames = 0u;
    game->scriptedShipInputFrameCarry = 0.0f;
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL ship begin ok position=%.6f,%.6f,%.6f\n",
                      game->player.position.x, game->player.position.y,
                      game->player.position.z);
}

static void GameDebugApplyShipInput(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING || !ShipIsDriving()) {
        GameDebugMarkCommandError(game, "ship_not_driving");
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ship input error reason=not_driving\n");
        return;
    }
    game->scriptedShipInput = (ShipControlInput){
        .forward = game->debugControl.shipInput.forward,
        .strafe = game->debugControl.shipInput.strafe,
        .vertical = game->debugControl.shipInput.vertical
    };
    game->scriptedShipInputFrames = game->debugControl.shipInput.frames;
    game->scriptedShipInputFrameCarry = 0.0f;
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL ship input ok forward=%.3f strafe=%.3f "
        "vertical=%.3f frames=%u\n",
        game->scriptedShipInput.forward, game->scriptedShipInput.strafe,
        game->scriptedShipInput.vertical, game->scriptedShipInputFrames);
}

static void GameDebugSetShipExhaust(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING || !ShipIsDriving()) {
        GameDebugMarkCommandError(game, "ship_not_driving");
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ship exhaust error reason=not_driving\n");
        return;
    }
    float demand = game->debugControl.shipExhaustDemand;
    if (!ShipSetDebugExhaust(&game->player, demand)) {
        GameDebugMarkCommandError(game, "ship_exhaust_invalid");
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ship exhaust error reason=invalid\n");
        return;
    }
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL ship exhaust ok demand=%.3f\n", demand);
}

static void GameDebugEmitShipDust(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING || !ShipIsDriving() ||
        !WorldIsSurfaceActive()) {
        GameDebugMarkCommandError(game, "ship_dust_unavailable");
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ship dust error reason=no_surface_ship\n");
        return;
    }
    ShipEmitTouchdownDust(&game->player);
    DebugControlReply(&game->debugControl, "DEBUG_CONTROL ship dust ok\n");
}

static void GameDebugSetView(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL view error reason=not_playing\n");
        return;
    }
    game->thirdPerson = game->debugControl.thirdPerson;
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL view ok mode=%s\n",
                      game->thirdPerson ? "third" : "first");
}

static void GameDebugInspectEvolution(GameRuntime *game)
{
    int index = EntityNearestEvolvable(game->player.position,
                                       game->debugControl.evolutionRadius);
    EntityEvolutionDebugInfo info = { 0 };
    if (index < 0 || !EntityEvolutionInspect(index, &info)) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL evolution inspect none radius=%.3f\n",
                          game->debugControl.evolutionRadius);
        return;
    }
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL evolution inspect ok organism=%u lineage=%u "
        "species=%u genome=%u generation=%u mutations=%u "
        "locomotion=%s modules=%u age=%.3f maturity=%.3f "
        "health=%.3f energy=%.3f diet=%.3f mass=%.3f speed=%.3f "
        "juvenile=%d pregnant=%d corpse=%d\n",
        info.organismId, info.lineageId, info.speciesId, info.genomeId,
        info.generation, info.mutationCount,
        EvolutionLocomotionName(info.locomotion), info.moduleCount,
        info.ageDays, info.maturityAgeDays, info.health, info.energy,
        info.diet, info.mass, info.speed, info.juvenile ? 1 : 0,
        info.pregnant ? 1 : 0, info.corpse ? 1 : 0);
}

static void GameDebugFocusEvolution(GameRuntime *game)
{
    int index = EntityNearestEvolvable(game->player.position,
                                       game->debugControl.evolutionRadius);
    EntityEvolutionDebugInfo info = { 0 };
    if (game->screen != SCREEN_PLAYING ||
        !EntityEvolutionInspect(index, &info)) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL evolution focus none radius=%.3f\n",
                          game->debugControl.evolutionRadius);
        return;
    }
    game->evolutionScanLocked = GameInteractionObserveEvolutionInfo(&info);
    game->evolutionLockedOrganismId =
        game->evolutionScanLocked ? info.organismId : 0u;
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL evolution focus %s organism=%u species=%u\n",
        game->evolutionScanLocked ? "ok" : "error", info.organismId,
        info.speciesId);
}

static float GameDebugEvolutionDaylight(const GameRuntime *game)
{
    float daylight = 0.0f;
    float sunset = 0.0f;
    PlanetLightState light = { 0 };
    if (game && PlanetWorldLightStateAt(game->player.position, &light)) {
        return light.daylight;
    }
    if (game) DayNightFactors(game->dayTime, &daylight, &sunset);
    return daylight;
}

static void GameDebugReplyEvolutionRegion(GameRuntime *game)
{
    PlanetEvolutionRegion evolution = { 0 };
    float daylight = GameDebugEvolutionDaylight(game);
    if (!PlanetEcologyEvolutionRegionAt(
            (int)floorf(game->player.position.x),
            (int)floorf(game->player.position.z), daylight, &evolution)) {
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL evolution region error reason=no_active_ecology\n");
        return;
    }
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL evolution region ok lineages=%u "
                      "bootstrap=%u complete=%d herbivore=%.6f omnivore=%.6f "
                      "carnivore=%.6f\n",
                      evolution.lineageCount, evolution.bootstrapGeneration,
                      evolution.bootstrapComplete ? 1 : 0,
                      evolution.herbivoreDensity, evolution.omnivoreDensity,
                      evolution.carnivoreDensity);
}

static void GameDebugAdvanceEvolution(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING) {
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL evolution advance error reason=not_playing\n");
        return;
    }
    GameSyncHomeDaylight(game);
    SpaceAdvanceTime(game->debugControl.evolutionAdvanceDays);
    GameEcologyRestoreClock(game);
    SimulationClockReset(&game->simulationClock, SpaceElapsedSimulationTime());
    game->ecologyCatchupPending = WorldIsSurfaceActive() &&
        !PlanetEcologyAdvanceActiveRegionsBudgetedAt(
            (int)floorf(game->player.position.x),
            (int)floorf(game->player.position.z), 128,
            SpaceElapsedSimulationTime(), GameDebugEvolutionDaylight(game), 8u);
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL evolution advance ok days=%.3f\n",
                      game->debugControl.evolutionAdvanceDays);
}

static void GameDebugToggleEvolutionAtlas(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING) {
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL evolution atlas error reason=not_playing\n");
        return;
    }
    game->biologyAtlasOpen = !game->biologyAtlasOpen;
    game->biologyAtlasSlot = EvolutionCatalogFirstSpeciesSlot();
    game->cursorReleased = game->biologyAtlasOpen;
    if (game->biologyAtlasOpen) EnableCursor();
    else DisableCursor();
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL evolution atlas %s species=%d\n",
                      game->biologyAtlasOpen ? "open" : "closed",
                      EvolutionCatalogSpeciesCount());
}

static GameDebugDispatchResult GameDebugDispatchSystemCommand(
    GameRuntime *game, DebugControlCommand command)
{
    switch (command) {
    case DEBUG_CONTROL_COMMAND_START:
        if (game->screen == SCREEN_START) return GAME_DEBUG_DISPATCH_START;
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL start ignored reason=already_playing\n");
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_TERRAIN_FLAT:
    case DEBUG_CONTROL_COMMAND_TERRAIN_VARIED:
    case DEBUG_CONTROL_COMMAND_TERRAIN_HISTORY:
    case DEBUG_CONTROL_COMMAND_WORLD_SEED:
        /* New-world selection lives in game_debug_world.c so this dispatcher
         * stays inside its size limit. */
        GameDebugWorldDispatch(game, command);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_SCREENSHOT:
        if (game->screen == SCREEN_PLAYING) {
            game->screenshotPending = true;
            DebugControlReply(&game->debugControl,
                              "DEBUG_CONTROL screenshot scheduled\n");
        } else {
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL screenshot error reason=not_playing\n");
        }
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_STATUS:
        GameDebugReplyStatus(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_HUD_DEBUG_ON:
    case DEBUG_CONTROL_COMMAND_HUD_DEBUG_OFF:
        game->showDebug = command == DEBUG_CONTROL_COMMAND_HUD_DEBUG_ON;
        if (game->showDebug) game->showHelp = false;
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL hud debug enabled=%d\n",
                          game->showDebug ? 1 : 0);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_PAUSE:
        GameDebugApplyPauseCommand(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_WORLD_TOPOLOGY:
        GameDebugTopologyReply(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_WATER_DEBUG:
        WorldRenderSetWaterDebug(game->debugControl.waterDebugEnabled);
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL water debug enabled=%d\n",
            game->debugControl.waterDebugEnabled ? 1 : 0);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_WATER_DEBUG_THROUGH:
        WorldRenderSetWaterDebugThrough(game->debugControl.waterDebugThrough);
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL water debug through=%d\n",
            game->debugControl.waterDebugThrough ? 1 : 0);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_STREAM_AUDIT:
        GameStreamAuditStart(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_STREAM_WAIT:
        GameStreamWaitStart(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_WORLD_TIME: {
        float time = game->debugControl.worldTime;
        game->debugDayTimeOverride = time;
        game->debugDayTimeOverrideEnabled = time >= 0.0f;
        float daylight = 0.0f;
        float sunset = 0.0f;
        DayNightFactors(game->dayTime, &daylight, &sunset);
        if (time >= 0.0f) {
            DayNightFactors(time, &daylight, &sunset);
            DebugControlReply(&game->debugControl,
                              "DEBUG_CONTROL world time ok time=%.6f daylight=%.6f\n",
                              time, daylight);
        } else {
            DebugControlReply(&game->debugControl,
                              "DEBUG_CONTROL world time ok mode=auto\n");
        }
        return GAME_DEBUG_DISPATCH_HANDLED;
    }
    case DEBUG_CONTROL_COMMAND_CGA_INSPECT:
        GameDebugCgaInspect(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_CGA_VISIBLE:
        GameDebugCgaVisible(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_VIEW_DISTANCE:
        ChunksSetRenderDistance((int)game->debugControl.viewDistance);
        DebugControlReply(&game->debugControl, "DEBUG_CONTROL view distance chunks=%d\n", ChunksRenderDistance());
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_QUIT:
        game->quitRequested = true;
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL quit accepted\n");
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_INVALID:
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL error reason=unknown_command\n");
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_NONE:
        return GAME_DEBUG_DISPATCH_HANDLED;
    default:
        return GAME_DEBUG_DISPATCH_UNHANDLED;
    }
}

static GameDebugDispatchResult GameDebugDispatchPersistenceCommand(
    GameRuntime *game, DebugControlCommand command)
{
    switch (command) {
    case DEBUG_CONTROL_COMMAND_SAVE:
        if (game->screen == SCREEN_PLAYING) {
            GameSyncHomeDaylight(game);
            GameSaveMap(&game->player);
            DebugControlReply(&game->debugControl,
                              "DEBUG_CONTROL save result=%s\n",
                              GameNoticeCurrent());
        } else {
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL save error reason=not_playing\n");
        }
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_UNDO:
        if (game->screen == SCREEN_PLAYING && UndoBlockEdit()) {
            DebugControlReply(&game->debugControl,
                              "DEBUG_CONTROL undo result=ok\n");
        } else {
            DebugControlReply(&game->debugControl,
                              "DEBUG_CONTROL undo result=empty\n");
        }
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_REDO:
        if (game->screen == SCREEN_PLAYING && RedoBlockEdit()) {
            DebugControlReply(&game->debugControl,
                              "DEBUG_CONTROL redo result=ok\n");
        } else {
            DebugControlReply(&game->debugControl,
                              "DEBUG_CONTROL redo result=empty\n");
        }
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_LOAD:
        if (game->screen == SCREEN_PLAYING) {
            if (GameLoadMap(&game->player)) {
                GameEcologyRestoreClock(game);
                SimulationClockReset(&game->simulationClock,
                                     SpaceElapsedSimulationTime());
                game->scriptedShipInput = (ShipControlInput){ 0 };
                game->scriptedShipInputFrames = 0u;
                game->scriptedShipInputFrameCarry = 0.0f;
                game->landingTransition = (LandingTransition){ 0 };
                game->wasInSpace = WorldIsSpaceActive();
                game->entitiesWorldActive = WorldIsSurfaceActive();
                game->entitiesWorldDimension = WorldCurrentSurfaceId();
                game->cursorReleased = false;
                DisableCursor();
            }
            DebugControlReply(&game->debugControl,
                              "DEBUG_CONTROL load result=%s\n",
                              GameNoticeCurrent());
        } else {
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL load error reason=not_playing\n");
        }
        return GAME_DEBUG_DISPATCH_HANDLED;
    default:
        return GAME_DEBUG_DISPATCH_UNHANDLED;
    }
}

static GameDebugDispatchResult GameDebugDispatchMapCommand(
    GameRuntime *game, DebugControlCommand command)
{
    switch (command) {
    case DEBUG_CONTROL_COMMAND_MAP:
        GameDebugToggleSurfaceMap(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_MAP_LAYER_LIQUIDS:
        HomeWorldMapSetSubsurfaceLiquidsVisible(
            game->debugControl.mapLiquidsVisible);
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL map layer liquids enabled=%d\n",
            HomeWorldMapSubsurfaceLiquidsVisible() ? 1 : 0);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_CUBE: {
        if (!GameDebugEnterCubeWorld(game)) {
            GameDebugMarkCommandError(game, "cube_activation_failed");
            DebugControlReply(&game->debugControl,
                "DEBUG_CONTROL surface debug error reason=cube_activation_failed\n");
            return GAME_DEBUG_DISPATCH_HANDLED;
        }
        DebugControlReply(&game->debugControl,
            "DEBUG_CONTROL surface debug ok world=cube seed=%u\n",
            game->debugControl.surfaceDebugSeed);
        return GAME_DEBUG_DISPATCH_HANDLED;
    }
    case DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_HOME:
    case DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_PLANET: {
        bool homeWorld = command == DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_HOME;
        SolarBodyStyle styles[] = {
            SOLAR_STYLE_TEMPERATE, SOLAR_STYLE_DESERT, SOLAR_STYLE_ICE,
            SOLAR_STYLE_LAVA, SOLAR_STYLE_CRATER
        };
        SolarBodyStyle style = styles[game->debugControl.surfaceDebugStyle];
        bool activated = GameDebugTransitionSurface(
            game, homeWorld, style, game->debugControl.surfaceDebugSeed);
        if (!activated) {
            GameDebugMarkCommandError(game, "surface_activation_failed");
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL surface debug error reason=activation_failed\n");
            return GAME_DEBUG_DISPATCH_HANDLED;
        }
        game->landingTransition = (LandingTransition){ 0 };
        game->wasInSpace = false;
        game->entitiesWorldActive = true;
        game->entitiesWorldDimension = WorldCurrentSurfaceId();
        game->cursorReleased = false;
        DisableCursor();
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL surface debug ok world=%s seed=%u\n",
            homeWorld ? "home" : PlanetWorldName(),
            homeWorld ? WorldGetSeed() : PlanetWorldSeed());
        return GAME_DEBUG_DISPATCH_HANDLED;
    }
    case DEBUG_CONTROL_COMMAND_SURFACE_POSE:
        if (!GameDebugSetSurfacePose(game)) {
            GameDebugMarkCommandError(game, "surface_pose_failed");
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL surface pose error reason=invalid_pose\n");
        }
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_SURFACE_ADDRESS:
        if (!GameDebugSetSurfaceAddress(game)) {
            GameDebugMarkCommandError(game, "surface_address_failed");
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL surface address error reason=invalid_address\n");
        }
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_MARKER_ADD:
        GameDebugMarkerAdd(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_MARKER_LIST:
        GameDebugMarkerList(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_MARKER_TARGET:
        GameDebugMarkerTarget(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_MARKER_REMOVE:
        GameDebugMarkerRemove(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    default:
        return GAME_DEBUG_DISPATCH_UNHANDLED;
    }
}

static GameDebugDispatchResult GameDebugDispatchFluidCommand(
    GameRuntime *game, DebugControlCommand command)
{
    switch (command) {
    case DEBUG_CONTROL_COMMAND_FLUID_INSPECT:
        GameDebugInspectFluid(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_FLUID_SET:
        GameDebugSetFluid(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_FLUID_STEP:
        GameDebugStepFluid(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    default:
        return GAME_DEBUG_DISPATCH_UNHANDLED;
    }
}

static GameDebugDispatchResult GameDebugDispatchMotionCommand(
    GameRuntime *game, DebugControlCommand command)
{
    switch (command) {
    case DEBUG_CONTROL_COMMAND_TELEPORT:
        GameDebugTeleport(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_LOOK:
        GameDebugLook(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_INPUT_MODE:
        game->manualInputEnabled = game->debugControl.manualInput;
        game->scriptedInputFrames = game->scriptedShipInputFrames = 0;
        game->scriptedInputFirstFrame = false;
        game->scriptedShipInputFrameCarry = 0;
        DebugControlReply(&game->debugControl, "DEBUG_CONTROL input mode=%s\n",
                          game->manualInputEnabled ? "manual" : "scripted");
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_FLY:
        game->player.floating = game->debugControl.flyEnabled;
        game->player.velocity = (Vector3){ 0 };
        game->player.onGround = false;
        DebugControlReply(&game->debugControl, "DEBUG_CONTROL fly enabled=%d\n",
                          game->player.floating ? 1 : 0);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_INPUT:
        GameDebugApplyInput(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_MOUSE_LEFT:
        GameInteractionQueueDebugMouse(game, false);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_MOUSE_RIGHT:
        GameInteractionQueueDebugMouse(game, true);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_UI_CLICK:
        if (game->screen != SCREEN_START && game->screen != SCREEN_PLAYING) {
            GameDebugMarkCommandError(game, "invalid_ui_screen");
            return GAME_DEBUG_DISPATCH_HANDLED;
        }
        if (!InputSourceInjectClick(
                &game->inputSource,
                (InputPoint){ game->debugControl.uiX,
                              game->debugControl.uiY },
                INPUT_BUTTON_LEFT)) {
            GameDebugMarkCommandError(game, "input_rejected");
            return GAME_DEBUG_DISPATCH_HANDLED;
        }
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ui click scheduled x=%.3f y=%.3f\n",
                          game->debugControl.uiX, game->debugControl.uiY);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_UI_KEY: {
        InputKey key = INPUT_KEY_UNKNOWN;
        if (!InputKeyFromName(game->debugControl.uiKey, &key)) {
            GameDebugMarkCommandError(game, "invalid_ui_key");
            return GAME_DEBUG_DISPATCH_HANDLED;
        }
        if (game->screen != SCREEN_START && game->screen != SCREEN_PLAYING) {
            GameDebugMarkCommandError(game, "invalid_ui_screen");
            return GAME_DEBUG_DISPATCH_HANDLED;
        }
        if (!InputSourceInjectKey(&game->inputSource, key, true) ||
            !InputSourceInjectKey(&game->inputSource, key, false)) {
            GameDebugMarkCommandError(game, "input_rejected");
            return GAME_DEBUG_DISPATCH_HANDLED;
        }
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ui key scheduled key=%s\n",
                          InputKeyName(key));
        return GAME_DEBUG_DISPATCH_HANDLED;
    }
    case DEBUG_CONTROL_COMMAND_VIEW:
        GameDebugSetView(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    default:
        return GAME_DEBUG_DISPATCH_UNHANDLED;
    }
}

static GameDebugDispatchResult GameDebugDispatchShipCommand(
    GameRuntime *game, DebugControlCommand command)
{
    switch (command) {
    case DEBUG_CONTROL_COMMAND_SHIP_BEGIN:
        GameDebugBeginShip(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_SHIP_ENTER:
        GameDebugEnterShip(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_SHIP_INPUT:
        GameDebugApplyShipInput(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_SHIP_EXHAUST:
        GameDebugSetShipExhaust(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_SHIP_DUST:
        GameDebugEmitShipDust(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    default:
        return GAME_DEBUG_DISPATCH_UNHANDLED;
    }
}

static GameDebugDispatchResult GameDebugDispatchEvolutionCommand(
    GameRuntime *game, DebugControlCommand command)
{
    switch (command) {
    case DEBUG_CONTROL_COMMAND_EVOLUTION_SCENARIO: {
        const float *v = game->debugControl.scenarioValues;
        int danger = game->debugControl.scenarioDanger;
        FaunaBehaviorInput input = {
            .needs = {v[0], v[1], v[2], v[3]},
            .environment = {.activityRatio = 1, .foodAvailability = v[4],
                .waterAvailability = v[5], .shelterAvailability = v[6],
                .stormPressure = v[7], .alerted = danger >= 1, .threatened = danger == 2},
            /* Fixtures assume a reachable better resource when local supply is low. */
            .food = {.yaw = 0.5f, .improvement = 1 - v[4], .shouldSeek = v[4] < 0.2f},
            .water = {.yaw = -0.5f, .improvement = 1 - v[5], .shouldSeek = v[5] < 0.2f},
            .shelter = {.yaw = 1, .improvement = 1 - v[6], .shouldSeek = v[6] < 0.2f},
            .foodDependence = 1, .waterDependence = 1,
            .baseThinkInterval = 1, .baseWanderDuration = 1, .wanderRoll = 99
        };
        if (game->screen != SCREEN_PLAYING || !game->biologyAtlasOpen ||
            !BiologyAtlasPreviewScenarioConfigure(&input)) {
            GameDebugMarkCommandError(game, "open_evolution_preview_first");
            return GAME_DEBUG_DISPATCH_HANDLED;
        }
        FaunaBehaviorDecision decision = BiologyAtlasPreviewScenarioInfo().decision;
        DebugControlReply(&game->debugControl,
            "DEBUG_CONTROL evolution scenario ok action=%s utility=%.3f "
            "energy=%.3f hydration=%.3f fatigue=%.3f stress=%.3f "
            "food=%.3f water=%.3f shelter=%.3f storm=%.3f danger=%d\n",
            FaunaBehaviorActionName(decision.action), decision.utility,
            v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], danger);
        return GAME_DEBUG_DISPATCH_HANDLED;
    }
    case DEBUG_CONTROL_COMMAND_EVOLUTION_POSE: {
        if (game->screen != SCREEN_PLAYING || !game->biologyAtlasOpen ||
            !BiologyAtlasPreviewPoseConfigure(game->debugControl.previewAction,
                game->debugControl.previewSpeed, game->debugControl.previewPhase,
                game->debugControl.previewTurn)) {
            GameDebugMarkCommandError(game, "open_evolution_preview_first");
            return GAME_DEBUG_DISPATCH_HANDLED;
        }
        BiologyPreviewPoseInfo pose = BiologyAtlasPreviewPoseInfo();
        DebugControlReply(&game->debugControl,
            "DEBUG_CONTROL evolution pose ok action=%s speed=%.3f phase=%.3f turn=%.3f frozen=%d\n",
            FaunaBehaviorActionName((FaunaBehaviorAction)pose.action),
            pose.speed, pose.phase, pose.turn, pose.frozen);
        return GAME_DEBUG_DISPATCH_HANDLED;
    }
    case DEBUG_CONTROL_COMMAND_EVOLUTION_INSPECT:
        GameDebugInspectEvolution(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_EVOLUTION_FOCUS:
        GameDebugFocusEvolution(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_EVOLUTION_REGION:
    case DEBUG_CONTROL_COMMAND_EVOLUTION_BOOTSTRAP:
        GameDebugReplyEvolutionRegion(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_EVOLUTION_ADVANCE:
        GameDebugAdvanceEvolution(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_EVOLUTION_PREVIEW:
        if (game->screen != SCREEN_PLAYING) return GAME_DEBUG_DISPATCH_UNHANDLED;
        BiologyAtlasPreviewConfigure(
            PlanetWorldIsActive() ? PlanetWorldSeed() : WorldGetSeed(),
            (EvolutionArchetype)game->debugControl.previewType,
            game->debugControl.previewSeed, game->debugControl.previewLength,
            game->debugControl.previewWidth, game->debugControl.previewAppendages,
            game->debugControl.previewSilhouette != 0);
        game->biologyAtlasOpen = true;
        EnableCursor();
        DebugControlReply(&game->debugControl, "DEBUG_CONTROL evolution preview ok\n");
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_EVOLUTION_ATLAS:
        GameDebugToggleEvolutionAtlas(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_EVOLUTION_CATALOG:
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL evolution catalog ok species=%d "
                          "individuals=%d surface=%u\n",
                          EvolutionCatalogSpeciesCount(),
                          EvolutionCatalogIndividualCount(),
                          EcologyWorldSurfaceId());
        return GAME_DEBUG_DISPATCH_HANDLED;
    default:
        return GAME_DEBUG_DISPATCH_UNHANDLED;
    }
}

static GameDebugDispatchResult GameDispatchDebugCommandValue(
    GameRuntime *game, DebugControlCommand command)
{
    game->debugCommandFailed = false;
    game->debugCommandFailure[0] = '\0';
    GameDebugDispatchResult result = (GameDebugLandformDispatch(game, command) ||
        GameDebugGeologyDispatch(game, command)) ?
        GAME_DEBUG_DISPATCH_HANDLED : GameDebugDispatchSystemCommand(game, command);
    if (result == GAME_DEBUG_DISPATCH_UNHANDLED) {
        result = GameDebugDispatchPersistenceCommand(game, command);
    }
    if (result == GAME_DEBUG_DISPATCH_UNHANDLED) {
        result = GameDebugDispatchMapCommand(game, command);
    }
    if (result == GAME_DEBUG_DISPATCH_UNHANDLED) {
        result = GameDebugDispatchFluidCommand(game, command);
    }
    if (result == GAME_DEBUG_DISPATCH_UNHANDLED) {
        result = GameDebugDispatchWeatherCommand(game, command);
    }
    if (result == GAME_DEBUG_DISPATCH_UNHANDLED) {
        result = GameDebugDispatchMotionCommand(game, command);
    }
    if (result == GAME_DEBUG_DISPATCH_UNHANDLED) {
        result = GameDebugDispatchShipCommand(game, command);
    }
    if (result == GAME_DEBUG_DISPATCH_UNHANDLED) {
        result = GameDebugDispatchEvolutionCommand(game, command);
    }
    if (result == GAME_DEBUG_DISPATCH_UNHANDLED &&
        GameDebugBlockDispatch(game, command)) result = GAME_DEBUG_DISPATCH_HANDLED;
    if (result == GAME_DEBUG_DISPATCH_UNHANDLED &&
        GameDebugFloraDispatch(game, command)) result = GAME_DEBUG_DISPATCH_HANDLED;
    if (result == GAME_DEBUG_DISPATCH_HANDLED && game->debugCommandFailed) {
        GameDebugInvalidateDiagnostics(game);
        return GAME_DEBUG_DISPATCH_ERROR;
    }
    if (result == GAME_DEBUG_DISPATCH_HANDLED ||
        result == GAME_DEBUG_DISPATCH_START) {
        GameDebugInvalidateDiagnostics(game);
    }
    return result;
}

static bool GameDebugDslBool(DebugDslValue *outValue, bool value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_BOOL,
        .as.boolean = value
    };
    return true;
}

static bool GameDebugDslNumber(DebugDslValue *outValue, double value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_NUMBER,
        .as.number = value
    };
    return true;
}

static bool GameDebugDslString(DebugDslValue *outValue, const char *value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_STRING,
        .as.string = value ? value : ""
    };
    return true;
}

static bool GameDebugDslVec3(DebugDslValue *outValue, Vector3 value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_VEC3,
        .as.vec3 = { value.x, value.y, value.z }
    };
    return true;
}

static HitResult GameDebugDslTarget(const GameRuntime *game)
{
    Vector3 eye = {
        game->player.position.x,
        game->player.position.y + EYE_HEIGHT,
        game->player.position.z
    };
    Vector3 direction = Vector3Normalize(
        Vector3Subtract(game->camera.target, game->camera.position));
    return RaycastBlocksFiltered(
        eye, direction, REACH_DISTANCE, RAYCAST_BLOCK_SOLID);
}

/* Legacy resolver contains the remaining built-ins in their historical
 * order.  The small registry below keeps module ownership explicit while
 * allowing future groups to be split out without changing lookup precedence.
 */
typedef bool (*GameDebugDslResolverFn)(GameRuntime *game, const char *name,
                                       DebugDslValue *outValue);

typedef enum GameDebugDslLegacyPhase {
    GAME_DEBUG_DSL_CORE,
    GAME_DEBUG_DSL_PLAYER,
    GAME_DEBUG_DSL_STREAM,
    GAME_DEBUG_DSL_TAIL
} GameDebugDslLegacyPhase;

static bool GameDebugDslResolveLegacy(GameRuntime *game, const char *name,
                                      DebugDslValue *outValue,
                                      GameDebugDslLegacyPhase phase);

static bool GameDebugDslResolveBlock(GameRuntime *game, const char *name,
                                     DebugDslValue *outValue)
{
    return GameDebugBlockDslResolve(game, name, outValue);
}

static bool GameDebugDslResolveFlora(GameRuntime *game, const char *name,
                                     DebugDslValue *outValue)
{
    return GameDebugFloraDslResolve(game, name, outValue);
}

static bool GameDebugDslResolveTopology(GameRuntime *game, const char *name,
                                        DebugDslValue *outValue)
{
    return GameDebugTopologyDslResolve(game, name, outValue);
}

static bool GameDebugDslResolveEvolution(GameRuntime *game, const char *name,
                                          DebugDslValue *outValue)
{
    if (!game || !name || !outValue || strncmp(name, "evolution.", 10u) != 0)
        return false;
    const char *field = name + 10u;
    if (strncmp(field, "scenario_", 9) == 0) {
        BiologyPreviewScenarioInfo scenario = BiologyAtlasPreviewScenarioInfo();
        bool valid = game->screen == SCREEN_PLAYING && game->biologyAtlasOpen && scenario.valid;
        if (!strcmp(field, "scenario_valid")) return GameDebugDslBool(outValue, valid);
        if (!strcmp(field, "scenario_action")) return GameDebugDslString(outValue,
            valid ? FaunaBehaviorActionName(scenario.decision.action) : "none");
        if (!strcmp(field, "scenario_utility")) return GameDebugDslNumber(outValue,
            valid ? scenario.decision.utility : 0);
        return false;
    }
    if (strncmp(field, "preview_", 8) == 0) {
        BiologyPreviewPoseInfo pose = BiologyAtlasPreviewPoseInfo();
        if (!strcmp(field, "preview_action")) return GameDebugDslString(outValue,
            FaunaBehaviorActionName((FaunaBehaviorAction)pose.action));
        if (!strcmp(field, "preview_speed")) return GameDebugDslNumber(outValue, pose.speed);
        if (!strcmp(field, "preview_phase")) return GameDebugDslNumber(outValue, pose.phase);
        if (!strcmp(field, "preview_turn")) return GameDebugDslNumber(outValue, pose.turn);
        if (!strcmp(field, "preview_frozen")) return GameDebugDslBool(outValue, pose.frozen);
        return false;
    }
    if (*field != 'r' && *field != 'l' && *field != 'b' && *field != 'h' &&
        *field != 'o' && *field != 'c')
        return false;
    PlanetEvolutionRegion evolution = { 0 };
    bool available = game->screen == SCREEN_PLAYING && WorldIsSurfaceActive();
    if (available) available = PlanetEcologyEvolutionRegionAt(
        (int)floorf(game->player.position.x),
        (int)floorf(game->player.position.z),
        GameDebugEvolutionDaylight(game), &evolution);
    if (!strcmp(field, "region_available")) return GameDebugDslBool(outValue, available);
    if (!strcmp(field, "lineage_count")) return GameDebugDslNumber(outValue, available ? evolution.lineageCount : 0u);
    if (!strcmp(field, "bootstrap_generation")) return GameDebugDslNumber(outValue, available ? evolution.bootstrapGeneration : 0u);
    if (!strcmp(field, "bootstrap_complete")) return GameDebugDslBool(outValue, available && evolution.bootstrapComplete);
    if (!strcmp(field, "herbivore_density")) return GameDebugDslNumber(outValue, available ? evolution.herbivoreDensity : 0.0f);
    if (!strcmp(field, "omnivore_density")) return GameDebugDslNumber(outValue, available ? evolution.omnivoreDensity : 0.0f);
    if (!strcmp(field, "carnivore_density")) return GameDebugDslNumber(outValue, available ? evolution.carnivoreDensity : 0.0f);
    return false;
}

static bool GameDebugDslResolveEcologyStorage(GameRuntime *game,
    const char *name, DebugDslValue *outValue)
{
    if (!game || !name || !outValue || strncmp(name, "ecology.", 8u) != 0)
        return false;
    const char *field = name + 8u;
    if (!strcmp(field, "catchup_pending"))
        return GameDebugDslBool(outValue, game->ecologyCatchupPending);
    PlanetEcologyStorageStats stats = PlanetEcologyGetStorageStats();
    if (!strcmp(field, "hot_regions"))
        return GameDebugDslNumber(outValue, (double)stats.activeRegions);
    if (!strcmp(field, "cold_regions"))
        return GameDebugDslNumber(outValue, (double)stats.coldRegions);
    if (!strcmp(field, "stored_regions"))
        return GameDebugDslNumber(outValue,
            (double)(stats.activeRegions + stats.coldRegions));
    if (!strcmp(field, "storage_bytes"))
        return GameDebugDslNumber(outValue, (double)stats.allocatedBytes);
    if (!strcmp(field, "storage_limit"))
        return GameDebugDslNumber(outValue, (double)stats.maxRegions);
    if (!strcmp(field, "failed_admissions"))
        return GameDebugDslNumber(outValue, (double)stats.failedAdmissions);
    return false;
}

static bool GameDebugDslResolveWeather(GameRuntime *game, const char *name,
                                       DebugDslValue *outValue)
{
    return GameDebugWeatherDslResolve(game, name, outValue);
}

static bool GameDebugDslResolveHydrology(GameRuntime *game, const char *name,
                                          DebugDslValue *outValue)
{
    return GameDebugHydrologyDslResolve(game, name, outValue);
}

static bool GameDebugDslResolveLod(GameRuntime *game, const char *name,
                                   DebugDslValue *outValue)
{
    if (strncmp(name, "stream.lod_", 11u) != 0) return false;
    const ChunkDiagnostics *diagnostics =
        GameDebugChunkDiagnostics(game, true);
    return GameDebugLodDslResolveSnapshot(diagnostics, name, outValue);
}

static bool GameDebugDslResolveCore(GameRuntime *game, const char *name,
                                    DebugDslValue *outValue)
{
    return GameDebugDslResolveLegacy(game, name, outValue,
                                     GAME_DEBUG_DSL_CORE);
}

static bool GameDebugDslResolvePlayer(GameRuntime *game, const char *name,
                                      DebugDslValue *outValue)
{
    if (!strcmp(name, "player.manual_input")) {
        outValue->type = DEBUG_DSL_VALUE_BOOL;
        outValue->as.boolean = !game->debugControlEnabled || game->manualInputEnabled;
        return true;
    }

    return GameDebugDslResolveLegacy(game, name, outValue,
                                     GAME_DEBUG_DSL_PLAYER);
}

static bool GameDebugDslResolveStream(GameRuntime *game, const char *name,
                                      DebugDslValue *outValue)
{
    return GameDebugDslResolveLegacy(game, name, outValue,
                                     GAME_DEBUG_DSL_STREAM);
}

static bool GameDebugDslResolveTail(GameRuntime *game, const char *name,
                                    DebugDslValue *outValue)
{
    return GameDebugDslResolveLegacy(game, name, outValue,
                                     GAME_DEBUG_DSL_TAIL);
}

static bool GameDebugDslResolveLegacy(GameRuntime *game, const char *name,
                                      DebugDslValue *outValue,
                                      GameDebugDslLegacyPhase phase)
{
    const ChunkDiagnostics *chunkDiagnostics = NULL;
    if ((phase == GAME_DEBUG_DSL_STREAM || phase == GAME_DEBUG_DSL_TAIL) &&
        strncmp(name, "stream.", 7u) == 0 &&
        strncmp(name, "stream.lod_", 11u) != 0) {
        chunkDiagnostics = GameDebugChunkDiagnostics(game, false);
    }
    if (!game || !name || !outValue) return false;
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "game.screen") == 0) {
        return GameDebugDslString(
            outValue, game->screen == SCREEN_PLAYING ? "playing" : "start");
    }
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "game.paused") == 0) {
        return GameDebugDslBool(outValue, game->paused);
    }
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "debug.frame") == 0) {
        return GameDebugDslNumber(outValue, (double)game->debugFrame);
    }
    const InputFrame *uiFrame = InputSourceFrame(&game->inputSource);
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "ui.pointer_x") == 0) {
        return GameDebugDslNumber(outValue, uiFrame->pointer.x);
    }
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "ui.pointer_y") == 0) {
        return GameDebugDslNumber(outValue, uiFrame->pointer.y);
    }
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "ui.pointer_injected") == 0) {
        return GameDebugDslBool(outValue, uiFrame->injected);
    }
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "ui.start_hovered") == 0) {
        StartPageUiLayout layout = StartPageUiLayoutFor(GetScreenWidth(),
                                                        GetScreenHeight());
        return GameDebugDslBool(
            outValue, UiPointInRect(uiFrame->pointer, layout.start));
    }
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "ui.start_pressed") == 0) {
        StartPageUiLayout layout = StartPageUiLayoutFor(GetScreenWidth(),
                                                        GetScreenHeight());
        return GameDebugDslBool(
            outValue,
            UiPointInRect(uiFrame->pointer, layout.start) &&
                InputFrameButtonPressed(uiFrame, &game->inputEvents,
                                        INPUT_BUTTON_LEFT, false));
    }
    if (phase == GAME_DEBUG_DSL_CORE &&
        strcmp(name, "ui.start_center_x") == 0) {
        StartPageUiLayout layout = StartPageUiLayoutFor(GetScreenWidth(),
                                                        GetScreenHeight());
        return GameDebugDslNumber(outValue,
                                  layout.start.x + layout.start.width / 2.0f);
    }
    if (phase == GAME_DEBUG_DSL_CORE &&
        strcmp(name, "ui.start_center_y") == 0) {
        StartPageUiLayout layout = StartPageUiLayoutFor(GetScreenWidth(),
                                                        GetScreenHeight());
        return GameDebugDslNumber(outValue,
                                  layout.start.y + layout.start.height / 2.0f);
    }
    if (phase == GAME_DEBUG_DSL_CORE &&
        strncmp(name, "ui.import_", 10u) == 0) {
        ImportDialogUiLayout layout = ImportDialogUiLayoutFor(
            GetScreenWidth(), GetScreenHeight());
        if (strcmp(name, "ui.import_open") == 0) {
            return GameDebugDslBool(outValue, game->importDialog.open);
        }
        if (strcmp(name, "ui.import_max_blocks") == 0) {
            return GameDebugDslNumber(outValue, game->importDialog.maxBlocks);
        }
        if (strcmp(name, "ui.import_relief") == 0) {
            return GameDebugDslBool(outValue, game->importDialog.relief);
        }
        if (strcmp(name, "ui.import_plus_center_x") == 0) {
            return GameDebugDslNumber(
                outValue, layout.plus.x + layout.plus.width / 2.0f);
        }
        if (strcmp(name, "ui.import_plus_center_y") == 0) {
            return GameDebugDslNumber(
                outValue, layout.plus.y + layout.plus.height / 2.0f);
        }
        if (strcmp(name, "ui.import_minus_center_x") == 0) {
            return GameDebugDslNumber(
                outValue, layout.minus.x + layout.minus.width / 2.0f);
        }
        if (strcmp(name, "ui.import_minus_center_y") == 0) {
            return GameDebugDslNumber(
                outValue, layout.minus.y + layout.minus.height / 2.0f);
        }
        if (strcmp(name, "ui.import_input_center_x") == 0) {
            return GameDebugDslNumber(
                outValue, layout.input.x + layout.input.width / 2.0f);
        }
        if (strcmp(name, "ui.import_input_center_y") == 0) {
            return GameDebugDslNumber(
                outValue, layout.input.y + layout.input.height / 2.0f);
        }
        return false;
    }
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "ui.screen_width") == 0) {
        return GameDebugDslNumber(outValue, GetScreenWidth());
    }
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "ui.screen_height") == 0) {
        return GameDebugDslNumber(outValue, GetScreenHeight());
    }
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "ui.render_width") == 0) {
        return GameDebugDslNumber(outValue, GetRenderWidth());
    }
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "ui.render_height") == 0) {
        return GameDebugDslNumber(outValue, GetRenderHeight());
    }
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "ui.scale_x") == 0) {
        return GameDebugDslNumber(outValue, GetWindowScaleDPI().x);
    }
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "ui.scale_y") == 0) {
        return GameDebugDslNumber(outValue, GetWindowScaleDPI().y);
    }
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "world.seed") == 0) {
        return GameDebugDslNumber(outValue, WorldGetSeed());
    }
    if (phase == GAME_DEBUG_DSL_CORE && strcmp(name, "world.dimension") == 0) {
        return GameDebugDslString(
            outValue, WorldDimensionName(WorldCurrentDimension()));
    }
    if (phase == GAME_DEBUG_DSL_PLAYER && strcmp(name, "player.position") == 0) {
        return GameDebugDslVec3(outValue, game->player.position);
    }
    if (phase == GAME_DEBUG_DSL_PLAYER && strcmp(name, "player.velocity") == 0) {
        return GameDebugDslVec3(outValue, game->player.velocity);
    }
    if (phase == GAME_DEBUG_DSL_PLAYER &&
        strcmp(name, "player.input_frames") == 0) {
        return GameDebugDslNumber(outValue, game->scriptedInputFrames);
    }

    if (phase == GAME_DEBUG_DSL_PLAYER &&
        strcmp(name, "interaction.mouse_left_mesh_ready") == 0) {
        return GameDebugDslBool(
            outValue, game->debugLeftMeshFrame != 0u &&
                          !game->debugLeftMeshPending);
    }
    if (phase == GAME_DEBUG_DSL_PLAYER &&
        strcmp(name, "interaction.mouse_left_press_frame") == 0) {
        return GameDebugDslNumber(outValue, game->debugLeftPressFrame);
    }
    if (phase == GAME_DEBUG_DSL_PLAYER &&
        strcmp(name, "interaction.mouse_left_handle_frame") == 0) {
        return GameDebugDslNumber(outValue, game->debugLeftHandleFrame);
    }
    if (phase == GAME_DEBUG_DSL_PLAYER &&
        strcmp(name, "interaction.mouse_left_set_block_frame") == 0) {
        return GameDebugDslNumber(outValue, game->debugLeftSetBlockFrame);
    }
    if (phase == GAME_DEBUG_DSL_PLAYER &&
        strcmp(name, "interaction.mouse_left_mesh_frame") == 0) {
        return GameDebugDslNumber(outValue, game->debugLeftMeshFrame);
    }

    if (phase == GAME_DEBUG_DSL_PLAYER && strcmp(name, "perf.enabled") == 0) {
        return GameDebugDslBool(outValue, PerfIsEnabled());
    }
    if (phase == GAME_DEBUG_DSL_PLAYER &&
        strcmp(name, "perf.route_complete") == 0) {
        return GameDebugDslBool(outValue, PerfRouteComplete());
    }
    if (phase == GAME_DEBUG_DSL_PLAYER &&
        strcmp(name, "perf.report_written") == 0) {
        return GameDebugDslBool(outValue, PerfReportWritten());
    }
    if (phase == GAME_DEBUG_DSL_PLAYER &&
        strcmp(name, "perf.report_passed") == 0) {
        return GameDebugDslBool(outValue, PerfReportPassed());
    }

    if (phase == GAME_DEBUG_DSL_PLAYER && strncmp(name, "water.", 6u) == 0) {
        PlayerWaterState water = PlayerWaterStateAt(game->player.position);
        if (strcmp(name, "water.feet_submerged") == 0) {
            return GameDebugDslBool(outValue, water.feetSubmerged);
        }
        if (strcmp(name, "water.body_submerged") == 0) {
            return GameDebugDslBool(outValue, water.bodySubmerged);
        }
        if (strcmp(name, "water.eyes_submerged") == 0) {
            return GameDebugDslBool(outValue, water.eyesSubmerged);
        }
        if (strcmp(name, "water.surface_y") == 0) {
            return GameDebugDslNumber(outValue, water.surfaceY);
        }
    }

    if (phase == GAME_DEBUG_DSL_STREAM &&
        strcmp(name, "stream.surface_ready") == 0) {
        bool ready = game->screen == SCREEN_PLAYING &&
            PlayerMissingSurfaceChunkCount(game->player.position) == 0;
        return GameDebugDslBool(outValue, ready);
    }
    if (phase == GAME_DEBUG_DSL_STREAM &&
        strcmp(name, "stream.missing_surface_chunks") == 0) {
        int missing = game->screen == SCREEN_PLAYING
            ? PlayerMissingSurfaceChunkCount(game->player.position) : -1;
        return GameDebugDslNumber(outValue, missing);
    }
    if (phase == GAME_DEBUG_DSL_STREAM && strncmp(name, "stream.cube.", 12u) == 0) {
        GameCoverageEvidence evidence;
        return GameCoverageEvidenceCapture(game, &evidence) &&
            GameCoverageEvidenceDslResolve(name, &evidence, outValue);
    }
    if (chunkDiagnostics && strncmp(name, "stream.chunks.", 14u) == 0) {
        return GameDebugDiagnosticsDslResolve(game, name, outValue);
    }
    if (phase == GAME_DEBUG_DSL_STREAM &&
        strcmp(name, "stream.audit_complete") == 0) {
        return GameDebugDslBool(
            outValue, !game->streamAudit.active && !game->streamAudit.wait.active);
    }
    if (phase == GAME_DEBUG_DSL_TAIL &&
        strcmp(name, "stream.workers_configured") == 0) {
        return GameDebugDslNumber(outValue,
                                  chunkDiagnostics->workerThreadsConfigured);
    }
    if (phase == GAME_DEBUG_DSL_TAIL &&
        strcmp(name, "stream.workers_started") == 0) {
        return GameDebugDslNumber(outValue,
                                  chunkDiagnostics->workerThreadsStarted);
    }
    if (phase == GAME_DEBUG_DSL_TAIL && strcmp(name, "stream.workers_active") == 0)
        return GameDebugDslNumber(outValue,
                                  chunkDiagnostics->workerThreadsActive);
    if (phase == GAME_DEBUG_DSL_TAIL && strncmp(name, "fluid.", 6u) == 0) {
        FluidSample sample = FluidSampleAt(game->player.position);
        FluidSample inspected = sample;
        if (!game->debugControl.fluidUsePlayerPosition) {
            inspected = FluidSampleAt((Vector3){
                (float)game->debugControl.fluidX + 0.5f,
                (float)game->debugControl.fluidY + 0.5f,
                (float)game->debugControl.fluidZ + 0.5f
            });
        }
        FluidStats stats = FluidGetStats();
        if (strcmp(name, "fluid.volume") == 0) {
            return GameDebugDslNumber(outValue, sample.volume);
        }
        if (strcmp(name, "fluid.inspect_volume") == 0) {
            return GameDebugDslNumber(outValue, inspected.volume);
        }
        if (strcmp(name, "fluid.inspect_surface_y") == 0) {
            return GameDebugDslNumber(outValue, inspected.surfaceY);
        }
        if (strcmp(name, "fluid.inspect_flow_x") == 0) {
            return GameDebugDslNumber(outValue, inspected.velocity.x);
        }
        if (strcmp(name, "fluid.inspect_flow_y") == 0) {
            return GameDebugDslNumber(outValue, inspected.velocity.y);
        }
        if (strcmp(name, "fluid.inspect_flow_z") == 0) {
            return GameDebugDslNumber(outValue, inspected.velocity.z);
        }
        if (strcmp(name, "fluid.surface_y") == 0) {
            return GameDebugDslNumber(outValue, sample.surfaceY);
        }
        if (strcmp(name, "fluid.queue_overflows") == 0) {
            return GameDebugDslNumber(outValue, stats.queueOverflows);
        }
        if (strcmp(name, "fluid.loaded_volume") == 0) {
            return GameDebugDslNumber(outValue, FluidLoadedVolume());
        }
    }

    if (phase == GAME_DEBUG_DSL_TAIL && strncmp(name, "target.", 7u) == 0) {
        HitResult target = GameDebugDslTarget(game);
        if (strcmp(name, "target.hit") == 0) {
            return GameDebugDslBool(outValue, target.hit);
        }
        if (strcmp(name, "target.position") == 0) {
            return GameDebugDslVec3(
                outValue, (Vector3){ target.x, target.y, target.z });
        }
        if (strcmp(name, "target.block") == 0) {
            BlockType block = target.hit
                ? GetBlockAt(target.x, target.y, target.z) : BLOCK_AIR;
            return GameDebugDslString(outValue, BlockName(block));
        }
    }

    if (phase == GAME_DEBUG_DSL_TAIL && strcmp(name, "ship.driving") == 0) {
        return GameDebugDslBool(outValue, ShipIsDriving());
    }
    if (phase == GAME_DEBUG_DSL_TAIL && strcmp(name, "ship.mode") == 0) {
        return GameDebugDslString(outValue, ShipDriveModeName());
    }

    if (phase == GAME_DEBUG_DSL_TAIL &&
        strncmp(name, "render.coverage.", 16u) == 0) {
        ChunkRenderCoverageStats coverage = ChunksGetRenderCoverageStats();
        const char *field = name + 16u;
#define RENDER_COVERAGE_NUMBER(fieldName, fieldValue) \
        if (strcmp(field, fieldName) == 0) \
            return GameDebugDslNumber(outValue, (fieldValue))
        if (strcmp(field, "saturated") == 0)
            return GameDebugDslBool(outValue, coverage.saturated);
        RENDER_COVERAGE_NUMBER("frame_sequence", coverage.frameSequence);
        RENDER_COVERAGE_NUMBER("observation_sequence",
                               coverage.observationSequence);
        RENDER_COVERAGE_NUMBER("requested", coverage.requested);
        RENDER_COVERAGE_NUMBER("admitted", coverage.admitted);
        RENDER_COVERAGE_NUMBER("target_exact",
                               coverage.lod.targetChunks[CHUNK_LOD_EXACT]);
        RENDER_COVERAGE_NUMBER("target_half",
                               coverage.lod.targetChunks[CHUNK_LOD_HALF]);
        RENDER_COVERAGE_NUMBER("target_quarter",
                               coverage.lod.targetChunks[CHUNK_LOD_QUARTER]);
        RENDER_COVERAGE_NUMBER("active_exact",
                               coverage.lod.activeChunks[CHUNK_LOD_EXACT]);
        RENDER_COVERAGE_NUMBER("active_half",
                               coverage.lod.activeChunks[CHUNK_LOD_HALF]);
        RENDER_COVERAGE_NUMBER("active_quarter",
                               coverage.lod.activeChunks[CHUNK_LOD_QUARTER]);
        RENDER_COVERAGE_NUMBER("target_missing_sections",
                               coverage.lod.targetMissingSections);
        RENDER_COVERAGE_NUMBER("main_admission_culled_columns",
                               coverage.mainAdmissionCulledColumns);
        RENDER_COVERAGE_NUMBER("main_distance_culled_columns",
                               coverage.mainDistanceCulledColumns);
        RENDER_COVERAGE_NUMBER("main_candidate_sections",
                               coverage.mainCandidateSections);
        RENDER_COVERAGE_NUMBER("main_frustum_culled_sections",
                               coverage.mainFrustumCulledSections);
        RENDER_COVERAGE_NUMBER("main_visible_sections",
                               coverage.mainVisibleSections);
        RENDER_COVERAGE_NUMBER("main_target_lod_missing_sections",
                               coverage.mainTargetLodMissingSections);
        RENDER_COVERAGE_NUMBER("main_drawn_sections",
                               coverage.mainDrawnSections);
        RENDER_COVERAGE_NUMBER("main_solid_draws", coverage.mainSolidDraws);
        RENDER_COVERAGE_NUMBER("main_water_draws", coverage.mainWaterDraws);
        RENDER_COVERAGE_NUMBER("main_flora_draws", coverage.mainFloraDraws);
        RENDER_COVERAGE_NUMBER("main_solid_triangles",
                               coverage.mainSolidTriangles);
        RENDER_COVERAGE_NUMBER("main_water_triangles",
                               coverage.mainWaterTriangles);
        RENDER_COVERAGE_NUMBER("main_flora_triangles",
                               coverage.mainFloraTriangles);
        RENDER_COVERAGE_NUMBER("shadow_admission_culled_columns",
                               coverage.shadowAdmissionCulledColumns);
        RENDER_COVERAGE_NUMBER("shadow_distance_culled_columns",
                               coverage.shadowDistanceCulledColumns);
        RENDER_COVERAGE_NUMBER("shadow_candidate_sections",
                               coverage.shadowCandidateSections);
        RENDER_COVERAGE_NUMBER("shadow_frustum_culled_sections",
                               coverage.shadowFrustumCulledSections);
        RENDER_COVERAGE_NUMBER("shadow_visible_sections",
                               coverage.shadowVisibleSections);
        RENDER_COVERAGE_NUMBER("shadow_target_lod_missing_sections",
                               coverage.shadowTargetLodMissingSections);
        RENDER_COVERAGE_NUMBER("shadow_drawn_sections",
                               coverage.shadowDrawnSections);
        RENDER_COVERAGE_NUMBER("shadow_solid_draws", coverage.shadowSolidDraws);
        RENDER_COVERAGE_NUMBER("shadow_water_draws", coverage.shadowWaterDraws);
        RENDER_COVERAGE_NUMBER("shadow_flora_draws", coverage.shadowFloraDraws);
        RENDER_COVERAGE_NUMBER("shadow_solid_triangles",
                               coverage.shadowSolidTriangles);
        RENDER_COVERAGE_NUMBER("shadow_water_triangles",
                               coverage.shadowWaterTriangles);
        RENDER_COVERAGE_NUMBER("shadow_flora_triangles",
                               coverage.shadowFloraTriangles);
#undef RENDER_COVERAGE_NUMBER
        return false;
    }

    if (phase == GAME_DEBUG_DSL_TAIL && strncmp(name, "render.", 7u) == 0) {
        WorldWaterRenderDebugInfo water = WorldRenderWaterDebugSnapshot();
        if (strcmp(name, "render.monitor_refresh_hz") == 0) {
            return GameDebugDslNumber(
                outValue, GetMonitorRefreshRate(GetCurrentMonitor()));
        }
        if (strcmp(name, "render.vsync") == 0) {
            return GameDebugDslBool(
                outValue, IsWindowState(FLAG_VSYNC_HINT));
        }
        if (strcmp(name, "render.water_debug") == 0) {
            return GameDebugDslBool(outValue, water.enabled);
        }
        if (strcmp(name, "render.water_debug_through") == 0) {
            return GameDebugDslBool(outValue, water.through);
        }
        if (strcmp(name, "render.water_triangles") == 0) {
            return GameDebugDslNumber(outValue, water.triangleCount);
        }
    }

    if (phase == GAME_DEBUG_DSL_TAIL && strcmp(name, "settings.autosave") == 0) {
        return GameDebugDslBool(outValue, game->autoSaveEnabled);
    }
    return false;
}

static const GameDebugDslResolverFn gameDebugDslResolvers[] = {
    GameDebugLandformResolve,
    GameDebugGeologyResolve,
    GameDebugDslResolveBlock,
    GameDebugDslResolveFlora,
    /* Preserve the historical ordering between each domain group. */
    GameDebugDslResolveCore,
    GameDebugDslResolveTopology,
    GameDebugDslResolveEcologyStorage,
    GameDebugDslResolveEvolution,
    GameDebugDslResolvePlayer,
    GameDebugDslResolveWeather,
    GameDebugDslResolveHydrology,
    GameDebugDslResolveStream,
    GameDebugDslResolveLod,
    GameDebugDslResolveTail
};

static bool GameDebugDslResolve(void *userData, const char *name,
                                DebugDslValue *outValue,
                                DebugDslError *outError)
{
    (void)outError;
    GameRuntime *game = userData;
    if (!game || !name || !outValue) return false;
    if (GameInventoryResolve(game, name, outValue)) return true;
    for (size_t index = 0u;
         index < sizeof(gameDebugDslResolvers) /
                     sizeof(gameDebugDslResolvers[0]);
         index++) {
        if (gameDebugDslResolvers[index](game, name, outValue)) return true;
    }
    return false;
}

static DebugDslCommandResult GameDebugDslCommand(
    void *userData, const char *commandText, DebugDslError *outError)
{
    GameRuntime *game = userData;
    if (strncmp(commandText, "inventory ", 10) == 0) {
        if (GameInventoryDebug(game, commandText)) return DEBUG_DSL_COMMAND_COMPLETE;
        outError->code = DEBUG_DSL_ERROR_CALLBACK;
        snprintf(outError->message, sizeof(outError->message),
                 "invalid inventory command or unavailable UI: %s", commandText);
        return DEBUG_DSL_COMMAND_ERROR;
    }
    DebugControlCommand command = DebugControlParseText(
        &game->debugControl, commandText);
    if (command == DEBUG_CONTROL_COMMAND_NONE ||
        command == DEBUG_CONTROL_COMMAND_INVALID ||
        command == DEBUG_CONTROL_COMMAND_QUIT) {
        outError->code = DEBUG_DSL_ERROR_CALLBACK;
        snprintf(outError->message, sizeof(outError->message),
                 command == DEBUG_CONTROL_COMMAND_QUIT
                     ? "use the DSL exit statement instead of quit"
                     : "unknown debug command: %s",
                 commandText);
        return DEBUG_DSL_COMMAND_ERROR;
    }
    const char *blocked = GameDebugDslCommandBlocked(game, command);
    if (blocked) {
        outError->code = DEBUG_DSL_ERROR_CALLBACK;
        snprintf(outError->message, sizeof(outError->message),
                 "command rejected: %s", blocked);
        return DEBUG_DSL_COMMAND_ERROR;
    }
    GameDebugDispatchResult result = GameDispatchDebugCommandValue(
        game, command);
    if (result == GAME_DEBUG_DISPATCH_ERROR) {
        outError->code = DEBUG_DSL_ERROR_CALLBACK;
        snprintf(outError->message, sizeof(outError->message),
                 "debug command failed: %s", game->debugCommandFailure);
        return DEBUG_DSL_COMMAND_ERROR;
    }
    if (result == GAME_DEBUG_DISPATCH_UNHANDLED) {
        outError->code = DEBUG_DSL_ERROR_CALLBACK;
        snprintf(outError->message, sizeof(outError->message),
                 "unhandled debug command: %s", commandText);
        return DEBUG_DSL_COMMAND_ERROR;
    }
    if (result == GAME_DEBUG_DISPATCH_START) {
        game->debugDslStartRequested = true;
    }
    return DEBUG_DSL_COMMAND_COMPLETE;
}

static void GameDebugDslReportError(GameRuntime *game, const char *source,
                                    const DebugDslError *error)
{
    const char *message = error && error->message[0] != '\0'
        ? error->message : "unknown error";
    DebugControlReply(
        &game->debugControl,
        "DEBUG_SCRIPT error source=%s line=%zu column=%zu code=%s message=%s\n",
        source ? source : "stdin", error ? error->line : 0u,
        error ? error->column : 0u,
        DebugDslErrorCodeName(error ? error->code : DEBUG_DSL_ERROR_CALLBACK),
        message);
    fprintf(stderr, "Debug script %s:%zu:%zu: %s\n",
            source ? source : "stdin", error ? error->line : 0u,
            error ? error->column : 0u, message);
}

void GameDebugScriptStop(GameRuntime *game)
{
    if (!game) return;
    DebugDslExecutorDestroy(game->debugDslExecutor);
    DebugDslScriptDestroy(game->debugDslScript);
    game->debugDslExecutor = NULL;
    game->debugDslScript = NULL;
    game->debugDslBatch = false;
    game->debugDslFromStdin = false;
}

static bool GameDebugDslBegin(GameRuntime *game, const char *source,
                              bool fromStdin, DebugDslError *outError)
{
    DebugDslScript *script = NULL;
    if (!DebugDslParse(source, &script, outError)) return false;
    if (!game->debugDslEnvironment) {
        game->debugDslEnvironment = DebugDslEnvironmentCreate();
    }
    DebugDslExecutor *executor = DebugDslExecutorCreateInEnvironment(
        script, (DebugDslCallbacks){
            .userData = game,
            .resolve = GameDebugDslResolve,
            .command = GameDebugDslCommand
        }, game->debugDslEnvironment);
    if (!executor) {
        DebugDslScriptDestroy(script);
        *outError = (DebugDslError){
            .code = DEBUG_DSL_ERROR_ALLOCATION,
            .line = 1u,
            .column = 1u
        };
        snprintf(outError->message, sizeof(outError->message),
                 "could not create DSL executor");
        return false;
    }
    GameDebugScriptStop(game);
    game->debugDslScript = script;
    game->debugDslExecutor = executor;
    game->debugDslBatch = DebugDslScriptIsBatch(script);
    game->debugDslFromStdin = fromStdin;
    return true;
}

static bool GameDebugDslSourceEndsWithExit(const char *source)
{
    const char *lastStart = NULL;
    size_t lastLength = 0u;
    const char *line = source;
    while (line && *line != '\0') {
        const char *end = strchr(line, '\n');
        if (!end) end = line + strlen(line);
        const char *start = line;
        while (start < end && isspace((unsigned char)*start)) start++;
        const char *trimmedEnd = end;
        while (trimmedEnd > start &&
               isspace((unsigned char)trimmedEnd[-1])) trimmedEnd--;
        if (trimmedEnd > start && trimmedEnd[-1] == ';') {
            trimmedEnd--;
            while (trimmedEnd > start &&
                   isspace((unsigned char)trimmedEnd[-1])) trimmedEnd--;
        }
        if (start < trimmedEnd && *start != '#') {
            lastStart = start;
            lastLength = (size_t)(trimmedEnd - start);
        }
        line = *end == '\0' ? NULL : end + 1;
    }
    return lastStart && lastLength >= 4u &&
           strncmp(lastStart, "exit", 4u) == 0 &&
           (lastLength == 4u || isspace((unsigned char)lastStart[4]));
}

static bool GameDebugReadScript(const char *path, char **outSource,
                                DebugDslError *outError)
{
    enum { MAX_SCRIPT_SIZE = 1024 * 1024 };
    FILE *file = fopen(path, "rb");
    if (!file) {
        outError->code = DEBUG_DSL_ERROR_ARGUMENT;
        snprintf(outError->message, sizeof(outError->message),
                 "cannot open script: %s", strerror(errno));
        return false;
    }
    bool ok = false;
    if (fseek(file, 0, SEEK_END) != 0) goto done;
    long size = ftell(file);
    if (size < 0 || size > MAX_SCRIPT_SIZE || fseek(file, 0, SEEK_SET) != 0) {
        outError->code = DEBUG_DSL_ERROR_LIMIT;
        snprintf(outError->message, sizeof(outError->message),
                 "script exceeds the %d byte limit", MAX_SCRIPT_SIZE);
        goto done;
    }
    char *source = malloc((size_t)size + 1u);
    if (!source) {
        outError->code = DEBUG_DSL_ERROR_ALLOCATION;
        snprintf(outError->message, sizeof(outError->message),
                 "could not allocate script buffer");
        goto done;
    }
    if (fread(source, 1u, (size_t)size, file) != (size_t)size) {
        free(source);
        outError->code = DEBUG_DSL_ERROR_ARGUMENT;
        snprintf(outError->message, sizeof(outError->message),
                 "could not read complete script");
        goto done;
    }
    source[size] = '\0';
    *outSource = source;
    ok = true;
done:
    fclose(file);
    return ok;
}

bool GameDebugScriptLoad(GameRuntime *game)
{
    if (!game || !game->debugScriptEnabled) return true;
    DebugDslError error = { 0 };
    if (game->debugScriptPathInvalid) {
        error.code = DEBUG_DSL_ERROR_ARGUMENT;
        snprintf(error.message, sizeof(error.message),
                 "invalid or repeated --debug-script option");
        GameDebugDslReportError(game, "command-line", &error);
        game->processExitCode = 2;
        return false;
    }
    char *source = NULL;
    if (!GameDebugReadScript(game->debugScriptPath, &source, &error)) {
        GameDebugDslReportError(game, game->debugScriptPath, &error);
        game->processExitCode = 2;
        return false;
    }
    bool batchIntent = GameDebugDslSourceEndsWithExit(source);
    bool parsed = GameDebugDslBegin(game, source, false, &error);
    free(source);
    if (!parsed) {
        GameDebugDslReportError(game, game->debugScriptPath, &error);
        if (batchIntent) {
            game->processExitCode = 3;
            return false;
        }
        return true;
    }
    DebugControlReply(
        &game->debugControl,
        "DEBUG_SCRIPT loaded path=%s batch=%d\n",
        game->debugScriptPath, game->debugDslBatch ? 1 : 0);
    return true;
}

static void GameDebugDslResetInput(GameRuntime *game)
{
    DebugDslStdinReset(&game->debugDslStdin);
}

static void GameDebugDslConsumeStdin(GameRuntime *game)
{
    char line[DEBUG_CONTROL_BUFFER_SIZE];
    DebugControlReadResult result = DebugControlReadLine(
        &game->debugControl, line, sizeof(line));
    if (result == DEBUG_CONTROL_READ_NONE) {
        return;
    }
    if (result == DEBUG_CONTROL_READ_EOF) {
        if (DebugDslStdinHasPending(&game->debugDslStdin)) {
            DebugDslError error = {
                .code = DEBUG_DSL_ERROR_SYNTAX,
                .line = 1u,
                .column = 1u
            };
            snprintf(error.message, sizeof(error.message),
                     "unterminated stdin DSL block at EOF");
            GameDebugDslReportError(game, "stdin", &error);
            GameDebugDslResetInput(game);
        }
        return;
    }
    if (result == DEBUG_CONTROL_READ_ERROR) {
        DebugDslError error = {
            .code = DEBUG_DSL_ERROR_ARGUMENT,
            .line = 0u,
            .column = 0u
        };
        snprintf(error.message, sizeof(error.message), "stdin read failed");
        GameDebugDslReportError(game, "stdin", &error);
        return;
    }
    DebugDslStdinAppendResult append =
        DebugDslStdinAppendLine(&game->debugDslStdin, line);
    if (append == DEBUG_DSL_STDIN_APPEND_LIMIT) {
        DebugDslError error = {
            .code = DEBUG_DSL_ERROR_LIMIT,
            .line = 1u,
            .column = 1u
        };
        snprintf(error.message, sizeof(error.message),
                 "stdin DSL block exceeds %u bytes",
                 DEBUG_DSL_STDIN_BUFFER_SIZE - 1u);
        GameDebugDslReportError(game, "stdin", &error);
        GameDebugDslResetInput(game);
        return;
    }
    if (append == DEBUG_DSL_STDIN_APPEND_INVALID ||
        append == DEBUG_DSL_STDIN_APPEND_PENDING) return;

    DebugDslError error = { 0 };
    if (!GameDebugDslBegin(game, DebugDslStdinSource(&game->debugDslStdin),
                           true, &error)) {
        GameDebugDslReportError(game, "stdin", &error);
    }
    GameDebugDslResetInput(game);
}

static bool GameDebugDslStep(GameRuntime *game)
{
    if (!game->debugDslExecutor) return false;
    game->debugDslStartRequested = false;
    DebugDslError error = { 0 };
    DebugDslStepResult result;
    if (game->debugStreamWaitFailed) {
        error.code = DEBUG_DSL_ERROR_TIMEOUT;
        snprintf(error.message, sizeof(error.message), "%s",
                 game->debugStreamWaitFailure[0]
                     ? game->debugStreamWaitFailure : "stream wait timed out");
        game->debugStreamWaitFailed = false;
        result = DebugDslExecutorAbort(game->debugDslExecutor, &error);
    } else {
        result = DebugDslExecutorStep(game->debugDslExecutor, &error);
    }
    bool startRequested = game->debugDslStartRequested;
    if (result == DEBUG_DSL_STEP_RUNNING) return startRequested;

    bool batch = game->debugDslBatch && !game->debugDslFromStdin;
    bool fromStdin = game->debugDslFromStdin;
    if (result == DEBUG_DSL_STEP_ERROR) {
        GameDebugDslReportError(
            game, fromStdin ? "stdin" : game->debugScriptPath, &error);
        GameDebugScriptStop(game);
        if (batch) {
            game->processExitCode = 3;
            game->processExitRequested = true;
            game->quitRequested = true;
        }
        return startRequested;
    }
    if (result == DEBUG_DSL_STEP_EXIT) {
        int exitCode = DebugDslExecutorExitCode(game->debugDslExecutor);
        DebugControlReply(&game->debugControl,
                          "DEBUG_SCRIPT exit code=%d\n", exitCode);
        GameDebugScriptStop(game);
        game->processExitCode = exitCode;
        game->processExitRequested = true;
        game->quitRequested = true;
        return startRequested;
    }
    DebugControlReply(
        &game->debugControl, "DEBUG_SCRIPT complete source=%s\n",
        fromStdin ? "stdin" : game->debugScriptPath);
    GameDebugScriptStop(game);
    return startRequested;
}

bool GameDispatchDebugCommand(GameRuntime *game)
{
    if (!game || !game->debugControlEnabled) return false;
    if (game->streamAudit.wait.active) return false;
    if (!game->debugDslExecutor && game->debugStdinEnabled) {
        GameDebugDslConsumeStdin(game);
    }
    return GameDebugDslStep(game);
}
