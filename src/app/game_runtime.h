#ifndef VOXELCRAFT_GAME_RUNTIME_H
#define VOXELCRAFT_GAME_RUNTIME_H

#include "core/debug_control.h"
#include "presentation/environment_runtime.h"
#include "app/game_settings.h"
#include "space/planet_profile.h"
#include "gameplay/player.h"
#include "world/world_types.h"
#include "app/app_types.h"
#include "gameplay/player_types.h"
#include "presentation/ui_types.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct LandingTransition {
  bool active;
  bool committed;
  bool landed;
  bool homeWorldTarget;
  float elapsed;
  float duration;
  float summaryRemaining;
  float targetGravity;
  float targetRadius;
  float startDistance;
  float targetDistance;
  float startYaw;
  float startPitch;
  float targetYaw;
  float targetPitch;
  float atmosphereDensity;
  PlanetAtmosphereType atmosphereType;
  int targetSystemAnchorX;
  int targetSystemAnchorZ;
  int targetPlanetIndex;
  Vector3 targetCenter;
  Vector3 targetVelocity;
  Vector3 outward;
  Vector3 atmosphereStart;
  Vector3 landingPosition;
  char targetName[48];
  char landingPoint[64];
  char environment[96];
  char biosphere[96];
} LandingTransition;

typedef struct GameRuntime {
  GameSettings settings;
  Player player;
  Camera3D camera;
  EnvironmentPresentationRuntime environment;
  DebugControl debugControl;
  PlayerInput scriptedPlayerInput;
  PlayerInput appliedPlayerInput;
  BlockType hotbar[HOTBAR_SIZE];
  ImportDialog importDialog;
  LandingTransition landingTransition;

  GameScreen screen;
  TerrainMode selectedTerrain;
  uint32_t selectedSeed;
  unsigned scriptedInputFrames;

  int selectedIndex;
  int biologyAtlasSlot;
  uint32_t evolutionLockedOrganismId;

  bool perfMode;
  bool debugControlEnabled;
  bool showHelp;
  bool showDebug;
  bool scannerActive;
  bool shipLocatorEnabled;
  bool showOrbitTrajectories;
  bool screenshotPending;
  bool quitRequested;
  bool cursorReleased;
  bool paused;
  bool albumOpen;
  bool albumRainSuspended;
  bool wasInSpace;
  bool entitiesWorldActive;
  uint32_t entitiesWorldDimension;
  bool thirdPerson;
  bool scriptedInputFirstFrame;
  bool autoSaveEnabled;
  bool dayCycleEnabled;
  bool evolutionScanLocked;
  bool biologyAtlasOpen;
  bool quitSaveDone;

  float autoSaveTimer;
  float dayTime;
  char perfReportPath[512];
  char perfBaselinePath[512];
} GameRuntime;

void GameRuntimeInit(GameRuntime *runtime, int argc, char **argv);

#endif
