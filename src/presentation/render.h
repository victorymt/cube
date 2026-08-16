#ifndef VOXELCRAFT_RENDER_H
#define VOXELCRAFT_RENDER_H

#include "world/world_types.h"
#include "presentation/ui_types.h"
#include "space/space.h"
#include "space/planet_observation.h"
#include "gameplay/ship_locator.h"
#include "presentation/weather_visual.h"
#include "presentation/world_renderer.h"
#include "presentation/environment_presentation.h"
#include "presentation/planet_renderer.h"
#include "world/terrain.h"

#include <stdint.h>

typedef struct PauseMenuActions {
    bool resume;
    bool saveWorld;
    bool saveAndQuit;
    bool returnToMenu;
    bool settingsChanged;
    bool qualityChanged;
} PauseMenuActions;

typedef struct HudFrameState {
    float dayTime;
    float shipSpeed;
    BlockType targetedBlock;
    int spaceEditCount;
    bool autoSaveEnabled;
} HudFrameState;

typedef struct ShipHudState {
    float speed;
    float targetSpeed;
    float closingSpeed;
    float brakingDistance;
    float etaSeconds;
    float altitude;
    float atmosphere;
    float heading;
    const char *systemName;
    const char *driveMode;
    bool cruising;
    bool approaching;
    bool supercruising;
    bool warping;
    bool nearPlanet;
    bool subsurface;
    bool submerged;
} ShipHudState;

void UiFontInit(void);
void UiFontShutdown(void);
int UiMeasureText(const char *text, int fontSize);
void UiDrawText(const char *text, int x, int y, int fontSize, Color color);
Model LoadCloudModel(void);
void SetCloudModel(Model model);
void UnloadCloudRenderResources(void);

void DayNightFactors(float currentDayTime, float *daylight, float *sunset);
Color WorldTintForLight(float daylight, float sunset);
Color MixWeather(Color color, float daylight,
                 const WeatherVisualState *weatherVisual);
void ApplyPlanetWorldPalette(Color *top, Color *horizon, Color *worldTint);
void ApplyPlanetWorldPaletteWithLight(Color *top, Color *horizon, Color *worldTint,
                                      const PlanetLightState *light);
void ApplyPlanetWorldPaletteWithObservation(
    Color *top, Color *horizon, Color *worldTint,
    const PlanetLightState *light, const PlanetObservationState *observation);
PlanetObservationState PlanetObservationForCamera(
    const Camera3D *camera, const PlanetLightState *light);
void DrawPlanetAtmosphereSky(const Camera3D *camera, const PlanetLightState *light,
                             const PlanetObservationState *observation,
                             const WeatherVisualState *weatherVisual);
void SkyColorsForLight(float daylight, float sunset, Color *top, Color *horizon);
void DrawStars(const Camera3D *camera, float daylight,
               const PlanetObservationState *observation,
               const WeatherVisualState *weatherVisual);
void DrawCelestial(const Camera3D *camera, float currentDayTime, float daylight,
                   const PlanetLightState *planetLight,
                   const PlanetObservationState *observation,
                   const WeatherVisualState *weatherVisual);
void UpdatePlanetSceneExposure(const Camera3D *camera);
void DrawSpaceSky(float spaceFade, float daylight, const Camera3D *camera);
void DrawWarpTunnel(const Camera3D *camera, float intensity,
                    bool supercruise);
void DrawSolarGuide(const Camera3D *camera, float spaceFade);
void DrawSolarOrbitTrajectories(const Camera3D *camera, float spaceFade);
void DrawSolarBodies(const Camera3D *camera, float spaceFade);
void DrawHomePlanet(const Camera3D *camera, float spaceFade);
bool PlanetRenderSurfaceVisual(bool planetSurface,
                               PlanetTextureSet *outTextures,
                               Color *outFallback);
void UnloadPlanetRenderResources(void);
void DrawBodyInfoPanel(const SpaceBodyInfo *body);
void DrawClouds(const Camera3D *camera, Color tint, double simulationTime,
                const WeatherVisualState *weatherVisual,
                const EnvironmentPresentationState *presentation,
                const WorldLightingState *lighting);
void DrawWeatherOverlay(const Camera3D *camera,
                        const WeatherVisualState *weatherVisual);
void DrawEnvironmentPostProcess(
    const EnvironmentPresentationState *presentation);
WorldLightingState WorldLightingForScene(
    const Camera3D *camera, float currentDayTime, float daylight, float sunset,
    const PlanetLightState *planetLight,
    const WeatherVisualState *weatherVisual, Color skyHorizon, bool inNether,
    const EnvironmentPresentationState *presentation);
void DrawWorld(const Camera3D *camera, int effectiveRenderDistance, Color tint,
               bool drawSurfaceChunks, bool drawNetherChunks,
               const WorldLightingState *lighting);
void DrawWorldShadowMap(const Camera3D *camera, int effectiveRenderDistance,
                        bool drawSurfaceChunks, bool drawNetherChunks,
                        const WorldLightingState *lighting);

void DrawCrosshair(int screenWidth, int screenHeight);
void DrawCenteredText(const char *text, int y, int fontSize, Color color);
bool DrawMenuButton(Rectangle rect, const char *label, bool primary);
void DrawHotbar(const BlockType *hotbar, int selectedIndex);
int HotbarKeyToIndex(void);
void DrawStartPage(bool *startGame, bool *quitGame, TerrainMode *selectedTerrain,
                   uint32_t *selectedSeed);
void DrawHelpPanel(bool floating, bool cursorReleased, int viewDistance);
void DrawCursorReleasedOverlay(void);
void DrawImportStatus(void);
void DrawImportDialog(ImportDialog *dialog);
void DrawPauseMenu(GameSettings *settings, PauseMenuActions *actions);
void DrawDebugHUD(Vector3 playerPosition, float yaw, float pitch, float daylight,
                  const PlanetLightState *light,
                  const PlanetObservationState *observation,
                  float seasonProgress,
                  const WeatherVisualState *weatherVisual,
                  const BathymetrySample *bathymetry,
                  const HudFrameState *hud);
void DrawShipHud(const ShipHudState *hud);
void DrawShipLocator(const Camera3D *camera, const ShipLocatorTarget *target);
void DrawMapNavigation(Vector3 playerPosition, float playerYaw);

#endif
