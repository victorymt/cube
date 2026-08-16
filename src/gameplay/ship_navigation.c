#include "gameplay/ship_navigation.h"

#include <math.h>

#define SHIP_NAVIGATION_APPROACH_LEAD_SECONDS 8.0f
#define SHIP_NAVIGATION_SUPERCRUISE_BRAKE_SECONDS 2.5f

float ShipNavigationSupercruiseExitMargin(float safeDistance,
                                          float approachSpeed)
{
    if (!isfinite(safeDistance) || safeDistance < 0.0f) safeDistance = 0.0f;
    if (!isfinite(approachSpeed) || approachSpeed <= 0.0f) approachSpeed = 1.0f;
    return fmaxf(safeDistance * 4.0f,
                 approachSpeed * SHIP_NAVIGATION_SUPERCRUISE_BRAKE_SECONDS);
}

ShipNavigationRoute ShipNavigationSelectRoute(
    const ShipNavigationRouteInput *input)
{
    if (!input) return SHIP_NAVIGATION_APPROACH;
    if (input->interstellar) return SHIP_NAVIGATION_INTERSTELLAR_WARP;
    if (!isfinite(input->gap) || input->gap <= 0.0f ||
        !isfinite(input->approachSpeed) || input->approachSpeed <= 0.0f) {
        return SHIP_NAVIGATION_APPROACH;
    }

    float exitMargin = ShipNavigationSupercruiseExitMargin(
        input->safeDistance, input->approachSpeed);
    float supercruiseThreshold = exitMargin +
        input->approachSpeed * SHIP_NAVIGATION_APPROACH_LEAD_SECONDS;
    return input->gap > supercruiseThreshold
        ? SHIP_NAVIGATION_SUPERCRUISE
        : SHIP_NAVIGATION_APPROACH;
}
