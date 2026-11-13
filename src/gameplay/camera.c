#include "gameplay/camera.h"

#include "core/config.h"
#include "gameplay/camera_distance.h"

#include "raymath.h"

#include <math.h>
#include <stddef.h>

#define CAMERA_COLLISION_MARGIN 0.025f
#define CAMERA_COLLISION_STEPS 24

static bool CameraVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static Vector3 CameraForwardFromAngles(float yaw, float pitch)
{
    return Vector3Normalize((Vector3){
        sinf(yaw) * cosf(pitch),
        sinf(pitch),
        cosf(yaw) * cosf(pitch)
    });
}

float CameraHeightFactor(float eyeHeight)
{
    float heightFactor = Clamp((eyeHeight - CAMERA_FOV_MIN_HEIGHT) /
                               (CAMERA_FOV_MAX_HEIGHT - CAMERA_FOV_MIN_HEIGHT),
                               0.0f, 1.0f);
    return heightFactor * heightFactor * (3.0f - 2.0f * heightFactor);
}

float CameraFovForHeight(float eyeHeight)
{
    return Lerp(CAMERA_MIN_FOV, CAMERA_MAX_FOV, CameraHeightFactor(eyeHeight));
}

CameraTargetHandle CameraPlayerTarget(void)
{
    return (CameraTargetHandle){ .kind = CAMERA_TARGET_PLAYER };
}

CameraTargetHandle CameraEntityTarget(int entityIndex, uint32_t spawnSerial)
{
    return (CameraTargetHandle){
        .kind = CAMERA_TARGET_ENTITY,
        .entityIndex = entityIndex,
        .entitySpawnSerial = spawnSerial
    };
}

bool CameraTargetEqual(CameraTargetHandle a, CameraTargetHandle b)
{
    if (a.kind != b.kind) return false;
    if (a.kind == CAMERA_TARGET_PLAYER) return true;
    return a.entityIndex == b.entityIndex &&
           a.entitySpawnSerial == b.entitySpawnSerial;
}

bool CameraTargetViewIsValid(const CameraTargetView *view)
{
    return view && view->valid &&
           CameraVectorIsFinite(view->position) &&
           isfinite(view->yaw) && isfinite(view->pitch);
}

void CameraTargetViewFromActor(const ActorBody *actor, CameraTargetView *out)
{
    if (!out) return;
    if (!actor) {
        CameraTargetViewInvalid(out);
        return;
    }
    *out = (CameraTargetView){
        .valid = true,
        .position = actor->position,
        .velocity = actor->velocity,
        .yaw = actor->yaw,
        .pitch = actor->pitch
    };
    if (!CameraTargetViewIsValid(out)) CameraTargetViewInvalid(out);
}

void CameraTargetViewInvalid(CameraTargetView *out)
{
    if (out) *out = (CameraTargetView){ 0 };
}

void CameraRigInitPlayer(CameraRig *rig)
{
    if (!rig) return;
    *rig = (CameraRig){
        .target = CameraPlayerTarget(),
        .config = {
            .thirdPerson = false,
            .useHeightFov = true,
            .fovDegrees = CAMERA_DEFAULT_ENTITY_FOV,
            .targetHeight = EYE_HEIGHT,
            .thirdPersonDistance = CAMERA_DEFAULT_ENTITY_DISTANCE
        },
        .camera = {
            .up = { 0.0f, 1.0f, 0.0f },
            .fovy = CameraFovForHeight(EYE_HEIGHT)
        }
    };
}

void CameraRigInitEntity(CameraRig *rig, int entityIndex, uint32_t spawnSerial)
{
    if (!rig) return;
    *rig = (CameraRig){
        .target = CameraEntityTarget(entityIndex, spawnSerial),
        .config = {
            .thirdPerson = true,
            .useHeightFov = false,
            .fovDegrees = CAMERA_DEFAULT_ENTITY_FOV,
            .targetHeight = CAMERA_DEFAULT_ENTITY_TARGET_HEIGHT,
            .thirdPersonDistance = CAMERA_DEFAULT_ENTITY_DISTANCE
        },
        .camera = {
            .up = { 0.0f, 1.0f, 0.0f },
            .fovy = CAMERA_DEFAULT_ENTITY_FOV
        }
    };
}

Vector3 CameraResolvePosition(const CameraWorldOps *world, Vector3 pivot,
                              Vector3 desired)
{
    if (!CameraVectorIsFinite(desired)) return pivot;
    if (!world || !world->pointInsideSolid ||
        !world->pointInsideSolid(world->user, desired)) return desired;

    if (CameraVectorIsFinite(pivot)) {
        Vector3 towardPivot = Vector3Subtract(pivot, desired);
        for (int step = 1; step <= CAMERA_COLLISION_STEPS; step++) {
            float amount = (float)step / (float)CAMERA_COLLISION_STEPS;
            Vector3 candidate = Vector3Add(
                desired, Vector3Scale(towardPivot, amount));
            if (!world->pointInsideSolid(world->user, candidate)) return candidate;
        }
    }

    int x = (int)floorf(desired.x);
    int y = (int)floorf(desired.y);
    int z = (int)floorf(desired.z);
    Vector3 candidates[6] = {
        { (float)x - CAMERA_COLLISION_MARGIN, desired.y, desired.z },
        { (float)x + 1.0f + CAMERA_COLLISION_MARGIN, desired.y, desired.z },
        { desired.x, (float)y - CAMERA_COLLISION_MARGIN, desired.z },
        { desired.x, (float)y + 1.0f + CAMERA_COLLISION_MARGIN, desired.z },
        { desired.x, desired.y, (float)z - CAMERA_COLLISION_MARGIN },
        { desired.x, desired.y, (float)z + 1.0f + CAMERA_COLLISION_MARGIN }
    };
    float bestDistance = INFINITY;
    Vector3 best = desired;
    for (unsigned index = 0; index < 6u; index++) {
        if (world->pointInsideSolid(world->user, candidates[index])) continue;
        float distance = Vector3DistanceSqr(desired, candidates[index]);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = candidates[index];
        }
    }
    return best;
}

bool CameraUpdateRig(CameraRig *rig, const CameraRuntimeContext *context,
                     const CameraWorldOps *world, float dt)
{
    if (!rig || !context) return false;
    if (!CameraTargetViewIsValid(&rig->targetView)) {
        rig->targetValid = false;
        return false;
    }
    rig->lastValidTarget = rig->targetView;
    rig->targetValid = true;

    float eyeHeight = rig->config.targetHeight;
    if (rig->target.kind == CAMERA_TARGET_PLAYER && context->inSpace)
        eyeHeight = 0.0f;
    Vector3 eye = Vector3Add(rig->targetView.position,
                             (Vector3){ 0.0f, eyeHeight, 0.0f });
    Vector3 look = CameraForwardFromAngles(rig->targetView.yaw,
                                           rig->targetView.pitch);
    float targetFov = rig->config.useHeightFov
        ? CameraFovForHeight(eye.y) : rig->config.fovDegrees;
    if (rig->target.kind == CAMERA_TARGET_PLAYER && context->shipDriving) {
        if (context->shipHighSpeed) targetFov += 24.0f;
        else if (context->shipCruising) targetFov += 12.0f;
    }

    Vector3 pivot = eye;
    if (rig->config.thirdPerson && rig->target.kind == CAMERA_TARGET_PLAYER &&
        context->shipDriving) {
        pivot.y = rig->targetView.position.y + 0.62f;
    }
    rig->camera.target = Vector3Add(pivot, look);

    if (rig->config.thirdPerson) {
        float distance = rig->config.thirdPersonDistance;
        if (rig->target.kind == CAMERA_TARGET_PLAYER && context->shipDriving)
            distance = 5.6f;
        float hitDistance = -1.0f;
        if (world && world->raycastOcclusion &&
            world->raycastOcclusion(world->user, pivot, Vector3Negate(look),
                                    distance, &hitDistance)) {
            if (hitDistance > 0.3f) distance = hitDistance - 0.25f;
            else if (hitDistance >= 0.0f) distance = 0.3f;
        }
        Vector3 desired = Vector3Add(
            pivot, Vector3Scale(Vector3Negate(look), distance));
        rig->camera.position = CameraResolvePosition(world, pivot, desired);
    } else {
        rig->camera.position = CameraResolvePosition(world, eye, eye);
    }
    float smoothing = isfinite(dt) && dt > 0.0f
        ? Clamp(dt * CAMERA_FOV_SMOOTHING, 0.0f, 1.0f) : 0.0f;
    rig->camera.fovy = Lerp(rig->camera.fovy, targetFov, smoothing);
    return true;
}

void CameraManagerInit(CameraManager *manager)
{
    if (!manager) return;
    *manager = (CameraManager){ 0 };
    manager->count = 1u;
    CameraRigInitPlayer(&manager->rigs[0]);
    manager->renderRig = 0u;
    manager->interactionRig = 0u;
}

int CameraManagerAdd(CameraManager *manager, const CameraViewConfig *config,
                     CameraTargetHandle target)
{
    if (!manager || manager->count >= CAMERA_MAX_RIGS ||
        (target.kind != CAMERA_TARGET_PLAYER &&
         target.kind != CAMERA_TARGET_ENTITY) ||
        (target.kind == CAMERA_TARGET_ENTITY && target.entityIndex < 0))
        return -1;
    unsigned slot = manager->count++;
    CameraRig *rig = &manager->rigs[slot];
    if (target.kind == CAMERA_TARGET_PLAYER)
        CameraRigInitPlayer(rig);
    else
        CameraRigInitEntity(rig, target.entityIndex, target.entitySpawnSerial);
    if (config) rig->config = *config;
    return (int)slot;
}

bool CameraManagerBind(CameraManager *manager, unsigned slot,
                       CameraTargetHandle target)
{
    if (!manager || slot >= manager->count ||
        (target.kind != CAMERA_TARGET_PLAYER &&
         target.kind != CAMERA_TARGET_ENTITY) ||
        (target.kind == CAMERA_TARGET_ENTITY && target.entityIndex < 0))
        return false;
    CameraRig *rig = &manager->rigs[slot];
    rig->target = target;
    rig->targetView = (CameraTargetView){ 0 };
    rig->lastValidTarget = (CameraTargetView){ 0 };
    rig->targetValid = false;
    return true;
}

bool CameraManagerRemove(CameraManager *manager, unsigned slot)
{
    if (!manager || slot == 0u || slot >= manager->count) return false;
    for (unsigned index = slot; index + 1u < manager->count; index++)
        manager->rigs[index] = manager->rigs[index + 1u];
    manager->count--;
    if (manager->renderRig == slot) manager->renderRig = 0u;
    else if (manager->renderRig > slot) manager->renderRig--;
    if (manager->interactionRig == slot) manager->interactionRig = 0u;
    else if (manager->interactionRig > slot) manager->interactionRig--;
    return true;
}

bool CameraManagerSetRenderRig(CameraManager *manager, unsigned slot)
{
    if (!manager || slot >= manager->count) return false;
    manager->renderRig = slot;
    return true;
}

bool CameraManagerSetInteractionRig(CameraManager *manager, unsigned slot)
{
    if (!manager || slot >= manager->count) return false;
    manager->interactionRig = slot;
    return true;
}

bool CameraManagerSetThirdPerson(CameraManager *manager, unsigned slot,
                                 bool thirdPerson)
{
    if (!manager || slot >= manager->count) return false;
    manager->rigs[slot].config.thirdPerson = thirdPerson;
    return true;
}

bool CameraManagerSetFov(CameraManager *manager, unsigned slot,
                         bool useHeightFov, float fovDegrees)
{
    if (!manager || slot >= manager->count ||
        (!useHeightFov && (!isfinite(fovDegrees) || fovDegrees <= 1.0f ||
                           fovDegrees >= 179.0f))) return false;
    manager->rigs[slot].config.useHeightFov = useHeightFov;
    if (!useHeightFov) manager->rigs[slot].config.fovDegrees = fovDegrees;
    return true;
}

void CameraManagerSetTargetView(CameraManager *manager, unsigned slot,
                                const CameraTargetView *view)
{
    if (!manager || slot >= manager->count || !view) return;
    manager->rigs[slot].targetView = CameraTargetViewIsValid(view)
        ? *view : (CameraTargetView){ 0 };
}

void CameraManagerUpdate(CameraManager *manager,
                         const CameraRuntimeContext *context,
                         const CameraWorldOps *world, float dt)
{
    if (!manager) return;
    for (unsigned index = 0; index < manager->count; index++)
        (void)CameraUpdateRig(&manager->rigs[index], context, world, dt);
}

Camera3D *CameraManagerRenderCamera(CameraManager *manager)
{
    return manager && manager->renderRig < manager->count
        ? &manager->rigs[manager->renderRig].camera : NULL;
}

const Camera3D *CameraManagerInteractionCamera(const CameraManager *manager)
{
    return manager && manager->interactionRig < manager->count
        ? &manager->rigs[manager->interactionRig].camera : NULL;
}

const CameraRig *CameraManagerRenderRig(const CameraManager *manager)
{
    return manager && manager->renderRig < manager->count
        ? &manager->rigs[manager->renderRig] : NULL;
}

const CameraRig *CameraManagerInteractionRig(const CameraManager *manager)
{
    return manager && manager->interactionRig < manager->count
        ? &manager->rigs[manager->interactionRig] : NULL;
}

int CameraManagerRenderDistance(const CameraManager *manager)
{
    const CameraRig *rig = CameraManagerRenderRig(manager);
    return rig ? CameraRenderDistanceForHeight(rig->camera.position.y) : 0;
}
