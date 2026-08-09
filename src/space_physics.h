#ifndef VOXELCRAFT_SPACE_PHYSICS_H
#define VOXELCRAFT_SPACE_PHYSICS_H

#include "types.h"

typedef struct SpacePhysicsGravityBody {
    Vector3 center;
    float radius;
    float gravitationalParameter;
    float sphereOfInfluence;
    int hierarchy;
} SpacePhysicsGravityBody;

float SpacePhysicsSphereOfInfluence(float orbitRadius, float bodyMass,
                                    float parentMass, float minimumRadius,
                                    float maximumRadius);
Vector3 SpacePhysicsGravityAcceleration(Vector3 position,
                                        const SpacePhysicsGravityBody *body);
int SpacePhysicsSelectPrimary(Vector3 position,
                              const SpacePhysicsGravityBody *bodies, int count);
Vector3 SpacePhysicsBrakeVelocity(Vector3 velocity, float deceleration, float dt);

#endif
