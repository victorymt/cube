#include "app/game_debug_world.h"

#include "core/config.h"
#include "core/debug_control.h"

#include <stddef.h>

/* The two terrain choices and the history toggle replace the saved selection
 * only for the next world; they never touch a running one. */
static bool GameDebugWorldTerrain(GameRuntime *game,
                                  DebugControlCommand command)
{
    if (command != DEBUG_CONTROL_COMMAND_TERRAIN_FLAT &&
        command != DEBUG_CONTROL_COMMAND_TERRAIN_VARIED) {
        return false;
    }
    if (game->screen != SCREEN_START) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL terrain error reason=already_playing\n");
        return true;
    }
    game->selectedTerrain = command == DEBUG_CONTROL_COMMAND_TERRAIN_FLAT
        ? TERRAIN_FLAT : TERRAIN_VARIED;
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL terrain selected=%s\n",
                      game->selectedTerrain == TERRAIN_FLAT ? "flat" : "varied");
    return true;
}

static bool GameDebugWorldHistory(GameRuntime *game,
                                  DebugControlCommand command)
{
    if (command != DEBUG_CONTROL_COMMAND_TERRAIN_HISTORY) return false;
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL terrain history %s=%s\n",
                      game->screen == SCREEN_START ? "selected" : "ignored",
                      game->debugControl.terrainHistoryDisabled ? "off" : "on");
    return true;
}

/* `world seed SEED` makes a reported `world.seed` reproducible from a script:
 * the report's seed can be replayed instead of only the default world. */
static bool GameDebugWorldSeed(GameRuntime *game, DebugControlCommand command)
{
    if (command != DEBUG_CONTROL_COMMAND_WORLD_SEED) return false;
    if (game->screen != SCREEN_START) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL world seed error reason=already_playing\n");
        return true;
    }
    if (game->debugControl.newWorldSeedSelected) {
        game->selectedSeed = game->debugControl.newWorldSeed;
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL world seed selected=%u\n",
                          game->selectedSeed);
    } else {
        game->selectedSeed = DEFAULT_WORLD_SEED;
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL world seed selected=%u default=1\n",
                          game->selectedSeed);
    }
    return true;
}

bool GameDebugWorldDispatch(GameRuntime *game, DebugControlCommand command)
{
    return GameDebugWorldTerrain(game, command) ||
           GameDebugWorldHistory(game, command) ||
           GameDebugWorldSeed(game, command);
}
