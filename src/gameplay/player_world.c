#include "gameplay/player_world.h"

#include "gameplay/player_motion.h"
#include "gameplay/player_sensors.h"
#include "space/space_query.h"
#include "world/world_environment.h"

static Player PlayerFromBody(const PlayerBodyState *body)
{
    return (Player){
        .position = body->position,
        .velocity = body->velocity,
        .onGround = body->onGround,
        .floating = body->floating
    };
}

static void BodyFromPlayer(PlayerBodyState *body, const Player *player)
{
    body->position = player->position;
    body->velocity = player->velocity;
    body->onGround = player->onGround;
    body->floating = player->floating;
}

static bool WorldMove(void *user, PlayerBodyState *body,
                      const PlayerMoveRequest *request)
{
    (void)user;
    if (!body || !request) return false;
    Player player = PlayerFromBody(body);
    PlayerMoveWithOptions(&player, request->delta, request->allowUnready,
                          request->canStepUp);
    BodyFromPlayer(body, &player);
    return true;
}

static bool WorldOverlaps(void *user, Vector3 position, bool allowUnready)
{
    (void)user;
    return PlayerOverlapsWorldWithOptions(position, allowUnready);
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
