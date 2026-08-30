#include "app/game_perf_route.h"

#include "core/perf.h"
#include "raymath.h"
#include "world/terrain.h"
#include "world/world.h"

#include <math.h>

void GameApplyPerfRoute(Player *player, int frame)
{
    if (!player) return;
    const float dt = 1.0f / 60.0f;
    Vector3 previous = player->position;
    float x = 0.5f;
    float z = 0.5f;
    if (frame >= 120 && !PerfRouteComplete()) {
        int phase = (frame - 120) % 300;
        if (phase < 150) {
            x += (float)phase * 0.78f;
            z += (float)phase * 0.39f;
        } else {
            int returning = phase - 150;
            x += 117.0f - (float)returning * 0.78f;
            z += 58.5f - (float)returning * 0.39f;
        }
    }
    int terrainY = TerrainHeight((int)floorf(x), (int)floorf(z),
                                 WorldTerrainMode());
    int seaLevel = TerrainSeaLevel(WorldTerrainMode());
    // The route must remain in the streamed vertical range over ocean cells.
    if (seaLevel >= 0 && terrainY < seaLevel) terrainY = seaLevel;
    player->position = (Vector3){ x, (float)terrainY + 3.0f, z };
    Vector3 delta = Vector3Scale(Vector3Subtract(player->position, previous),
                                 1.0f / dt);
    player->velocity = delta;
    if (Vector3LengthSqr(delta) > 0.001f)
        player->yaw = atan2f(delta.x, delta.z);
    player->pitch = -0.18f;
    player->onGround = true;
    player->floating = false;
}
