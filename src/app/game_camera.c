#include "app/game_internal.h"

#include "core/game_notice.h"
#include "ecology/entity.h"
#include "gameplay/camera.h"
#include "gameplay/player_actor.h"
#include "gameplay/ship.h"
#include "world/world_environment.h"

static void GameReadCameraTargetView(GameRuntime *game,
                                     CameraTargetHandle target,
                                     CameraTargetView *outView)
{
    if (target.kind == CAMERA_TARGET_PLAYER) {
        ActorBody actor = PlayerActorBody(&game->player);
        CameraTargetViewFromActor(&actor, outView);
        return;
    }

    const Entity *entities = EntitiesView();
    if (target.entityIndex < 0 || target.entityIndex >= MAX_ENTITIES ||
        !entities[target.entityIndex].active ||
        entities[target.entityIndex].spawnSerial != target.entitySpawnSerial) {
        CameraTargetViewInvalid(outView);
        return;
    }
    const Entity *entity = &entities[target.entityIndex];
    *outView = (CameraTargetView){
        .valid = true,
        .position = entity->position,
        .velocity = entity->velocity,
        .yaw = entity->yaw,
        .pitch = 0.0f
    };
}

void GameUpdateCamera(GameRuntime *game, float dt)
{
    CameraRuntimeContext context = {
        .inSpace = WorldIsSpaceActive(),
        .shipDriving = ShipIsDriving(),
        .shipHighSpeed = ShipIsHighSpeedTransit(),
        .shipCruising = ShipIsCruising()
    };
    for (unsigned slot = 0; slot < game->cameras.count; slot++) {
        CameraTargetView view;
        GameReadCameraTargetView(game, game->cameras.rigs[slot].target, &view);
        CameraManagerSetTargetView(&game->cameras, slot, &view);
    }

    const CameraRig *interactionRig =
        CameraManagerInteractionRig(&game->cameras);
    if (interactionRig && interactionRig->target.kind == CAMERA_TARGET_PLAYER) {
        CameraManagerSetThirdPerson(
            &game->cameras, game->cameras.interactionRig, game->thirdPerson);
    }

    CameraManagerUpdate(&game->cameras, &context, &game->cameraWorldOps, dt);
    Camera3D *camera = CameraManagerRenderCamera(&game->cameras);
    if (camera) game->camera = *camera;

    const CameraRig *renderRig = CameraManagerRenderRig(&game->cameras);
    if (renderRig && !renderRig->targetValid) {
        if (!game->cameraTargetLossNotified) {
            GameNoticePost("Camera target lost; holding the last view.");
            game->cameraTargetLossNotified = true;
        }
    } else {
        game->cameraTargetLossNotified = false;
    }
}
