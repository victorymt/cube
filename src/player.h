#ifndef VOXELCRAFT_PLAYER_H
#define VOXELCRAFT_PLAYER_H

#include "types.h"

typedef struct PlayerInput {
    float forward;
    float strafe;
    float vertical;
    bool sprint;
    bool jumpPressed;
    bool toggleFloating;
    Vector2 lookDelta;
} PlayerInput;

typedef struct PlayerWaterState {
    bool feetSubmerged;
    bool bodySubmerged;
    bool eyesSubmerged;
    float surfaceY;
    float eyeDepth;
    Vector3 flowVelocity;
} PlayerWaterState;

bool IsSolidBlockAt(int x, int y, int z);
bool PlayerOverlapsWorld(Vector3 position);
void MovePlayer(Player *player, Vector3 delta);
Vector3 ForwardFromAngles(float yaw, float pitch);
Vector3 FlatForward(float yaw);
Vector3 RightFromYaw(float yaw);
float CameraHeightFactor(float eyeHeight);
float CameraFovForHeight(float eyeHeight);
int EffectiveRenderDistanceForHeight(float eyeHeight);
bool PlayerCameraPositionInsideSolid(Vector3 position);
Vector3 PlayerResolveCameraPosition(Vector3 pivot, Vector3 desired);
void UpdatePlayerCamera(Camera3D *camera, const Player *player, float dt, bool thirdPerson);
void PlayerResetRuntimeState(Player *player);
PlayerWaterState PlayerWaterStateAt(Vector3 position);
PlayerInput PlayerInputFromKeyboard(void);
void UpdatePlayerWithInput(Player *player, float dt, const PlayerInput *input);
void UpdatePlayer(Player *player, float dt);

#endif
