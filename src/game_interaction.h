#ifndef VOXELCRAFT_GAME_INTERACTION_H
#define VOXELCRAFT_GAME_INTERACTION_H

#include "ship.h"
#include "space.h"
#include "types.h"

struct EntityEvolutionDebugInfo;
struct GameRuntime;

typedef struct GameInteractionContext {
    HitResult hit;
    HitResult interactionHit;
    int entityHit;
    SpaceBodyInfo aimBody;
    bool haveAimBody;
    ParkedShip hitShip;
    bool hitParkedShip;
    int placeX;
    int placeY;
    int placeZ;
    bool canPlace;
    ShipDirection placementDirection;
} GameInteractionContext;

int GameUpdateInteractions(struct GameRuntime *game, float dt,
                           bool inputBlocked,
                           GameInteractionContext *context);

bool GameInteractionObserveEvolutionInfo(
    const struct EntityEvolutionDebugInfo *info);

#endif
