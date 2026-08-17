#include "gameplay/ship_internal.h"

ShipRuntime shipRuntime = {
    .driveMode = SHIP_DRIVE_MANEUVER,
    .fuel = SHIP_MAX_FUEL
};

void ShipRuntimeReset(void)
{
    shipRuntime = (ShipRuntime){
        .driveMode = SHIP_DRIVE_MANEUVER,
        .fuel = SHIP_MAX_FUEL
    };
}
