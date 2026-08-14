#ifndef VOXELCRAFT_GAME_SETTINGS_H
#define VOXELCRAFT_GAME_SETTINGS_H

#include <stdbool.h>

#define GAME_SETTINGS_FILE "voxelcraft_settings.cfg"

typedef enum GraphicsQuality {
    GRAPHICS_QUALITY_LOW = 0,
    GRAPHICS_QUALITY_MEDIUM,
    GRAPHICS_QUALITY_HIGH,
    GRAPHICS_QUALITY_COUNT
} GraphicsQuality;

typedef struct GraphicsQualityProfile {
    int shadowMapSize;
    int shadowUpdateInterval;
    int shadowChunkRadius;
    int cloudGridRadius;
    int cloudRaySteps;
    int cloudLightSteps;
    float precipitationScale;
    bool postProcessing;
} GraphicsQualityProfile;

typedef struct GameSettings {
    int version;
    GraphicsQuality graphicsQuality;
    float masterVolume;
    float ambientVolume;
    float musicVolume;
    bool musicEnabled;
} GameSettings;

GameSettings GameSettingsDefaults(void);
GameSettings GameSettingsSanitize(GameSettings settings);
GraphicsQualityProfile GraphicsQualityProfileFor(GraphicsQuality quality);
const char *GraphicsQualityName(GraphicsQuality quality);

bool GameSettingsLoadPath(const char *path, GameSettings *settings);
bool GameSettingsSavePath(const char *path, const GameSettings *settings);
bool GameSettingsLoad(GameSettings *settings);
bool GameSettingsSave(const GameSettings *settings);

#endif
