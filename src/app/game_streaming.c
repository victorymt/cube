#include "app/game_runtime.h"
#include "ecology/entity.h"
#include "gameplay/ship.h"
#include "space/space_chunks.h"
#include "space/space_runtime.h"
#include "space/space_state.h"
#include "world/chunks.h"
#include "world/terrain_cube.h"
#include "world/chunks_cube.h"
#include <math.h>
#include "world/surface_pose.h"
#include "world/nether.h"
#include "world/world.h"
#include "world/world_environment.h"

/* Releases the cube generation input. Called whenever the surface is torn down
 * or replaced so a stale world cannot keep generating into the shared pool. */
void GameReleaseCubeContext(GameRuntime *game)
{
    if (!game || !game->cubeContext) return;
    TerrainCubeContextRelease(game->cubeContext);
    game->cubeContext = NULL;
}

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
    /* Coverage focuses on the player's physical position: the world-to-surface
     * helper applies the map origin and projection, so the focus is the cell
     * the player actually stands on. */
    SurfaceVector cubeFocus = { 0, 0, 0 };
    if (*localWorldActive) {
        SurfaceAddress playerCell = SurfaceAddressAtWorld(
            game->player.position.x, game->player.position.z,
            (int)floorf(game->player.position.y));
        SurfaceVector centre;
        if (SurfaceCellCenter(playerCell, &centre)) cubeFocus = centre;
    }
    if (*localWorldActive &&
        WorldCurrentSurfaceContext().topology.kind == SURFACE_TOPOLOGY_CUBE_GRID) {
        /* Cube surfaces stream through physical coverage, not the legacy map
         * square. The context is created once per installed world. */
        if (!game->cubeContext) {
            game->cubeContext = TerrainCubeContextCreate(
                WorldCurrentSurfaceSnapshot(), WorldTerrainMode(),
                WorldCurrentGeneration());
        }
        if (game->cubeContext) {
            CubeCoverageStatus status;
            (void)ChunksUpdateCubeCoverage(game->cubeContext, cubeFocus,
                (double)effectiveRenderDistance * CHUNK_SIZE, 8u, &status);
        }
        /* Report the real render distance: returning 0 made every caller that
         * reads it before the next overwrite see no world at all. */
        return effectiveRenderDistance;
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
