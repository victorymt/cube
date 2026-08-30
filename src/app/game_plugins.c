#include "app/game_plugins.h"

#include "app/game_runtime.h"
#include "core/plugin.h"

#include <stdio.h>

bool GamePluginsArgumentsValid(const GameRuntime *game)
{
    if (!game || !game->pluginDirectoryInvalid) return true;
    fprintf(stderr,
            "Invalid or repeated --plugin-dir option; expected a "
            "non-empty directory path shorter than %u bytes.\n",
            VOXELCRAFT_PLUGIN_MAX_PATH);
    return false;
}

bool GamePluginsStart(GameRuntime *game)
{
    if (!game) return false;
    if (game->pluginDirectory[0] != '\0' &&
        !VoxelcraftPluginHostLoadDirectory(&game->plugins,
                                           game->pluginDirectory)) {
        goto fail;
    }
    if (VoxelcraftPluginHostCount(&game->plugins) > 0u &&
        !VoxelcraftPluginHostActivate(&game->plugins, game)) {
        goto fail;
    }
    return true;

fail:
    fprintf(stderr, "Failed to load plugins from '%s': %s\n",
            game->pluginDirectory,
            VoxelcraftPluginHostLastError(&game->plugins));
    game->processExitCode = 2;
    VoxelcraftPluginHostShutdown(&game->plugins);
    return false;
}

void GamePluginsReset(GameRuntime *game)
{
    if (game) VoxelcraftPluginHostReset(&game->plugins);
}

void GamePluginsUpdate(GameRuntime *game, float dt)
{
    if (!game) return;
    VoxelcraftPluginFrame frame = {
        .frame_index = game->debugFrame,
        .dt = dt,
        .debug_mode = game->debugControlEnabled
    };
    VoxelcraftPluginHostUpdate(&game->plugins, &frame);
}

void GamePluginsRender(GameRuntime *game, float dt)
{
    if (!game) return;
    VoxelcraftPluginFrame frame = {
        .frame_index = game->debugFrame,
        .dt = dt,
        .debug_mode = game->debugControlEnabled
    };
    VoxelcraftPluginHostRender(&game->plugins, &frame);
}

void GamePluginsShutdown(GameRuntime *game)
{
    if (game) VoxelcraftPluginHostShutdown(&game->plugins);
}

size_t GamePluginsCount(const GameRuntime *game)
{
    return game ? VoxelcraftPluginHostCount(&game->plugins) : 0u;
}
