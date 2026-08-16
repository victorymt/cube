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
#include "gameplay/player.h"
#include "presentation/render.h"
#include "space/space.h"
#include "world/terrain.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <math.h>
#include <stdint.h>

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
        PlayerCameraPositionInsideSolid(game->camera.position) ? 1 : 0,
        game->autoSaveEnabled ? 1 : 0);
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
