#ifndef VOXELCRAFT_SHIP_NAVIGATION_H
#define VOXELCRAFT_SHIP_NAVIGATION_H

#include <stdbool.h>

typedef enum ShipNavigationRoute {
    SHIP_NAVIGATION_APPROACH = 0,
    SHIP_NAVIGATION_SUPERCRUISE,
    SHIP_NAVIGATION_INTERSTELLAR_WARP
} ShipNavigationRoute;

typedef struct ShipNavigationRouteInput {
    float gap;
    float safeDistance;
    float approachSpeed;
    bool interstellar;
} ShipNavigationRouteInput;

ShipNavigationRoute ShipNavigationSelectRoute(
    const ShipNavigationRouteInput *input);
float ShipNavigationSupercruiseExitMargin(float safeDistance,
                                          float approachSpeed);

#endif
