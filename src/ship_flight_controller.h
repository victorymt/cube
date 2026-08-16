#ifndef VOXELCRAFT_SHIP_FLIGHT_CONTROLLER_H
#define VOXELCRAFT_SHIP_FLIGHT_CONTROLLER_H

#include "raylib.h"

#include <stdbool.h>

typedef struct ShipFlightGuidanceInput {
    Vector3 position;
    Vector3 velocity;
    Vector3 targetPosition;
    Vector3 targetVelocity;
    float safeDistance;
    float arrivalTolerance;
    float maxSpeed;
    float acceleration;
    float deceleration;
    float dt;
} ShipFlightGuidanceInput;

typedef struct ShipFlightGuidance {
    Vector3 velocity;
    Vector3 direction;
    float distance;
    float gap;
    float relativeSpeed;
    float closingSpeed;
    float desiredSpeed;
    float brakingDistance;
    float etaSeconds;
    bool arrived;
} ShipFlightGuidance;

Vector3 ShipFlightApproachVelocity(Vector3 velocity, Vector3 desiredVelocity,
                                   float acceleration, float deceleration,
                                   float dt);
Vector3 ShipFlightClampRelativeVelocity(Vector3 velocity,
                                        Vector3 referenceVelocity,
                                        float maxSpeed);
bool ShipFlightGuideToTarget(const ShipFlightGuidanceInput *input,
                             ShipFlightGuidance *out);

#endif
