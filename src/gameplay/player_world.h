#ifndef VOXELCRAFT_PLAYER_WORLD_H
#define VOXELCRAFT_PLAYER_WORLD_H

#include "world/actor_motion.h"
#include "world/environment_query.h"

#include "raylib.h"

#include <stdbool.h>

typedef WorldGravitySample PlayerGravitySample;

/* The world-facing half of player motion. Implementations may wrap either the
 * cube or legacy surface backend, but this contract exposes neither topology
 * nor chunk identity to player policy. */
typedef struct PlayerWorldOps {
    const void *user;
    bool (*move)(const void *user, ActorBody *body,
                 const ActorMotionRequest *request);
    bool (*overlaps)(const void *user, Vector3 position, bool allowUnready);
    /* One combined sample keeps water and gravity in the same rule/world
     * context. The default adapter binds the active body id and optional
     * runtime rule set; Player policy only consumes the resulting values. */
    bool (*sampleEnvironment)(const void *user, Vector3 position,
                              double simulationTime,
                              WorldEnvironmentSample *out);
} PlayerWorldOps;

PlayerWorldOps PlayerWorldOpsDefault(void);
PlayerWorldOps PlayerWorldOpsWithRules(const WorldRuleSet *rules);

#endif
