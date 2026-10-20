#include "app/game_debug_cga.h"

#include "app/game_debug_trace.h"
#include "app/game_runtime.h"
#include "core/config.h"
#include "core/debug_control.h"
#include "world/chunks.h"
#include "world/cga_voxel.h"
#include "world/world.h"
#include "gameplay/interaction.h"

#include "raylib.h"
#include "raymath.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_CGA_MAX_FEATURES 64
#define DEBUG_CGA_GRID_W 64
#define DEBUG_CGA_GRID_H 36

static bool GameDebugCgaContains(CgaScope scope, float x, float z)
{
    return x >= (float)scope.minX && x < (float)(scope.minX + scope.sizeX) &&
        z >= (float)scope.minZ && z < (float)(scope.minZ + scope.sizeZ);
}

static size_t GameDebugCgaCollect(GameRuntime *game, unsigned radius,
                                  CgaFeature *out, size_t capacity)
{
    if (!out || capacity == 0) return 0;
    static CgaWorkspace plan;
    int pcx = (int)floorf(game->player.position.x / (float)CHUNK_SIZE);
    int pcz = (int)floorf(game->player.position.z / (float)CHUNK_SIZE);
    int r = (int)radius;
    size_t count = 0;
    for (int dz = -r; dz <= r; dz++) {
        for (int dx = -r; dx <= r; dx++) {
            CgaWorkspaceReset(&plan);
            if (!CgaPlanStructureFeatures(&plan, pcx + dx, pcz + dz,
                                          WorldTerrainMode(), NULL)) continue;
            for (size_t i = 0; i < plan.featureCount && count < capacity; i++) {
                bool duplicate = false;
                for (size_t k = 0; k < count; k++) {
                    if (out[k].id == plan.features[i].id) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) out[count++] = plan.features[i];
            }
        }
    }
    return count;
}

static size_t GameDebugCgaOpCount(CgaFeature feature)
{
    static CgaWorkspace expand;
    CgaWorkspaceReset(&expand);
    return CgaExpandFeature(&expand, feature) ? expand.opCount : 0;
}

typedef struct GameDebugCgaBounds {
    int minX;
    int minY;
    int minZ;
    int maxX;
    int maxY;
    int maxZ;
} GameDebugCgaBounds;

typedef struct GameDebugCgaOccupancy {
    GameDebugCgaBounds bounds;
    int sizeX;
    int sizeY;
    int sizeZ;
    unsigned char *voxels;
} GameDebugCgaOccupancy;

static bool GameDebugCgaOccupied(const GameDebugCgaOccupancy *occ,
                                 int x, int y, int z)
{
    if (x < occ->bounds.minX || x > occ->bounds.maxX ||
        y < occ->bounds.minY || y > occ->bounds.maxY ||
        z < occ->bounds.minZ || z > occ->bounds.maxZ) return false;
    size_t index = (size_t)(x - occ->bounds.minX) +
        (size_t)(z - occ->bounds.minZ) * (size_t)occ->sizeX +
        (size_t)(y - occ->bounds.minY) * (size_t)occ->sizeX *
            (size_t)occ->sizeZ;
    return occ->voxels[index] != 0;
}

static bool GameDebugCgaBuildOccupancy(CgaFeature feature,
                                       GameDebugCgaOccupancy *out)
{
    if (!out) return false;
    *out = (GameDebugCgaOccupancy){ 0 };
    static CgaWorkspace expand;
    CgaWorkspaceReset(&expand);
    if (!CgaExpandFeature(&expand, feature)) return false;
    /* Match CgaEvaluateChunk: last write wins in evaluation order. */
    CgaSortOps(&expand);
    bool any = false;
    GameDebugCgaBounds bounds = { 0 };
    for (size_t i = 0; i < expand.opCount; i++) {
        const CgaVoxelOp *op = &expand.ops[i];
        if (op->kind != CGA_OP_FILL) continue;
        int minX = op->scope.minX;
        int minY = op->scope.minY;
        int minZ = op->scope.minZ;
        int maxX = op->scope.minX + op->scope.sizeX - 1;
        int maxY = op->scope.minY + op->scope.sizeY - 1;
        int maxZ = op->scope.minZ + op->scope.sizeZ - 1;
        if (!any) {
            bounds = (GameDebugCgaBounds){ minX, minY, minZ, maxX, maxY, maxZ };
            any = true;
        } else {
            if (minX < bounds.minX) bounds.minX = minX;
            if (minY < bounds.minY) bounds.minY = minY;
            if (minZ < bounds.minZ) bounds.minZ = minZ;
            if (maxX > bounds.maxX) bounds.maxX = maxX;
            if (maxY > bounds.maxY) bounds.maxY = maxY;
            if (maxZ > bounds.maxZ) bounds.maxZ = maxZ;
        }
    }
    if (!any) return false;
    int sx = bounds.maxX - bounds.minX + 1;
    int sy = bounds.maxY - bounds.minY + 1;
    int sz = bounds.maxZ - bounds.minZ + 1;
    if (sx <= 0 || sy <= 0 || sz <= 0) return false;
    size_t volume = (size_t)sx * (size_t)sy * (size_t)sz;
    unsigned char *voxels = calloc(volume, 1);
    if (!voxels) return false;
    for (size_t i = 0; i < expand.opCount; i++) {
        const CgaVoxelOp *op = &expand.ops[i];
        if (op->kind != CGA_OP_FILL && op->kind != CGA_OP_CARVE) continue;
        int x0 = op->scope.minX > bounds.minX ? op->scope.minX : bounds.minX;
        int y0 = op->scope.minY > bounds.minY ? op->scope.minY : bounds.minY;
        int z0 = op->scope.minZ > bounds.minZ ? op->scope.minZ : bounds.minZ;
        int x1 = op->scope.minX + op->scope.sizeX - 1;
        int y1 = op->scope.minY + op->scope.sizeY - 1;
        int z1 = op->scope.minZ + op->scope.sizeZ - 1;
        if (x1 > bounds.maxX) x1 = bounds.maxX;
        if (y1 > bounds.maxY) y1 = bounds.maxY;
        if (z1 > bounds.maxZ) z1 = bounds.maxZ;
        unsigned char value = op->kind == CGA_OP_FILL ? 1u : 0u;
        for (int y = y0; y <= y1; y++) {
            for (int z = z0; z <= z1; z++) {
                size_t row = (size_t)(z - bounds.minZ) * (size_t)sx +
                    (size_t)(y - bounds.minY) * (size_t)sx * (size_t)sz;
                for (int x = x0; x <= x1; x++) {
                    voxels[row + (size_t)(x - bounds.minX)] = value;
                }
            }
        }
    }
    *out = (GameDebugCgaOccupancy){
        .bounds = bounds,
        .sizeX = sx,
        .sizeY = sy,
        .sizeZ = sz,
        .voxels = voxels
    };
    return true;
}

static void GameDebugCgaGridVisibility(const GameDebugCgaOccupancy *occ,
                                       Camera3D camera, int screenW,
                                       int screenH, float clipMinX,
                                       float clipMinY, float clipMaxX,
                                       float clipMaxY, int *visibleCells,
                                       int *occludedCells, int *emptyCells,
                                       float *coverage,
                                       float *visibleFraction)
{
    const int gw = DEBUG_CGA_GRID_W;
    const int gh = DEBUG_CGA_GRID_H;
    *visibleCells = 0;
    *occludedCells = 0;
    *emptyCells = 0;
    *coverage = 0.0f;
    *visibleFraction = 0.0f;
    float width = clipMaxX - clipMinX;
    float height = clipMaxY - clipMinY;
    if (width <= 0.0f || height <= 0.0f || screenW <= 0 || screenH <= 0)
        return;
    float cellW = width / (float)gw;
    float cellH = height / (float)gh;
    Vector3 eye = camera.position;
    float farX = (float)occ->bounds.maxX - eye.x;
    float farY = (float)occ->bounds.maxY - eye.y;
    float farZ = (float)occ->bounds.maxZ - eye.z;
    float farDistance = sqrtf(farX * farX + farY * farY + farZ * farZ) + 8.0f;
    for (int gy = 0; gy < gh; gy++) {
        for (int gx = 0; gx < gw; gx++) {
            Vector2 screen = {
                clipMinX + (gx + 0.5f) * cellW,
                clipMinY + (gy + 0.5f) * cellH
            };
            Ray ray = GetScreenToWorldRay(screen, camera);
            HitResult hit = RaycastBlocksFiltered(
                ray.position, ray.direction, farDistance, RAYCAST_BLOCK_SOLID);
            if (!hit.hit) {
                (*emptyCells)++;
            } else if (GameDebugCgaOccupied(occ, hit.x, hit.y, hit.z)) {
                (*visibleCells)++;
            } else {
                (*occludedCells)++;
            }
        }
    }
    *coverage = (float)(*visibleCells) * cellW * cellH /
        ((float)screenW * (float)screenH);
    int structureCells = *visibleCells + *occludedCells;
    if (structureCells > 0) {
        *visibleFraction = (float)(*visibleCells) / (float)structureCells;
    }
}

static int GameDebugCgaPick(const CgaFeature *features, size_t count,
                            const GameRuntime *game)
{
    int containing = -1;
    int nearest = -1;
    float bestArea = 0.0f;
    float bestDistance = 1.0e30f;
    for (size_t i = 0; i < count; i++) {
        const CgaScope scope = features[i].scope;
        float cx = (float)scope.minX + (float)scope.sizeX * 0.5f;
        float cz = (float)scope.minZ + (float)scope.sizeZ * 0.5f;
        float dx = cx - game->player.position.x;
        float dz = cz - game->player.position.z;
        float distance = dx * dx + dz * dz;
        if (distance < bestDistance) {
            bestDistance = distance;
            nearest = (int)i;
        }
        if (!GameDebugCgaContains(scope, game->player.position.x,
                                  game->player.position.z)) continue;
        float area = (float)scope.sizeX * (float)scope.sizeZ;
        bool complex =
            features[i].grammarVersion == CGA_CLASSICAL_COMPLEX_GRAMMAR_VERSION;
        bool bestComplex = containing >= 0 &&
            features[containing].grammarVersion ==
            CGA_CLASSICAL_COMPLEX_GRAMMAR_VERSION;
        if (containing < 0 || (complex && !bestComplex) ||
            (complex == bestComplex && area > bestArea)) {
            containing = (int)i;
            bestArea = area;
        }
    }
    return containing >= 0 ? containing : nearest;
}

void GameDebugCgaInspect(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL cga inspect error reason=not_playing\n");
        return;
    }
    if (!WorldIsSurfaceActive()) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL cga inspect error reason=no_surface\n");
        return;
    }
    CgaFeature features[DEBUG_CGA_MAX_FEATURES];
    size_t count = GameDebugCgaCollect(game, game->debugControl.cgaRadius,
                                       features, DEBUG_CGA_MAX_FEATURES);
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL cga inspect count=%zu radius=%u seed=%u\n",
                      count, game->debugControl.cgaRadius, WorldGetSeed());
    for (size_t i = 0; i < count; i++) {
        const CgaFeature *f = &features[i];
        size_t ops = GameDebugCgaOpCount(*f);
        unsigned budget =
            f->grammarVersion == CGA_CLASSICAL_COMPLEX_GRAMMAR_VERSION
            ? CGA_CLASSICAL_COMPLEX_OP_BUDGET : 0u;
        bool overBudget = budget != 0u && ops > budget;
        DebugControlReply(&game->debugControl,
                          "DEBUG_CGA feature id=%" PRIu64 " grammar=%" PRIu32
                          " anchor=%d,%d base=%d scope=%d,%d,%d "
                          "size=%d,%d,%d ops=%zu budget=%u over_budget=%d\n",
                          f->id, f->grammarVersion, f->anchorX, f->anchorZ,
                          f->scope.minY, f->scope.minX, f->scope.minY,
                          f->scope.minZ, f->scope.sizeX, f->scope.sizeY,
                          f->scope.sizeZ, ops, budget, overBudget ? 1 : 0);
    }
    GameDebugTraceEvent(game, "cga-inspect");
}

void GameDebugCgaVisible(GameRuntime *game)
{
    if (game->screen != SCREEN_PLAYING) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL cga visible error reason=not_playing\n");
        return;
    }
    if (!WorldIsSurfaceActive()) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL cga visible error reason=no_surface\n");
        return;
    }
    CgaFeature features[DEBUG_CGA_MAX_FEATURES];
    size_t count = GameDebugCgaCollect(game, game->debugControl.cgaRadius,
                                       features, DEBUG_CGA_MAX_FEATURES);
    int index = GameDebugCgaPick(features, count, game);
    if (index < 0) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL cga visible count=0 on_screen=0\n");
        return;
    }
    GameDebugCgaOccupancy occupancy;
    if (!GameDebugCgaBuildOccupancy(features[index], &occupancy)) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL cga visible error reason=no_fill\n");
        return;
    }
    GameDebugCgaBounds fill = occupancy.bounds;
    float eyeX = game->player.position.x;
    float eyeY = game->player.position.y + EYE_HEIGHT;
    float eyeZ = game->player.position.z;
    float cp = cosf(game->player.pitch);
    Vector3 forward = { sinf(game->player.yaw) * cp,
                        sinf(game->player.pitch),
                        cosf(game->player.yaw) * cp };
    Camera3D camera = game->camera;
    camera.position = (Vector3){ eyeX, eyeY, eyeZ };
    camera.target = (Vector3){ eyeX + forward.x, eyeY + forward.y,
                               eyeZ + forward.z };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    int screenW = game->screenWidth;
    int screenH = game->screenHeight;
    if (screenW <= 0 || screenH <= 0) {
        free(occupancy.voxels);
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL cga visible error reason=no_screen\n");
        return;
    }
    int front = 0;
    float minX = (float)screenW, minY = (float)screenH;
    float maxX = 0.0f, maxY = 0.0f;
    for (int c = 0; c < 8; c++) {
        float x = (float)((c & 1) ? fill.maxX : fill.minX);
        float y = (float)((c & 2) ? fill.maxY : fill.minY);
        float z = (float)((c & 4) ? fill.maxZ : fill.minZ);
        Vector3 rel = { x - eyeX, y - eyeY, z - eyeZ };
        if (Vector3DotProduct(rel, forward) <= 0.05f) continue;
        Vector2 p = GetWorldToScreen((Vector3){ x, y, z }, camera);
        if (!isfinite(p.x) || !isfinite(p.y)) continue;
        front++;
        if (p.x < minX) minX = p.x;
        if (p.y < minY) minY = p.y;
        if (p.x > maxX) maxX = p.x;
        if (p.y > maxY) maxY = p.y;
    }
    int visibleCells = 0;
    int occludedCells = 0;
    int emptyCells = 0;
    float coverage = 0.0f;
    float visibleFraction = 0.0f;
    bool onScreen = false;
    if (front > 0) {
        float clipMinX = Clamp(minX, 0.0f, (float)screenW);
        float clipMinY = Clamp(minY, 0.0f, (float)screenH);
        float clipMaxX = Clamp(maxX, 0.0f, (float)screenW);
        float clipMaxY = Clamp(maxY, 0.0f, (float)screenH);
        if (clipMaxX > clipMinX && clipMaxY > clipMinY) {
            GameDebugCgaGridVisibility(&occupancy, camera, screenW, screenH,
                                       clipMinX, clipMinY, clipMaxX, clipMaxY,
                                       &visibleCells, &occludedCells,
                                       &emptyCells, &coverage,
                                       &visibleFraction);
            onScreen = visibleCells > 0;
        }
    }
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL cga visible feature=%" PRIu64
                      " grammar=%" PRIu32 " on_screen=%d coverage=%.6f "
                      "bounds=%.1f,%.1f,%.1f,%.1f screen=%d,%d "
                      "fill=%d,%d,%d,%d,%d,%d grid=%dx%d "
                      "visible_cells=%d occluded_cells=%d empty_cells=%d "
                      "visible_fraction=%.3f\n",
                      features[index].id, features[index].grammarVersion,
                      onScreen ? 1 : 0, coverage, minX, minY, maxX, maxY,
                      screenW, screenH, fill.minX, fill.minY, fill.minZ,
                      fill.maxX, fill.maxY, fill.maxZ, DEBUG_CGA_GRID_W,
                      DEBUG_CGA_GRID_H, visibleCells, occludedCells,
                      emptyCells, visibleFraction);
    free(occupancy.voxels);
    GameDebugTraceEvent(game, "cga-visible");
}
