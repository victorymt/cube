#include "app/game_settings.h"

#include "core/save_io.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define GAME_SETTINGS_VERSION 2

static float SettingsUnit(float value, float fallback)
{
    if (!isfinite(value)) return fallback;
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

GameSettings GameSettingsDefaults(void)
{
    return (GameSettings){
        .version = GAME_SETTINGS_VERSION,
        .graphicsQuality = GRAPHICS_QUALITY_MEDIUM,
        .masterVolume = 1.0f,
        .ambientVolume = 0.70f,
        .musicVolume = 0.22f,
        .musicEnabled = true,
        .weatherDamageEnabled = true
    };
}

GameSettings GameSettingsSanitize(GameSettings settings)
{
    GameSettings defaults = GameSettingsDefaults();
    settings.version = GAME_SETTINGS_VERSION;
    if (settings.graphicsQuality < GRAPHICS_QUALITY_LOW ||
        settings.graphicsQuality >= GRAPHICS_QUALITY_COUNT) {
        settings.graphicsQuality = defaults.graphicsQuality;
    }
    settings.masterVolume = SettingsUnit(settings.masterVolume,
                                         defaults.masterVolume);
    settings.ambientVolume = SettingsUnit(settings.ambientVolume,
                                          defaults.ambientVolume);
    settings.musicVolume = SettingsUnit(settings.musicVolume,
                                        defaults.musicVolume);
    return settings;
}

static char *Trim(char *text)
{
    while (*text && isspace((unsigned char)*text)) text++;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return text;
}

bool GameSettingsLoadPath(const char *path, GameSettings *settings)
{
    if (!settings) return false;
    *settings = GameSettingsDefaults();
    if (!path) return false;

    FILE *file = fopen(path, "rb");
    if (!file) return false;
    char line[256];
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return false;
    }
    int version = 0;
    if (sscanf(line, "VOXELCRAFT_SETTINGS %d", &version) != 1 ||
        version < 1 || version > GAME_SETTINGS_VERSION) {
        fclose(file);
        return false;
    }

    GameSettings loaded = GameSettingsDefaults();
    bool valid = true;
    while (fgets(line, sizeof(line), file)) {
        char *separator = strchr(line, '=');
        if (!separator) continue;
        *separator = '\0';
        char *key = Trim(line);
        char *value = Trim(separator + 1);
        int integer = 0;
        float number = 0.0f;
        if (strcmp(key, "graphics_quality") == 0) {
            if (sscanf(value, "%d", &integer) != 1) valid = false;
            else loaded.graphicsQuality = (GraphicsQuality)integer;
        } else if (strcmp(key, "master_volume") == 0) {
            if (sscanf(value, "%f", &number) != 1) valid = false;
            else loaded.masterVolume = number;
        } else if (strcmp(key, "ambient_volume") == 0) {
            if (sscanf(value, "%f", &number) != 1) valid = false;
            else loaded.ambientVolume = number;
        } else if (strcmp(key, "music_volume") == 0) {
            if (sscanf(value, "%f", &number) != 1) valid = false;
            else loaded.musicVolume = number;
        } else if (strcmp(key, "music_enabled") == 0) {
            if (sscanf(value, "%d", &integer) != 1 ||
                (integer != 0 && integer != 1)) valid = false;
            else loaded.musicEnabled = integer != 0;
        } else if (strcmp(key, "weather_damage_enabled") == 0) {
            if (sscanf(value, "%d", &integer) != 1 ||
                (integer != 0 && integer != 1)) valid = false;
            else loaded.weatherDamageEnabled = integer != 0;
        }
    }
    if (ferror(file)) valid = false;
    fclose(file);
    if (!valid) return false;
    *settings = GameSettingsSanitize(loaded);
    return true;
}

static bool WriteSettings(FILE *file, void *context)
{
    const GameSettings *settings = context;
    return fprintf(file,
                   "VOXELCRAFT_SETTINGS %d\n"
                   "graphics_quality=%d\n"
                   "master_volume=%.3f\n"
                   "ambient_volume=%.3f\n"
                   "music_volume=%.3f\n"
                   "music_enabled=%d\n"
                   "weather_damage_enabled=%d\n",
                   GAME_SETTINGS_VERSION, (int)settings->graphicsQuality,
                   settings->masterVolume, settings->ambientVolume,
                   settings->musicVolume, settings->musicEnabled ? 1 : 0,
                   settings->weatherDamageEnabled ? 1 : 0) > 0;
}

bool GameSettingsSavePath(const char *path, const GameSettings *settings)
{
    if (!path || !settings) return false;
    char backup[512];
    int length = snprintf(backup, sizeof(backup), "%s.bak", path);
    if (length < 0 || (size_t)length >= sizeof(backup)) return false;
    GameSettings clean = GameSettingsSanitize(*settings);
    return SaveIoWriteAtomic(path, backup, WriteSettings, &clean);
}

bool GameSettingsLoad(GameSettings *settings)
{
    return GameSettingsLoadPath(GAME_SETTINGS_FILE, settings);
}

bool GameSettingsSave(const GameSettings *settings)
{
    return GameSettingsSavePath(GAME_SETTINGS_FILE, settings);
}
