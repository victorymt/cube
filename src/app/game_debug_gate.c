#include "app/game_debug_gate.h"

#include "app/game_runtime.h"
#include "gameplay/ship.h"
#include "world/world.h"

#include <stddef.h>

const char *GameDebugDslCommandBlocked(
    const GameRuntime *game, DebugControlCommand command)
{
    switch (command) {
    case DEBUG_CONTROL_COMMAND_START:
        return game->screen == SCREEN_START ? NULL : "already_playing";
    case DEBUG_CONTROL_COMMAND_SCREENSHOT:
    case DEBUG_CONTROL_COMMAND_SAVE:
    case DEBUG_CONTROL_COMMAND_LOAD:
    case DEBUG_CONTROL_COMMAND_TELEPORT:
    case DEBUG_CONTROL_COMMAND_LOOK:
    case DEBUG_CONTROL_COMMAND_INPUT:
    case DEBUG_CONTROL_COMMAND_MOUSE_LEFT:
    case DEBUG_CONTROL_COMMAND_MOUSE_RIGHT:
    case DEBUG_CONTROL_COMMAND_VIEW:
    case DEBUG_CONTROL_COMMAND_SHIP_BEGIN:
    case DEBUG_CONTROL_COMMAND_SHIP_ENTER:
    case DEBUG_CONTROL_COMMAND_EVOLUTION_ADVANCE:
    case DEBUG_CONTROL_COMMAND_EVOLUTION_ATLAS:
    case DEBUG_CONTROL_COMMAND_BLOCK_INSPECT:
    case DEBUG_CONTROL_COMMAND_BLOCK_SET:
    case DEBUG_CONTROL_COMMAND_FLORA_INSPECT:
        return game->screen == SCREEN_PLAYING ? NULL : "not_playing";
    case DEBUG_CONTROL_COMMAND_SHIP_INPUT:
    case DEBUG_CONTROL_COMMAND_SHIP_EXHAUST:
        return game->screen == SCREEN_PLAYING && ShipIsDriving()
            ? NULL : "not_driving";
    case DEBUG_CONTROL_COMMAND_SHIP_DUST:
        return game->screen == SCREEN_PLAYING && ShipIsDriving() &&
                       WorldIsSurfaceActive()
            ? NULL : "no_surface_ship";
    case DEBUG_CONTROL_COMMAND_MAP:
    case DEBUG_CONTROL_COMMAND_MAP_LAYER_LIQUIDS:
    case DEBUG_CONTROL_COMMAND_WORLD_TOPOLOGY:
    case DEBUG_CONTROL_COMMAND_MARKER_ADD:
    case DEBUG_CONTROL_COMMAND_MARKER_LIST:
    case DEBUG_CONTROL_COMMAND_MARKER_TARGET:
    case DEBUG_CONTROL_COMMAND_MARKER_REMOVE:
    case DEBUG_CONTROL_COMMAND_FLUID_SET:
    case DEBUG_CONTROL_COMMAND_FLUID_STEP:
    case DEBUG_CONTROL_COMMAND_WEATHER_INSPECT:
    case DEBUG_CONTROL_COMMAND_WEATHER_FORCE:
    case DEBUG_CONTROL_COMMAND_WEATHER_CLOUD_FORCE:
    case DEBUG_CONTROL_COMMAND_WEATHER_FIRE_IGNITE:
    case DEBUG_CONTROL_COMMAND_WEATHER_FIRE_SUPPRESS:
    case DEBUG_CONTROL_COMMAND_WEATHER_FIRE_CLEAR:
    case DEBUG_CONTROL_COMMAND_WEATHER_STEP:
    case DEBUG_CONTROL_COMMAND_BLOCK_GALLERY:
    case DEBUG_CONTROL_COMMAND_FLORA_SAMPLE:
    case DEBUG_CONTROL_COMMAND_FLORA_GALLERY:
        return WorldIsSurfaceActive() ? NULL : "no_active_surface";
    case DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_HOME:
    case DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_PLANET:
        return game->screen == SCREEN_PLAYING ? NULL : "not_playing";
    case DEBUG_CONTROL_COMMAND_STREAM_AUDIT:
    case DEBUG_CONTROL_COMMAND_STREAM_WAIT:
        if (game->screen != SCREEN_PLAYING || !WorldIsSurfaceActive()) {
            return "not_in_surface_world";
        }
        if (game->streamAudit.active || game->streamAudit.wait.active) {
            return "stream_operation_in_progress";
        }
        return NULL;
    default:
        return NULL;
    }
}
