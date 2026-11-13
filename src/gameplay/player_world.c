#include "gameplay/player_world.h"

#include "gameplay/player_motion.h"
#include "gameplay/player_sensors.h"
#include "space/space_query.h"
#include "world/world_environment.h"

static bool WorldMove(void *user, ActorBody *body,
                      const ActorMotionRequest *request)
{
    (void)user;
    ActorMotionResult result;
    if (!WorldActorResolveMotion(body, PlayerMotionCollider(), request,
                                 &result)) return false;
    *body = result.body;
    return true;
}

static bool WorldOverlaps(void *user, Vector3 position, bool allowUnready)
{
    (void)user;
    return WorldActorOverlaps(PlayerMotionCollider(), position, allowUnready);
}

static PlayerWaterState WorldSampleWater(void *user, Vector3 position)
{
    (void)user;
    return PlayerWaterStateAt(position);
}

static bool WorldSampleGravity(void *user, Vector3 position,
                               PlayerGravitySample *out)
{
    (void)user;
    if (!out) return false;
    *out = (PlayerGravitySample){
        .inSpace = WorldIsSpaceActive(),
        .gravityScale = WorldGravityScale()
    };
    if (!out->inSpace) return true;

    out->hasPlanetGravity = PlanetSurfaceAt(
        position, &out->direction, &out->surfaceDistance,
        &out->surfaceGravityScale);
    return true;
}

PlayerWorldOps PlayerWorldOpsDefault(void)
{
    return (PlayerWorldOps){
        .move = WorldMove,
        .overlaps = WorldOverlaps,
        .sampleWater = WorldSampleWater,
        .sampleGravity = WorldSampleGravity
    };
}
