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

/* Accumulate render-frame commands until the next fixed simulation tick.
 * Held axes track the latest frame; look and edge-triggered actions accumulate
 * until one tick consumes them. */
void GamePlayerInputStageFixed(PlayerInput *staged, bool *edgesPending,
                               const PlayerInput *frameInput);
PlayerInput GamePlayerInputConsumeFixed(PlayerInput *staged,
                                        bool *edgesPending);
#endif
