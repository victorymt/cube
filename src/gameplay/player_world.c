#include "gameplay/player_world.h"

#include "core/config.h"
#include "gameplay/player_motion.h"
#include "world/world_environment.h"

#include <stddef.h>

static bool WorldMove(const void *user, ActorBody *body,
                      const ActorMotionRequest *request)
{
    (void)user;
    ActorMotionResult result;
    if (!WorldActorResolveMotion(body, PlayerMotionCollider(), request,
                                 &result)) return false;
    *body = result.body;
    return true;
}

static bool WorldOverlaps(const void *user, Vector3 position, bool allowUnready)
{
    (void)user;
    return WorldActorOverlaps(PlayerMotionCollider(), position, allowUnready);
}

static bool WorldSampleEnvironment(const void *user, Vector3 position,
                                   double simulationTime,
                                   WorldEnvironmentSample *out)
{
    if (!out) return false;
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    /* The current world has no independent sub-region identity. Zero keeps
     * global/body rules active while reserving region rules for a later
     * world-definition input rather than inventing a coordinate convention. */
    *out = WorldEnvironmentSampleAtWithRules(
        position, EYE_HEIGHT, PLAYER_HEIGHT, simulationTime,
        context.bodyId, 0u, (const WorldRuleSet *)user);
    return true;
}

PlayerWorldOps PlayerWorldOpsWithRules(const WorldRuleSet *rules)
{
    return (PlayerWorldOps){
        .user = rules,
        .move = WorldMove,
        .overlaps = WorldOverlaps,
        .sampleEnvironment = WorldSampleEnvironment
    };
}

PlayerWorldOps PlayerWorldOpsDefault(void)
{
    return PlayerWorldOpsWithRules(NULL);
}
