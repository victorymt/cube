/* Verifies unified ordinary Player mode selection, simulation, common
 * collision, post-move environment sampling, and deterministic results. */
#include "gameplay/player_motion_simulation.h"

#include "raymath.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

typedef struct FakeMotionWorld {
    WorldEnvironmentSample before;
    WorldEnvironmentSample after;
    int sampleCalls;
    int moveCalls;
    float minX;
    bool blockMinX;
} FakeMotionWorld;

static bool FakeSampleEnvironment(const void *user, Vector3 position,
                                  double simulationTime,
                                  WorldEnvironmentSample *out)
{
    FakeMotionWorld *world = (FakeMotionWorld *)user;
    (void)position;
    (void)simulationTime;
    if (!world || !out) return false;
    world->sampleCalls++;
    *out = world->sampleCalls == 1 ? world->before : world->after;
    return true;
}

static bool FakeMove(const void *user, ActorBody *body,
                     const ActorMotionRequest *request)
{
    FakeMotionWorld *world = (FakeMotionWorld *)user;
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

static PlayerWorldOps FakeOps(FakeMotionWorld *world)
{
    return (PlayerWorldOps){
        .user = world,
        .move = FakeMove,
        .sampleEnvironment = FakeSampleEnvironment
    };
}

static PlayerMotionProfile TestProfile(void)
{
    return (PlayerMotionProfile){
        .ground = {
            .walkSpeed = 5.0f,
            .sprintSpeed = 8.0f,
            .jumpSpeed = 7.0f,
            .minGravityScale = 0.45f,
            .maxGravityScale = 1.75f,
            .maxFallSpeed = 35.0f
        },
        .water = {
            .walkSpeed = 3.0f,
            .sprintSpeed = 4.2f,
            .verticalSpeed = 3.0f,
            .maxVerticalSpeed = 3.5f,
            .horizontalResponse = 6.0f,
            .verticalResponse = 5.0f
        },
        .space = {
            .verticalInputAcceleration = 12.0f,
            .verticalDamping = 2.0f,
            .minVerticalSpeed = -10.0f,
            .maxVerticalSpeed = 10.0f
        },
        .floatingVerticalSpeed = 5.5f
    };
}

static WorldEnvironmentSample NormalEnvironment(void)
{
    return (WorldEnvironmentSample){
        .gravity = {
            .gravityScale = 1.0f,
            .acceleration = { 0.0f, -20.0f, 0.0f }
        }
    };
}

static WorldEnvironmentSample WaterEnvironment(void)
{
    WorldEnvironmentSample environment = NormalEnvironment();
    environment.water.bodySubmerged = true;
    environment.water.eyesSubmerged = true;
    environment.water.feetSubmerged = true;
    environment.water.flowVelocity = (Vector3){ 2.0f, 0.5f, 0.0f };
    return environment;
}

static WorldEnvironmentSample SpaceEnvironment(void)
{
    return (WorldEnvironmentSample){
        .gravity = {
            .inSpace = true,
            .gravityScale = 1.0f,
            .acceleration = { 0.0f, -2.0f, 0.0f }
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

static void TestModePriority(void)
{
    ActorBody body = GroundBody();
    WorldEnvironmentSample environment = NormalEnvironment();
    PlayerMotionMode mode;
    assert(PlayerMotionModeSelect(&body, &environment, &mode));
    assert(mode == PLAYER_MOTION_GROUND);

    environment = WaterEnvironment();
    assert(PlayerMotionModeSelect(&body, &environment, &mode));
    assert(mode == PLAYER_MOTION_WATER);

    body.floating = true;
    assert(PlayerMotionModeSelect(&body, &environment, &mode));
    assert(mode == PLAYER_MOTION_FLOATING);

    environment.gravity.inSpace = true;
    assert(PlayerMotionModeSelect(&body, &environment, &mode));
    assert(mode == PLAYER_MOTION_SPACE);

    assert(!PlayerMotionModeSelect(NULL, &environment, &mode));
}

static void TestGroundUsesCommonPipeline(void)
{
    FakeMotionWorld fake = {
        .before = NormalEnvironment(),
        .after = NormalEnvironment()
    };
    PlayerWorldOps world = FakeOps(&fake);
    PlayerMotionProfile profile = TestProfile();
    ActorBody body = GroundBody();
    PlayerMotionIntent intent = { .forward = 1.0f };
    PlayerMotionResult result;

    assert(PlayerMotionStep(
        &body, &intent, &profile, &world, 10.0, 0.1f, &result));
    assert(result.mode == PLAYER_MOTION_GROUND);
    assert(fabsf(result.body.velocity.z - 5.0f) < 1e-6f);
    assert(fabsf(result.body.velocity.x) < 1e-6f);
    assert(result.moved);
    assert(fake.moveCalls == 1);
    assert(fake.sampleCalls == 2);
}

static void TestWaterFlowAndVerticalResponse(void)
{
    FakeMotionWorld fake = {
        .before = WaterEnvironment(),
        .after = WaterEnvironment()
    };
    PlayerWorldOps world = FakeOps(&fake);
    PlayerMotionProfile profile = TestProfile();
    ActorBody body = GroundBody();
    PlayerMotionIntent intent = { .forward = 1.0f };
    PlayerMotionResult result;

    assert(PlayerMotionStep(
        &body, &intent, &profile, &world, 11.0, 0.1f, &result));
    assert(result.mode == PLAYER_MOTION_WATER);
    assert(result.body.velocity.x > 0.0f);
    assert(result.body.velocity.z > 0.0f);
    assert(result.body.velocity.y > 0.0f);
    assert(fake.moveCalls == 1);
}

static void TestSpaceAndFloatingModes(void)
{
    FakeMotionWorld spaceFake = {
        .before = SpaceEnvironment(),
        .after = SpaceEnvironment()
    };
    PlayerWorldOps spaceWorld = FakeOps(&spaceFake);
    PlayerMotionProfile profile = TestProfile();
    ActorBody body = GroundBody();
    PlayerMotionIntent spaceIntent = {
        .forward = 1.0f, .vertical = 1.0f
    };
    PlayerMotionResult result;
    assert(PlayerMotionStep(
        &body, &spaceIntent, &profile, &spaceWorld, 12.0, 0.1f, &result));
    assert(result.mode == PLAYER_MOTION_SPACE);
    assert(fabsf(result.body.velocity.z - 5.0f) < 1e-6f);
    assert(fabsf(result.body.velocity.y - 1.04f) < 1e-5f);

    FakeMotionWorld floatFake = {
        .before = NormalEnvironment(),
        .after = NormalEnvironment()
    };
    PlayerWorldOps floatWorld = FakeOps(&floatFake);
    body = GroundBody();
    body.floating = true;
    PlayerMotionIntent floatIntent = {
        .forward = 1.0f, .vertical = 1.0f
    };
    assert(PlayerMotionStep(
        &body, &floatIntent, &profile, &floatWorld, 12.1, 0.1f, &result));
    assert(result.mode == PLAYER_MOTION_FLOATING);
    assert(fabsf(result.body.velocity.z - 5.0f) < 1e-6f);
    assert(fabsf(result.body.velocity.y - 5.5f) < 1e-6f);
}

static void TestToggleLookAndBlockedCollision(void)
{
    FakeMotionWorld fake = {
        .before = NormalEnvironment(),
        .after = NormalEnvironment(),
        .minX = -0.2f,
        .blockMinX = true
    };
    PlayerWorldOps world = FakeOps(&fake);
    PlayerMotionProfile profile = TestProfile();
    ActorBody body = GroundBody();
    PlayerMotionIntent intent = {
        .strafe = 1.0f,
        .toggleFloating = true,
        .lookDelta = { 1.0f, -1.0f }
    };
    PlayerMotionResult result;

    float beforeYaw = body.yaw;
    assert(PlayerMotionStep(
        &body, &intent, &profile, &world, 13.0, 0.1f, &result));
    assert(result.mode == PLAYER_MOTION_FLOATING);
    assert(result.body.floating);
    assert(result.body.position.x == -0.2f);
    assert(result.body.velocity.x == 0.0f);
    assert(result.body.yaw < beforeYaw);
    assert(result.body.pitch > 0.0f);
    assert(fake.moveCalls == 1);
}

static void TestDeterminismAndInvalidInputs(void)
{
    PlayerMotionProfile profile = TestProfile();
    PlayerMotionIntent intent = {
        .forward = 1.0f, .vertical = -0.5f, .sprint = true
    };
    ActorBody firstBody = GroundBody();
    ActorBody secondBody = GroundBody();
    FakeMotionWorld firstWorld = {
        .before = NormalEnvironment(), .after = NormalEnvironment()
    };
    FakeMotionWorld secondWorld = {
        .before = NormalEnvironment(), .after = NormalEnvironment()
    };
    PlayerWorldOps firstOps = FakeOps(&firstWorld);
    PlayerWorldOps secondOps = FakeOps(&secondWorld);
    PlayerMotionResult first;
    PlayerMotionResult second;
    assert(PlayerMotionStep(
        &firstBody, &intent, &profile, &firstOps, 14.0, 1.0f / 60.0f,
        &first));
    assert(PlayerMotionStep(
        &secondBody, &intent, &profile, &secondOps, 14.0, 1.0f / 60.0f,
        &second));
    assert(first.body.position.x == second.body.position.x);
    assert(first.body.position.y == second.body.position.y);
    assert(first.body.position.z == second.body.position.z);
    assert(first.body.velocity.x == second.body.velocity.x);
    assert(first.body.velocity.y == second.body.velocity.y);
    assert(first.body.velocity.z == second.body.velocity.z);
    assert(first.mode == second.mode && first.moved == second.moved &&
           first.jumped == second.jumped);

    assert(!PlayerMotionStep(
        NULL, &intent, &profile, &firstOps, 14.0, 0.1f, &first));
    assert(!PlayerMotionStep(
        &firstBody, &intent, &profile, &firstOps, 14.0, 0.0f, &first));
    intent.forward = NAN;
    assert(!PlayerMotionStep(
        &firstBody, &intent, &profile, &firstOps, 14.0, 0.1f, &first));
    intent.forward = 1.0f;
    profile.water.horizontalResponse = 0.0f;
    assert(!PlayerMotionStep(
        &firstBody, &intent, &profile, &firstOps, 14.0, 0.1f, &first));
}

int main(void)
{
    TestModePriority();
    TestGroundUsesCommonPipeline();
    TestWaterFlowAndVerticalResponse();
    TestSpaceAndFloatingModes();
    TestToggleLookAndBlockedCollision();
    TestDeterminismAndInvalidInputs();
    puts("player motion simulation tests passed");
    return 0;
}
