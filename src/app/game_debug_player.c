#include "app/game_debug_player.h"

#include "app/game_debug_trace.h"
#include "app/game_runtime.h"
#include "core/config.h"
#include "core/debug_control.h"
#include "gameplay/player.h"
#include "gameplay/player_state.h"

#include "raylib.h"
#include "raymath.h"

#include <math.h>

void GameDebugTeleport(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL teleport error reason=not_playing\n");
        return;
    }
    PlayerPlaceAt(&game->player,
        (Vector3){ game->debugControl.teleport.x,
                   game->debugControl.teleport.y,
                   game->debugControl.teleport.z },
        Vector3Zero(), game->debugControl.teleport.yaw,
        game->debugControl.teleport.pitch, false, game->player.floating);
    PlayerResetRuntimeState(&game->player);
    game->scriptedInputFrames = 0u;
    game->scriptedInputFirstFrame = false;
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL teleport ok position=%.6f,%.6f,%.6f\n",
                      game->player.position.x, game->player.position.y,
                      game->player.position.z);
    GameDebugTraceEvent(game, "teleport");
}

void GameDebugLook(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL look error reason=not_playing\n");
        return;
    }
    if (game->debugControl.lookAt) {
        float dx = game->debugControl.lookAtX - game->player.position.x;
        float dy = game->debugControl.lookAtY -
            (game->player.position.y + EYE_HEIGHT);
        float dz = game->debugControl.lookAtZ - game->player.position.z;
        float horizontal = sqrtf(dx * dx + dz * dz);
        if (horizontal > 0.0001f) {
            PlayerSetLook(&game->player, atan2f(dx, dz),
                          atan2f(dy, horizontal));
        } else {
            PlayerSetLook(&game->player, game->player.yaw,
                          dy >= 0.0f ? 1.45f : -1.45f);
        }
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL look at ok target=%.3f,%.3f,%.3f "
                          "yaw=%.6f pitch=%.6f\n",
                          game->debugControl.lookAtX, game->debugControl.lookAtY,
                          game->debugControl.lookAtZ, game->player.yaw,
                          game->player.pitch);
        GameDebugTraceEvent(game, "look-at");
        return;
    }
    if (game->debugControl.lookRelative) {
        PlayerSetLook(&game->player,
                      game->player.yaw + game->debugControl.lookYaw,
                      game->player.pitch + game->debugControl.lookPitch);
    } else {
        PlayerSetLook(&game->player, game->debugControl.lookYaw,
                      game->debugControl.lookPitch);
    }
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL look ok yaw=%.6f pitch=%.6f\n",
                      game->player.yaw, game->player.pitch);
    GameDebugTraceEvent(game, "look");
}

