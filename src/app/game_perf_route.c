#include "app/game_perf_route.h"

#include "core/perf.h"
#include "gameplay/player_state.h"
#include "raymath.h"
#include "world/world_environment.h"

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
    int terrainY = WorldSurfaceHeightAt((int)floorf(x), (int)floorf(z));
    SurfaceContractSample sample;
    int seaLevel = WorldSurfaceSampleAt((int)floorf(x), (int)floorf(z),
                                        &sample)
        ? (int)lroundf(sample.seaLevel) : -1;
    // The route must remain in the streamed vertical range over ocean cells.
    if (seaLevel >= 0 && terrainY < seaLevel) terrainY = seaLevel;
    Vector3 position = (Vector3){ x, (float)terrainY + 3.0f, z };
    Vector3 delta = Vector3Scale(Vector3Subtract(position, previous),
                                 1.0f / dt);
    float yaw = player->yaw;
    if (Vector3LengthSqr(delta) > 0.001f)
        yaw = atan2f(delta.x, delta.z);
    PlayerPlaceAt(player, position, delta, yaw, -0.18f, true, false);
}
