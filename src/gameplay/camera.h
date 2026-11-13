#ifndef VOXELCRAFT_CAMERA_H
#define VOXELCRAFT_CAMERA_H

#include "world/actor_body.h"

#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

#define CAMERA_MAX_RIGS 8u
#define CAMERA_DEFAULT_ENTITY_TARGET_HEIGHT 0.75f
#define CAMERA_DEFAULT_ENTITY_DISTANCE 3.5f
#define CAMERA_DEFAULT_ENTITY_FOV 70.0f

typedef enum CameraTargetKind {
    CAMERA_TARGET_PLAYER = 0,
    CAMERA_TARGET_ENTITY
} CameraTargetKind;

typedef struct CameraTargetHandle {
    CameraTargetKind kind;
    int entityIndex;
    uint32_t entitySpawnSerial;
} CameraTargetHandle;

typedef struct CameraTargetView {
    bool valid;
    Vector3 position;
    Vector3 velocity;
    float yaw;
    float pitch;
} CameraTargetView;

typedef struct CameraViewConfig {
    bool thirdPerson;
    bool useHeightFov;
    float fovDegrees;
    float targetHeight;
    float thirdPersonDistance;
} CameraViewConfig;

typedef struct CameraRuntimeContext {
    bool inSpace;
    bool shipDriving;
    bool shipHighSpeed;
    bool shipCruising;
} CameraRuntimeContext;

typedef struct CameraWorldOps {
    void *user;
    bool (*pointInsideSolid)(void *user, Vector3 point);
    bool (*raycastOcclusion)(void *user, Vector3 origin, Vector3 direction,
                             float maxDistance, float *outDistance);
} CameraWorldOps;

typedef struct CameraRig {
    CameraTargetHandle target;
    CameraTargetView targetView;
    CameraTargetView lastValidTarget;
    CameraViewConfig config;
    Camera3D camera;
    bool targetValid;
} CameraRig;

typedef struct CameraManager {
    CameraRig rigs[CAMERA_MAX_RIGS];
    unsigned count;
    unsigned renderRig;
    unsigned interactionRig;
} CameraManager;

float CameraHeightFactor(float eyeHeight);
float CameraFovForHeight(float eyeHeight);

CameraTargetHandle CameraPlayerTarget(void);
CameraTargetHandle CameraEntityTarget(int entityIndex, uint32_t spawnSerial);
bool CameraTargetEqual(CameraTargetHandle a, CameraTargetHandle b);
bool CameraTargetViewIsValid(const CameraTargetView *view);
void CameraTargetViewFromActor(const ActorBody *actor, CameraTargetView *out);
void CameraTargetViewInvalid(CameraTargetView *out);

void CameraRigInitPlayer(CameraRig *rig);
void CameraRigInitEntity(CameraRig *rig, int entityIndex, uint32_t spawnSerial);
bool CameraUpdateRig(CameraRig *rig, const CameraRuntimeContext *context,
                     const CameraWorldOps *world, float dt);
Vector3 CameraResolvePosition(const CameraWorldOps *world, Vector3 pivot,
                              Vector3 desired);

void CameraManagerInit(CameraManager *manager);
int CameraManagerAdd(CameraManager *manager, const CameraViewConfig *config,
                     CameraTargetHandle target);
bool CameraManagerBind(CameraManager *manager, unsigned slot,
                       CameraTargetHandle target);
bool CameraManagerRemove(CameraManager *manager, unsigned slot);
bool CameraManagerSetRenderRig(CameraManager *manager, unsigned slot);
bool CameraManagerSetInteractionRig(CameraManager *manager, unsigned slot);
bool CameraManagerSetThirdPerson(CameraManager *manager, unsigned slot,
                                 bool thirdPerson);
bool CameraManagerSetFov(CameraManager *manager, unsigned slot,
                         bool useHeightFov, float fovDegrees);
void CameraManagerSetTargetView(CameraManager *manager, unsigned slot,
                                const CameraTargetView *view);
void CameraManagerUpdate(CameraManager *manager,
                         const CameraRuntimeContext *context,
                         const CameraWorldOps *world, float dt);
Camera3D *CameraManagerRenderCamera(CameraManager *manager);
const Camera3D *CameraManagerInteractionCamera(const CameraManager *manager);
const CameraRig *CameraManagerRenderRig(const CameraManager *manager);
const CameraRig *CameraManagerInteractionRig(const CameraManager *manager);
int CameraManagerRenderDistance(const CameraManager *manager);

#endif
