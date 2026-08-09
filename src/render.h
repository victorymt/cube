#ifndef VOXELCRAFT_RENDER_H
#define VOXELCRAFT_RENDER_H

#include "types.h"
#include "space.h"

#include <stdint.h>

extern Model cloudModel;

Model LoadCloudModel(void);

void DayNightFactors(float currentDayTime, float *daylight, float *sunset);
Color WorldTintForLight(float daylight, float sunset);
Color MixWeather(Color color, float daylight);
void ApplyPlanetWorldPalette(Color *top, Color *horizon, Color *worldTint);
void ApplyPlanetWorldPaletteWithLight(Color *top, Color *horizon, Color *worldTint,
                                      const PlanetLightState *light);
void DrawPlanetAtmosphereSky(const Camera3D *camera, const PlanetLightState *light);
void SkyColorsForLight(float daylight, float sunset, Color *top, Color *horizon);
void DrawStars(const Camera3D *camera, float daylight);
void DrawCelestial(const Camera3D *camera, float currentDayTime, float daylight);
void DrawSpaceSky(float spaceFade, const Camera3D *camera);
void DrawSolarGuide(const Camera3D *camera, float spaceFade);
void DrawSolarOrbitTrajectories(const Camera3D *camera, float spaceFade);
void DrawSolarBodies(const Camera3D *camera, float spaceFade);
void DrawHomePlanet(const Camera3D *camera, float spaceFade);
void UnloadPlanetRenderResources(void);
void DrawBodyInfoPanel(const SpaceBodyInfo *body);
void DrawClouds(const Camera3D *camera, Color tint);
void DrawWorld(const Camera3D *camera, int effectiveRenderDistance, Color tint,
               bool drawSurfaceChunks, bool drawNetherChunks);

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
void DrawPauseMenu(bool *resume, bool *saveWorld, bool *saveAndQuit, bool *toggleMusic, bool *returnToMenu);
void DrawDebugHUD(Vector3 playerPosition, float yaw, float pitch);
extern float dayTimeForHud;
extern bool autoSaveForHud;
extern BlockType blockForHud;
extern int SpaceEditCountForHud;
extern float shipSpeedForHud;
extern float shipHudSpeed;
extern float shipHudAlt;
extern float shipHudHeading;
extern char shipHudSystem[48];
extern bool shipHudCruising;
extern bool shipHudNearPlanet;
void DrawShipHud(void);

#endif
