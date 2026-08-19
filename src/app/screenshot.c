#include "app/screenshot.h"

#include "raylib.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define SCREENSHOT_NAMES_PER_SECOND 1000

static ScreenshotResult ScreenshotEnsureDirectory(const char *directory)
{
    if (DirectoryExists(directory)) return SCREENSHOT_RESULT_OK;
    if (MakeDirectory(directory) != 0 || !DirectoryExists(directory)) {
        return SCREENSHOT_RESULT_DIRECTORY_FAILED;
    }
    return SCREENSHOT_RESULT_OK;
}

ScreenshotResult ScreenshotNextPath(
    const char *directory, time_t timestamp, char *path, size_t pathSize)
{
    if (!directory || directory[0] == '\0' || !path || pathSize == 0) {
        return SCREENSHOT_RESULT_INVALID_ARGUMENT;
    }

    struct tm *local = localtime(&timestamp);
    if (!local) return SCREENSHOT_RESULT_INVALID_ARGUMENT;
    struct tm localTime = *local;

    ScreenshotResult directoryResult = ScreenshotEnsureDirectory(directory);
    if (directoryResult != SCREENSHOT_RESULT_OK) return directoryResult;

    for (int sequence = 0; sequence < SCREENSHOT_NAMES_PER_SECOND; sequence++) {
        int length = snprintf(
            path, pathSize,
            "%s/voxelcraft_%04d%02d%02d_%02d%02d%02d_%03d.png",
            directory, localTime.tm_year + 1900, localTime.tm_mon + 1,
            localTime.tm_mday, localTime.tm_hour, localTime.tm_min,
            localTime.tm_sec, sequence);
        if (length < 0 || (size_t)length >= pathSize) {
            path[0] = '\0';
            return SCREENSHOT_RESULT_INVALID_ARGUMENT;
        }
        if (FileExists(path)) continue;

        // An orphaned report must not be overwritten by a later capture.
        memcpy(path + length - 3, "txt", 3);
        bool reportExists = FileExists(path);
        memcpy(path + length - 3, "png", 3);
        if (!reportExists) return SCREENSHOT_RESULT_OK;
    }

    path[0] = '\0';
    return SCREENSHOT_RESULT_NAME_EXHAUSTED;
}

ScreenshotResult ScreenshotCaptureFrame(
    const char *directory, time_t timestamp, char *path, size_t pathSize)
{
    ScreenshotResult result = ScreenshotNextPath(
        directory, timestamp, path, pathSize);
    if (result != SCREENSHOT_RESULT_OK) return result;

    TakeScreenshot(path);
    return FileExists(path) ? SCREENSHOT_RESULT_OK :
                              SCREENSHOT_RESULT_WRITE_FAILED;
}

ScreenshotResult ScreenshotDebugReportPath(
    const char *imagePath, char *reportPath, size_t reportPathSize)
{
    if (!imagePath || !reportPath || reportPathSize == 0) {
        return SCREENSHOT_RESULT_INVALID_ARGUMENT;
    }

    size_t imagePathLength = strlen(imagePath);
    if (imagePathLength < 5 || strcmp(imagePath + imagePathLength - 4, ".png") != 0 ||
        imagePathLength + 1 > reportPathSize) {
        reportPath[0] = '\0';
        return SCREENSHOT_RESULT_INVALID_ARGUMENT;
    }

    memcpy(reportPath, imagePath, imagePathLength + 1);
    memcpy(reportPath + imagePathLength - 3, "txt", 3);
    return SCREENSHOT_RESULT_OK;
}

static const char *ScreenshotBool(bool value)
{
    return value ? "true" : "false";
}

static const char *ScreenshotText(const char *value)
{
    return value && value[0] != '\0' ? value : "unknown";
}

static bool ScreenshotWriteDebugFields(
    FILE *file, const char *imagePath, time_t timestamp,
    const struct tm *localTime, const ScreenshotDebugInfo *info)
{
#define REPORT_LINE(...) do { if (fprintf(file, __VA_ARGS__) < 0) return false; } while (0)
    REPORT_LINE("format=voxelcraft-screenshot-debug\n");
    REPORT_LINE("format.version=11\n");
    REPORT_LINE("image.path=%s\n", imagePath);
    REPORT_LINE("capture.unix_time=%lld\n", (long long)timestamp);
    REPORT_LINE("capture.local_time=%04d-%02d-%02dT%02d:%02d:%02d\n",
                localTime->tm_year + 1900, localTime->tm_mon + 1,
                localTime->tm_mday, localTime->tm_hour, localTime->tm_min,
                localTime->tm_sec);

    REPORT_LINE("world.seed=%" PRIu32 "\n", info->world.seed);
    REPORT_LINE("world.surface_id=%" PRIu32 "\n", info->world.surfaceId);
    REPORT_LINE("world.dimension=%s\n", ScreenshotText(info->world.dimension));
    REPORT_LINE("world.day_time=%.6f\n", info->world.dayTime);
    REPORT_LINE("world.daylight=%.6f\n", info->world.daylight);
    REPORT_LINE("world.day_cycle_enabled=%s\n",
                ScreenshotBool(info->world.dayCycleEnabled));

    REPORT_LINE("player.position=%.6f,%.6f,%.6f\n",
                info->player.position.x, info->player.position.y,
                info->player.position.z);
    REPORT_LINE("player.velocity=%.6f,%.6f,%.6f\n",
                info->player.velocity.x, info->player.velocity.y,
                info->player.velocity.z);
    REPORT_LINE("player.yaw_radians=%.6f\n", info->player.yaw);
    REPORT_LINE("player.pitch_radians=%.6f\n", info->player.pitch);
    REPORT_LINE("player.on_ground=%s\n", ScreenshotBool(info->player.onGround));
    REPORT_LINE("player.floating=%s\n", ScreenshotBool(info->player.floating));
    REPORT_LINE("player.driving=%s\n", ScreenshotBool(info->player.driving));

    REPORT_LINE("camera.position=%.6f,%.6f,%.6f\n",
                info->camera.position.x, info->camera.position.y,
                info->camera.position.z);
    REPORT_LINE("camera.target=%.6f,%.6f,%.6f\n",
                info->camera.target.x, info->camera.target.y,
                info->camera.target.z);
    REPORT_LINE("camera.fov_y_degrees=%.6f\n", info->camera.fovY);
    REPORT_LINE("camera.third_person=%s\n",
                ScreenshotBool(info->camera.thirdPerson));
    REPORT_LINE("camera.inside_solid=%s\n",
                ScreenshotBool(info->camera.insideSolid));

    REPORT_LINE("weather.name=%s\n", ScreenshotText(info->weather.name));
    REPORT_LINE("weather.climate=%s\n",
                ScreenshotText(info->weather.climate));
    REPORT_LINE("weather.phenomenon=%s\n",
                ScreenshotText(info->weather.phenomenon));
    REPORT_LINE("weather.cloud_genus=%s\n",
                ScreenshotText(info->weather.cloudGenus));
    REPORT_LINE("weather.simulation_time=%.6f\n", info->weather.simulationTime);
    REPORT_LINE("weather.active=%s\n", ScreenshotBool(info->weather.active));
    REPORT_LINE("weather.atmosphere_density=%.6f\n",
                info->weather.atmosphereDensity);
    REPORT_LINE("weather.cloud_cover=%.6f\n", info->weather.cloudCover);
    REPORT_LINE("weather.cloud_base_height=%.6f\n", info->weather.cloudBaseHeight);
    REPORT_LINE("weather.cloud_thickness=%.6f\n", info->weather.cloudThickness);
    REPORT_LINE("weather.cloud_opacity=%.6f\n", info->weather.cloudOpacity);
    REPORT_LINE("weather.cloud_genera=0x%X\n",
                (unsigned)info->weather.cloudGenera);
    REPORT_LINE("weather.cloud_layer_count=%u\n",
                info->weather.cloudLayerCount);
    for (unsigned index = 0u;
         index < SCREENSHOT_WEATHER_CLOUD_LAYER_CAPACITY; index++) {
        REPORT_LINE("weather.cloud_layer_%u_name=%s\n", index,
                    ScreenshotText(info->weather.cloudLayerNames[index]));
        REPORT_LINE("weather.cloud_layer_%u_coverage=%.6f\n", index,
                    info->weather.cloudLayerCoverage[index]);
        REPORT_LINE("weather.cloud_layer_%u_base_height=%.6f\n", index,
                    info->weather.cloudLayerBaseHeight[index]);
        REPORT_LINE("weather.cloud_layer_%u_thickness=%.6f\n", index,
                    info->weather.cloudLayerThickness[index]);
    }
    REPORT_LINE("weather.fog_density=%.6f\n", info->weather.fogDensity);
    REPORT_LINE("weather.visibility=%.6f\n", info->weather.visibility);
    REPORT_LINE("weather.precipitation_veil=%.6f\n",
                info->weather.precipitationVeil);
    REPORT_LINE("weather.storm_darkening=%.6f\n", info->weather.stormDarkening);
    REPORT_LINE("weather.wind_drift=%.6f\n", info->weather.windDrift);
    REPORT_LINE("weather.wind_angle_radians=%.6f\n", info->weather.windAngle);
    REPORT_LINE("weather.snow_fraction=%.6f\n", info->weather.snowFraction);
    REPORT_LINE("weather.temperature_k=%.6f\n", info->weather.temperatureK);
    REPORT_LINE("weather.temperature_anomaly_k=%.6f\n",
                info->weather.temperatureAnomalyK);
    REPORT_LINE("weather.pressure_atm=%.6f\n", info->weather.pressureAtm);
    REPORT_LINE("weather.relative_humidity=%.6f\n",
                info->weather.relativeHumidity);
    REPORT_LINE("weather.dew_point_k=%.6f\n", info->weather.dewPointK);
    REPORT_LINE("weather.wet_bulb_k=%.6f\n", info->weather.wetBulbK);
    REPORT_LINE("weather.precipitation=%.6f\n", info->weather.precipitation);
    REPORT_LINE("weather.drizzle=%.6f\n", info->weather.drizzle);
    REPORT_LINE("weather.rain=%.6f\n", info->weather.rain);
    REPORT_LINE("weather.snow=%.6f\n", info->weather.snow);
    REPORT_LINE("weather.sleet=%.6f\n", info->weather.sleet);
    REPORT_LINE("weather.freezing_rain=%.6f\n", info->weather.freezingRain);
    REPORT_LINE("weather.hail=%.6f\n", info->weather.hail);
    REPORT_LINE("weather.lightning=%.6f\n", info->weather.lightning);
    REPORT_LINE("weather.frost=%.6f\n", info->weather.frost);
    REPORT_LINE("weather.dust=%.6f\n", info->weather.dust);
    REPORT_LINE("weather.wind=%.6f\n", info->weather.wind);
    REPORT_LINE("weather.gust=%.6f\n", info->weather.gust);
    REPORT_LINE("weather.rainbow=%.6f\n", info->weather.rainbow);
    REPORT_LINE("weather.aurora=%.6f\n", info->weather.aurora);
    REPORT_LINE("weather.forced_frames=%u\n", info->weather.forcedFrames);
    REPORT_LINE("weather.forced_cloud_frames=%u\n",
                info->weather.forcedCloudFrames);
    REPORT_LINE("weather.damage_enabled=%s\n",
                ScreenshotBool(info->weather.damageEnabled));
    REPORT_LINE("weather.surface_count=%" PRIu32 "\n",
                info->weather.surfaceCount);
    REPORT_LINE("weather.active_fires=%" PRIu32 "\n",
                info->weather.activeFires);
    REPORT_LINE("weather.block_damage_events=%" PRIu32 "\n",
                info->weather.blockDamageEvents);
    REPORT_LINE("weather.fire_present=%s\n",
                ScreenshotBool(info->weather.firePresent));
    REPORT_LINE("weather.fire_phase=%s\n",
                ScreenshotText(info->weather.firePhase));
    REPORT_LINE("weather.fire_position=%.6f,%.6f,%.6f\n",
                info->weather.firePosition.x, info->weather.firePosition.y,
                info->weather.firePosition.z);
    REPORT_LINE("weather.fire_distance=%.6f\n",
                info->weather.fireDistance);
    REPORT_LINE("weather.fire_intensity=%.6f\n",
                info->weather.fireIntensity);
    REPORT_LINE("weather.fire_fuel=%.6f\n", info->weather.fireFuel);
    REPORT_LINE("weather.fire_moisture=%.6f\n",
                info->weather.fireMoisture);
    REPORT_LINE("weather.fire_heat_output=%.6f\n",
                info->weather.fireHeatOutput);
    REPORT_LINE("weather.fire_smoke_output=%.6f\n",
                info->weather.fireSmokeOutput);
    REPORT_LINE("weather.fire_local_heat=%.6f\n",
                info->weather.fireLocalHeat);
    REPORT_LINE("weather.fire_local_smoke=%.6f\n",
                info->weather.fireLocalSmoke);
    REPORT_LINE("weather.fire_plume_wind_angle_radians=%.6f\n",
                info->weather.firePlumeWindAngle);
    REPORT_LINE("weather.fire_plume_wind_drift=%.6f\n",
                info->weather.firePlumeWindDrift);
    REPORT_LINE("weather.fire_haze=%.6f\n", info->weather.fireHaze);
    REPORT_LINE("weather.fire_snapshot_count=%u\n",
                info->weather.fireSnapshotCount);
    REPORT_LINE("weather.fire_render_max_fires=%u\n",
                info->weather.fireRenderMaxFires);
    REPORT_LINE("weather.fire_render_flame_tongues=%u\n",
                info->weather.fireRenderFlameTongues);
    REPORT_LINE("weather.fire_render_smoke_puffs=%u\n",
                info->weather.fireRenderSmokePuffs);
    REPORT_LINE("weather.fire_ignitions=%" PRIu32 "\n",
                info->weather.fireIgnitions);
    REPORT_LINE("weather.fire_spread_ignitions=%" PRIu32 "\n",
                info->weather.fireSpreadIgnitions);
    REPORT_LINE("weather.fire_extinctions=%" PRIu32 "\n",
                info->weather.fireExtinctions);
    REPORT_LINE("weather.fire_suppressions=%" PRIu32 "\n",
                info->weather.fireSuppressions);
    REPORT_LINE("weather.fire_burned_blocks=%" PRIu32 "\n",
                info->weather.fireBurnedBlocks);
    REPORT_LINE("weather.fire_burn_site_count=%" PRIu32 "\n",
                info->weather.fireBurnSiteCount);
    REPORT_LINE("weather.fire_recovered_sites=%" PRIu32 "\n",
                info->weather.fireRecoveredSites);
    REPORT_LINE("weather.fire_dropped_ignitions=%" PRIu32 "\n",
                info->weather.fireDroppedIgnitions);
    REPORT_LINE("weather.fire_dropped_burn_sites=%" PRIu32 "\n",
                info->weather.fireDroppedBurnSites);
    REPORT_LINE("weather.tornado_active=%s\n",
                ScreenshotBool(info->weather.tornadoActive));
    REPORT_LINE("weather.tornado_forced=%s\n",
                ScreenshotBool(info->weather.tornadoForced));
    REPORT_LINE("weather.tornado_phase=%s\n",
                ScreenshotText(info->weather.tornadoPhase));
    REPORT_LINE("weather.tornado_center=%.6f,%.6f,%.6f\n",
                info->weather.tornadoCenter.x,
                info->weather.tornadoCenter.y,
                info->weather.tornadoCenter.z);
    REPORT_LINE("weather.tornado_distance=%.6f\n",
                info->weather.tornadoDistance);
    REPORT_LINE("weather.tornado_intensity=%.6f\n",
                info->weather.tornadoIntensity);
    REPORT_LINE("weather.tornado_radius=%.6f\n",
                info->weather.tornadoRadius);
    REPORT_LINE("weather.tornado_funnel_height=%.6f\n",
                info->weather.tornadoFunnelHeight);
    REPORT_LINE("weather.tornado_wind_mps=%.6f\n",
                info->weather.tornadoWindMps);
    REPORT_LINE("weather.tornado_condensation=%.6f\n",
                info->weather.tornadoCondensation);
    REPORT_LINE("weather.tornado_dust_loading=%.6f\n",
                info->weather.tornadoDustLoading);
    REPORT_LINE("weather.tornado_forced_frames=%u\n",
                info->weather.tornadoForcedFrames);
    REPORT_LINE("weather.tornado_block_damage_events=%" PRIu32 "\n",
                info->weather.tornadoBlockDamageEvents);
    REPORT_LINE("weather.tornado_debris_emitted=%" PRIu32 "\n",
                info->weather.tornadoDebrisEmitted);
    REPORT_LINE("weather.tornado_dust_emitted=%" PRIu32 "\n",
                info->weather.tornadoDustEmitted);
    REPORT_LINE("weather.tornado_dropped_effects=%" PRIu32 "\n",
                info->weather.tornadoDroppedEffects);

    REPORT_LINE("environment.altitude=%.6f\n", info->environment.altitude);
    REPORT_LINE("environment.atmosphere_fade=%.6f\n",
                info->environment.atmosphereFade);
    REPORT_LINE("environment.underwater_depth=%.6f\n",
                info->environment.underwaterDepth);
    REPORT_LINE("environment.water_surface_y=%.6f\n",
                info->environment.waterSurfaceY);
    REPORT_LINE("environment.seabed_y=%d\n", info->environment.seabedY);
    REPORT_LINE("environment.water_column_depth=%d\n",
                info->environment.waterColumnDepth);
    REPORT_LINE("environment.bathymetry_zone=%s\n",
                ScreenshotText(info->environment.bathymetryZone));
    REPORT_LINE("environment.seabed_material=%s\n",
                ScreenshotText(info->environment.seabedMaterial));
    REPORT_LINE("environment.underwater=%s\n",
                ScreenshotBool(info->environment.underwater));
    REPORT_LINE("environment.feet_submerged=%s\n",
                ScreenshotBool(info->environment.feetSubmerged));
    REPORT_LINE("environment.body_submerged=%s\n",
                ScreenshotBool(info->environment.bodySubmerged));
    REPORT_LINE("environment.eyes_submerged=%s\n",
                ScreenshotBool(info->environment.eyesSubmerged));
    REPORT_LINE("environment.sheltered=%s\n",
                ScreenshotBool(info->environment.sheltered));
    REPORT_LINE("environment.forest=%s\n",
                ScreenshotBool(info->environment.forest));
    REPORT_LINE("environment.near_water=%s\n",
                ScreenshotBool(info->environment.nearWater));
    REPORT_LINE("environment.ship_interior=%s\n",
                ScreenshotBool(info->environment.shipInterior));

    REPORT_LINE("fluid.local_volume=%u\n", info->fluid.volume);
    REPORT_LINE("fluid.local_surface_y=%.6f\n", info->fluid.surfaceY);
    REPORT_LINE("fluid.local_flow=%.6f,%.6f,%.6f\n",
                info->fluid.flowVelocity.x, info->fluid.flowVelocity.y,
                info->fluid.flowVelocity.z);
    REPORT_LINE("fluid.ticks=%" PRIu64 "\n", info->fluid.ticks);
    REPORT_LINE("fluid.loaded_volume=%" PRIu64 "\n",
                info->fluid.loadedVolume);
    REPORT_LINE("fluid.active_cells=%u\n", info->fluid.activeCells);
    REPORT_LINE("fluid.last_processed_cells=%u\n",
                info->fluid.lastProcessedCells);
    REPORT_LINE("fluid.edit_count=%u\n", info->fluid.editCount);
    REPORT_LINE("fluid.queue_overflows=%u\n", info->fluid.queueOverflows);

    REPORT_LINE("block.catalog_count=%u\n", info->block.catalogCount);
    REPORT_LINE("block.natural_count=%u\n", info->block.naturalCount);
    REPORT_LINE("block.stage05_count=%u\n", info->block.stage05Count);
    REPORT_LINE("block.gallery_active=%s\n",
                ScreenshotBool(info->block.galleryActive));
    REPORT_LINE("block.gallery_origin=%.6f,%.6f,%.6f\n",
                info->block.galleryOrigin.x, info->block.galleryOrigin.y,
                info->block.galleryOrigin.z);
    REPORT_LINE("block.gallery_placed=%u\n", info->block.galleryPlaced);
    REPORT_LINE("block.gallery_rows=%u\n", info->block.galleryRows);
    REPORT_LINE("block.gallery_width=%u\n", info->block.galleryWidth);

    REPORT_LINE("input.forward=%.6f\n", info->input.forward);
    REPORT_LINE("input.strafe=%.6f\n", info->input.strafe);
    REPORT_LINE("input.vertical=%.6f\n", info->input.vertical);
    REPORT_LINE("input.sprint=%s\n", ScreenshotBool(info->input.sprint));
    REPORT_LINE("input.remaining_frames=%u\n", info->input.remainingFrames);

    REPORT_LINE("render.graphics_quality=%s\n",
                ScreenshotText(info->render.graphicsQuality));
    REPORT_LINE("render.distance_chunks=%d\n", info->render.renderDistanceChunks);
    REPORT_LINE("render.fps=%d\n", info->render.fps);
    REPORT_LINE("render.screen=%d,%d\n", info->render.screenWidth,
                info->render.screenHeight);
    REPORT_LINE("render.frame_time_ms=%.6f\n", info->render.frameTimeMs);
    REPORT_LINE("render.performance_mode=%s\n",
                ScreenshotBool(info->render.performanceMode));

    REPORT_LINE("ui.paused=%s\n", ScreenshotBool(info->ui.paused));
    REPORT_LINE("ui.album_open=%s\n", ScreenshotBool(info->ui.albumOpen));
    REPORT_LINE("ui.star_map_open=%s\n", ScreenshotBool(info->ui.starMapOpen));
    REPORT_LINE("ui.import_dialog_open=%s\n",
                ScreenshotBool(info->ui.importDialogOpen));
    REPORT_LINE("ui.cursor_released=%s\n",
                ScreenshotBool(info->ui.cursorReleased));
    REPORT_LINE("ui.help_visible=%s\n", ScreenshotBool(info->ui.helpVisible));
    REPORT_LINE("ui.debug_hud_visible=%s\n",
                ScreenshotBool(info->ui.debugHudVisible));
    REPORT_LINE("ui.landing_transition_active=%s\n",
                ScreenshotBool(info->ui.landingTransitionActive));

    REPORT_LINE("evolution.entity_selected=%s\n",
                ScreenshotBool(info->evolution.entitySelected));
    REPORT_LINE("evolution.scan_locked=%s\n",
                ScreenshotBool(info->evolution.scanLocked));
    REPORT_LINE("evolution.atlas_open=%s\n",
                ScreenshotBool(info->evolution.atlasOpen));
    REPORT_LINE("evolution.organism_id=%" PRIu32 "\n",
                info->evolution.organismId);
    REPORT_LINE("evolution.lineage_id=%" PRIu32 "\n",
                info->evolution.lineageId);
    REPORT_LINE("evolution.species_id=%" PRIu32 "\n",
                info->evolution.speciesId);
    REPORT_LINE("evolution.genome_id=%" PRIu32 "\n",
                info->evolution.genomeId);
    REPORT_LINE("evolution.generation=%" PRIu32 "\n",
                info->evolution.generation);
    REPORT_LINE("evolution.mutation_count=%" PRIu32 "\n",
                info->evolution.mutationCount);
    REPORT_LINE("evolution.module_count=%" PRIu32 "\n",
                info->evolution.moduleCount);
    REPORT_LINE("evolution.mother_id=%" PRIu32 "\n",
                info->evolution.motherId);
    REPORT_LINE("evolution.father_id=%" PRIu32 "\n",
                info->evolution.fatherId);
    REPORT_LINE("evolution.child_count=%" PRIu32 "\n",
                info->evolution.childCount);
    REPORT_LINE("evolution.catalog_species_count=%" PRIu32 "\n",
                info->evolution.catalogSpeciesCount);
    REPORT_LINE("evolution.catalog_individual_count=%" PRIu32 "\n",
                info->evolution.catalogIndividualCount);
    REPORT_LINE("evolution.sex=%s\n", ScreenshotText(info->evolution.sex));
    REPORT_LINE("evolution.locomotion=%s\n",
                ScreenshotText(info->evolution.locomotion));
    REPORT_LINE("evolution.corpse=%s\n",
                ScreenshotBool(info->evolution.corpse));
    REPORT_LINE("evolution.juvenile=%s\n",
                ScreenshotBool(info->evolution.juvenile));
    REPORT_LINE("evolution.pregnant=%s\n",
                ScreenshotBool(info->evolution.pregnant));
    REPORT_LINE("evolution.age_days=%.6f\n", info->evolution.ageDays);
    REPORT_LINE("evolution.maturity_age_days=%.6f\n",
                info->evolution.maturityAgeDays);
    REPORT_LINE("evolution.health=%.6f\n", info->evolution.health);
    REPORT_LINE("evolution.energy=%.6f\n", info->evolution.energy);
    REPORT_LINE("evolution.diet=%.6f\n", info->evolution.diet);
    REPORT_LINE("evolution.mass=%.6f\n", info->evolution.mass);
    REPORT_LINE("evolution.speed=%.6f\n", info->evolution.speed);
    REPORT_LINE("evolution.region_available=%s\n",
                ScreenshotBool(info->evolution.regionAvailable));
    REPORT_LINE("evolution.regional_lineage_count=%" PRIu32 "\n",
                info->evolution.regionalLineageCount);
    REPORT_LINE("evolution.bootstrap_generation=%" PRIu32 "\n",
                info->evolution.bootstrapGeneration);
    REPORT_LINE("evolution.bootstrap_complete=%s\n",
                ScreenshotBool(info->evolution.bootstrapComplete));
    REPORT_LINE("evolution.herbivore_density=%.6f\n",
                info->evolution.herbivoreDensity);
    REPORT_LINE("evolution.omnivore_density=%.6f\n",
                info->evolution.omnivoreDensity);
    REPORT_LINE("evolution.carnivore_density=%.6f\n",
                info->evolution.carnivoreDensity);

    REPORT_LINE("streaming.active_chunks=%d\n", info->streaming.activeChunks);
    REPORT_LINE("streaming.active_space_chunks=%d\n",
                info->streaming.activeSpaceChunks);
    REPORT_LINE("streaming.active_nether_chunks=%d\n",
                info->streaming.activeNetherChunks);
    REPORT_LINE("streaming.active_entities=%d\n", info->streaming.activeEntities);
    REPORT_LINE("streaming.pending_generation_jobs=%d\n",
                info->streaming.pendingGenerationJobs);
    REPORT_LINE("streaming.pending_mesh_jobs=%d\n",
                info->streaming.pendingMeshJobs);
    REPORT_LINE("streaming.surface_chunk=%d,%d\n",
                info->streaming.surfaceChunkX,
                info->streaming.surfaceChunkZ);
    REPORT_LINE("streaming.surface_section_y=%d\n",
                info->streaming.surfaceSectionY);
    REPORT_LINE("streaming.surface_chunk_loaded=%s\n",
                ScreenshotBool(info->streaming.surfaceChunkLoaded));
    REPORT_LINE("streaming.surface_ready=%s\n",
                ScreenshotBool(info->streaming.surfaceReady));
    REPORT_LINE("streaming.player_missing_surface_chunks=%d\n",
                info->streaming.playerMissingSurfaceChunks);
    REPORT_LINE("streaming.water_neighbor_loaded_mask=0x%X\n",
                info->streaming.waterNeighborLoadedMask);
    REPORT_LINE("streaming.water_triangle_count=%d\n",
                info->streaming.waterTriangleCount);
    REPORT_LINE("streaming.water_section_triangle_count=%d\n",
                info->streaming.waterSectionTriangleCount);
    REPORT_LINE("streaming.water_debug_enabled=%s\n",
                ScreenshotBool(info->streaming.waterDebugEnabled));
    REPORT_LINE("streaming.water_debug_through=%s\n",
                ScreenshotBool(info->streaming.waterDebugThrough));
    REPORT_LINE("streaming.water_visible_section_count=%d\n",
                info->streaming.waterVisibleSectionCount);
    REPORT_LINE("streaming.water_draw_item_count=%d\n",
                info->streaming.waterDrawItemCount);
    REPORT_LINE("streaming.water_draw_triangle_count=%d\n",
                info->streaming.waterDrawTriangleCount);
    REPORT_LINE("streaming.water_has_nearest=%s\n",
                ScreenshotBool(info->streaming.waterHasNearest));
    REPORT_LINE("streaming.water_nearest_chunk=%d,%d\n",
                info->streaming.waterNearestChunkX,
                info->streaming.waterNearestChunkZ);
    REPORT_LINE("streaming.water_nearest_section_y=%d\n",
                info->streaming.waterNearestSectionY);
    REPORT_LINE("streaming.generation_submitted=%" PRIu64 "\n",
                info->streaming.generationSubmitted);
    REPORT_LINE("streaming.generation_completed=%" PRIu64 "\n",
                info->streaming.generationCompleted);
    REPORT_LINE("streaming.generation_canceled=%" PRIu64 "\n",
                info->streaming.generationCanceled);
    REPORT_LINE("streaming.mesh_submitted=%" PRIu64 "\n",
                info->streaming.meshSubmitted);
    REPORT_LINE("streaming.mesh_completed=%" PRIu64 "\n",
                info->streaming.meshCompleted);
    REPORT_LINE("streaming.mesh_canceled=%" PRIu64 "\n",
                info->streaming.meshCanceled);
    REPORT_LINE("streaming.mesh_snapshot_bytes=%" PRIu64 "\n",
                info->streaming.meshSnapshotBytes);
    REPORT_LINE("streaming.sync_rebuilds=%" PRIu64 "\n",
                info->streaming.syncRebuilds);
    REPORT_LINE("streaming.uploaded_meshes=%" PRIu64 "\n",
                info->streaming.uploadedMeshes);
    REPORT_LINE("streaming.upload_budget_deferrals=%" PRIu64 "\n",
                info->streaming.uploadBudgetDeferrals);
    REPORT_LINE("streaming.generation_queue_peak=%" PRIu64 "\n",
                info->streaming.generationQueuePeak);
    REPORT_LINE("streaming.mesh_queue_peak=%" PRIu64 "\n",
                info->streaming.meshQueuePeak);
    REPORT_LINE("streaming.pending_mesh_snapshot_bytes=%" PRIu64 "\n",
                info->streaming.pendingMeshSnapshotBytes);
    REPORT_LINE("streaming.pending_mesh_snapshot_bytes_peak=%" PRIu64 "\n",
                info->streaming.pendingMeshSnapshotBytesPeak);
    REPORT_LINE("streaming.generation_cpu_ms=%.6f\n",
                info->streaming.generationCpuMs);
    REPORT_LINE("streaming.mesh_cpu_ms=%.6f\n", info->streaming.meshCpuMs);
    REPORT_LINE("streaming.upload_cpu_ms=%.6f\n", info->streaming.uploadCpuMs);
    REPORT_LINE("streaming.max_upload_cpu_ms=%.6f\n",
                info->streaming.maxUploadCpuMs);
#undef REPORT_LINE
    return true;
}

ScreenshotResult ScreenshotWriteDebugReport(
    const char *imagePath, time_t timestamp, const ScreenshotDebugInfo *info,
    char *reportPath, size_t reportPathSize)
{
    if (!info) return SCREENSHOT_RESULT_INVALID_ARGUMENT;
    ScreenshotResult pathResult = ScreenshotDebugReportPath(
        imagePath, reportPath, reportPathSize);
    if (pathResult != SCREENSHOT_RESULT_OK) return pathResult;

    struct tm *local = localtime(&timestamp);
    if (!local) return SCREENSHOT_RESULT_INVALID_ARGUMENT;
    struct tm localTime = *local;

    FILE *file = fopen(reportPath, "wb");
    if (!file) return SCREENSHOT_RESULT_REPORT_WRITE_FAILED;
    bool written = ScreenshotWriteDebugFields(
        file, imagePath, timestamp, &localTime, info);
    if (fflush(file) != 0) written = false;
    if (fclose(file) != 0) written = false;
    if (!written) {
        remove(reportPath);
        return SCREENSHOT_RESULT_REPORT_WRITE_FAILED;
    }
    return SCREENSHOT_RESULT_OK;
}

const char *ScreenshotResultMessage(ScreenshotResult result)
{
    switch (result) {
    case SCREENSHOT_RESULT_OK:
        return "Screenshot saved";
    case SCREENSHOT_RESULT_DIRECTORY_FAILED:
        return "Screenshot failed: could not create screenshots directory";
    case SCREENSHOT_RESULT_NAME_EXHAUSTED:
        return "Screenshot failed: no available filename";
    case SCREENSHOT_RESULT_WRITE_FAILED:
        return "Screenshot failed: image could not be written";
    case SCREENSHOT_RESULT_REPORT_WRITE_FAILED:
        return "Screenshot debug report could not be written";
    case SCREENSHOT_RESULT_INVALID_ARGUMENT:
    default:
        return "Screenshot failed: invalid output path";
    }
}
