#ifndef VOXELCRAFT_SHIP_H
#define VOXELCRAFT_SHIP_H

#include "types.h"

#include <stdio.h>

#define SHIP_MAX_FUEL 100.0f

bool ShipTryEnter(int x, int y, int z, Player *player);
bool ShipIsDriving(void);
bool ShipIsCruising(void);
bool ShipIsWarping(void);
bool ShipFlightAssistEnabled(void);
bool ShipHasGravityPrimary(void);
const char *ShipGravityPrimaryName(void);
float ShipGravityPrimaryDistance(void);
float ShipGravitySphereOfInfluence(void);
bool ShipHasWarpTarget(void);
bool ShipWarpTargetIsSystem(void);
const char *ShipWarpTargetName(void);
bool ShipBeginSystemWarp(Player *player, int systemAnchorX, int systemAnchorZ);
void ShipReset(void);
float ShipGetFuel(void);
bool ShipRefuel(void);
bool ShipSaveState(FILE *file);
bool ShipLoadState(FILE *file);
void ShipToggleCruise(void);
void ShipUpdate(Player *player, float dt);
void ShipExit(Player *player);
void ShipForceExit(Player *player);
void ShipLoadModel(void);
void ShipCleanup(void);
void ShipDraw(const Player *player);

#endif
