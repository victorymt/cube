#include "app/game_internal.h"

#include "raymath.h"
#include "presentation/surface_render_transform.h"
#include "world/block_type.h"
#include "world/surface_contract.h"
#include "world/world_environment.h"
#include "world/world_surface_snapshot.h"

static bool GameCubeCellGuidesActive(void)
{
    return WorldIsSurfaceActive() &&
        SurfaceContractSupportsPhysicalCoverage(
            WorldCurrentSurfaceContract());
}

static bool GameDrawCubeCellGuide(GameRuntime *game, SurfaceAddress cell,
                                  Color color)
{
    Vector3 corners[8];
    WorldSurfaceContext surface = WorldCurrentSurfaceContext();
    if (!SurfaceCubeCellRenderCorners(cell, game->camera.position,
            surface.originX, surface.originZ, corners)) return false;
    Vector3 center = Vector3Zero();
    for (unsigned i = 0; i < 8; i++) center = Vector3Add(center, corners[i]);
    center = Vector3Scale(center, 0.125f);
    Vector3 toCamera = Vector3Subtract(game->camera.position, center);
    if (Vector3LengthSqr(toCamera) > 1e-12f)
        toCamera = Vector3Scale(Vector3Normalize(toCamera), 0.01f);
    else
        toCamera = Vector3Zero();
    for (unsigned i = 0; i < 8; i++)
        corners[i] = Vector3Add(corners[i], toCamera);
    static const unsigned char edges[12][2] = {
        {0, 1}, {1, 3}, {3, 2}, {2, 0},
        {4, 5}, {5, 7}, {7, 6}, {6, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };
    for (unsigned i = 0; i < 12; i++)
        DrawLine3D(corners[edges[i][0]], corners[edges[i][1]], color);
    return true;
}

void GameRenderInteractionGuides(GameRuntime *game,
                                 const GameFrameView *frame)
{
    bool cubeCells = GameCubeCellGuidesActive();
    if (frame->hit.hit) {
        if (frame->hitParkedShip && !frame->hitShip.legacy) {
            Vector3 center = {
                frame->hitShip.coreX + 1.0f,
                frame->hitShip.coreY + 1.0f,
                frame->hitShip.coreZ + 1.0f
            };
            DrawCubeWires(center, 4.03f, 2.03f, 4.03f, WHITE);
        } else if (!(cubeCells && frame->hit.hasAddress &&
                     GameDrawCubeCellGuide(game, frame->hit.address.at.surface,
                                           WHITE))) {
            Vector3 center = {
                frame->hit.x + 0.5f,
                frame->hit.y + 0.5f,
                frame->hit.z + 0.5f
            };
            DrawCubeWires(center, 1.03f, 1.03f, 1.03f, WHITE);
        }
    }
    if (!frame->canPlace) return;

    if (game->hotbar[game->selectedIndex] == BLOCK_SPACESHIP) {
        Vector3 center = {
            frame->placeX + 1.0f,
            frame->placeY + 1.0f,
            frame->placeZ + 1.0f
        };
        DrawCubeWires(center, 4.02f, 2.02f, 4.02f, Fade(GREEN, 0.9f));
    } else if (!(cubeCells && frame->hasPlaceAddress &&
                 GameDrawCubeCellGuide(game, frame->placeAddress.at.surface,
                                       Fade(GREEN, 0.9f)))) {
        Vector3 center = {
            frame->placeX + 0.5f,
            frame->placeY + 0.5f,
            frame->placeZ + 0.5f
        };
        DrawCubeWires(center, 1.02f, 1.02f, 1.02f, Fade(GREEN, 0.9f));
    }
}
