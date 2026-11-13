#include "gameplay/player_world.h"

#include "gameplay/player_motion.h"
#include "gameplay/player_sensors.h"

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

static PlayerWaterState WorldSampleWater(void *user, Vector3 position,
                                         double simulationTime)
{
    (void)user;
    (void)simulationTime;
    return WorldWaterSampleAt(position, EYE_HEIGHT, PLAYER_HEIGHT);
}

static bool WorldSampleGravity(void *user, Vector3 position,
                               double simulationTime,
                               PlayerGravitySample *out)
{
    (void)user;
    if (!out) return false;
    *out = WorldGravitySampleAt(position, simulationTime);
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
