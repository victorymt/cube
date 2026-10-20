#include "app/game_debug_cga.h"

#include "app/game_debug_trace.h"
#include "app/game_runtime.h"
#include "core/config.h"
#include "core/debug_control.h"
#include "world/chunks.h"
#include "world/cga_voxel.h"
#include "world/world.h"

#include "raylib.h"
#include "raymath.h"

#include <inttypes.h>
#include <math.h>

#define DEBUG_CGA_MAX_FEATURES 64

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
        DebugControlReply(&game->debugControl,
                          "DEBUG_CGA feature id=%" PRIu64 " grammar=%" PRIu32
                          " anchor=%d,%d base=%d scope=%d,%d,%d "
                          "size=%d,%d,%d ops=%zu\n",
                          f->id, f->grammarVersion, f->anchorX, f->anchorZ,
                          f->scope.minY, f->scope.minX, f->scope.minY,
                          f->scope.minZ, f->scope.sizeX, f->scope.sizeY,
                          f->scope.sizeZ, ops);
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
    CgaScope scope = features[index].scope;
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
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL cga visible error reason=no_screen\n");
        return;
    }
    int front = 0;
    float minX = (float)screenW, minY = (float)screenH;
    float maxX = 0.0f, maxY = 0.0f;
    for (int c = 0; c < 8; c++) {
        float x = (float)(scope.minX + ((c & 1) ? scope.sizeX : 0));
        float y = (float)(scope.minY + ((c & 2) ? scope.sizeY : 0));
        float z = (float)(scope.minZ + ((c & 4) ? scope.sizeZ : 0));
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
    bool onScreen = false;
    float coverage = 0.0f;
    if (front > 0) {
        float clipMinX = Clamp(minX, 0.0f, (float)screenW);
        float clipMinY = Clamp(minY, 0.0f, (float)screenH);
        float clipMaxX = Clamp(maxX, 0.0f, (float)screenW);
        float clipMaxY = Clamp(maxY, 0.0f, (float)screenH);
        if (clipMaxX > clipMinX && clipMaxY > clipMinY) {
            coverage = (clipMaxX - clipMinX) * (clipMaxY - clipMinY) /
                ((float)screenW * (float)screenH);
            onScreen = coverage > 0.0001f;
        }
    }
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL cga visible feature=%" PRIu64
                      " grammar=%" PRIu32 " on_screen=%d coverage=%.6f "
                      "bounds=%.1f,%.1f,%.1f,%.1f screen=%d,%d\n",
                      features[index].id, features[index].grammarVersion,
                      onScreen ? 1 : 0, coverage, minX, minY, maxX, maxY,
                      screenW, screenH);
    GameDebugTraceEvent(game, "cga-visible");
}

