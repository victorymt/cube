#include "space_physics.h"

#include <float.h>
#include <math.h>

static float SpacePhysicsDistanceSquared(Vector3 a, Vector3 b)
{
    float x = a.x - b.x;
    float y = a.y - b.y;
    float z = a.z - b.z;
    return x * x + y * y + z * z;
}

Vector3 SpacePhysicsGravityAcceleration(Vector3 position,
                                        const SpacePhysicsGravityBody *body)
{
    if (!body || body->gravitationalParameterGame <= 0.0f) {
        return (Vector3){ 0 };
    }

    Vector3 delta = {
        body->center.x - position.x,
        body->center.y - position.y,
        body->center.z - position.z
    };
    float distanceSquared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    if (distanceSquared <= 0.000001f) return (Vector3){ 0 };

    float distance = sqrtf(distanceSquared);
    float minimumDistance = fmaxf(body->softeningRadiusGame, 0.001f);
    float effectiveDistanceSquared = fmaxf(distanceSquared,
                                            minimumDistance * minimumDistance);
    float magnitude = body->gravitationalParameterGame /
                      effectiveDistanceSquared;
    float scale = magnitude / distance;
    return (Vector3){ delta.x * scale, delta.y * scale, delta.z * scale };
}

int SpacePhysicsSelectPrimary(Vector3 position,
                              const SpacePhysicsGravityBody *bodies, int count)
{
    if (!bodies || count <= 0) return -1;

    int selected = -1;
    int selectedHierarchy = -1;
    float selectedFraction = FLT_MAX;
    for (int i = 0; i < count; i++) {
        float soi = bodies[i].encounterRadiusGame;
        if (soi <= 0.0f) continue;
        float distanceSquared = SpacePhysicsDistanceSquared(position, bodies[i].center);
        if (distanceSquared > soi * soi) continue;

        float fraction = distanceSquared / (soi * soi);
        if (bodies[i].hierarchy > selectedHierarchy ||
            (bodies[i].hierarchy == selectedHierarchy &&
             fraction < selectedFraction)) {
            selected = i;
            selectedHierarchy = bodies[i].hierarchy;
            selectedFraction = fraction;
        }
    }
    return selected;
}

Vector3 SpacePhysicsBrakeVelocity(Vector3 velocity, float deceleration, float dt)
{
    if (deceleration <= 0.0f || dt <= 0.0f) return velocity;

    float speedSquared = velocity.x * velocity.x + velocity.y * velocity.y +
                         velocity.z * velocity.z;
    if (speedSquared <= 0.000001f) return (Vector3){ 0 };

    float speed = sqrtf(speedSquared);
    float nextSpeed = fmaxf(0.0f, speed - deceleration * dt);
    float scale = nextSpeed / speed;
    return (Vector3){ velocity.x * scale, velocity.y * scale, velocity.z * scale };
}
