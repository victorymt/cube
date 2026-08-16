#include "raylib.h"
#include "raymath.h"

#include "app/game_debug.h"
#include "world/chunks.h"
#include "core/debug_control.h"
#include "ecology/ecology.h"
#include "ecology/entity.h"
#include "ecology/evolution.h"
#include "ecology/evolution_catalog.h"
#include "world/fluid.h"
#include "app/game_interaction.h"
#include "app/game_runtime.h"
#include "gameplay/map_markers.h"
#include "gameplay/player.h"
#include "gameplay/ship.h"
#include "presentation/homeworld_map.h"
#include "presentation/render.h"
#include "space/space.h"
#include "world/terrain.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <math.h>
#include <stdint.h>
#include <strings.h>

static void GameDebugReplyStatus(GameRuntime *game)
{
    PlayerWaterState water = PlayerWaterStateAt(game->player.position);
    FluidSample fluid = FluidSampleAt(game->player.position);
    FluidStats fluidStats = FluidGetStats();
    uint64_t loadedVolume = FluidLoadedVolume();
    ChunkWaterRenderDebugInfo waterRender = { 0 };
    ChunksGetWaterRenderDebugInfo(game->player.position, &waterRender);
    BathymetrySample bathymetry = {
        .seaLevel = -1,
        .seabedY = (int)floorf(game->player.position.y),
        .waterDepth = 0,
        .zone = BATHYMETRY_ZONE_LAND,
        .material = BATHYMETRY_MATERIAL_ROCK
    };
    if (PlanetWorldIsActive()) {
        bathymetry = PlanetBathymetryAt(
            (int)floorf(game->player.position.x),
            (int)floorf(game->player.position.z));
    } else if (HomeWorldSurfaceIsActive()) {
        bathymetry = TerrainBathymetryAt(
            (int)floorf(game->player.position.x),
            (int)floorf(game->player.position.z), WorldTerrainMode());
    }
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL status screen=%s seed=%u dimension=%s "
        "position=%.6f,%.6f,%.6f velocity=%.6f,%.6f,%.6f "
        "water=%d,%d,%d depth=%.6f surface=%.6f "
        "fluid_volume=%u fluid_surface=%.6f "
        "fluid_flow=%.6f,%.6f,%.6f fluid_queue=%u "
        "fluid_processed=%u fluid_edits=%u fluid_total=%llu "
        "fluid_overflows=%u "
        "bathymetry=%s seabed=%d water_column=%d material=%s "
        "chunk=%d,%d,%d chunk_loaded=%d neighbors=0x%X "
        "water_triangles=%d section_water_triangles=%d "
        "ship_driving=%d ship_mode=%s ship_exhaust=%.3f "
        "ship_input_frames=%u third_person=%d "
        "camera_inside_solid=%d autosave=%d\n",
        game->screen == SCREEN_PLAYING ? "playing" : "start", WorldGetSeed(),
        WorldDimensionName(WorldCurrentDimension()),
        game->player.position.x, game->player.position.y,
        game->player.position.z, game->player.velocity.x,
        game->player.velocity.y, game->player.velocity.z,
        water.feetSubmerged ? 1 : 0, water.bodySubmerged ? 1 : 0,
        water.eyesSubmerged ? 1 : 0, water.eyeDepth, water.surfaceY,
        (unsigned)fluid.volume, fluid.surfaceY, fluid.velocity.x,
        fluid.velocity.y, fluid.velocity.z, fluidStats.activeCells,
        fluidStats.lastProcessedCells, fluidStats.editCount,
        (unsigned long long)loadedVolume, fluidStats.queueOverflows,
        BathymetryZoneName(bathymetry.zone), bathymetry.seabedY,
        bathymetry.waterDepth, BathymetryMaterialName(bathymetry.material),
        waterRender.cx, waterRender.cz, waterRender.sectionY,
        waterRender.chunkLoaded ? 1 : 0, waterRender.neighborLoadedMask,
        waterRender.triangleCount, waterRender.sectionTriangleCount,
        ShipIsDriving() ? 1 : 0, ShipDriveModeName(),
        ShipVisualExhaustIntensity(), game->scriptedShipInputFrames,
        game->thirdPerson ? 1 : 0,
        PlayerCameraPositionInsideSolid(game->camera.position) ? 1 : 0,
        game->autoSaveEnabled ? 1 : 0);
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
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL marker add error reason=invalid_color\n");
        return;
    }
    uint32_t id = 0u;
    if (!MapMarkersCreate(GameDebugMarkerSurface(),
                          game->debugControl.marker.x,
                          game->debugControl.marker.z,
                          game->debugControl.marker.name, color, &id)) {
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

static void GameDebugTeleport(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL teleport error reason=not_playing\n");
        return;
    }
    game->player.position = (Vector3){ game->debugControl.teleport.x,
                                       game->debugControl.teleport.y,
                                       game->debugControl.teleport.z };
    game->player.velocity = Vector3Zero();
    game->player.yaw = game->debugControl.teleport.yaw;
    game->player.pitch = game->debugControl.teleport.pitch;
    game->player.onGround = false;
    PlayerResetRuntimeState(&game->player);
    game->scriptedInputFrames = 0u;
    game->scriptedInputFirstFrame = false;
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL teleport ok position=%.6f,%.6f,%.6f\n",
                      game->player.position.x, game->player.position.y,
                      game->player.position.z);
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
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ship enter ignored reason=already_driving\n");
        return;
    }

    ShipLocatorTarget target = { 0 };
    if (!ShipLocatorTargetAt(game->player.position, &target)) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ship enter error reason=no_recorded_ship\n");
        return;
    }
    if (target.status != SHIP_LOCATOR_TARGET_LOCAL) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ship enter error reason=ship_not_local\n");
        return;
    }
    if (!ShipTryEnter(target.blockX, target.blockY, target.blockZ,
                      &game->player)) {
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
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL ship exhaust error reason=not_driving\n");
        return;
    }
    float demand = game->debugControl.shipExhaustDemand;
    if (!ShipSetDebugExhaust(&game->player, demand)) {
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

static void GameDebugReplyEvolutionRegion(GameRuntime *game)
{
    PlanetEvolutionRegion evolution = { 0 };
    float daylight = 0.0f;
    float sunset = 0.0f;
    PlanetLightState light = { 0 };
    if (PlanetWorldLightStateAt(game->player.position, &light)) {
        daylight = light.daylight;
    } else {
        DayNightFactors(game->dayTime, &daylight, &sunset);
    }
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
    SpaceAdvanceTime(game->debugControl.evolutionAdvanceDays);
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

bool GameDispatchDebugCommand(GameRuntime *game)
{
    switch (DebugControlPoll(&game->debugControl)) {
    case DEBUG_CONTROL_COMMAND_START:
        if (game->screen == SCREEN_START) return true;
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL start ignored reason=already_playing\n");
        break;
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
        break;
    case DEBUG_CONTROL_COMMAND_STATUS:
        GameDebugReplyStatus(game);
        break;
    case DEBUG_CONTROL_COMMAND_SAVE:
        if (game->screen == SCREEN_PLAYING) {
            SaveMap(&game->player);
            DebugControlReply(&game->debugControl,
                              "DEBUG_CONTROL save result=%s\n",
                              WorldGetImportMessage());
        } else {
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL save error reason=not_playing\n");
        }
        break;
    case DEBUG_CONTROL_COMMAND_LOAD:
        if (game->screen == SCREEN_PLAYING) {
            LoadMap(&game->player);
            game->scriptedShipInput = (ShipControlInput){ 0 };
            game->scriptedShipInputFrames = 0u;
            game->scriptedShipInputFrameCarry = 0.0f;
            game->landingTransition = (LandingTransition){ 0 };
            game->wasInSpace = WorldIsSpaceActive();
            game->entitiesWorldActive = WorldIsSurfaceActive();
            game->entitiesWorldDimension = WorldCurrentSurfaceId();
            game->cursorReleased = false;
            DisableCursor();
            DebugControlReply(&game->debugControl,
                              "DEBUG_CONTROL load result=%s\n",
                              WorldGetImportMessage());
        } else {
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL load error reason=not_playing\n");
        }
        break;
    case DEBUG_CONTROL_COMMAND_MAP:
        GameDebugToggleSurfaceMap(game);
        break;
    case DEBUG_CONTROL_COMMAND_MARKER_ADD:
        GameDebugMarkerAdd(game);
        break;
    case DEBUG_CONTROL_COMMAND_MARKER_LIST:
        GameDebugMarkerList(game);
        break;
    case DEBUG_CONTROL_COMMAND_MARKER_TARGET:
        GameDebugMarkerTarget(game);
        break;
    case DEBUG_CONTROL_COMMAND_MARKER_REMOVE:
        GameDebugMarkerRemove(game);
        break;
    case DEBUG_CONTROL_COMMAND_FLUID_INSPECT:
        GameDebugInspectFluid(game);
        break;
    case DEBUG_CONTROL_COMMAND_FLUID_SET:
        GameDebugSetFluid(game);
        break;
    case DEBUG_CONTROL_COMMAND_FLUID_STEP:
        GameDebugStepFluid(game);
        break;
    case DEBUG_CONTROL_COMMAND_TELEPORT:
        GameDebugTeleport(game);
        break;
    case DEBUG_CONTROL_COMMAND_INPUT:
        GameDebugApplyInput(game);
        break;
    case DEBUG_CONTROL_COMMAND_SHIP_BEGIN:
        GameDebugBeginShip(game);
        break;
    case DEBUG_CONTROL_COMMAND_SHIP_ENTER:
        GameDebugEnterShip(game);
        break;
    case DEBUG_CONTROL_COMMAND_SHIP_INPUT:
        GameDebugApplyShipInput(game);
        break;
    case DEBUG_CONTROL_COMMAND_SHIP_EXHAUST:
        GameDebugSetShipExhaust(game);
        break;
    case DEBUG_CONTROL_COMMAND_SHIP_DUST:
        GameDebugEmitShipDust(game);
        break;
    case DEBUG_CONTROL_COMMAND_VIEW:
        GameDebugSetView(game);
        break;
    case DEBUG_CONTROL_COMMAND_EVOLUTION_INSPECT:
        GameDebugInspectEvolution(game);
        break;
    case DEBUG_CONTROL_COMMAND_EVOLUTION_FOCUS:
        GameDebugFocusEvolution(game);
        break;
    case DEBUG_CONTROL_COMMAND_EVOLUTION_REGION:
    case DEBUG_CONTROL_COMMAND_EVOLUTION_BOOTSTRAP:
        GameDebugReplyEvolutionRegion(game);
        break;
    case DEBUG_CONTROL_COMMAND_EVOLUTION_ADVANCE:
        GameDebugAdvanceEvolution(game);
        break;
    case DEBUG_CONTROL_COMMAND_EVOLUTION_ATLAS:
        GameDebugToggleEvolutionAtlas(game);
        break;
    case DEBUG_CONTROL_COMMAND_EVOLUTION_CATALOG:
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL evolution catalog ok species=%d "
                          "individuals=%d surface=%u\n",
                          EvolutionCatalogSpeciesCount(),
                          EvolutionCatalogIndividualCount(),
                          WorldCurrentSurfaceId());
        break;
    case DEBUG_CONTROL_COMMAND_QUIT:
        game->quitRequested = true;
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL quit accepted\n");
        break;
    case DEBUG_CONTROL_COMMAND_INVALID:
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL error reason=unknown_command\n");
        break;
    case DEBUG_CONTROL_COMMAND_NONE:
    default:
        break;
    }
    return false;
}
