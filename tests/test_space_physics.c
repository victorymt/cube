#include "space_physics.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

static void AssertNear(float actual, float expected, float tolerance)
{
    assert(fabsf(actual - expected) <= tolerance);
}

static void TestGravityAcceleration(void)
{
    SpacePhysicsGravityBody body = {
        .center = { 0.0f, 0.0f, 0.0f },
        .softeningRadiusGame = 2.0f,
        .gravitationalParameterGame = 100.0f,
        .encounterRadiusGame = 20.0f,
        .hierarchy = 1
    };
    Vector3 acceleration = SpacePhysicsGravityAcceleration(
        (Vector3){ 10.0f, 0.0f, 0.0f }, &body);
    AssertNear(acceleration.x, -1.0f, 0.0001f);
    AssertNear(acceleration.y, 0.0f, 0.0001f);

    acceleration = SpacePhysicsGravityAcceleration(
        (Vector3){ 1.0f, 0.0f, 0.0f }, &body);
    AssertNear(acceleration.x, -25.0f, 0.0001f);
}

static void TestPrimarySelection(void)
{
    SpacePhysicsGravityBody bodies[3] = {
        {
            .center = { 0.0f, 0.0f, 0.0f },
            .encounterRadiusGame = 100.0f,
            .hierarchy = 0
        },
        {
            .center = { 30.0f, 0.0f, 0.0f },
            .encounterRadiusGame = 20.0f,
            .hierarchy = 1
        },
        {
            .center = { 60.0f, 0.0f, 0.0f },
            .encounterRadiusGame = 12.0f,
            .hierarchy = 1
        }
    };
    assert(SpacePhysicsSelectPrimary((Vector3){ 31.0f, 0.0f, 0.0f },
                                     bodies, 3) == 1);
    assert(SpacePhysicsSelectPrimary((Vector3){ 5.0f, 0.0f, 0.0f },
                                     bodies, 3) == 0);
    assert(SpacePhysicsSelectPrimary((Vector3){ 200.0f, 0.0f, 0.0f },
                                     bodies, 3) == -1);
}

static void TestFlightAssistBraking(void)
{
    Vector3 velocity = { 3.0f, 4.0f, 0.0f };
    Vector3 braked = SpacePhysicsBrakeVelocity(velocity, 2.0f, 1.0f);
    AssertNear(braked.x, 1.8f, 0.0001f);
    AssertNear(braked.y, 2.4f, 0.0001f);

    braked = SpacePhysicsBrakeVelocity(velocity, 20.0f, 1.0f);
    AssertNear(braked.x, 0.0f, 0.0001f);
    AssertNear(braked.y, 0.0f, 0.0001f);
}

static void TestCircularOrbitIntegration(void)
{
    SpacePhysicsGravityBody body = {
        .center = { 0.0f, 0.0f, 0.0f },
        .softeningRadiusGame = 1.0f,
        .gravitationalParameterGame = 100.0f,
        .encounterRadiusGame = 100.0f,
        .hierarchy = 0
    };
    Vector3 position = { 10.0f, 0.0f, 0.0f };
    Vector3 velocity = { 0.0f, 0.0f, sqrtf(10.0f) };
    const float dt = 0.002f;

    for (int step = 0; step < 10000; step++) {
        Vector3 acceleration = SpacePhysicsGravityAcceleration(position, &body);
        velocity.x += acceleration.x * dt;
        velocity.y += acceleration.y * dt;
        velocity.z += acceleration.z * dt;
        position.x += velocity.x * dt;
        position.y += velocity.y * dt;
        position.z += velocity.z * dt;
    }

    float radius = sqrtf(position.x * position.x + position.y * position.y +
                         position.z * position.z);
    AssertNear(radius, 10.0f, 0.02f);
}

static void TestFiniteInputContract(void)
{
    SpacePhysicsGravityBody body = {
        .center = { 0.0f, 0.0f, 0.0f },
        .softeningRadiusGame = 1.0f,
        .gravitationalParameterGame = 100.0f,
        .encounterRadiusGame = 10.0f,
        .hierarchy = 0
    };
    assert(SpacePhysicsGravityAcceleration(
               (Vector3){ NAN, 0.0f, 0.0f }, &body).x == 0.0f);
    body.gravitationalParameterGame = INFINITY;
    assert(SpacePhysicsGravityAcceleration(
               (Vector3){ 2.0f, 0.0f, 0.0f }, &body).x == 0.0f);
    body = (SpacePhysicsGravityBody){
        .center = { 0.0f, 0.0f, 0.0f },
        .softeningRadiusGame = NAN,
        .gravitationalParameterGame = 100.0f,
        .encounterRadiusGame = 10.0f,
        .hierarchy = 0
    };
    assert(SpacePhysicsGravityAcceleration(
               (Vector3){ 2.0f, 0.0f, 0.0f }, &body).x == 0.0f);

    assert(SpacePhysicsSelectPrimary(
               (Vector3){ NAN, 0.0f, 0.0f }, &body, 1) == -1);
    body.center.x = NAN;
    assert(SpacePhysicsSelectPrimary(
               (Vector3){ 0.0f, 0.0f, 0.0f }, &body, 1) == -1);

    assert(SpacePhysicsBrakeVelocity(
               (Vector3){ NAN, 0.0f, 0.0f }, 1.0f, 1.0f).x == 0.0f);
    assert(SpacePhysicsBrakeVelocity(
               (Vector3){ 1.0f, 0.0f, 0.0f }, INFINITY, 1.0f).x == 1.0f);
    assert(SpacePhysicsBrakeVelocity(
               (Vector3){ FLT_MAX, FLT_MAX, FLT_MAX }, 1.0f, 1.0f).x == 0.0f);
}

int main(void)
{
    TestGravityAcceleration();
    TestPrimarySelection();
    TestFlightAssistBraking();
    TestCircularOrbitIntegration();
    TestFiniteInputContract();
    puts("space_physics tests passed");
    return 0;
}
