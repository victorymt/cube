#ifndef GAME_DEBUG_WORLD_H
#define GAME_DEBUG_WORLD_H

#include "app/game_runtime.h"
#include "core/debug_control.h"

/*
 * New-world selection commands: the terrain, history and seed choices that the
 * next `start` consumes. They live together because they share one lifetime —
 * they are only meaningful on the start screen and become inert once a world
 * is running — and because game_debug.c is at its size limit.
 */

/* Apply a start-screen world-selection command.
 * Returns true when the command was consumed. */
bool GameDebugWorldDispatch(GameRuntime *game, DebugControlCommand command);

#endif
