#include "app/game_internal.h"

#include "app/screenshot.h"
#include "core/debug_control.h"
#include "core/game_notice.h"
#include "ecology/ecology.h"
#include "ecology/entity.h"
#include "ecology/evolution.h"
#include "ecology/evolution_catalog.h"
#include "gameplay/player.h"
#include "gameplay/ship.h"
#include "presentation/starmap.h"
#include "presentation/render.h"
#include "space/space_chunks.h"
#include "space/space_state.h"
#include "world/chunks.h"
#include "world/fluid.h"
#include "world/nether.h"
#include "world/terrain.h"
#include "world/weather.h"
#include "world/weather_impact.h"
#include "world/tornado.h"
#include "world/world.h"

#include "raylib.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

static ScreenshotVector3 ScreenshotVector(Vector3 value)
{
    return (ScreenshotVector3){ value.x, value.y, value.z };
}

void GameCaptureScreenshot(GameRuntime *game,
                                  const GameFrameView *frame)
{
    if (game->screenshotPending) {
        char screenshotPath[512];
        char debugReportPath[512];
        time_t screenshotTime = time(NULL);
        ChunkStreamingStats streamingStats = ChunksGetStreamingStats();
        ChunkWaterRenderDebugInfo screenshotWaterRender = { 0 };
        ChunksGetWaterRenderDebugInfo(game->player.position,
                                      &screenshotWaterRender);
        WorldWaterRenderDebugInfo screenshotWaterPass =
            WorldRenderWaterDebugSnapshot();
        EntityEvolutionDebugInfo screenshotEntity = { 0 };
        int screenshotEntityIndex = game->evolutionScanLocked
            ? EntityEvolutionFindByOrganism(game->evolutionLockedOrganismId)
            : EntityNearestEvolvable(game->player.position, 32.0f);
        bool haveScreenshotEntity = EntityEvolutionInspect(
            screenshotEntityIndex, &screenshotEntity);
        EvolutionCatalogIndividual screenshotIndividual = { 0 };
        bool haveScreenshotIndividual = haveScreenshotEntity &&
            EvolutionCatalogGetIndividual(
                PlanetWorldIsActive() ? PlanetWorldSeed() : WorldGetSeed(),
                WorldCurrentSurfaceId(), screenshotEntity.organismId,
                &screenshotIndividual);
        PlanetEvolutionRegion screenshotRegion = { 0 };
        bool haveScreenshotRegion = PlanetEcologyEvolutionRegionAt(
            (int)floorf(game->player.position.x),
            (int)floorf(game->player.position.z), frame->daylight, &screenshotRegion);
        FluidSample screenshotFluid = FluidSampleAt(game->player.position);
        FluidStats screenshotFluidStats = FluidGetStats();
        WeatherFieldSample screenshotWeather = WeatherCurrentSample();
        LocalClimateState screenshotClimate = { 0 };
        bool haveScreenshotClimate = WeatherLocalClimateAtWorldTime(
            (int)floorf(game->player.position.x),
            (int)floorf(game->player.position.z),
            frame->weatherSimulationTime, &screenshotClimate);
        WeatherImpactStats screenshotWeatherImpacts = WeatherImpactGetStats();
        bool haveScreenshotFire = frame->wildfireCount > 0u;
        WeatherImpactFireSnapshot screenshotFire = haveScreenshotFire ?
            frame->wildfires[0] : (WeatherImpactFireSnapshot){ 0 };
        GraphicsQualityProfile screenshotQuality = GraphicsQualityProfileFor(
            game->settings.graphicsQuality);
        TornadoState screenshotTornado = frame->tornado;
        TornadoStats screenshotTornadoStats = TornadoGetStats();
        int screenshotMissingSurfaceChunks =
            PlayerMissingSurfaceChunkCount(game->player.position);
        ScreenshotDebugInfo debugInfo = {
            .world = {
                .seed = PlanetWorldIsActive() ? PlanetWorldSeed() :
                                                WorldGetSeed(),
                .surfaceId = WorldCurrentSurfaceId(),
                .dimension = WorldDimensionName(frame->cameraDimension),
                .dayTime = game->dayTime,
                .daylight = frame->daylight,
                .dayCycleEnabled = game->dayCycleEnabled
            },
            .player = {
                .position = ScreenshotVector(game->player.position),
                .velocity = ScreenshotVector(game->player.velocity),
                .yaw = game->player.yaw,
                .pitch = game->player.pitch,
                .onGround = game->player.onGround,
                .floating = game->player.floating,
                .driving = ShipIsDriving()
            },
            .camera = {
                .position = ScreenshotVector(game->camera.position),
                .target = ScreenshotVector(game->camera.target),
                .fovY = game->camera.fovy,
                .thirdPerson = game->thirdPerson,
                .insideSolid =
                    PlayerCameraPositionInsideSolid(game->camera.position)
            },
            .weather = {
                .name = WeatherName(),
                .climate = haveScreenshotClimate
                    ? ClimateRegimeName(screenshotClimate.regime)
                    : "unavailable",
                .phenomenon = WeatherPhenomenonName(
                    screenshotWeather.dominantPhenomenon),
                .cloudGenus = WeatherCloudGenusName(
                    screenshotWeather.dominantCloudGenus),
                .simulationTime = frame->weatherSimulationTime,
                .active = frame->weatherVisual.active,
                .atmosphereDensity = frame->weatherVisual.atmosphereDensity,
                .cloudCover = frame->weatherVisual.cloudCover,
                .cloudBaseHeight = frame->weatherVisual.cloudBaseHeight,
                .cloudThickness = frame->weatherVisual.cloudThickness,
                .cloudOpacity = frame->weatherVisual.cloudOpacity,
                .cloudGenera = screenshotWeather.cloudGenera,
                .cloudLayerCount = frame->weatherVisual.cloudLayerCount,
                .cloudLayerNames = {
                    WeatherCloudGenusName(frame->weatherVisual.cloudLayers[0].genus),
                    WeatherCloudGenusName(frame->weatherVisual.cloudLayers[1].genus),
                    WeatherCloudGenusName(frame->weatherVisual.cloudLayers[2].genus)
                },
                .cloudLayerCoverage = {
                    frame->weatherVisual.cloudLayers[0].coverage,
                    frame->weatherVisual.cloudLayers[1].coverage,
                    frame->weatherVisual.cloudLayers[2].coverage
                },
                .cloudLayerBaseHeight = {
                    frame->weatherVisual.cloudLayers[0].baseHeight,
                    frame->weatherVisual.cloudLayers[1].baseHeight,
                    frame->weatherVisual.cloudLayers[2].baseHeight
                },
                .cloudLayerThickness = {
                    frame->weatherVisual.cloudLayers[0].thickness,
                    frame->weatherVisual.cloudLayers[1].thickness,
                    frame->weatherVisual.cloudLayers[2].thickness
                },
                .fogDensity = frame->weatherVisual.fogDensity,
                .visibility = frame->weatherVisual.visibility,
                .precipitationVeil = frame->weatherVisual.precipitationVeil,
                .stormDarkening = frame->weatherVisual.stormDarkening,
                .windDrift = frame->weatherVisual.windDrift,
                .windAngle = frame->weatherVisual.windAngle,
                .snowFraction = frame->weatherVisual.snowFraction,
                .temperatureK = screenshotWeather.temperatureK,
                .temperatureAnomalyK = screenshotWeather.temperatureAnomalyK,
                .pressureAtm = screenshotWeather.pressureAtm,
                .relativeHumidity = screenshotWeather.relativeHumidity,
                .dewPointK = screenshotWeather.dewPointK,
                .wetBulbK = screenshotWeather.wetBulbK,
                .precipitation = screenshotWeather.precipitation,
                .drizzle = screenshotWeather.drizzle,
                .rain = screenshotWeather.rain,
                .snow = screenshotWeather.snow,
                .sleet = screenshotWeather.sleet,
                .freezingRain = screenshotWeather.freezingRain,
                .hail = screenshotWeather.hail,
                .lightning = screenshotWeather.lightning,
                .frost = screenshotWeather.frost,
                .dust = screenshotWeather.dust,
                .wind = screenshotWeather.wind,
                .gust = screenshotWeather.gust,
                .rainbow = screenshotWeather.rainbow,
                .aurora = screenshotWeather.aurora,
                .forcedFrames = WeatherForcedFramesRemaining(),
                .forcedCloudFrames = WeatherForcedCloudFramesRemaining(),
                .surfaceCount = screenshotWeatherImpacts.surfaceCount,
                .activeFires = screenshotWeatherImpacts.activeFires,
                .blockDamageEvents =
                    screenshotWeatherImpacts.blockDamageEvents,
                .firePresent = haveScreenshotFire,
                .firePhase = haveScreenshotFire ?
                    WildfirePhaseName(screenshotFire.state.phase) : "inactive",
                .firePosition = {
                    screenshotFire.x, screenshotFire.y, screenshotFire.z
                },
                .fireDistance = haveScreenshotFire &&
                                isfinite(frame->wildfireExposure.nearestDistance)
                    ? frame->wildfireExposure.nearestDistance : -1.0f,
                .fireIntensity = screenshotFire.state.intensity,
                .fireFuel = screenshotFire.state.fuel,
                .fireMoisture = screenshotFire.state.moisture,
                .fireHeatOutput = screenshotFire.state.heatOutput,
                .fireSmokeOutput = screenshotFire.state.smokeOutput,
                .fireLocalHeat = frame->wildfireExposure.heat,
                .fireLocalSmoke = frame->wildfireExposure.smoke,
                .firePlumeWindAngle = frame->weatherVisual.windAngle,
                .firePlumeWindDrift = frame->weatherVisual.windDrift,
                .fireHaze = frame->environmentPresentation.smokeHaze,
                .fireSnapshotCount = frame->wildfireCount,
                .fireRenderMaxFires =
                    (unsigned)screenshotQuality.wildfireMaxFires,
                .fireRenderFlameTongues =
                    (unsigned)screenshotQuality.wildfireFlameTongues,
                .fireRenderSmokePuffs =
                    (unsigned)screenshotQuality.wildfireSmokePuffs,
                .fireIgnitions = screenshotWeatherImpacts.ignitions,
                .fireSpreadIgnitions =
                    screenshotWeatherImpacts.spreadIgnitions,
                .fireExtinctions = screenshotWeatherImpacts.extinctions,
                .fireSuppressions = screenshotWeatherImpacts.suppressions,
                .fireBurnedBlocks = screenshotWeatherImpacts.burnedBlocks,
                .fireBurnSiteCount = screenshotWeatherImpacts.burnSiteCount,
                .fireRecoveredSites =
                    screenshotWeatherImpacts.recoveredBurnSites,
                .fireDroppedIgnitions =
                    screenshotWeatherImpacts.droppedIgnitions,
                .fireDroppedBurnSites =
                    screenshotWeatherImpacts.droppedBurnSites,
                .damageEnabled = WeatherImpactEnabled(),
                .tornadoPhase = TornadoPhaseName(screenshotTornado.phase),
                .tornadoCenter = ScreenshotVector(screenshotTornado.center),
                .tornadoActive = screenshotTornado.active,
                .tornadoForced = screenshotTornado.forced,
                .tornadoDistance = TornadoDistanceTo(game->camera.position),
                .tornadoIntensity = screenshotTornado.intensity,
                .tornadoRadius = screenshotTornado.radius,
                .tornadoFunnelHeight = screenshotTornado.funnelHeight,
                .tornadoWindMps = screenshotTornado.maximumWindMps,
                .tornadoCondensation = screenshotTornado.condensation,
                .tornadoDustLoading = screenshotTornado.dustLoading,
                .tornadoForcedFrames = TornadoForcedFramesRemaining(),
                .tornadoBlockDamageEvents =
                    screenshotTornadoStats.blockDamageEvents,
                .tornadoDebrisEmitted =
                    screenshotTornadoStats.debrisEmitted,
                .tornadoDustEmitted = screenshotTornadoStats.dustEmitted,
                .tornadoDroppedEffects = screenshotTornadoStats.droppedEffects
            },
            .environment = {
                .altitude = frame->environmentSample.altitude,
                .atmosphereFade = frame->skyFade,
                .underwaterDepth = frame->environmentSample.underwaterDepth,
                .waterSurfaceY = frame->playerWater.surfaceY,
                .seabedY = frame->bathymetry.seabedY,
                .waterColumnDepth = frame->bathymetry.waterDepth,
                .bathymetryZone = BathymetryZoneName(frame->bathymetry.zone),
                .seabedMaterial = BathymetryMaterialName(
                    frame->bathymetry.material),
                .underwater = frame->environmentSample.underwater,
                .feetSubmerged = frame->playerWater.feetSubmerged,
                .bodySubmerged = frame->playerWater.bodySubmerged,
                .eyesSubmerged = frame->playerWater.eyesSubmerged,
                .sheltered = frame->environmentSample.sheltered,
                .forest = frame->environmentSample.forest,
                .nearWater = frame->environmentSample.nearWater,
                .shipInterior = frame->environmentSample.shipInterior
            },
            .fluid = {
                .volume = screenshotFluid.volume,
                .surfaceY = screenshotFluid.surfaceY,
                .flowVelocity = ScreenshotVector(
                    screenshotFluid.velocity),
                .ticks = screenshotFluidStats.ticks,
                .loadedVolume = FluidLoadedVolume(),
                .activeCells = screenshotFluidStats.activeCells,
                .lastProcessedCells =
                    screenshotFluidStats.lastProcessedCells,
                .editCount = screenshotFluidStats.editCount,
                .queueOverflows = screenshotFluidStats.queueOverflows
            },
            .block = {
                .catalogCount = BLOCK_NATURAL_END + 1 + COLOR_BLOCK_COUNT,
                .naturalCount =
                    BLOCK_NATURAL_END - BLOCK_NATURAL_START + 1,
                .stage05Count =
                    BLOCK_STAGE05_END - BLOCK_STAGE05_START + 1,
                .galleryActive = game->blockGalleryActive,
                .galleryOrigin = ScreenshotVector(game->blockGalleryOrigin),
                .galleryPlaced = game->blockGalleryPlaced,
                .galleryRows = 3u,
                .galleryWidth = 14u
            },
            .input = {
                .forward = game->appliedPlayerInput.forward,
                .strafe = game->appliedPlayerInput.strafe,
                .vertical = game->appliedPlayerInput.vertical,
                .sprint = game->appliedPlayerInput.sprint,
                .remainingFrames = game->scriptedInputFrames
            },
            .render = {
                .graphicsQuality = GraphicsQualityName(game->settings.graphicsQuality),
                .renderDistanceChunks = frame->effectiveRenderDistance,
                .fps = GetFPS(),
                .screenWidth = GetScreenWidth(),
                .screenHeight = GetScreenHeight(),
                .frameTimeMs = frame->dt * 1000.0f,
                .performanceMode = game->perfMode
            },
            .ui = {
                .paused = game->paused,
                .albumOpen = game->albumOpen,
                .starMapOpen = StarMapIsOpen(),
                .importDialogOpen = game->importDialog.open,
                .cursorReleased = game->cursorReleased,
                .helpVisible = game->showHelp,
                .debugHudVisible = game->showDebug,
                .landingTransitionActive = game->landingTransition.active
            },
            .streaming = {
                .activeChunks = GetActiveChunkCount(),
                .activeSpaceChunks = GetActiveSpaceChunkCount(),
                .activeNetherChunks = GetActiveNetherChunkCount(),
                .activeEntities = GetActiveEntityCount(),
                .pendingGenerationJobs = GetPendingGenJobCount(),
                .pendingMeshJobs = GetPendingMeshJobCount(),
                .surfaceChunkX = screenshotWaterRender.cx,
                .surfaceChunkZ = screenshotWaterRender.cz,
                .surfaceSectionY = screenshotWaterRender.sectionY,
                .surfaceChunkLoaded = screenshotWaterRender.chunkLoaded,
                .playerMissingSurfaceChunks =
                    screenshotMissingSurfaceChunks,
                .surfaceReady = screenshotMissingSurfaceChunks == 0,
                .waterNeighborLoadedMask =
                    screenshotWaterRender.neighborLoadedMask,
                .waterTriangleCount = screenshotWaterRender.triangleCount,
                .waterSectionTriangleCount =
                    screenshotWaterRender.sectionTriangleCount,
                .waterDebugEnabled = screenshotWaterPass.enabled,
                .waterDebugThrough = screenshotWaterPass.through,
                .waterVisibleSectionCount =
                    screenshotWaterPass.visibleSectionCount,
                .waterDrawItemCount = screenshotWaterPass.drawItemCount,
                .waterDrawTriangleCount = screenshotWaterPass.triangleCount,
                .waterHasNearest = screenshotWaterPass.hasNearest,
                .waterNearestChunkX = screenshotWaterPass.nearestChunkX,
                .waterNearestChunkZ = screenshotWaterPass.nearestChunkZ,
                .waterNearestSectionY = screenshotWaterPass.nearestSectionY,
                .generationSubmitted = streamingStats.generationSubmitted,
                .generationCompleted = streamingStats.generationCompleted,
                .generationCanceled = streamingStats.generationCanceled,
                .meshSubmitted = streamingStats.meshSubmitted,
                .meshCompleted = streamingStats.meshCompleted,
                .meshCanceled = streamingStats.meshCanceled,
                .meshSnapshotBytes = streamingStats.meshSnapshotBytes,
                .syncRebuilds = streamingStats.syncRebuilds,
                .uploadedMeshes = streamingStats.uploadedMeshes,
                .uploadBudgetDeferrals = streamingStats.uploadBudgetDeferrals,
                .generationQueuePeak = streamingStats.generationQueuePeak,
                .meshQueuePeak = streamingStats.meshQueuePeak,
                .pendingMeshSnapshotBytes =
                    streamingStats.pendingMeshSnapshotBytes,
                .pendingMeshSnapshotBytesPeak =
                    streamingStats.pendingMeshSnapshotBytesPeak,
                .generationCpuMs = streamingStats.generationCpuMs,
                .meshCpuMs = streamingStats.meshCpuMs,
                .uploadCpuMs = streamingStats.uploadCpuMs,
                .maxUploadCpuMs = streamingStats.maxUploadCpuMs
            },
            .evolution = {
                .entitySelected = haveScreenshotEntity,
                .scanLocked = game->evolutionScanLocked,
                .atlasOpen = game->biologyAtlasOpen,
                .corpse = screenshotEntity.corpse,
                .juvenile = screenshotEntity.juvenile,
                .pregnant = screenshotEntity.pregnant,
                .regionAvailable = haveScreenshotRegion,
                .bootstrapComplete = screenshotRegion.bootstrapComplete,
                .organismId = screenshotEntity.organismId,
                .lineageId = screenshotEntity.lineageId,
                .speciesId = screenshotEntity.speciesId,
                .genomeId = screenshotEntity.genomeId,
                .generation = screenshotEntity.generation,
                .mutationCount = screenshotEntity.mutationCount,
                .moduleCount = screenshotEntity.moduleCount,
                .motherId = screenshotEntity.motherId,
                .fatherId = screenshotEntity.fatherId,
                .childCount = haveScreenshotIndividual
                    ? screenshotIndividual.childCount : 0u,
                .catalogSpeciesCount =
                    (uint32_t)EvolutionCatalogSpeciesCount(),
                .catalogIndividualCount =
                    (uint32_t)EvolutionCatalogIndividualCount(),
                .regionalLineageCount = screenshotRegion.lineageCount,
                .bootstrapGeneration =
                    screenshotRegion.bootstrapGeneration,
                .sex = !haveScreenshotEntity ? "none" :
                       screenshotEntity.sex == CREATURE_SEX_FEMALE ?
                       "female" : "male",
                .locomotion = haveScreenshotEntity ?
                    EvolutionLocomotionName(screenshotEntity.locomotion) :
                    "none",
                .ageDays = screenshotEntity.ageDays,
                .maturityAgeDays = screenshotEntity.maturityAgeDays,
                .health = screenshotEntity.health,
                .energy = screenshotEntity.energy,
                .diet = screenshotEntity.diet,
                .mass = screenshotEntity.mass,
                .speed = screenshotEntity.speed,
                .herbivoreDensity = screenshotRegion.herbivoreDensity,
                .omnivoreDensity = screenshotRegion.omnivoreDensity,
                .carnivoreDensity = screenshotRegion.carnivoreDensity
            }
        };
        ScreenshotResult screenshotResult = ScreenshotCaptureFrame(
            SCREENSHOT_DIRECTORY, screenshotTime, screenshotPath,
            sizeof(screenshotPath));
        if (screenshotResult == SCREENSHOT_RESULT_OK) {
            ScreenshotResult reportResult = ScreenshotWriteDebugReport(
                screenshotPath, screenshotTime, &debugInfo, debugReportPath,
                sizeof(debugReportPath));
            if (reportResult == SCREENSHOT_RESULT_OK) {
                GameNoticePost(TextFormat(
                    "Debug capture saved: %s (+ .txt)", screenshotPath));
                DebugControlReply(
                    &game->debugControl,
                    "DEBUG_CONTROL capture ok png=%s report=%s\n",
                    screenshotPath, debugReportPath);
            } else {
                GameNoticePost(TextFormat(
                    "Screenshot saved; %s",
                    ScreenshotResultMessage(reportResult)));
                DebugControlReply(
                    &game->debugControl,
                    "DEBUG_CONTROL capture partial png=%s error=%s\n",
                    screenshotPath, ScreenshotResultMessage(reportResult));
            }
        } else {
            GameNoticePost(ScreenshotResultMessage(screenshotResult));
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL capture error reason=%s\n",
                ScreenshotResultMessage(screenshotResult));
        }
        game->screenshotPending = false;
    }
}
