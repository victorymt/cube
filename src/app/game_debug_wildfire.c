#include "app/game_debug_wildfire.h"

#include "app/game_runtime.h"
#include "world/weather_impact.h"
#include "world/wildfire_model.h"
#include "world/world.h"

#include <stdio.h>

static void GameDebugWildfireMarkCommandError(
    GameRuntime *game, const char *reason)
{
    game->debugCommandFailed = true;
    snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure),
             "%s", reason);
}

bool GameDebugDispatchWildfireCommand(
    GameRuntime *game, DebugControlCommand command)
{
    if (!game) return false;

    int x = game->debugControl.weatherFireX;
    int y = game->debugControl.weatherFireY;
    int z = game->debugControl.weatherFireZ;
    switch (command) {
    case DEBUG_CONTROL_COMMAND_WEATHER_FIRE_IGNITE: {
        if (!SurfaceBlockReadyAt(x, y, z)) {
            GameDebugWildfireMarkCommandError(game, "weather_fire_unloaded");
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL weather fire ignite error "
                "reason=unloaded position=%d,%d,%d\n", x, y, z);
            return true;
        }
        if (!WeatherImpactIgniteAt(
                x, y, z, game->debugControl.weatherFireIntensity)) {
            GameDebugWildfireMarkCommandError(
                game, "weather_fire_ignition_rejected");
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL weather fire ignite error "
                "reason=not_ignitable position=%d,%d,%d\n", x, y, z);
            return true;
        }
        WeatherImpactFireSnapshot fire = { 0 };
        WeatherImpactFireStateAt(x, y, z, &fire);
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL weather fire ignite ok position=%d,%d,%d "
            "phase=%s intensity=%.6f fuel=%.6f moisture=%.6f\n",
            x, y, z, WildfirePhaseName(fire.state.phase),
            fire.state.intensity, fire.state.fuel, fire.state.moisture);
        return true;
    }
    case DEBUG_CONTROL_COMMAND_WEATHER_FIRE_SUPPRESS: {
        if (!SurfaceBlockReadyAt(x, y, z)) {
            GameDebugWildfireMarkCommandError(game, "weather_fire_unloaded");
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL weather fire suppress error "
                "reason=unloaded position=%d,%d,%d\n", x, y, z);
            return true;
        }
        unsigned affected = WeatherImpactSuppressAt(
            x, y, z, game->debugControl.weatherFireRadius,
            game->debugControl.weatherFireSuppression);
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL weather fire suppress ok position=%d,%d,%d "
            "radius=%.3f amount=%.6f affected=%u\n",
            x, y, z, game->debugControl.weatherFireRadius,
            game->debugControl.weatherFireSuppression, affected);
        return true;
    }
    case DEBUG_CONTROL_COMMAND_WEATHER_FIRE_CLEAR: {
        unsigned cleared = WeatherImpactClearFires();
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL weather fire clear ok cleared=%u\n",
                          cleared);
        return true;
    }
    default:
        return false;
    }
}
