#ifndef VOXELCRAFT_GAME_INTERACTION_H
#define VOXELCRAFT_GAME_INTERACTION_H

#include "gameplay/ship.h"
#include "space/space_types.h"
#include "world/world_types.h"

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
    bool meshPriority;
    int meshPriorityX;
    int meshPriorityY;
    int meshPriorityZ;
} GameInteractionContext;

int GameUpdateInteractions(struct GameRuntime *game, float dt,
                           bool inputBlocked,
                           GameInteractionContext *context);
void GameInteractionQueueDebugMouse(struct GameRuntime *game, bool right);

bool GameInteractionObserveEvolutionInfo(
    const struct EntityEvolutionDebugInfo *info);

#endif
