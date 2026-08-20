#include "app/game_runtime.h"

#include "core/game_notice.h"
#include "gameplay/player.h"
#include "world/chunks.h"

#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_RESOLUTION_MIN_WIDTH 320
#define DEBUG_RESOLUTION_MIN_HEIGHT 240
#define DEBUG_RESOLUTION_MAX_WIDTH 7680
#define DEBUG_RESOLUTION_MAX_HEIGHT 4320

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
                                size_t pathSize, bool *outInvalid) {
  bool enabled = false;
  bool invalid = false;
  path[0] = '\0';
  for (int index = 1; index < argc; index++) {
    if (strcmp(argv[index], "--debug-trace") == 0) {
      enabled = true;
      if (index + 1 < argc && strncmp(argv[index + 1], "--", 2) != 0) {
        if (!GameDebugTraceSetPath(path, pathSize, argv[++index])) {
          invalid = true;
        }
      }
    } else if (strncmp(argv[index], "--debug-trace=", 14) == 0) {
      enabled = true;
      if (!GameDebugTraceSetPath(path, pathSize, argv[index] + 14)) {
        invalid = true;
      }
    }
  }
  if (outInvalid) *outInvalid = invalid;
  return enabled;
}

static bool ParseDebugScriptArgs(int argc, char **argv, char *path,
                                 size_t pathSize, bool *outInvalid) {
  bool enabled = false;
  bool invalid = false;
  path[0] = '\0';
  for (int index = 1; index < argc; index++) {
    const char *value = NULL;
    if (strcmp(argv[index], "--debug-script") == 0) {
      if (index + 1 < argc && strncmp(argv[index + 1], "--", 2) != 0)
        value = argv[++index];
      else
        invalid = true;
    } else if (strncmp(argv[index], "--debug-script=", 15) == 0) {
      value = argv[index] + 15;
    } else {
      continue;
    }
    if (enabled || !value || value[0] == '\0' || strlen(value) >= pathSize) {
      invalid = true;
      enabled = true;
      continue;
    }
    snprintf(path, pathSize, "%s", value);
    enabled = true;
  }
  if (outInvalid) *outInvalid = invalid;
  return enabled;
}

static bool ParseDebugResolutionValue(const char *value, int *outWidth,
                                      int *outHeight) {
  if (!value || !outWidth || !outHeight || value[0] == '\0')
    return false;

  errno = 0;
  char *widthEnd = NULL;
  long width = strtol(value, &widthEnd, 10);
  if (errno != 0 || widthEnd == value || *widthEnd != 'x')
    return false;

  const char *heightText = widthEnd + 1;
  errno = 0;
  char *heightEnd = NULL;
  long height = strtol(heightText, &heightEnd, 10);
  if (errno != 0 || heightEnd == heightText || *heightEnd != '\0' ||
      width < DEBUG_RESOLUTION_MIN_WIDTH ||
      width > DEBUG_RESOLUTION_MAX_WIDTH ||
      height < DEBUG_RESOLUTION_MIN_HEIGHT ||
      height > DEBUG_RESOLUTION_MAX_HEIGHT) {
    return false;
  }

  *outWidth = (int)width;
  *outHeight = (int)height;
  return true;
}

static bool ParseDebugResolutionArgs(int argc, char **argv, int *outWidth,
                                     int *outHeight, bool *outInvalid) {
  bool enabled = false;
  bool invalid = false;
  for (int index = 1; index < argc; index++) {
    const char *value = NULL;
    if (strcmp(argv[index], "--debug-resolution") == 0) {
      if (index + 1 < argc && strncmp(argv[index + 1], "--", 2) != 0)
        value = argv[++index];
      else
        invalid = true;
    } else if (strncmp(argv[index], "--debug-resolution=", 19) == 0) {
      value = argv[index] + 19;
    } else {
      continue;
    }

    if (enabled || !ParseDebugResolutionValue(value, outWidth, outHeight))
      invalid = true;
    enabled = true;
  }
  if (outInvalid) *outInvalid = invalid;
  return enabled;
}

static void ParseChunkWorkerArgs(int argc, char **argv, int *outCount,
                                 bool *outInvalid, bool *outOverridden) {
  bool seen = false;
  bool invalid = false;
  int count = 0;
  for (int index = 1; index < argc; index++) {
    const char *value = NULL;
    if (strcmp(argv[index], "--chunk-workers") == 0) {
      if (index + 1 < argc && strncmp(argv[index + 1], "--", 2) != 0)
        value = argv[++index];
      else
        invalid = true;
    } else if (strncmp(argv[index], "--chunk-workers=", 16) == 0) {
      value = argv[index] + 16;
    } else {
      continue;
    }
    if (seen || !value || value[0] == '\0') {
      invalid = true;
      seen = true;
      continue;
    }
    if (strcmp(value, "auto") == 0) {
      count = 0;
    } else {
      errno = 0;
      char *end = NULL;
      long parsed = strtol(value, &end, 10);
      if (errno != 0 || end == value || *end != '\0' || parsed < 1 ||
          parsed > MAX_CHUNK_WORKER_THREADS) {
        invalid = true;
      } else {
        count = (int)parsed;
      }
    }
    seen = true;
  }
  if (outCount) *outCount = count;
  if (outInvalid) *outInvalid = invalid;
  if (outOverridden) *outOverridden = seen;
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
      .screenWidth = 1280,
      .screenHeight = 720,
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
  runtime->debugStdinEnabled = CommandLineHasFlag(argc, argv, "--debug-stdin");
  runtime->debugScriptEnabled = ParseDebugScriptArgs(
      argc, argv, runtime->debugScriptPath, sizeof(runtime->debugScriptPath),
      &runtime->debugScriptPathInvalid);
  runtime->debugControlEnabled =
      runtime->debugStdinEnabled || runtime->debugScriptEnabled;
  if (runtime->debugControlEnabled) signal(SIGPIPE, SIG_IGN);
  runtime->debugTraceEnabled = ParseDebugTraceArgs(
      argc, argv, runtime->debugTracePath, sizeof(runtime->debugTracePath),
      &runtime->debugTracePathInvalid);
  ParseDebugResolutionArgs(argc, argv, &runtime->screenWidth,
                           &runtime->screenHeight,
                           &runtime->debugResolutionInvalid);
  ParseChunkWorkerArgs(argc, argv, &runtime->chunkWorkerCount,
                       &runtime->chunkWorkerCountInvalid,
                       &runtime->chunkWorkerCountOverridden);
  if (runtime->debugControlEnabled) {
    runtime->autoSaveEnabled = false;
    runtime->showHelp = false;
  }
  GameSettingsLoad(&runtime->settings);
  if (!runtime->chunkWorkerCountOverridden) {
    runtime->chunkWorkerCount = runtime->settings.chunkWorkerCount;
  }
  PlayerResetRuntimeState(&runtime->player);
  runtime->camera.up = (Vector3){0.0f, 1.0f, 0.0f};
  runtime->camera.fovy =
      CameraFovForHeight(runtime->player.position.y + EYE_HEIGHT);
  runtime->camera.projection = CAMERA_PERSPECTIVE;
  DebugControlInit(&runtime->debugControl, runtime->debugControlEnabled);
  if (!runtime->debugStdinEnabled) runtime->debugControl.inputClosed = true;
}

void GameRuntimeApplyPendingChunkWorkerReconfigure(GameRuntime *runtime) {
  if (!runtime || !runtime->chunkWorkerReconfigurePending || runtime->paused)
    return;
  runtime->chunkWorkerReconfigurePending = false;

  int requested = runtime->settings.chunkWorkerCount;
  int previous = runtime->chunkWorkerCount;
  if (ChunksRestartGenThreads(requested)) {
    runtime->chunkWorkerCount = requested;
    return;
  }

  runtime->settings.chunkWorkerCount = previous;
  runtime->chunkWorkerCount = previous;
  bool restored = ChunksRestartGenThreads(previous);
  GameSettingsSave(&runtime->settings);
  GameNoticePost(restored
                     ? "Chunk worker change failed; previous count restored."
                     : "Chunk worker change failed; using synchronous generation.");
}
