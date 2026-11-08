#ifndef VOXELCRAFT_GAME_UI_INPUT_H
#define VOXELCRAFT_GAME_UI_INPUT_H

#include <stdbool.h>

typedef struct GameRuntime GameRuntime;

/* Routes one captured InputFrame through the active UI layer. Returns true
 * when the start page requested that a new world begin. */
bool GameUiInputDispatch(GameRuntime *game);

#endif
