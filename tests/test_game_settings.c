#include "app/game_settings.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>

static void TestDefaultsAndQualityProfiles(void)
{
    GameSettings settings = GameSettingsDefaults();
    assert(settings.graphicsQuality == GRAPHICS_QUALITY_MEDIUM);
    assert(settings.chunkWorkerCount == 0);
    assert(settings.masterVolume == 1.0f);
    assert(settings.ambientVolume == 0.70f);
    assert(settings.musicVolume == 0.22f);
    assert(settings.musicEnabled);
    assert(settings.weatherDamageEnabled);
    assert(GraphicsQualityProfileFor(GRAPHICS_QUALITY_LOW).shadowMapSize == 0);
    assert(GraphicsQualityProfileFor(GRAPHICS_QUALITY_MEDIUM).shadowUpdateInterval == 2);
    assert(GraphicsQualityProfileFor(GRAPHICS_QUALITY_HIGH).shadowMapSize >
           GraphicsQualityProfileFor(GRAPHICS_QUALITY_MEDIUM).shadowMapSize);
    GraphicsQualityProfile low = GraphicsQualityProfileFor(GRAPHICS_QUALITY_LOW);
    GraphicsQualityProfile medium = GraphicsQualityProfileFor(GRAPHICS_QUALITY_MEDIUM);
    GraphicsQualityProfile high = GraphicsQualityProfileFor(GRAPHICS_QUALITY_HIGH);
    assert(low.cloudRaySteps < medium.cloudRaySteps);
    assert(medium.cloudRaySteps < high.cloudRaySteps);
    assert(low.cloudLightSteps < high.cloudLightSteps);
    assert(low.cloudGridRadius < high.cloudGridRadius);
}

static void TestRoundTripAndCorruption(void)
{
    const char *path = "/tmp/voxelcraft_settings_test.cfg";
    unlink(path);
    unlink("/tmp/voxelcraft_settings_test.cfg.bak");
    GameSettings written = GameSettingsDefaults();
    written.graphicsQuality = GRAPHICS_QUALITY_HIGH;
    written.chunkWorkerCount = 6;
    written.masterVolume = 0.63f;
    written.ambientVolume = 0.41f;
    written.musicVolume = 0.17f;
    written.musicEnabled = false;
    written.weatherDamageEnabled = false;
    assert(GameSettingsSavePath(path, &written));
    GameSettings loaded;
    assert(GameSettingsLoadPath(path, &loaded));
    assert(loaded.graphicsQuality == GRAPHICS_QUALITY_HIGH);
    assert(loaded.chunkWorkerCount == 6);
    assert(fabsf(loaded.masterVolume - 0.63f) < 0.001f);
    assert(fabsf(loaded.ambientVolume - 0.41f) < 0.001f);
    assert(fabsf(loaded.musicVolume - 0.17f) < 0.001f);
    assert(!loaded.musicEnabled);
    assert(!loaded.weatherDamageEnabled);

    FILE *file = fopen(path, "wb");
    assert(file);
    fputs("not settings\n", file);
    fclose(file);
    assert(!GameSettingsLoadPath(path, &loaded));
    assert(loaded.graphicsQuality == GRAPHICS_QUALITY_MEDIUM);
    unlink(path);
    unlink("/tmp/voxelcraft_settings_test.cfg.bak");
}

static void TestVersionOneMigration(void)
{
    const char *path = "/tmp/voxelcraft_settings_v1_test.cfg";
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs("VOXELCRAFT_SETTINGS 1\n"
          "graphics_quality=0\n"
          "master_volume=0.5\n"
          "ambient_volume=0.4\n"
          "music_volume=0.3\n"
          "music_enabled=0\n", file);
    fclose(file);
    GameSettings loaded;
    assert(GameSettingsLoadPath(path, &loaded));
    assert(loaded.graphicsQuality == GRAPHICS_QUALITY_LOW);
    assert(loaded.chunkWorkerCount == 0);
    assert(!loaded.musicEnabled);
    assert(loaded.weatherDamageEnabled);
    unlink(path);
}

static void TestVersionTwoMigration(void)
{
    const char *path = "/tmp/voxelcraft_settings_v2_test.cfg";
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs("VOXELCRAFT_SETTINGS 2\n"
          "graphics_quality=2\n"
          "master_volume=0.8\n"
          "ambient_volume=0.6\n"
          "music_volume=0.4\n"
          "music_enabled=1\n"
          "weather_damage_enabled=0\n", file);
    fclose(file);
    GameSettings loaded;
    assert(GameSettingsLoadPath(path, &loaded));
    assert(loaded.graphicsQuality == GRAPHICS_QUALITY_HIGH);
    assert(loaded.chunkWorkerCount == 0);
    assert(!loaded.weatherDamageEnabled);
    unlink(path);
}

static void TestSanitize(void)
{
    GameSettings invalid = GameSettingsDefaults();
    invalid.graphicsQuality = (GraphicsQuality)99;
    invalid.chunkWorkerCount = 99;
    invalid.masterVolume = NAN;
    invalid.ambientVolume = -3.0f;
    invalid.musicVolume = 4.0f;
    GameSettings clean = GameSettingsSanitize(invalid);
    assert(clean.graphicsQuality == GRAPHICS_QUALITY_MEDIUM);
    assert(clean.chunkWorkerCount == 0);
    assert(clean.masterVolume == 1.0f);
    assert(clean.ambientVolume == 0.0f);
    assert(clean.musicVolume == 1.0f);

    invalid = GameSettingsDefaults();
    invalid.chunkWorkerCount = -1;
    clean = GameSettingsSanitize(invalid);
    assert(clean.chunkWorkerCount == 0);
}

int main(void)
{
    TestDefaultsAndQualityProfiles();
    TestRoundTripAndCorruption();
    TestVersionOneMigration();
    TestVersionTwoMigration();
    TestSanitize();
    puts("game settings tests passed");
    return 0;
}
