#ifndef VOXELCRAFT_SHIP_H
#define VOXELCRAFT_SHIP_H

#include "world/world_types.h"
#include "gameplay/player_types.h"
#include "gameplay/ship_locator.h"

#include <stdio.h>

#define SHIP_MAX_FUEL 100.0f
#define SHIP_FOOTPRINT_SIZE 4
#define SHIP_FOOTPRINT_HEIGHT 2

typedef enum ShipDirection {
    SHIP_DIRECTION_NORTH = 0,
    SHIP_DIRECTION_EAST,
    SHIP_DIRECTION_SOUTH,
    SHIP_DIRECTION_WEST
} ShipDirection;

typedef enum ShipDriveMode {
    SHIP_DRIVE_MANEUVER = 0,
    SHIP_DRIVE_ORBIT,
    SHIP_DRIVE_MANUAL_CRUISE,
    SHIP_DRIVE_APPROACH,
    SHIP_DRIVE_SUPERCRUISE,
    SHIP_DRIVE_INTERSTELLAR_WARP
} ShipDriveMode;

typedef struct ShipControlInput {
    float forward;
    float strafe;
    float vertical;
} ShipControlInput;

typedef struct ParkedShip {
    int coreX;
    int coreY;
    int coreZ;
    ShipDirection direction;
    bool legacy;
} ParkedShip;

bool ShipTryEnter(int x, int y, int z, Player *player);
bool ShipBeginDebugFlight(Player *player);
ShipDirection ShipDirectionFromYaw(float yaw);
BlockType ShipCoreBlockForDirection(ShipDirection direction);
bool ShipBlockIsParkedCore(BlockType type);
bool ShipBlockIsOccupied(BlockType type);
bool ShipResolveParkedAt(int x, int y, int z, ParkedShip *out);
bool ShipCanPlaceParked(int coreX, int coreY, int coreZ,
                        ShipDirection direction, const Player *player);
bool ShipPlaceParked(int coreX, int coreY, int coreZ,
                     ShipDirection direction, bool recordUndo);
bool ShipRemoveParkedAt(int x, int y, int z, bool recordUndo);
bool ShipIsDriving(void);
bool ShipIsCruising(void);
bool ShipIsOrbiting(void);
bool ShipIsApproaching(void);
bool ShipIsSupercruising(void);
bool ShipIsInterstellarWarping(void);
bool ShipIsHighSpeedTransit(void);
ShipDriveMode ShipGetDriveMode(void);
const char *ShipDriveModeName(void);
bool ShipFlightAssistEnabled(void);
bool ShipHasGravityPrimary(void);
const char *ShipGravityPrimaryName(void);
float ShipGravityPrimaryDistance(void);
float ShipGravitySphereOfInfluence(void);
bool ShipHasNavigationTarget(void);
bool ShipNavigationTargetIsSystem(void);
const char *ShipNavigationTargetName(void);
float ShipRelativeSpeed(void);
float ShipTargetSpeed(void);
float ShipTargetClosingSpeed(void);
float ShipTargetBrakingDistance(void);
float ShipTargetEtaSeconds(void);
float ShipInterstellarWarpVisualIntensity(void);
float ShipDriveTunnelIntensity(void);
bool ShipBeginSystemWarp(Player *player, int systemAnchorX, int systemAnchorZ);
void ShipToggleNavigation(Player *player);
void ShipReset(void);
float ShipGetFuel(void);
/* Consume fuel atomically; invalid or insufficient amounts return false. */
bool ShipConsumeFuel(float amount);
bool ShipRefuel(void);
bool ShipSaveState(FILE *file);
bool ShipLoadState(FILE *file);
void ShipTrackParkedAt(int x, int y, int z);
void ShipForgetParkedAt(int x, int y, int z);
bool ShipLocatorTargetAt(Vector3 observer, ShipLocatorTarget *out);
void ShipToggleCruise(void);
void ShipUpdate(Player *player, float dt);
void ShipUpdateWithInput(Player *player, float dt,
                         const ShipControlInput *input);
bool ShipExit(Player *player);
bool ShipForceExit(Player *player);
void ShipLoadModel(void);
void ShipCleanup(void);
void ShipDraw(const Player *player);
void ShipResetVisualEffects(void);
float ShipVisualExhaustIntensity(void);
bool ShipSetDebugExhaust(const Player *player, float demand);
void ShipUpdateLandingEffects(const Player *player, float dt,
                              float descentProgress);
void ShipEmitTouchdownDust(const Player *player);

#endif
