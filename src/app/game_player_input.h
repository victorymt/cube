#ifndef VOXELCRAFT_GAME_PLAYER_INPUT_H
#define VOXELCRAFT_GAME_PLAYER_INPUT_H

#include "core/input_event.h"
#include "gameplay/player_input.h"

struct GameRuntime;

/* Maps the immutable frame captured by app/input_source into player commands.
   Gameplay owns the command semantics; app owns the physical polling. */
PlayerInput GamePlayerInputFromFrame(const InputFrame *frame);

/* Script windows take precedence; debug sessions otherwise require explicit
   manual handoff before reading desktop keyboard/mouse state. */
PlayerInput GameReadPlayerInput(struct GameRuntime *game);
#endif
