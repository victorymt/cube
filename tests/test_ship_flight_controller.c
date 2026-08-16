#include "gameplay/ship_flight_controller.h"

#include "raymath.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static bool Near(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

static void TestRelativeSpeedClamp(void)
{
    Vector3 clamped = ShipFlightClampRelativeVelocity(
        (Vector3){ 13.0f, 4.0f, 0.0f },
        (Vector3){ 3.0f, 4.0f, 0.0f }, 8.0f);
    assert(Near(clamped.x, 11.0f, 0.0001f));
    assert(Near(clamped.y, 4.0f, 0.0001f));
}

static void TestApproachIsFrameRateIndependent(void)
{
    Vector3 desired = { 40.0f, 0.0f, 0.0f };
    Vector3 coarse = Vector3Zero();
    Vector3 fine = Vector3Zero();
    for (int i = 0; i < 10; i++) {
        coarse = ShipFlightApproachVelocity(coarse, desired, 6.0f, 10.0f,
                                            0.1f);
    }
    for (int i = 0; i < 100; i++) {
        fine = ShipFlightApproachVelocity(fine, desired, 6.0f, 10.0f,
                                          0.01f);
    }
    assert(Near(coarse.x, 6.0f, 0.0001f));
    assert(Near(fine.x, coarse.x, 0.0001f));
}

static void TestMovingTargetGuidanceAndBraking(void)
{
    ShipFlightGuidance guidance;
    ShipFlightGuidanceInput input = {
        .position = { 0.0f, 0.0f, 0.0f },
        .velocity = { 0.0f, 0.0f, 0.0f },
        .targetPosition = { 1000.0f, 0.0f, 0.0f },
        .targetVelocity = { 0.0f, 0.0f, 17.0f },
        .safeDistance = 1.0f,
        .arrivalTolerance = 0.05f,
        .maxSpeed = 40.0f,
        .acceleration = 6.0f,
        .deceleration = 10.0f,
        .dt = 1.0f
    };
    assert(ShipFlightGuideToTarget(&input, &guidance));
    assert(!guidance.arrived);
    assert(Near(guidance.velocity.z, 17.0f * 6.0f /
                                    sqrtf(40.0f * 40.0f + 17.0f * 17.0f),
                0.0001f));
    assert(guidance.desiredSpeed == 40.0f);

    input.position.x = 998.80f;
    input.velocity = (Vector3){ 5.0f, 0.0f, 17.0f };
    input.dt = 0.1f;
    assert(ShipFlightGuideToTarget(&input, &guidance));
    assert(!guidance.arrived);
    assert(guidance.desiredSpeed < 5.0f);
    assert(guidance.velocity.x < 5.0f);

    input.position.x = 998.97f;
    input.velocity = (Vector3){ 0.02f, 0.0f, 17.0f };
    assert(ShipFlightGuideToTarget(&input, &guidance));
    assert(guidance.arrived);
    assert(Near(guidance.velocity.z, 17.0f, 0.0001f));
}

static void TestInvalidInputs(void)
{
    ShipFlightGuidance guidance;
    ShipFlightGuidanceInput input = {
        .maxSpeed = 40.0f,
        .acceleration = 6.0f,
        .deceleration = 10.0f,
        .dt = 0.1f
    };
    assert(!ShipFlightGuideToTarget(NULL, &guidance));
    assert(!ShipFlightGuideToTarget(&input, NULL));
    input.dt = NAN;
    assert(!ShipFlightGuideToTarget(&input, &guidance));
}

static void TestCircularOrbitTracksMovingCenter(void)
{
    ShipCircularOrbitInput input = {
        .center = { 100.0f, 20.0f, -30.0f },
        .centerVelocity = { 4.0f, 0.0f, -2.0f },
        .position = { 108.0f, 20.0f, -30.0f },
        .normal = { 0.0f, 1.0f, 0.0f },
        .gravitationalParameter = 32.0f,
        .radius = 8.0f,
        .dt = 0.25f
    };
    ShipCircularOrbitState orbit;
    assert(ShipFlightStepCircularOrbit(&input, &orbit));
    assert(Near(Vector3Distance(orbit.position, input.center), 8.0f,
                0.0001f));
    assert(Near(orbit.speed, 2.0f, 0.0001f));
    assert(Near(Vector3Length(Vector3Subtract(orbit.velocity,
                                              input.centerVelocity)),
                2.0f, 0.0001f));
    assert(Near(Vector3DotProduct(orbit.radial, orbit.tangent), 0.0f,
                0.0001f));
}

static void TestCircularOrbitHasNoLongTermRadiusDrift(void)
{
    const float radius = 0.34f;
    const float mu = 0.88f;
    Vector3 center = { 1000.0f, 0.0f, 0.0f };
    Vector3 position = Vector3Add(center, (Vector3){ radius, 0.0f, 0.0f });
    Vector3 radial = { 1.0f, 0.0f, 0.0f };
    ShipCircularOrbitState orbit = { 0 };
    for (int frame = 0; frame < 60 * 30; frame++) {
        assert(ShipFlightStepCircularOrbit(&(ShipCircularOrbitInput){
            .center = center,
            .centerVelocity = { 0.0f, 0.0f, 17.0f },
            .position = Vector3Add(center, Vector3Scale(radial, radius)),
            .normal = { 0.0f, 1.0f, 0.0f },
            .gravitationalParameter = mu,
            .radius = radius,
            .dt = 1.0f / 60.0f
        }, &orbit));
        position = orbit.position;
        radial = orbit.radial;
    }
    assert(Near(Vector3Distance(position, center), radius, 0.0001f));
}

static void TestCircularOrbitRejectsInvalidInputs(void)
{
    ShipCircularOrbitState orbit;
    ShipCircularOrbitInput input = {
        .position = { 1.0f, 0.0f, 0.0f },
        .normal = { 0.0f, 1.0f, 0.0f },
        .gravitationalParameter = 1.0f,
        .radius = 1.0f,
        .dt = 0.1f
    };
    assert(!ShipFlightStepCircularOrbit(NULL, &orbit));
    assert(!ShipFlightStepCircularOrbit(&input, NULL));
    input.normal = input.position;
    assert(!ShipFlightStepCircularOrbit(&input, &orbit));
    input.normal = (Vector3){ 0.0f, 1.0f, 0.0f };
    input.gravitationalParameter = NAN;
    assert(!ShipFlightStepCircularOrbit(&input, &orbit));
}

static void TestTypicalInterplanetaryPacing(void)
{
    const float dt = 1.0f / 60.0f;
    ShipFlightGuidanceInput input = {
        .position = { 0.0f, 0.0f, 0.0f },
        .velocity = { 0.0f, 0.0f, 0.0f },
        .targetPosition = { 1800.0f, 0.0f, 0.0f },
        .targetVelocity = { 0.0f, 0.0f, 17.0f },
        .safeDistance = 1.0f,
        .arrivalTolerance = 0.05f,
        .maxSpeed = 40.0f,
        .acceleration = 6.0f,
        .deceleration = 10.0f,
        .dt = dt
    };
    ShipFlightGuidance guidance = { 0 };
    float elapsed = 0.0f;
    for (int frame = 0; frame < 60 * 90; frame++) {
        assert(ShipFlightGuideToTarget(&input, &guidance));
        if (guidance.arrived) break;
        input.velocity = guidance.velocity;
        input.position = Vector3Add(input.position,
                                    Vector3Scale(input.velocity, dt));
        input.targetPosition = Vector3Add(
            input.targetPosition, Vector3Scale(input.targetVelocity, dt));
        elapsed += dt;
    }
    assert(guidance.arrived);
    assert(elapsed >= 30.0f && elapsed <= 60.0f);
    assert(Vector3Distance(guidance.velocity, input.targetVelocity) < 0.0001f);
}

int main(void)
{
    TestRelativeSpeedClamp();
    TestApproachIsFrameRateIndependent();
    TestMovingTargetGuidanceAndBraking();
    TestInvalidInputs();
    TestCircularOrbitTracksMovingCenter();
    TestCircularOrbitHasNoLongTermRadiusDrift();
    TestCircularOrbitRejectsInvalidInputs();
    TestTypicalInterplanetaryPacing();
    puts("ship flight controller tests passed");
    return 0;
}
