#include "raylib.h"
#include "raymath.h"

#include "app/game_debug.h"
#include "app/game_debug_trace.h"
#include "app/game_save.h"
#include "world/chunks.h"
#include "core/debug_control.h"
#include "core/debug_dsl.h"
#include "core/game_notice.h"
#include "core/perf.h"
#include "ecology/ecology.h"
#include "ecology/entity.h"
#include "ecology/evolution.h"
#include "ecology/evolution_catalog.h"
#include "world/fluid.h"
#include "app/game_interaction.h"
#include "app/game_runtime.h"
#include "app/game_stream_audit.h"
#include "app/game_world_transition.h"
#include "gameplay/interaction.h"
#include "gameplay/map_markers.h"
#include "gameplay/player.h"
#include "gameplay/ship.h"
#include "presentation/homeworld_map.h"
#include "presentation/render.h"
#include "space/space_runtime.h"
#include "space/space_state.h"
#include "world/terrain.h"
#include "world/surface_topology.h"
#include "world/weather.h"
#include "world/weather_impact.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void GameDebugMarkCommandError(GameRuntime *game, const char *reason)
{
    if (!game) return;
    game->debugCommandFailed = true;
    snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure),
             "%s", reason ? reason : "command_failed");
}

static int GameDebugCountSurfaceFaceVertices(
    const Chunk *chunk, const ChunkSection *section,
    int worldX, int worldY, int worldZ, int nx, int ny, int nz)
{
    if (!chunk || !section || !section->hasModel ||
        section->model.meshCount <= 0 || !section->model.meshes) return 0;
    const Mesh *mesh = &section->model.meshes[0];
    if (!mesh->vertices || !mesh->normals || mesh->vertexCount <= 0) return 0;

    int radialBase = section->sectionY * SURFACE_SECTION_HEIGHT;
    float chunkOriginX = (float)(chunk->cx * CHUNK_SIZE);
    float chunkOriginZ = (float)(chunk->cz * CHUNK_SIZE);
    SurfaceFrame anchorFrame = SurfaceLocalFrameAtOffset(
        0.0f, 0.0f, radialBase);
    int matches = 0;
    for (int cornerA = 0; cornerA <= 1; cornerA++) {
        for (int cornerB = 0; cornerB <= 1; cornerB++) {
            int cornerX = worldX;
            int cornerY = worldY;
            int cornerZ = worldZ;
            if (nx != 0) {
                cornerX += nx > 0 ? 1 : 0;
                cornerY += cornerA;
                cornerZ += cornerB;
            } else if (ny != 0) {
                cornerX += cornerA;
                cornerY += ny > 0 ? 1 : 0;
                cornerZ += cornerB;
            } else {
                cornerX += cornerA;
                cornerY += cornerB;
                cornerZ += nz > 0 ? 1 : 0;
            }
            float offsetX = (float)cornerX - chunkOriginX;
            float offsetZ = (float)cornerZ - chunkOriginZ;
            float localY = (float)(cornerY - radialBase);
            SurfaceFrame vertexFrame = SurfaceLocalFrameAtOffset(
                offsetX, offsetZ, radialBase);
            Vector3 planet = Vector3Add(
                vertexFrame.origin, Vector3Scale(vertexFrame.up, localY));
            Vector3 expected = SurfaceFramePlanetToLocal(
                &anchorFrame, planet);
            Vector3 planetNormal = Vector3Add(
                Vector3Scale(vertexFrame.east, (float)nx),
                Vector3Scale(vertexFrame.up, (float)ny));
            planetNormal = Vector3Add(
                planetNormal, Vector3Scale(vertexFrame.north, (float)nz));
            Vector3 expectedNormal = {
                Vector3DotProduct(planetNormal, anchorFrame.east),
                Vector3DotProduct(planetNormal, anchorFrame.up),
                Vector3DotProduct(planetNormal, anchorFrame.north)
            };
            for (int vertex = 0; vertex < mesh->vertexCount; vertex++) {
                Vector3 actual = {
                    mesh->vertices[vertex * 3],
                    mesh->vertices[vertex * 3 + 1],
                    mesh->vertices[vertex * 3 + 2]
                };
                Vector3 normal = {
                    mesh->normals[vertex * 3],
                    mesh->normals[vertex * 3 + 1],
                    mesh->normals[vertex * 3 + 2]
                };
                if (Vector3DistanceSqr(actual, expected) < 0.000001f &&
                    Vector3DotProduct(normal, expectedNormal) > 0.999f) {
                    matches++;
                }
            }
        }
    }
    return matches;
}

static void GameDebugReplyStatus(GameRuntime *game)
{
    Vector3 aimEye = {
        game->player.position.x,
        game->player.position.y + EYE_HEIGHT,
        game->player.position.z
    };
    Vector3 aimDirection = Vector3Normalize(
        Vector3Subtract(game->camera.target, game->camera.position));
    HitResult targeted = RaycastBlocksFiltered(
        aimEye, aimDirection, REACH_DISTANCE, RAYCAST_BLOCK_SOLID);
    BlockType targetedBlock = targeted.hit
        ? GetBlockAt(targeted.x, targeted.y, targeted.z) : BLOCK_AIR;
    BlockType targetNeighbor = targeted.hit
        ? GetBlockAt(targeted.x + targeted.nx, targeted.y + targeted.ny,
                     targeted.z + targeted.nz)
        : BLOCK_AIR;
    int targetCx = 0;
    int targetCz = 0;
    int targetLx = 0;
    int targetLz = 0;
    const ChunkSection *targetSection = NULL;
    BlockType targetStoredBlock = BLOCK_AIR;
    bool targetStored = false;
    if (targeted.hit) {
        WorldToChunkLocal(targeted.x, targeted.z, &targetCx, &targetCz,
                          &targetLx, &targetLz);
        const Chunk *targetChunk = FindChunk(targetCx, targetCz);
        targetSection = targetChunk
            ? ChunkGetSectionConst(
                  targetChunk, SurfaceSectionYFromBlockY(targeted.y))
            : NULL;
        targetStored = targetChunk && ChunkTryGetLocalBlock(
            targetChunk, targetLx, targeted.y, targetLz,
            &targetStoredBlock);
    }
    int targetSolidVertices = targetSection && targetSection->hasModel &&
                                      targetSection->model.meshes
        ? targetSection->model.meshes[0].vertexCount : 0;
    const Chunk *targetChunk = targeted.hit ? FindChunk(targetCx, targetCz)
                                             : NULL;
    int targetFaceVertices = targeted.hit ? GameDebugCountSurfaceFaceVertices(
        targetChunk, targetSection, targeted.x, targeted.y, targeted.z,
        targeted.nx, targeted.ny, targeted.nz) : 0;
    bool targetBaseGenerated = false;
    bool targetBaseExposed = false;
    BlockType targetBaseBlock = BLOCK_AIR;
    if (targeted.hit && HomeWorldSurfaceIsActive()) {
        int targetSectionY = SurfaceSectionYFromBlockY(targeted.y);
        Chunk staged = {
            .cx = targetCx,
            .cz = targetCz,
            .spherical = targetChunk ? targetChunk->spherical : false,
            .surfaceAddress = targetChunk
                ? targetChunk->surfaceAddress : (SurfaceAddress){ 0 }
        };
        targetBaseGenerated = GenerateChunkTerrainSectionBase(
            &staged, targetCx, targetCz, targetSectionY,
            WorldTerrainMode());
        const ChunkSection *baseSection = ChunkGetSectionConst(
            &staged, targetSectionY);
        targetBaseExposed = TerrainSectionHasExposedFaces(
            baseSection, targetCx, targetCz, targetSectionY,
            WorldTerrainMode());
        ChunkTryGetLocalBlock(&staged, targetLx, targeted.y, targetLz,
                              &targetBaseBlock);
        ChunkClearBlockStorage(&staged);
    }
    PlayerWaterState water = PlayerWaterStateAt(game->player.position);
    int missingSurfaceChunks =
        PlayerMissingSurfaceChunkCount(game->player.position);
    FluidSample fluid = FluidSampleAt(game->player.position);
    FluidStats fluidStats = FluidGetStats();
    uint64_t loadedVolume = FluidLoadedVolume();
    ChunkWaterRenderDebugInfo waterRender = { 0 };
    ChunksGetWaterRenderDebugInfo(game->player.position, &waterRender);
    WorldWaterRenderDebugInfo waterPass = WorldRenderWaterDebugSnapshot();
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
        "surface_ready=%d player_missing_surface_chunks=%d "
        "chunk=%d,%d,%d chunk_loaded=%d neighbors=0x%X "
        "water_triangles=%d section_water_triangles=%d "
        "water_debug=%d water_debug_through=%d water_visible_sections=%d water_draw_items=%d "
        "water_draw_triangles=%d water_nearest=%d,%d,%d,%d "
        "ship_driving=%d ship_mode=%s ship_exhaust=%.3f "
        "ship_input_frames=%u third_person=%d "
        "target=%d,%d,%d target_hit=%d target_block=%s "
        "target_normal=%d,%d,%d target_neighbor=%s "
        "target_stored=%d target_stored_block=%s target_section=%d "
        "target_dirty=%d target_stamp=%u target_solid_vertices=%d "
        "target_face_vertices=%d target_base_generated=%d "
        "target_base_exposed=%d target_base_block=%s "
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
        missingSurfaceChunks == 0 ? 1 : 0, missingSurfaceChunks,
        waterRender.cx, waterRender.cz, waterRender.sectionY,
        waterRender.chunkLoaded ? 1 : 0, waterRender.neighborLoadedMask,
        waterRender.triangleCount, waterRender.sectionTriangleCount,
        waterPass.enabled ? 1 : 0, waterPass.through ? 1 : 0,
        waterPass.visibleSectionCount,
        waterPass.drawItemCount, waterPass.triangleCount,
        waterPass.hasNearest ? 1 : 0, waterPass.nearestChunkX,
        waterPass.nearestChunkZ, waterPass.nearestSectionY,
        ShipIsDriving() ? 1 : 0, ShipDriveModeName(),
        ShipVisualExhaustIntensity(), game->scriptedShipInputFrames,
        game->thirdPerson ? 1 : 0,
        targeted.x, targeted.y, targeted.z, targeted.hit ? 1 : 0,
        BlockName(targetedBlock),
        targeted.nx, targeted.ny, targeted.nz, BlockName(targetNeighbor),
        targetStored ? 1 : 0, BlockName(targetStoredBlock),
        targetSection ? 1 : 0,
        targetSection && targetSection->dirty ? 1 : 0,
        targetSection ? targetSection->dirtyStamp : 0u, targetSolidVertices,
        targetFaceVertices, targetBaseGenerated ? 1 : 0,
        targetBaseExposed ? 1 : 0, BlockName(targetBaseBlock),
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
    GameDebugTraceEvent(game, "teleport");
}

static void GameDebugLook(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL look error reason=not_playing\n");
        return;
    }
    if (game->debugControl.lookRelative) {
        game->player.yaw += game->debugControl.lookYaw;
        game->player.pitch = Clamp(
            game->player.pitch + game->debugControl.lookPitch,
            -1.45f, 1.45f);
    } else {
        game->player.yaw = game->debugControl.lookYaw;
        game->player.pitch = game->debugControl.lookPitch;
    }
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL look ok yaw=%.6f pitch=%.6f\n",
                      game->player.yaw, game->player.pitch);
    GameDebugTraceEvent(game, "look");
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

typedef enum GameDebugDispatchResult {
    GAME_DEBUG_DISPATCH_UNHANDLED = 0,
    GAME_DEBUG_DISPATCH_HANDLED,
    GAME_DEBUG_DISPATCH_START,
    GAME_DEBUG_DISPATCH_ERROR
} GameDebugDispatchResult;

static GameDebugDispatchResult GameDebugDispatchSystemCommand(
    GameRuntime *game, DebugControlCommand command)
{
    switch (command) {
    case DEBUG_CONTROL_COMMAND_START:
        if (game->screen == SCREEN_START) return GAME_DEBUG_DISPATCH_START;
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL start ignored reason=already_playing\n");
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
    case DEBUG_CONTROL_COMMAND_LOAD:
        if (game->screen == SCREEN_PLAYING) {
            GameLoadMap(&game->player);
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
    case DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_HOME:
    case DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_PLANET: {
        bool homeWorld = command == DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_HOME;
        SolarBodyStyle styles[] = {
            SOLAR_STYLE_TEMPERATE, SOLAR_STYLE_DESERT, SOLAR_STYLE_ICE,
            SOLAR_STYLE_LAVA, SOLAR_STYLE_CRATER
        };
        SolarBodyStyle style = styles[game->debugControl.surfaceDebugStyle];
        HomeWorldMapClose();
        bool activated = GameWorldTransitionDebugSurface(
            &game->player, homeWorld, style,
            game->debugControl.surfaceDebugSeed);
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

static bool GameDebugLocalClimate(const GameRuntime *game,
                                  LocalClimateState *outClimate)
{
    if (!game || !outClimate || !WorldIsSurfaceActive() ||
        !isfinite(game->player.position.x) ||
        !isfinite(game->player.position.z)) {
        return false;
    }
    return WeatherLocalClimateAtWorldTime(
        (int)floorf(game->player.position.x),
        (int)floorf(game->player.position.z),
        SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()),
        outClimate);
}

static void GameDebugInspectWeather(GameRuntime *game)
{
    WeatherFieldSample weather = WeatherCurrentSample();
    WeatherVisualState visual = WeatherVisualStateAtWorld(
        game->player.position,
        SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()), 0.5f);
    WeatherImpactStats impacts = WeatherImpactGetStats();
    LocalClimateState climate = { 0 };
    bool haveClimate = GameDebugLocalClimate(game, &climate);
    char genera[256] = "none";
    size_t generaLength = 0u;
    for (int value = WEATHER_CLOUD_GENUS_CIRRUS;
         value < WEATHER_CLOUD_GENUS_COUNT; value++) {
        WeatherCloudGenus genus = (WeatherCloudGenus)value;
        if (!WeatherSampleHasCloudGenus(weather, genus)) continue;
        int written = snprintf(
            genera + generaLength, sizeof(genera) - generaLength,
            "%s%s", generaLength > 0u ? "," : "", WeatherCloudGenusName(genus));
        if (written < 0 || (size_t)written >= sizeof(genera) - generaLength) {
            genera[sizeof(genera) - 1u] = '\0';
            break;
        }
        generaLength += (size_t)written;
    }
    char layers[384] = "none";
    size_t layerLength = 0u;
    for (unsigned index = 0u; index < visual.cloudLayerCount; index++) {
        WeatherCloudVisualLayer layer = visual.cloudLayers[index];
        int written = snprintf(
            layers + layerLength, sizeof(layers) - layerLength,
            "%s%s:%.3f:%.2f:%.2f", layerLength > 0u ? "," : "",
            WeatherCloudGenusName(layer.genus), layer.coverage,
            layer.baseHeight, layer.thickness);
        if (written < 0 || (size_t)written >= sizeof(layers) - layerLength) {
            layers[sizeof(layers) - 1u] = '\0';
            break;
        }
        layerLength += (size_t)written;
    }
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL weather inspect ok climate=%s phenomenon=%s "
        "temperature_k=%.3f temperature_c=%.3f pressure_atm=%.6f "
        "humidity=%.6f dew_point_k=%.3f wet_bulb_k=%.3f "
        "wind=%.6f gust=%.6f visibility=%.6f rain=%.6f snow=%.6f "
        "sleet=%.6f freezing_rain=%.6f hail=%.6f lightning=%.6f "
        "fog=%.6f dust=%.6f rainbow=%.6f aurora=%.6f "
        "cloud_genus=%s cloud_genera=%s cloud_layers=%u "
        "cloud_layer_data=%s cloud_forced_frames=%u "
        "forced_frames=%u damage=%d surfaces=%u fires=%u\n",
        haveClimate ? ClimateRegimeName(climate.regime) : "unavailable",
        WeatherPhenomenonName(weather.dominantPhenomenon),
        weather.temperatureK, weather.temperatureK - 273.15f,
        weather.pressureAtm, weather.relativeHumidity, weather.dewPointK,
        weather.wetBulbK, weather.wind, weather.gust, weather.visibility,
        weather.rain, weather.snow, weather.sleet, weather.freezingRain,
        weather.hail, weather.lightning, weather.fog, weather.dust,
        weather.rainbow, weather.aurora,
        WeatherCloudGenusName(weather.dominantCloudGenus), genera,
        visual.cloudLayerCount, layers, WeatherForcedCloudFramesRemaining(),
        WeatherForcedFramesRemaining(),
        WeatherImpactEnabled() ? 1 : 0, impacts.surfaceCount,
        impacts.activeFires);
}

static GameDebugDispatchResult GameDebugDispatchWeatherCommand(
    GameRuntime *game, DebugControlCommand command)
{
    switch (command) {
    case DEBUG_CONTROL_COMMAND_WEATHER_INSPECT:
        GameDebugInspectWeather(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_WEATHER_FORCE: {
        WeatherPhenomenon phenomenon;
        if (!WeatherPhenomenonFromName(
                game->debugControl.weatherPhenomenon, &phenomenon) ||
            !WeatherForcePhenomenon(
                phenomenon, game->debugControl.weatherIntensity,
                game->debugControl.weatherFrames)) {
            GameDebugMarkCommandError(game, "invalid_weather_force");
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL weather force error reason=invalid_arguments\n");
            return GAME_DEBUG_DISPATCH_HANDLED;
        }
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL weather force ok phenomenon=%s intensity=%.6f "
            "frames=%u\n",
            WeatherPhenomenonName(phenomenon),
            game->debugControl.weatherIntensity,
            game->debugControl.weatherFrames);
        return GAME_DEBUG_DISPATCH_HANDLED;
    }
    case DEBUG_CONTROL_COMMAND_WEATHER_CLOUD_FORCE: {
        WeatherCloudGenus genus;
        if (!WeatherCloudGenusFromName(
                game->debugControl.weatherCloudGenus, &genus) ||
            genus == WEATHER_CLOUD_GENUS_NONE ||
            !WeatherForceCloudGenus(
                genus, game->debugControl.weatherCloudCoverage,
                game->debugControl.weatherCloudFrames)) {
            GameDebugMarkCommandError(game, "invalid_weather_cloud");
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL weather cloud error reason=invalid_arguments\n");
            return GAME_DEBUG_DISPATCH_HANDLED;
        }
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL weather cloud ok genus=%s coverage=%.6f frames=%u\n",
            WeatherCloudGenusName(genus),
            game->debugControl.weatherCloudCoverage,
            game->debugControl.weatherCloudFrames);
        return GAME_DEBUG_DISPATCH_HANDLED;
    }
    case DEBUG_CONTROL_COMMAND_WEATHER_CLOUD_CLEAR:
        WeatherClearForcedCloud();
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL weather cloud clear ok\n");
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_WEATHER_CLEAR:
        WeatherClearForced();
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL weather clear ok\n");
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_WEATHER_DAMAGE:
        game->settings.weatherDamageEnabled =
            game->debugControl.weatherDamageEnabled;
        WeatherImpactSetEnabled(game->settings.weatherDamageEnabled);
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL weather damage enabled=%d\n",
            WeatherImpactEnabled() ? 1 : 0);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_WEATHER_STEP: {
        WeatherImpactStepTicks(game->debugControl.weatherTicks,
                               game->player.position,
                               WeatherCurrentSample());
        WeatherImpactStats stats = WeatherImpactGetStats();
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL weather step ok ticks=%u total_ticks=%llu "
            "surfaces=%u fires=%u damage_events=%u\n",
            game->debugControl.weatherTicks,
            (unsigned long long)stats.ticks, stats.surfaceCount,
            stats.activeFires, stats.blockDamageEvents);
        return GAME_DEBUG_DISPATCH_HANDLED;
    }
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
    case DEBUG_CONTROL_COMMAND_INPUT:
        GameDebugApplyInput(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
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
    case DEBUG_CONTROL_COMMAND_EVOLUTION_ATLAS:
        GameDebugToggleEvolutionAtlas(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_EVOLUTION_CATALOG:
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL evolution catalog ok species=%d "
                          "individuals=%d surface=%u\n",
                          EvolutionCatalogSpeciesCount(),
                          EvolutionCatalogIndividualCount(),
                          WorldCurrentSurfaceId());
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
    GameDebugDispatchResult result = GameDebugDispatchSystemCommand(
        game, command);
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
    if (result == GAME_DEBUG_DISPATCH_HANDLED && game->debugCommandFailed) {
        return GAME_DEBUG_DISPATCH_ERROR;
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

static bool GameDebugDslResolve(void *userData, const char *name,
                                DebugDslValue *outValue,
                                DebugDslError *outError)
{
    GameRuntime *game = userData;
    (void)outError;
    if (!game || !name || !outValue) return false;

    if (strcmp(name, "game.screen") == 0) {
        return GameDebugDslString(
            outValue, game->screen == SCREEN_PLAYING ? "playing" : "start");
    }
    if (strcmp(name, "world.seed") == 0) {
        return GameDebugDslNumber(outValue, WorldGetSeed());
    }
    if (strcmp(name, "world.dimension") == 0) {
        return GameDebugDslString(
            outValue, WorldDimensionName(WorldCurrentDimension()));
    }
    if (strcmp(name, "player.position") == 0) {
        return GameDebugDslVec3(outValue, game->player.position);
    }
    if (strcmp(name, "player.velocity") == 0) {
        return GameDebugDslVec3(outValue, game->player.velocity);
    }

    if (strcmp(name, "perf.enabled") == 0) {
        return GameDebugDslBool(outValue, PerfIsEnabled());
    }
    if (strcmp(name, "perf.route_complete") == 0) {
        return GameDebugDslBool(outValue, PerfRouteComplete());
    }
    if (strcmp(name, "perf.report_written") == 0) {
        return GameDebugDslBool(outValue, PerfReportWritten());
    }
    if (strcmp(name, "perf.report_passed") == 0) {
        return GameDebugDslBool(outValue, PerfReportPassed());
    }

    if (strncmp(name, "water.", 6u) == 0) {
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

    if (strncmp(name, "weather.", 8u) == 0) {
        WeatherFieldSample weather = WeatherCurrentSample();
        WeatherVisualState visual = WeatherVisualStateAtWorld(
            game->player.position,
            SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()), 0.5f);
        WeatherImpactStats impacts = WeatherImpactGetStats();
        LocalClimateState climate = { 0 };
        bool haveClimate = GameDebugLocalClimate(game, &climate);
        if (strcmp(name, "weather.climate") == 0) {
            return GameDebugDslString(
                outValue, haveClimate ? ClimateRegimeName(climate.regime) :
                                        "unavailable");
        }
        if (strcmp(name, "weather.phenomenon") == 0) {
            return GameDebugDslString(
                outValue,
                WeatherPhenomenonName(weather.dominantPhenomenon));
        }
        if (strcmp(name, "weather.cloud_genus") == 0) {
            return GameDebugDslString(
                outValue, WeatherCloudGenusName(weather.dominantCloudGenus));
        }
#define WEATHER_NUMBER(fieldName, fieldValue) \
        if (strcmp(name, fieldName) == 0) { \
            return GameDebugDslNumber(outValue, (fieldValue)); \
        }
        WEATHER_NUMBER("weather.temperature", weather.temperatureK)
        WEATHER_NUMBER("weather.temperature_k", weather.temperatureK)
        WEATHER_NUMBER("weather.pressure", weather.pressureAtm)
        WEATHER_NUMBER("weather.pressure_atm", weather.pressureAtm)
        WEATHER_NUMBER("weather.humidity", weather.relativeHumidity)
        WEATHER_NUMBER("weather.relative_humidity", weather.relativeHumidity)
        WEATHER_NUMBER("weather.dew_point", weather.dewPointK)
        WEATHER_NUMBER("weather.dew_point_k", weather.dewPointK)
        WEATHER_NUMBER("weather.wet_bulb", weather.wetBulbK)
        WEATHER_NUMBER("weather.wet_bulb_k", weather.wetBulbK)
        WEATHER_NUMBER("weather.wind", weather.wind)
        WEATHER_NUMBER("weather.gust", weather.gust)
        WEATHER_NUMBER("weather.visibility", weather.visibility)
        WEATHER_NUMBER("weather.rain", weather.rain)
        WEATHER_NUMBER("weather.snow", weather.snow)
        WEATHER_NUMBER("weather.sleet", weather.sleet)
        WEATHER_NUMBER("weather.freezing_rain", weather.freezingRain)
        WEATHER_NUMBER("weather.hail", weather.hail)
        WEATHER_NUMBER("weather.lightning", weather.lightning)
        WEATHER_NUMBER("weather.fog", weather.fog)
        WEATHER_NUMBER("weather.dust", weather.dust)
        WEATHER_NUMBER("weather.rainbow", weather.rainbow)
        WEATHER_NUMBER("weather.aurora", weather.aurora)
        WEATHER_NUMBER("weather.cloud_cover", weather.cloudCover)
        WEATHER_NUMBER("weather.cloud_flags", weather.cloudGenera)
        WEATHER_NUMBER("weather.cloud_layers", visual.cloudLayerCount)
        WEATHER_NUMBER("weather.cloud_forced_frames",
                       WeatherForcedCloudFramesRemaining())
        WEATHER_NUMBER("weather.surface_count", impacts.surfaceCount)
        WEATHER_NUMBER("weather.active_fires", impacts.activeFires)
        WEATHER_NUMBER("weather.forced_frames",
                       WeatherForcedFramesRemaining())
#undef WEATHER_NUMBER
        if (strcmp(name, "weather.damage_enabled") == 0) {
            return GameDebugDslBool(outValue, WeatherImpactEnabled());
        }
    }

    if (strcmp(name, "stream.surface_ready") == 0) {
        bool ready = game->screen == SCREEN_PLAYING &&
            PlayerMissingSurfaceChunkCount(game->player.position) == 0;
        return GameDebugDslBool(outValue, ready);
    }
    if (strcmp(name, "stream.missing_surface_chunks") == 0) {
        int missing = game->screen == SCREEN_PLAYING
            ? PlayerMissingSurfaceChunkCount(game->player.position) : -1;
        return GameDebugDslNumber(outValue, missing);
    }
    if (strcmp(name, "stream.audit_complete") == 0) {
        return GameDebugDslBool(
            outValue, !game->streamAudit.active && !game->streamAudit.wait.active);
    }

    if (strncmp(name, "fluid.", 6u) == 0) {
        FluidSample sample = FluidSampleAt(game->player.position);
        FluidStats stats = FluidGetStats();
        if (strcmp(name, "fluid.volume") == 0) {
            return GameDebugDslNumber(outValue, sample.volume);
        }
        if (strcmp(name, "fluid.surface_y") == 0) {
            return GameDebugDslNumber(outValue, sample.surfaceY);
        }
        if (strcmp(name, "fluid.queue_overflows") == 0) {
            return GameDebugDslNumber(outValue, stats.queueOverflows);
        }
    }

    if (strncmp(name, "target.", 7u) == 0) {
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

    if (strcmp(name, "ship.driving") == 0) {
        return GameDebugDslBool(outValue, ShipIsDriving());
    }
    if (strcmp(name, "ship.mode") == 0) {
        return GameDebugDslString(outValue, ShipDriveModeName());
    }

    if (strncmp(name, "render.", 7u) == 0) {
        WorldWaterRenderDebugInfo water = WorldRenderWaterDebugSnapshot();
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

    if (strcmp(name, "settings.autosave") == 0) {
        return GameDebugDslBool(outValue, game->autoSaveEnabled);
    }
    return false;
}

static const char *GameDebugDslCommandBlocked(
    const GameRuntime *game, DebugControlCommand command)
{
    switch (command) {
    case DEBUG_CONTROL_COMMAND_START:
        return game->screen == SCREEN_START ? NULL : "already_playing";
    case DEBUG_CONTROL_COMMAND_SCREENSHOT:
    case DEBUG_CONTROL_COMMAND_SAVE:
    case DEBUG_CONTROL_COMMAND_LOAD:
    case DEBUG_CONTROL_COMMAND_TELEPORT:
    case DEBUG_CONTROL_COMMAND_LOOK:
    case DEBUG_CONTROL_COMMAND_INPUT:
    case DEBUG_CONTROL_COMMAND_VIEW:
    case DEBUG_CONTROL_COMMAND_SHIP_BEGIN:
    case DEBUG_CONTROL_COMMAND_SHIP_ENTER:
    case DEBUG_CONTROL_COMMAND_EVOLUTION_ADVANCE:
    case DEBUG_CONTROL_COMMAND_EVOLUTION_ATLAS:
        return game->screen == SCREEN_PLAYING ? NULL : "not_playing";
    case DEBUG_CONTROL_COMMAND_SHIP_INPUT:
    case DEBUG_CONTROL_COMMAND_SHIP_EXHAUST:
        return game->screen == SCREEN_PLAYING && ShipIsDriving()
            ? NULL : "not_driving";
    case DEBUG_CONTROL_COMMAND_SHIP_DUST:
        return game->screen == SCREEN_PLAYING && ShipIsDriving() &&
                       WorldIsSurfaceActive()
            ? NULL : "no_surface_ship";
    case DEBUG_CONTROL_COMMAND_MAP:
    case DEBUG_CONTROL_COMMAND_MAP_LAYER_LIQUIDS:
    case DEBUG_CONTROL_COMMAND_MARKER_ADD:
    case DEBUG_CONTROL_COMMAND_MARKER_LIST:
    case DEBUG_CONTROL_COMMAND_MARKER_TARGET:
    case DEBUG_CONTROL_COMMAND_MARKER_REMOVE:
    case DEBUG_CONTROL_COMMAND_FLUID_SET:
    case DEBUG_CONTROL_COMMAND_FLUID_STEP:
    case DEBUG_CONTROL_COMMAND_WEATHER_INSPECT:
    case DEBUG_CONTROL_COMMAND_WEATHER_FORCE:
    case DEBUG_CONTROL_COMMAND_WEATHER_CLOUD_FORCE:
    case DEBUG_CONTROL_COMMAND_WEATHER_STEP:
        return WorldIsSurfaceActive() ? NULL : "no_active_surface";
    case DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_HOME:
    case DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_PLANET:
        return game->screen == SCREEN_PLAYING ? NULL : "not_playing";
    case DEBUG_CONTROL_COMMAND_STREAM_AUDIT:
    case DEBUG_CONTROL_COMMAND_STREAM_WAIT:
        if (game->screen != SCREEN_PLAYING || !WorldIsSurfaceActive()) {
            return "not_in_surface_world";
        }
        if (game->streamAudit.active || game->streamAudit.wait.active) {
            return "stream_operation_in_progress";
        }
        return NULL;
    default:
        return NULL;
    }
}

static DebugDslCommandResult GameDebugDslCommand(
    void *userData, const char *commandText, DebugDslError *outError)
{
    GameRuntime *game = userData;
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
    game->debugDslInputLength = 0u;
    game->debugDslInput[0] = '\0';
    game->debugDslBraceDepth = 0;
}

static void GameDebugDslCountBraces(GameRuntime *game, const char *line)
{
    bool string = false;
    bool escaped = false;
    for (const char *cursor = line; *cursor != '\0'; cursor++) {
        if (!string && *cursor == '#') break;
        if (string && escaped) {
            escaped = false;
            continue;
        }
        if (string && *cursor == '\\') {
            escaped = true;
            continue;
        }
        if (*cursor == '"') {
            string = !string;
            continue;
        }
        if (string) continue;
        if (*cursor == '{') game->debugDslBraceDepth++;
        else if (*cursor == '}') game->debugDslBraceDepth--;
    }
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
        if (game->debugDslInputLength != 0u) {
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
    size_t length = strlen(line);
    if (length + 2u > sizeof(game->debugDslInput) - game->debugDslInputLength) {
        DebugDslError error = {
            .code = DEBUG_DSL_ERROR_LIMIT,
            .line = 1u,
            .column = 1u
        };
        snprintf(error.message, sizeof(error.message),
                 "stdin DSL block exceeds %zu bytes",
                 sizeof(game->debugDslInput) - 1u);
        GameDebugDslReportError(game, "stdin", &error);
        GameDebugDslResetInput(game);
        return;
    }
    memcpy(game->debugDslInput + game->debugDslInputLength, line, length);
    game->debugDslInputLength += length;
    game->debugDslInput[game->debugDslInputLength++] = '\n';
    game->debugDslInput[game->debugDslInputLength] = '\0';
    GameDebugDslCountBraces(game, line);
    if (game->debugDslBraceDepth > 0) return;

    DebugDslError error = { 0 };
    if (!GameDebugDslBegin(game, game->debugDslInput, true, &error)) {
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
