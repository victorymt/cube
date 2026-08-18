#ifndef VOXELCRAFT_GAME_SETTINGS_H
#define VOXELCRAFT_GAME_SETTINGS_H

#include "presentation/render_quality.h"

#include <stdbool.h>

#define GAME_SETTINGS_FILE "voxelcraft_settings.cfg"

typedef struct GameSettings {
    int version;
    GraphicsQuality graphicsQuality;
    float masterVolume;
    float ambientVolume;
    float musicVolume;
    bool musicEnabled;
    bool weatherDamageEnabled;
} GameSettings;

GameSettings GameSettingsDefaults(void);
GameSettings GameSettingsSanitize(GameSettings settings);

bool GameSettingsLoadPath(const char *path, GameSettings *settings);
bool GameSettingsSavePath(const char *path, const GameSettings *settings);
bool GameSettingsLoad(GameSettings *settings);
bool GameSettingsSave(const GameSettings *settings);

#endif
