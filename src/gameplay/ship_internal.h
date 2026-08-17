#ifndef VOXELCRAFT_SHIP_INTERNAL_H
#define VOXELCRAFT_SHIP_INTERNAL_H

#include "gameplay/ship.h"
#include "space/space_types.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum NavigationTargetType {
    NAVIGATION_TARGET_NONE = 0,
    NAVIGATION_TARGET_PLANET,
    NAVIGATION_TARGET_SYSTEM
} NavigationTargetType;

typedef enum NavigationIntent {
    NAVIGATION_INTENT_CONTEXTUAL = 0,
    NAVIGATION_INTENT_INTERSTELLAR
} NavigationIntent;

typedef struct NavigationTarget {
    bool locked;
    NavigationTargetType type;
    int systemAnchorX;
    int systemAnchorZ;
    uint32_t bodyId;
    int planetIndex;
    char name[48];
} NavigationTarget;

typedef struct ShipOrbitState {
    bool active;
    Vector3 normal;
    Vector3 radial;
    float radius;
    float gravitationalParameter;
} ShipOrbitState;

typedef struct ShipRuntime {
    bool driving;
    ShipDriveMode driveMode;
    bool flightAssist;
    float fuel;
    NavigationTarget navigationTarget;
    ShipOrbitState orbitState;
    NavigationIntent navigationIntent;
    SpaceGravitySample gravityPrimary;
    float cruiseSetSpeed;
    float relativeSpeed;
    float targetSpeed;
    float targetClosingSpeed;
    float targetBrakingDistance;
    float targetEtaSeconds;
} ShipRuntime;

extern ShipRuntime shipRuntime;

void ShipRuntimeReset(void);

#endif
