#ifndef VOXELCRAFT_SPACE_PHYSICS_H
#define VOXELCRAFT_SPACE_PHYSICS_H

#include "types.h"

typedef struct SpacePhysicsGravityBody {
    Vector3 center; // Scene position in game distance units.
    // These fields belong to the gameplay encounter transform, not the
    // canonical physical body dimensions stored in Planet/StellarProfile.
    float softeningRadiusGame;
    float gravitationalParameterGame;
    float encounterRadiusGame;
    int hierarchy;
} SpacePhysicsGravityBody;

Vector3 SpacePhysicsGravityAcceleration(Vector3 position,
                                        const SpacePhysicsGravityBody *body);
int SpacePhysicsSelectPrimary(Vector3 position,
                              const SpacePhysicsGravityBody *bodies, int count);
Vector3 SpacePhysicsBrakeVelocity(Vector3 velocity, float deceleration, float dt);

#endif
