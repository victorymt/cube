/* Verifies intent-driven ordinary ground locomotion independently of Player,
 * input polling, cube/legacy geometry, and the concrete world solver. */
#include "gameplay/actor_locomotion.h"

#include "raymath.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct FakeWorld {
    float minX;
    bool blockMinX;
    int moveCalls;
} FakeWorld;

static bool FakeMove(const void *user, ActorBody *body,
                     const ActorMotionRequest *request)
{
    FakeWorld *world = (FakeWorld *)user;
    if (!world || !body || !request) return false;
    world->moveCalls++;
    Vector3 next = Vector3Add(body->position, request->delta);
    if (world->blockMinX && next.x < world->minX) {
        next.x = world->minX;
        body->velocity.x = 0.0f;
    }
    body->position = next;
    return true;
}

static ActorLocomotionProfile TestProfile(void)
{
    return (ActorLocomotionProfile){
        .walkSpeed = 5.0f,
        .sprintSpeed = 8.0f,
        .jumpSpeed = 7.0f,
        .minGravityScale = 0.45f,
        .maxGravityScale = 1.75f,
        .maxFallSpeed = 35.0f
    };
}

static WorldEnvironmentSample NoGravity(void)
{
    return (WorldEnvironmentSample){
        .gravity = { .gravityScale = 1.0f }
    };
}

static WorldEnvironmentSample GroundGravity(void)
{
    return (WorldEnvironmentSample){
        .gravity = {
            .gravityScale = 1.0f,
            .acceleration = { 0.0f, -20.0f, 0.0f }
        }
    };
}

static ActorBody GroundBody(void)
{
    return (ActorBody){
        .position = { 0.0f, 1.0f, 0.0f },
        .onGround = true
    };
}

static void TestIdleAndGravity(void)
{
    FakeWorld fake = { 0 };
    PlayerWorldOps world = { .user = &fake, .move = FakeMove };
    ActorBody body = GroundBody();
    ActorLocomotionIntent intent = { 0 };
    ActorLocomotionResult result;
    WorldEnvironmentSample environment = GroundGravity();
    const ActorLocomotionProfile profile = TestProfile();

    assert(ActorGroundLocomotionStep(
        &body, &intent, &profile, &environment, &world, 0.1f, &result));
    assert(!result.jumped);
    assert(result.moved);
    assert(fabsf(result.body.velocity.x) < 1e-6f);
    assert(fabsf(result.body.velocity.z) < 1e-6f);
    assert(fabsf(result.body.velocity.y + 2.0f) < 1e-6f);
    assert(fabsf(result.body.position.y - 0.8f) < 1e-6f);
    assert(fake.moveCalls == 1);
}

static void TestWalkSprintStrafeAndDiagonal(void)
{
    FakeWorld fake = { 0 };
    PlayerWorldOps world = { .user = &fake, .move = FakeMove };
    const ActorLocomotionProfile profile = TestProfile();
    WorldEnvironmentSample environment = NoGravity();

    ActorBody body = GroundBody();
    ActorLocomotionIntent forward = { .forward = 1.0f };
    ActorLocomotionResult result;
    assert(ActorGroundLocomotionStep(
        &body, &forward, &profile, &environment, &world, 0.1f, &result));
    assert(fabsf(result.body.velocity.x) < 1e-6f);
    assert(fabsf(result.body.velocity.z - 5.0f) < 1e-6f);

    ActorLocomotionIntent sprint = { .forward = 1.0f, .sprint = true };
    assert(ActorGroundLocomotionStep(
        &body, &sprint, &profile, &environment, &world, 0.1f, &result));
    assert(fabsf(result.body.velocity.z - 8.0f) < 1e-6f);

    ActorLocomotionIntent strafe = { .strafe = 1.0f };
    assert(ActorGroundLocomotionStep(
        &body, &strafe, &profile, &environment, &world, 0.1f, &result));
    assert(fabsf(result.body.velocity.x + 5.0f) < 1e-6f);
    assert(fabsf(result.body.velocity.z) < 1e-6f);

    ActorLocomotionIntent diagonal = { .forward = 1.0f, .strafe = 1.0f };
    assert(ActorGroundLocomotionStep(
        &body, &diagonal, &profile, &environment, &world, 0.1f, &result));
    float speed = sqrtf(result.body.velocity.x * result.body.velocity.x +
                        result.body.velocity.z * result.body.velocity.z);
    assert(fabsf(speed - 5.0f) < 1e-5f);
    assert(fabsf(result.body.velocity.x + result.body.velocity.z) < 1e-5f);
}

static void TestJumpEdgeAndFallClamp(void)
{
    FakeWorld fake = { 0 };
    PlayerWorldOps world = { .user = &fake, .move = FakeMove };
    const ActorLocomotionProfile profile = TestProfile();
    WorldEnvironmentSample environment = GroundGravity();
    ActorBody body = GroundBody();
    ActorLocomotionIntent jump = { .jump = true };
    ActorLocomotionResult result;

    assert(ActorGroundLocomotionStep(
        &body, &jump, &profile, &environment, &world, 0.1f, &result));
    assert(result.jumped);
    assert(!result.body.onGround);
    assert(fabsf(result.body.velocity.y - 5.0f) < 1e-6f);

    ActorLocomotionIntent idle = { 0 };
    assert(ActorGroundLocomotionStep(
        &result.body, &idle, &profile, &environment, &world, 0.1f, &result));
    assert(!result.jumped);
    assert(fabsf(result.body.velocity.y - 3.0f) < 1e-6f);

    body = GroundBody();
    body.velocity.y = -100.0f;
    environment.gravity.acceleration.y = 0.0f;
    assert(ActorGroundLocomotionStep(
        &body, &idle, &profile, &environment, &world, 0.1f, &result));
    assert(fabsf(result.body.velocity.y + 35.0f) < 1e-6f);
}

static void TestBlockedMovementAndProfileVariation(void)
{
    FakeWorld fake = { .minX = -0.2f, .blockMinX = true };
    PlayerWorldOps world = { .user = &fake, .move = FakeMove };
    ActorLocomotionProfile profile = TestProfile();
    WorldEnvironmentSample environment = NoGravity();
    ActorBody body = GroundBody();
    ActorLocomotionIntent strafe = { .strafe = 1.0f };
    ActorLocomotionResult result;

    assert(ActorGroundLocomotionStep(
        &body, &strafe, &profile, &environment, &world, 0.1f, &result));
    assert(fabsf(result.body.position.x + 0.2f) < 1e-6f);
    assert(result.body.velocity.x == 0.0f);

    profile.walkSpeed = 2.0f;
    assert(ActorGroundLocomotionStep(
        &body, &strafe, &profile, &environment, &world, 0.1f, &result));
    assert(fabsf(result.body.velocity.x + 2.0f) < 1e-6f);
}

static void TestDeterminism(void)
{
    const ActorLocomotionProfile profile = TestProfile();
    WorldEnvironmentSample environment = GroundGravity();
    ActorLocomotionIntent intent = {
        .forward = 1.0f, .strafe = -0.5f, .jump = true
    };
    ActorBody firstBody = GroundBody();
    ActorBody secondBody = GroundBody();
    ActorLocomotionResult first;
    ActorLocomotionResult second;
    FakeWorld firstWorld = { 0 };
    FakeWorld secondWorld = { 0 };
    PlayerWorldOps firstOps = { .user = &firstWorld, .move = FakeMove };
    PlayerWorldOps secondOps = { .user = &secondWorld, .move = FakeMove };

    assert(ActorGroundLocomotionStep(
        &firstBody, &intent, &profile, &environment, &firstOps, 1.0f / 60.0f,
        &first));
    assert(ActorGroundLocomotionStep(
        &secondBody, &intent, &profile, &environment, &secondOps, 1.0f / 60.0f,
        &second));
    assert(first.body.position.x == second.body.position.x);
    assert(first.body.position.y == second.body.position.y);
    assert(first.body.position.z == second.body.position.z);
    assert(first.body.velocity.x == second.body.velocity.x);
    assert(first.body.velocity.y == second.body.velocity.y);
    assert(first.body.velocity.z == second.body.velocity.z);
    assert(first.body.yaw == second.body.yaw);
    assert(first.body.pitch == second.body.pitch);
    assert(first.body.onGround == second.body.onGround);
    assert(first.body.floating == second.body.floating);
    assert(first.moved == second.moved && first.jumped == second.jumped);
}

static void TestInvalidInputsAreRejected(void)
{
    FakeWorld fake = { 0 };
    PlayerWorldOps world = { .user = &fake, .move = FakeMove };
    ActorLocomotionIntent intent = { .forward = 1.0f };
    ActorBody body = GroundBody();
    ActorLocomotionProfile profile = TestProfile();
    WorldEnvironmentSample environment = NoGravity();
    ActorLocomotionResult result;

    assert(!ActorGroundLocomotionStep(
        NULL, &intent, &profile, &environment, &world, 0.1f, &result));
    assert(!ActorGroundLocomotionStep(
        &body, &intent, &profile, &environment, &world, 0.0f, &result));
    intent.forward = NAN;
    assert(!ActorGroundLocomotionStep(
        &body, &intent, &profile, &environment, &world, 0.1f, &result));
    intent.forward = 1.0f;
    body.floating = true;
    assert(!ActorGroundLocomotionStep(
        &body, &intent, &profile, &environment, &world, 0.1f, &result));
    body.floating = false;
    profile.walkSpeed = -1.0f;
    assert(!ActorGroundLocomotionStep(
        &body, &intent, &profile, &environment, &world, 0.1f, &result));
}

int main(void)
{
    TestIdleAndGravity();
    TestWalkSprintStrafeAndDiagonal();
    TestJumpEdgeAndFallClamp();
    TestBlockedMovementAndProfileVariation();
    TestDeterminism();
    TestInvalidInputsAreRejected();
    puts("actor locomotion tests passed");
    return 0;
}
