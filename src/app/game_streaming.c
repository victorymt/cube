#include "app/game_runtime.h"
#include "ecology/entity.h"
#include "gameplay/ship.h"
#include "space/space_chunks.h"
#include "space/space_runtime.h"
#include "space/space_state.h"
#include "world/chunks.h"
#include "world/nether.h"
#include "world/world.h"
#include "world/world_environment.h"

int GameUpdateWorldStreaming(GameRuntime *game, bool *localWorldActive)
{
    int effectiveRenderDistance = EffectiveRenderDistanceForHeight(
        game->player.position.y + EYE_HEIGHT);
    *localWorldActive = WorldIsSurfaceActive();
    uint32_t currentEntityDimension = WorldCurrentSurfaceId();
    if (*localWorldActive != game->entitiesWorldActive ||
        (*localWorldActive &&
         currentEntityDimension != game->entitiesWorldDimension)) {
        EntitiesClear();
        game->evolutionScanLocked = false;
        game->evolutionLockedOrganismId = 0u;
        game->entitiesWorldActive = *localWorldActive;
        game->entitiesWorldDimension = currentEntityDimension;
    }
    if (*localWorldActive) {
        ChunkStreamingRequest request = {
            .position = game->player.position,
            .velocity = game->player.velocity,
            .effectiveRenderDistance = effectiveRenderDistance,
            .maxPreloadChunks = ShipIsDriving()
                ? CHUNK_PRELOAD_SHIP_MAX_DISTANCE
                : CHUNK_PRELOAD_WALK_MAX_DISTANCE
        };
        UpdateChunks(&request);
    }
    if (WorldCurrentDimension() != WORLD_DIMENSION_PLANET) {
        SpaceProcessFinishedGenJobs();
        int spaceGenPerFrame = 2;
        if (ShipIsDriving()) {
            spaceGenPerFrame = ShipIsHighSpeedTransit()
                                   ? 16
                                   : (ShipIsCruising() ? 12 : 4);
        }
        UpdateSpaceChunks(game->player.position, effectiveRenderDistance,
                          spaceGenPerFrame);
        if (HomeWorldSurfaceIsActive() &&
            WorldCurrentDimensionAt(game->player.position.y + EYE_HEIGHT) ==
                WORLD_DIMENSION_NETHER) {
            UpdateNetherChunks(game->player.position,
                               effectiveRenderDistance, 4);
        }
        SpaceUpdateSolarGlow(game->player.position);
    }
    return effectiveRenderDistance;
}
