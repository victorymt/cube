#include "app/game_runtime.h"

#include "gameplay/player.h"

#include <stdio.h>
#include <string.h>

static bool CommandLineHasFlag(int argc, char **argv, const char *flag) {
  for (int index = 1; index < argc; index++) {
    if (strcmp(argv[index], flag) == 0)
      return true;
  }
  return false;
}

static bool ParsePerfArgs(int argc, char **argv, char *reportPath,
                          size_t reportPathSize, char *baselinePath,
                          size_t baselinePathSize) {
  bool enabled = false;
  reportPath[0] = '\0';
  baselinePath[0] = '\0';
  for (int index = 1; index < argc; index++) {
    if (strcmp(argv[index], "--perf") == 0) {
      enabled = true;
    } else if (strcmp(argv[index], "--perf-report") == 0 && index + 1 < argc) {
      snprintf(reportPath, reportPathSize, "%s", argv[++index]);
    } else if (strcmp(argv[index], "--perf-baseline") == 0 &&
               index + 1 < argc) {
      snprintf(baselinePath, baselinePathSize, "%s", argv[++index]);
    }
  }
  return enabled;
}

static bool ParseDebugTraceArgs(int argc, char **argv, char *path,
                                size_t pathSize) {
  bool enabled = false;
  path[0] = '\0';
  for (int index = 1; index < argc; index++) {
    if (strcmp(argv[index], "--debug-trace") == 0) {
      enabled = true;
      if (index + 1 < argc && strncmp(argv[index + 1], "--", 2) != 0) {
        snprintf(path, pathSize, "%s", argv[++index]);
      }
    } else if (strncmp(argv[index], "--debug-trace=", 14) == 0) {
      enabled = true;
      snprintf(path, pathSize, "%s", argv[index] + 14);
    }
  }
  return enabled;
}

void GameRuntimeInit(GameRuntime *runtime, int argc, char **argv) {
  if (!runtime)
    return;
  *runtime = (GameRuntime){
      .player = {.position = {0.5f, 12.0f, 0.5f},
                 .velocity = {0.0f, 0.0f, 0.0f},
                 .yaw = PI,
                 .pitch = -0.25f},
      .hotbar = {BLOCK_GRASS, BLOCK_DIRT, BLOCK_STONE, BLOCK_WOOD, BLOCK_PLANK,
                 BLOCK_SAND, BLOCK_SNOW, BLOCK_GLASS, BLOCK_WATER,
                 BLOCK_SPACESHIP},
      .importDialog = {.relief = true, .maxBlocks = IMPORT_DEFAULT_BLOCKS},
      .screen = SCREEN_START,
      .selectedTerrain = TERRAIN_VARIED,
      .selectedSeed = DEFAULT_WORLD_SEED,
      .biologyAtlasSlot = -1,
      .showHelp = true,
      .showOrbitTrajectories = true,
      .entitiesWorldActive = true,
      .autoSaveEnabled = true,
      .dayCycleEnabled = true,
      .autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS,
      .dayTime = 0.30f};

  runtime->perfMode = ParsePerfArgs(
      argc, argv, runtime->perfReportPath, sizeof(runtime->perfReportPath),
      runtime->perfBaselinePath, sizeof(runtime->perfBaselinePath));
  runtime->debugControlEnabled =
      CommandLineHasFlag(argc, argv, "--debug-stdin");
  runtime->debugTraceEnabled = ParseDebugTraceArgs(
      argc, argv, runtime->debugTracePath, sizeof(runtime->debugTracePath));
  if (runtime->debugControlEnabled)
    runtime->autoSaveEnabled = false;
  GameSettingsLoad(&runtime->settings);
  PlayerResetRuntimeState(&runtime->player);
  runtime->camera.up = (Vector3){0.0f, 1.0f, 0.0f};
  runtime->camera.fovy =
      CameraFovForHeight(runtime->player.position.y + EYE_HEIGHT);
  runtime->camera.projection = CAMERA_PERSPECTIVE;
  DebugControlInit(&runtime->debugControl, runtime->debugControlEnabled);
}
