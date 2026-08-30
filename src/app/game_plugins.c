#include "app/game_plugins.h"

#include "app/game_runtime.h"
#include "core/game_notice.h"
#include "core/plugin.h"
#include "world/world.h"

#include <stdio.h>

static bool GamePluginIsPlaying(void *userData)
{
    const GameRuntime *game = userData;
    return game && game->screen == SCREEN_PLAYING;
}

static bool GamePluginIsPaused(void *userData)
{
    const GameRuntime *game = userData;
    return game && game->paused;
}

static uint64_t GamePluginFrameIndex(void *userData)
{
    const GameRuntime *game = userData;
    return game ? game->debugFrame : 0u;
}

static uint32_t GamePluginWorldSeed(void *userData)
{
    (void)userData;
    return WorldGetSeed();
}

static void GamePluginPostNotice(void *userData, const char *message)
{
    (void)userData;
    GameNoticePost(message);
}

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
    if (VoxelcraftPluginHostCount(&game->plugins) > 0u) {
        game->pluginGameService = (VoxelcraftPluginGameService){
            .abi_version = VOXELCRAFT_PLUGIN_GAME_SERVICE_ABI_VERSION,
            .struct_size = sizeof(VoxelcraftPluginGameService),
            .user_data = game,
            .is_playing = GamePluginIsPlaying,
            .is_paused = GamePluginIsPaused,
            .frame_index = GamePluginFrameIndex,
            .world_seed = GamePluginWorldSeed,
            .post_notice = GamePluginPostNotice};
        const VoxelcraftPluginService gameService = {
            .abi_version = VOXELCRAFT_PLUGIN_GAME_SERVICE_ABI_VERSION,
            .struct_size = sizeof(VoxelcraftPluginService),
            .id = VOXELCRAFT_PLUGIN_GAME_SERVICE_ID,
            .interface = &game->pluginGameService};
        if (!VoxelcraftPluginHostProvideBuiltin(&game->plugins, "host",
                                                &gameService) ||
            !VoxelcraftPluginHostActivate(&game->plugins, game)) {
            goto fail;
        }
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
