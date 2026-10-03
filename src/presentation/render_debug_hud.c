#include "presentation/render.h"
#include "presentation/render_debug_hud_internal.h"
#include "presentation/render_internal.h"
#include "presentation/audio.h"
#include "presentation/particles.h"
#include "core/simulation_time.h"
#include "ecology/ecology.h"
#include "ecology/entity.h"
#include "gameplay/ship.h"
#include "space/planet_observation.h"
#include "space/space_chunks.h"
#include "space/space_query.h"
#include "space/space_state.h"
#include "space/space_units.h"
#include "world/chunks.h"
#include "world/nether.h"
#include "world/terrain.h"
#include "world/weather.h"
#include "world/weather_impact.h"
#include "world/world.h"
#include "world/world_environment.h"

#include "raymath.h"

#include <math.h>

static Font debugHudFont = { 0 };
static bool debugHudFontReady = false;
static int debugHudScrollY = 0;
static int debugHudContentBottom = 0;
static int debugHudVisibleBottom = 0;

void DebugHudFontShutdown(void)
{
    if (debugHudFontReady) UnloadFont(debugHudFont);
    debugHudFont = (Font){ 0 };
    debugHudFontReady = false;
    debugHudScrollY = 0;
    debugHudContentBottom = 0;
    debugHudVisibleBottom = 0;
}

void DebugHudFontInit(const char *fontPath)
{
    DebugHudFontShutdown();
    if (fontPath) {
        int ascii[95];
        for (int i = 0; i < 95; i++) ascii[i] = 32 + i;
        Font loaded = LoadFontEx(fontPath, 32, ascii, 95);
        if (IsFontValid(loaded) && loaded.texture.id != 0u &&
            loaded.texture.id != GetFontDefault().texture.id) {
            debugHudFont = loaded;
            debugHudFontReady = true;
            SetTextureFilter(debugHudFont.texture, TEXTURE_FILTER_BILINEAR);
        }
    }
}

static void DebugHudDrawText(const char *text, int x, int y, int fontSize,
                              Color color)
{
    if (y + fontSize > debugHudContentBottom) {
        debugHudContentBottom = y + fontSize;
    }
    y -= debugHudScrollY;
    if (y + fontSize < 70 || y > debugHudVisibleBottom) return;
    if (debugHudFontReady) {
        DrawTextEx(debugHudFont, text, (Vector2){ (float)x, (float)y },
                   (float)fontSize, 0.5f, color);
    } else {
        UiDrawText(text, x, y, fontSize, color);
    }
}

void DrawDebugHUD(Vector3 playerPosition, float yaw, float pitch, float daylight,
                  const PlanetLightState *light,
                  const PlanetObservationState *observation,
                  float seasonProgress,
                  const WeatherVisualState *weatherVisual,
                  const BathymetrySample *bathymetry,
                  const HudFrameState *hud,
                  const DiagnosticsCaptureIdentity *diagnosticsIdentity)
{
    if (!hud) return;
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    Rectangle panel = {
        10.0f, 10.0f, fminf((float)screenWidth - 20.0f, 740.0f),
        fminf((float)screenHeight - 70.0f, 670.0f)
    };
    DrawRectangleRounded(panel, 0.025f, 8, Fade(BLACK, 0.78f));
    DrawRectangleRoundedLinesEx(panel, 0.025f, 8, 1.0f,
                                Fade(WHITE, 0.18f));
    if (debugHudFontReady) {
        DrawTextEx(debugHudFont, "F3 DEBUG", (Vector2){ 18.0f, 22.0f },
                   18.0f, 0.5f, Fade(WHITE, 0.95f));
    } else {
        UiDrawText("F3 DEBUG", 18, 22, 18, Fade(WHITE, 0.95f));
    }
    int visibleTop = 70;
    debugHudVisibleBottom = (int)(panel.y + panel.height) - 34;
    int visibleHeight = debugHudVisibleBottom - visibleTop;
    int maxScroll = debugHudContentBottom - debugHudVisibleBottom;
    if (maxScroll < 0) maxScroll = 0;
    if (IsKeyPressed(KEY_PAGE_DOWN)) debugHudScrollY += visibleHeight - 40;
    if (IsKeyPressed(KEY_PAGE_UP)) debugHudScrollY -= visibleHeight - 40;
    if (debugHudScrollY > maxScroll) debugHudScrollY = maxScroll;
    if (debugHudScrollY < 0) debugHudScrollY = 0;
    debugHudContentBottom = 0;
    BeginScissorMode(16, visibleTop, (int)panel.width - 22,
                     visibleHeight);
    int x = 18;
    int y = 76;
    int line = 20;
    int fs = 17;

    DebugHudDrawText(TextFormat("XYZ %.1f %.1f %.1f   yaw %.1f pitch %.1f",
                        playerPosition.x, playerPosition.y, playerPosition.z, yaw * RAD2DEG, pitch * RAD2DEG),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    DebugHudDrawText(TextFormat("FPS %d   frame %.2f ms", GetFPS(), GetFrameTime() * 1000.0f), x, y, fs, Fade(WHITE, 0.85f)); y += line;
    ChunkDiagnostics chunkDiagnostics = { 0 };
    ChunksCaptureDiagnosticsAt(&chunkDiagnostics, diagnosticsIdentity);
    DebugHudDrawText(TextFormat("Chunks loaded %d   gen queue %d   mesh queue %d",
                        chunkDiagnostics.activeChunks,
                        chunkDiagnostics.pendingGenerationJobs,
                        chunkDiagnostics.pendingMeshJobs),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    ChunkStreamingStats streaming = chunkDiagnostics.streaming;
    DebugHudDrawText(TextFormat("Stream gen %.1fms  mesh %.1fms  upload %.1fms (max %.2f)  sync %llu",
                        streaming.generationCpuMs, streaming.meshCpuMs,
                        streaming.uploadCpuMs, streaming.maxUploadCpuMs,
                        (unsigned long long)streaming.syncRebuilds),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    DebugHudDrawText(TextFormat("Stream peak queues gen %llu  mesh %llu  upload defers %llu",
                        (unsigned long long)streaming.generationQueuePeak,
                        (unsigned long long)streaming.meshQueuePeak,
                        (unsigned long long)streaming.uploadBudgetDeferrals),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    DebugHudDrawText(TextFormat("Particles %d   edits %d   render dist %d",
                        ParticlesActiveCount(), WorldGetEditCount(),
                        ChunksRenderDistance()),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    DebugHudDrawText(TextFormat("Space %d/%d   nether %d   entities %d",
                        GetActiveSpaceChunkCount(), hud->spaceEditCount,
                        GetActiveNetherChunkCount(), GetActiveEntityCount()),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    DebugHudDrawText(TextFormat("Weather %s   time %02d:00   auto-save %s",
                        WeatherName(), (int)(hud->dayTime * 24.0f) % 24,
                        hud->autoSaveEnabled ? "on" : "off"),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    if (HomeWorldSurfaceIsActive() || PlanetWorldIsActive()) {
        WeatherFieldSample localWeather = WeatherCurrentSample();
        LocalClimateState localClimate = { 0 };
        bool haveClimate = WeatherLocalClimateAtWorldTime(
            (int)floorf(playerPosition.x), (int)floorf(playerPosition.z),
            SimulationTimePeriodic(SpaceElapsedSimulationTime()),
            &localClimate);
        WeatherImpactStats weatherImpacts = WeatherImpactGetStats();
        DebugHudDrawText(TextFormat("Climate %s   phenomenon %s",
                            haveClimate ? ClimateRegimeName(localClimate.regime) :
                                          "unavailable",
                            WeatherPhenomenonName(
                                localWeather.dominantPhenomenon)),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("T %.1f C   P %.3f atm   RH %.0f%%   wind %.2f gust %.2f",
                            localWeather.temperatureK - 273.15f,
                            localWeather.pressureAtm,
                            localWeather.relativeHumidity * 100.0f,
                            localWeather.wind, localWeather.gust),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Cloud %s %.2f (%u layers)   precip %.2f   storm %.2f",
                            WeatherCloudGenusName(
                                localWeather.dominantCloudGenus),
                            localWeather.cloudCover,
                            weatherVisual ? weatherVisual->cloudLayerCount : 0u,
                            localWeather.precipitation, localWeather.storm),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Weather effects surfaces %u   fires %u   damage %s (%u)",
                            weatherImpacts.surfaceCount,
                            weatherImpacts.activeFires,
                            WeatherImpactEnabled() ? "on" : "off",
                            weatherImpacts.blockDamageEvents),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        if (weatherVisual && weatherVisual->active) {
            DebugHudDrawText(TextFormat("Visibility %.2f   fog %.2f   veil %.2f   cloud AGL %.1f",
                                weatherVisual->visibility,
                                weatherVisual->fogDensity,
                                weatherVisual->precipitationVeil,
                                weatherVisual->cloudBaseHeight),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
        }
    }
    if (bathymetry && bathymetry->seaLevel >= 0) {
        DebugHudDrawText(TextFormat("Bathymetry %s   seabed %d   water %d   material %s",
                            BathymetryZoneName(bathymetry->zone),
                            bathymetry->seabedY,
                            bathymetry->waterDepth,
                            BathymetryMaterialName(bathymetry->material)),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    }
    SolarSystemDef hudSystem;
    float hudSystemDist = 0.0f;
    if (FindSystemForGuide(playerPosition, &hudSystem, &hudSystemDist)) {
        double distanceAu = SpaceUnitsGameDistanceToKilometers(hudSystemDist) /
                            SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
        DebugHudDrawText(TextFormat("System %s (%.3g AU)", hudSystem.name,
                              distanceAu),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    } else {
        DebugHudDrawText("Deep space", x, y, fs, Fade(WHITE, 0.85f)); y += line;
    }
    if (PlanetWorldIsActive()) {
        if (light && observation && observation->valid) {
            DebugHudDrawText(TextFormat(
                         "Sky %s   season %.0f%%   day %.1fh   local flux %.3f   stars %.2f   moon %.2f   eclipse %.2f",
                         PlanetObservationPhaseName(observation->phase),
                         Clamp(seasonProgress, 0.0f, 1.0f) * 100.0f,
                         Clamp(light->dayLengthFraction, 0.0f, 1.0f) * 24.0f,
                         fmaxf(light->incidentIrradiance, 0.0f),
                         observation->starVisibility,
                         observation->moonVisibility,
                         observation->eclipseDarkening),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
        }
        PlanetLocalEcology ecology = PlanetEcologyLocalAt(
            (int)floorf(playerPosition.x), (int)floorf(playerPosition.z), daylight);
        DebugHudDrawText(TextFormat("Ecology capacity %.2f   flora %.2f   fauna %.2f   limit %s",
                            ecology.suitability.carryingCapacity,
                            ecology.suitability.floraCapacity,
                            ecology.suitability.faunaCapacity,
                            PlanetEcologyLimitingFactorName(
                                ecology.suitability.limitingFactor)),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Activity flora %.2f   fauna %.2f   water %.2f   rain %.2f",
                            ecology.suitability.floraActivity,
                            ecology.suitability.faunaActivity,
                            ecology.environment.liquidWaterAccess,
                            ecology.environment.precipitationRate),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Region (%d,%d)   disturbance %.3f   flora stress %.3f",
                            ecology.diagnostics.regionX,
                            ecology.diagnostics.regionZ,
                            ecology.environment.disturbance,
                            ecology.environment.disturbance * 0.82f),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Fauna harvest %.3f   stress %.3f   net %+.5f/day",
                            ecology.population.faunaHarvestPressure,
                            ecology.diagnostics.faunaStress,
                            ecology.diagnostics.faunaNetRecoveryRate),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Population flora %.2f/%.2f   fauna %.2f/%.2f   seasonal %.2f   radiation memory %.2f",
                            ecology.population.floraDensity,
                            ecology.population.floraCarryingCapacity,
                            ecology.population.faunaDensity,
                            ecology.population.faunaCarryingCapacity,
                            ecology.population.seasonalMemory,
                            ecology.diagnostics.radiationMemory),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Migration net flora %+.3f   fauna %+.3f",
                            ecology.migration.floraNet,
                            ecology.migration.faunaNet),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Flow flora (%+.3f,%+.3f)   fauna (%+.3f,%+.3f)",
                            ecology.migration.floraFlowX,
                            ecology.migration.floraFlowZ,
                            ecology.migration.faunaFlowX,
                            ecology.migration.faunaFlowZ),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Climate %.0f/%.0f K   light %.2f/%.2f   storm %.2f   radiation %.2f   ejecta %.2f",
                            ecology.environment.meanTemperatureK,
                            ecology.environment.currentTemperatureK,
                            ecology.environment.meanUsableLight,
                            ecology.environment.currentUsableLight,
                            ecology.environment.currentStorm,
                            ecology.environment.radiationExposure,
                            ecology.environment.ejectaExposure),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Terrain elevation %.2f   slope %.2f   shelter %.2f",
                            ecology.environment.elevation,
                            ecology.environment.slope,
                            ecology.environment.shelter),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    }
    SpaceScaleDiagnostics scale;
    if (SpaceScaleDiagnosticsAt(playerPosition, &scale)) {
        DebugHudDrawText(TextFormat("Scale %.0f u/AU   1 play s = 1 sim day   error %.3f ppm [%s]",
                            SPACE_UNITS_GAME_DISTANCE_PER_AU,
                            scale.maxRelativeError * 1000000.0,
                            scale.withinErrorBudget ? "OK" : "OUT"),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("%s radius %.0f km = %.5f linear u",
                            scale.bodyName, scale.physicalRadiusKm,
                            scale.physicalRadiusGame),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Proxy visual %.1f u   landing %.1f u   x%.0f",
                            scale.visualRadiusGame, scale.landingRadiusGame,
                            scale.landingRadiusScale),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Gravity %.2f m/s2 (%.2f g)   gameplay %.2f u/s2",
                            scale.physicalGravityMetersPerSecondSquared,
                            scale.physicalGravityEarth,
                            scale.gameplaySurfaceGravity),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Orbit speed %.2f km/s   %.3f u/play-s",
                            scale.orbitalSpeedKilometersPerSecond,
                            scale.orbitalSpeedGame),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("SOI %.0f km (%.3f linear u)   Hill %.0f km",
                            scale.sphereOfInfluenceKm,
                            scale.physicalSphereOfInfluenceGame,
                            scale.hillSphereKm),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Encounter %.1f u   x%.1f [%s]",
                            scale.encounterRadiusGame,
                            scale.encounterRadiusScale,
                            scale.encounterRadiusClamped ? "proxy clamp" : "physical"),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Flux now %.3f Earth   climate mean %.3f Earth",
                            scale.currentIrradianceEarth,
                            scale.climateIrradianceEarth),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        DebugHudDrawText(TextFormat("Temperature radiative %.0f K   surface %.0f K",
                            scale.radiativeTemperatureK,
                            scale.surfaceTemperatureK),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    } else {
        DebugHudDrawText("Scale target: no planet within 700 u", x, y, fs,
                 Fade(WHITE, 0.68f)); y += line;
    }
    if (!PlanetWorldIsActive() && !HomeWorldSurfaceIsActive()) {
        SpaceSatelliteScaleDiagnostics satelliteScale;
        if (SpaceSatelliteScaleDiagnosticsAt(playerPosition,
                                             &satelliteScale)) {
            DebugHudDrawText(TextFormat("Moon %s radius %.0f km = %.5f linear u",
                                satelliteScale.bodyName,
                                satelliteScale.physicalRadiusKm,
                                satelliteScale.physicalRadiusGame),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
            DebugHudDrawText(TextFormat("Moon gravity %.2f m/s2   orbit %.2f km/s",
                                satelliteScale.physicalGravityMetersPerSecondSquared,
                                satelliteScale.orbitalSpeedKilometersPerSecond),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
            DebugHudDrawText(TextFormat("Moon SOI %.0f km   Hill %.0f km",
                                satelliteScale.sphereOfInfluenceKm,
                                satelliteScale.hillSphereKm),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
            DebugHudDrawText(TextFormat("Moon encounter %.2f u [%s]",
                                satelliteScale.encounterRadiusGame,
                                satelliteScale.withinErrorBudget ? "OK" : "OUT"),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
        }
    }
    DebugHudDrawText(TextFormat("Block %s   music %s", BlockName(hud->targetedBlock),
                        AudioIsMusicEnabled() ? "on" : "off"),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    if (ShipIsDriving()) {
        if (WorldIsSpaceActive()) {
            DebugHudDrawText(TextFormat("Ship speed %.2f km/s",
                                  SpaceUnitsGameVelocityToKilometersPerSecond(
                                      hud->shipSpeed)),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
        } else {
            DebugHudDrawText(TextFormat("Ship speed %.1f blocks/s", hud->shipSpeed),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
        }
    }
    EndScissorMode();
    if (debugHudContentBottom > debugHudVisibleBottom) {
        const char *hint = "PgUp / PgDn scroll";
        if (debugHudFontReady) {
            DrawTextEx(debugHudFont, hint,
                       (Vector2){ 18.0f, panel.y + panel.height - 27.0f },
                       14.0f, 0.5f, Fade(WHITE, 0.8f));
        } else {
            UiDrawText(hint, 18, (int)(panel.y + panel.height) - 27, 14,
                       Fade(WHITE, 0.8f));
        }
    }
}
