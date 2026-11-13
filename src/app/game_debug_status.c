#include "app/game_debug_status.h"
#include "app/game_internal.h"
#include "app/game_runtime.h"
#include "core/debug_control.h"
#include "gameplay/interaction.h"
#include "gameplay/camera_distance.h"
#include "gameplay/camera_world.h"
#include "gameplay/player_motion.h"
#include "gameplay/player_sensors.h"
#include "gameplay/ship.h"
#include "presentation/import_dialog_ui.h"
#include "presentation/render.h"
#include "space/space_state.h"
#include "world/chunks.h"
#include "world/fluid.h"
#include "world/surface_topology.h"
#include "world/terrain.h"
#include "world/world_environment.h"
#include "world/world_state.h"
#include "world/block_types.h"
#include "world/block_edits.h"
#include "raymath.h"

#include <math.h>
#include <stdint.h>

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
void GameDebugReplyStatus(GameRuntime *game)
{
    Vector2 uiScale = GetWindowScaleDPI();
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
    const InputFrame *inputFrame = InputSourceFrame(&game->inputSource);
    StartPageUiLayout startLayout = StartPageUiLayoutFor(
        GetScreenWidth(), GetScreenHeight());
    ImportDialogUiLayout importLayout = ImportDialogUiLayoutFor(
        GetScreenWidth(), GetScreenHeight());
    bool uiStartHovered = UiPointInRect(inputFrame->pointer,
                                       startLayout.start);
    bool uiStartPressed = uiStartHovered &&
        InputFrameButtonPressed(inputFrame, &game->inputEvents,
                                INPUT_BUTTON_LEFT, false);
    PlayerWaterState water = PlayerWaterStateAt(game->player.position);
    int missingSurfaceChunks =
        PlayerMissingSurfaceChunkCount(game->player.position);
    FluidSample fluid = FluidSampleAt(game->player.position);
    FluidStats fluidStats = FluidGetStats();
    int cameraRenderDistance = CameraManagerRenderDistance(&game->cameras);
    int streamingDistance = CameraStreamingDistanceForHeight(
        game->player.position.y +
        (WorldIsSpaceActive() ? 0.0f : EYE_HEIGHT));
    const CameraRig *renderRig = CameraManagerRenderRig(&game->cameras);
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
    (void)WorldSurfaceBathymetryAt(
        (int)floorf(game->player.position.x),
        (int)floorf(game->player.position.z), &bathymetry);
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
        "camera_render_distance=%d streaming_distance=%d "
        "target=%d,%d,%d target_hit=%d target_block=%s "
        "target_normal=%d,%d,%d target_neighbor=%s "
        "target_stored=%d target_stored_block=%s target_section=%d "
        "target_dirty=%d target_stamp=%u target_solid_vertices=%d "
        "target_face_vertices=%d target_base_generated=%d "
        "target_base_exposed=%d target_base_block=%s "
        "camera_inside_solid=%d autosave=%d plugins=%zu debug_frame=%llu "
        "mouse_left_press_frame=%llu mouse_left_handle_frame=%llu "
        "mouse_left_set_block_frame=%llu mouse_left_mesh_frame=%llu "
        "mouse_left_mesh_pending=%d mouse_left_target=%d,%d,%d "
        "ui_pointer=%.3f,%.3f ui_pointer_injected=%d "
        "ui_start_hovered=%d ui_start_pressed=%d "
        "ui_import_open=%d ui_import_max_blocks=%d ui_import_relief=%d "
        "ui_import_plus_center=%.3f,%.3f "
        "ui_import_minus_center=%.3f,%.3f "
        "ui_import_input_center=%.3f,%.3f "
        "ui_screen=%d,%d ui_render=%d,%d ui_scale=%.3f,%.3f "
        "window_focused=%d mouse_down=%d mouse_pressed=%d "
        "chunk_workers=%d,%d,%d\n",
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
        renderRig ? renderRig->config.thirdPerson : game->thirdPerson ? 1 : 0,
        cameraRenderDistance, streamingDistance,
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
        game->autoSaveEnabled ? 1 : 0,
        GamePluginsCount(game),
        (unsigned long long)game->debugFrame,
        (unsigned long long)game->debugLeftPressFrame,
        (unsigned long long)game->debugLeftHandleFrame,
        (unsigned long long)game->debugLeftSetBlockFrame,
        (unsigned long long)game->debugLeftMeshFrame,
        game->debugLeftMeshPending ? 1 : 0,
        game->debugLeftTargetX, game->debugLeftTargetY,
        game->debugLeftTargetZ, inputFrame->pointer.x, inputFrame->pointer.y,
        inputFrame->injected ? 1 : 0,
        uiStartHovered ? 1 : 0, uiStartPressed ? 1 : 0,
        game->importDialog.open ? 1 : 0, game->importDialog.maxBlocks,
        game->importDialog.relief ? 1 : 0,
        importLayout.plus.x + importLayout.plus.width / 2.0f,
        importLayout.plus.y + importLayout.plus.height / 2.0f,
        importLayout.minus.x + importLayout.minus.width / 2.0f,
        importLayout.minus.y + importLayout.minus.height / 2.0f,
        importLayout.input.x + importLayout.input.width / 2.0f,
        importLayout.input.y + importLayout.input.height / 2.0f,
        GetScreenWidth(), GetScreenHeight(), GetRenderWidth(), GetRenderHeight(),
        uiScale.x, uiScale.y, IsWindowFocused() ? 1 : 0,
        inputFrame->buttonsDown[INPUT_BUTTON_LEFT] ? 1 : 0,
        InputFrameButtonPressed(inputFrame, &game->inputEvents,
                                INPUT_BUTTON_LEFT, false) ? 1 : 0,
        ChunksConfiguredWorkerCount(),
        ChunksStartedWorkerCount(), ChunksActiveWorkerCount());
}
