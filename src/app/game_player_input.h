#ifndef VOXELCRAFT_GAME_PLAYER_INPUT_H
#define VOXELCRAFT_GAME_PLAYER_INPUT_H
#include "gameplay/player.h"
struct GameRuntime;
/* Script windows take precedence; debug sessions otherwise require explicit
   manual handoff before reading desktop keyboard/mouse state. */
PlayerInput GameReadPlayerInput(struct GameRuntime *game);
#endif
