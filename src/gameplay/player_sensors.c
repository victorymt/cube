#include "gameplay/player_sensors.h"

#include "core/config.h"

PlayerWaterState PlayerWaterStateAt(Vector3 position)
{
    return WorldWaterSampleAt(position, EYE_HEIGHT, PLAYER_HEIGHT);
}
