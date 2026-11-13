/* Camera rig behavior is tested against a fake world query backend. The test
 * intentionally links no Player, Ship, interaction, or terrain implementation;
 * camera pose generation and multi-rig routing must stay independent of those
 * runtime owners. */
#include "gameplay/camera.h"
#include "gameplay/camera_distance.h"
#include "core/config.h"

#include "raymath.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

static int testRenderDistance = 5;

int ChunksRenderDistance(void)
{
    return testRenderDistance;
}

typedef struct FakeCameraWorld {
    bool blockPositiveX;
    float rayHitDistance;
    bool hasRayHit;
    int pointQueries;
    int rayQueries;
    Vector3 lastRayDirection;
} FakeCameraWorld;

static bool FakePointInsideSolid(void *user, Vector3 point)
{
    FakeCameraWorld *world = user;
    world->pointQueries++;
    return world->blockPositiveX && point.x > 1.5f;
}

static bool FakeRaycastOcclusion(void *user, Vector3 origin, Vector3 direction,
                                 float maxDistance, float *outDistance)
{
    FakeCameraWorld *world = user;
    (void)origin;
    (void)maxDistance;
    world->rayQueries++;
    world->lastRayDirection = direction;
    if (!world->hasRayHit) return false;
    *outDistance = world->rayHitDistance;
    return true;
}

static CameraTargetView TargetView(Vector3 position, float yaw, float pitch)
{
    return (CameraTargetView){
        .valid = true,
        .position = position,
        .yaw = yaw,
        .pitch = pitch
    };
}

static void TestTargetViewValidation(void)
{
    ActorBody actor = {
        .position = { 1.0f, 2.0f, 3.0f },
        .velocity = { 0.25f, 0.0f, -0.25f },
        .yaw = 0.5f,
        .pitch = -0.25f
    };
    CameraTargetView view;
    CameraTargetViewFromActor(&actor, &view);
    assert(CameraTargetViewIsValid(&view));
    assert(Vector3Distance(view.position, actor.position) == 0.0f);
    assert(view.yaw == actor.yaw && view.pitch == actor.pitch);

    actor.position.x = NAN;
    CameraTargetViewFromActor(&actor, &view);
    assert(!CameraTargetViewIsValid(&view));
    assert(!CameraTargetViewIsValid(NULL));
}

static void TestFirstPersonCameraUsesActorPose(void)
{
    CameraRig rig;
    CameraRigInitPlayer(&rig);
    CameraTargetView view = TargetView(
        (Vector3){ 4.0f, 6.0f, 8.0f }, 0.0f, 0.0f);
    rig.targetView = view;
    CameraRuntimeContext context = { 0 };
    FakeCameraWorld world = { 0 };
    CameraWorldOps ops = {
        .user = &world,
        .pointInsideSolid = FakePointInsideSolid,
        .raycastOcclusion = FakeRaycastOcclusion
    };

    assert(CameraUpdateRig(&rig, &context, &ops, 1.0f));
    Vector3 expectedEye = { 4.0f, 6.0f + EYE_HEIGHT, 8.0f };
    assert(Vector3Distance(rig.camera.position, expectedEye) < 1e-5f);
    assert(Vector3Distance(rig.camera.target,
                           Vector3Add(expectedEye, (Vector3){ 0, 0, 1 })) <
           1e-5f);
    assert(rig.camera.up.y == 1.0f);
    assert(fabsf(rig.camera.fovy - CameraFovForHeight(expectedEye.y)) < 1e-5f);
    assert(world.rayQueries == 0);
}

static void TestThirdPersonCameraUsesOcclusionPort(void)
{
    CameraRig rig;
    CameraRigInitPlayer(&rig);
    rig.config.thirdPerson = true;
    rig.targetView = TargetView((Vector3){ 0.0f, 10.0f, 0.0f }, 0.0f, 0.0f);
    CameraRuntimeContext context = { 0 };
    FakeCameraWorld world = {
        .rayHitDistance = 2.4f,
        .hasRayHit = true
    };
    CameraWorldOps ops = {
        .user = &world,
        .pointInsideSolid = FakePointInsideSolid,
        .raycastOcclusion = FakeRaycastOcclusion
    };

    assert(CameraUpdateRig(&rig, &context, &ops, 0.0f));
    assert(world.rayQueries == 1);
    assert(Vector3Distance(world.lastRayDirection, (Vector3){ 0, 0, -1 }) <
           1e-5f);
    float expectedZ = -(2.4f - 0.25f);
    assert(fabsf(rig.camera.position.z - expectedZ) < 1e-5f);
    assert(fabsf(rig.camera.position.y - (10.0f + EYE_HEIGHT)) < 1e-5f);
}

static void TestCameraCollisionAndInvalidDesired(void)
{
    FakeCameraWorld world = { .blockPositiveX = true };
    CameraWorldOps ops = {
        .user = &world,
        .pointInsideSolid = FakePointInsideSolid
    };
    Vector3 pivot = { 0.5f, 2.0f, 0.5f };
    Vector3 resolved = CameraResolvePosition(
        &ops, pivot, (Vector3){ 2.5f, 2.0f, 0.5f });
    assert(resolved.x <= 1.5f + 1e-5f);
    assert(world.pointQueries >= 2);

    Vector3 invalid = { NAN, 2.0f, 0.5f };
    resolved = CameraResolvePosition(&ops, pivot, invalid);
    assert(Vector3Distance(resolved, pivot) == 0.0f);
    assert(Vector3Distance(
        CameraResolvePosition(NULL, pivot, (Vector3){ 9, 8, 7 }),
        (Vector3){ 9, 8, 7 }) == 0.0f);
}

static void TestInvalidTargetHoldsLastPose(void)
{
    CameraRig rig;
    CameraRigInitPlayer(&rig);
    rig.targetView = TargetView((Vector3){ 1.0f, 2.0f, 3.0f }, 0.0f, 0.0f);
    CameraRuntimeContext context = { 0 };
    assert(CameraUpdateRig(&rig, &context, NULL, 1.0f));
    Camera3D last = rig.camera;

    CameraTargetViewInvalid(&rig.targetView);
    assert(!CameraUpdateRig(&rig, &context, NULL, 1.0f));
    assert(!rig.targetValid);
    assert(Vector3Distance(rig.camera.position, last.position) == 0.0f);
    assert(Vector3Distance(rig.camera.target, last.target) == 0.0f);
    assert(rig.camera.fovy == last.fovy);
}

static void TestManagerRoutesIndependentRigs(void)
{
    CameraManager manager;
    CameraManagerInit(&manager);
    CameraViewConfig entityConfig = {
        .thirdPerson = false,
        .useHeightFov = false,
        .fovDegrees = 65.0f,
        .targetHeight = 0.75f,
        .thirdPersonDistance = 4.0f
    };
    int entitySlot = CameraManagerAdd(&manager, &entityConfig,
                                      CameraEntityTarget(7, 42u));
    assert(entitySlot == 1);
    assert(CameraManagerSetRenderRig(&manager, (unsigned)entitySlot));
    assert(CameraManagerSetInteractionRig(&manager, 0u));

    CameraTargetView playerView = TargetView(
        (Vector3){ 1.0f, 2.0f, 3.0f }, 0.0f, 0.0f);
    CameraTargetView entityView = TargetView(
        (Vector3){ 15.0f, 4.0f, 20.0f }, 1.0f, 0.2f);
    CameraManagerSetTargetView(&manager, 0u, &playerView);
    CameraManagerSetTargetView(&manager, 1u, &entityView);
    CameraRuntimeContext context = { 0 };
    CameraManagerUpdate(&manager, &context, NULL, 1.0f);

    const CameraRig *playerRig = &manager.rigs[0];
    const CameraRig *entityRig = &manager.rigs[1];
    assert(Vector3Distance(playerRig->camera.position,
                           (Vector3){ 1.0f, 2.0f + EYE_HEIGHT, 3.0f }) < 1e-5f);
    assert(fabsf(entityRig->camera.fovy - 65.0f) < 1e-5f);
    assert(CameraManagerRenderCamera(&manager) == &entityRig->camera);
    assert(CameraManagerInteractionCamera(&manager) == &playerRig->camera);
    assert(CameraManagerRenderRig(&manager) == entityRig);
    assert(CameraManagerInteractionRig(&manager) == playerRig);

    assert(CameraManagerBind(&manager, 1u, CameraEntityTarget(8, 99u)));
    assert(!manager.rigs[1].targetView.valid);
    assert(manager.rigs[1].target.entityIndex == 8 &&
           manager.rigs[1].target.entitySpawnSerial == 99u);
    assert(!CameraManagerBind(&manager, 1u, CameraEntityTarget(-1, 1u)));

    assert(CameraManagerRemove(&manager, 1u));
    assert(manager.count == 1u);
    assert(manager.renderRig == 0u);
    assert(manager.interactionRig == 0u);
    assert(!CameraManagerSetRenderRig(&manager, 2u));
    assert(CameraManagerAdd(&manager, NULL, CameraEntityTarget(-1, 1u)) < 0);
}

static void TestTargetHandleIdentityIncludesSpawnSerial(void)
{
    CameraTargetHandle first = CameraEntityTarget(3, 10u);
    CameraTargetHandle same = CameraEntityTarget(3, 10u);
    CameraTargetHandle respawned = CameraEntityTarget(3, 11u);
    assert(CameraTargetEqual(first, same));
    assert(!CameraTargetEqual(first, respawned));
    assert(!CameraTargetEqual(CameraPlayerTarget(), first));
    assert(CameraTargetEqual(CameraPlayerTarget(), CameraPlayerTarget()));
}

static void TestDistancePoliciesAreSeparateEntryPoints(void)
{
    testRenderDistance = 5;
    assert(CameraRenderDistanceForHeight(-100.0f) == 5);
    assert(CameraStreamingDistanceForHeight(-100.0f) == 5);
    assert(CameraRenderDistanceForHeight(100.0f) == 7);
    assert(CameraStreamingDistanceForHeight(100.0f) == 7);

    testRenderDistance = MAX_RENDER_DISTANCE_CHUNKS;
    assert(CameraRenderDistanceForHeight(100.0f) ==
           MAX_RENDER_DISTANCE_CHUNKS);
}

int main(void)
{
    TestTargetViewValidation();
    TestFirstPersonCameraUsesActorPose();
    TestThirdPersonCameraUsesOcclusionPort();
    TestCameraCollisionAndInvalidDesired();
    TestInvalidTargetHoldsLastPose();
    TestManagerRoutesIndependentRigs();
    TestTargetHandleIdentityIncludesSpawnSerial();
    TestDistancePoliciesAreSeparateEntryPoints();
    return 0;
}
