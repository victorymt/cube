#include "app/game_debug_block.h"

#include "app/game_runtime.h"
#include "world/block_atlas.h"
#include "world/chunks.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_GALLERY_WIDTH 14
#define BLOCK_GALLERY_ROWS 3

static void BlockCommandError(GameRuntime *game, const char *reason)
{
    game->debugCommandFailed = true;
    snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure),
             "%s", reason);
}

static bool BlockNamesEqual(const char *left, const char *right)
{
    if (!left || !right) return false;
    for (;;) {
        while (*left == ' ' || *left == '-' || *left == '_') left++;
        while (*right == ' ' || *right == '-' || *right == '_') right++;
        int a = tolower((unsigned char)*left);
        int b = tolower((unsigned char)*right);
        if (a != b) return false;
        if (a == '\0') return true;
        left++;
        right++;
    }
}

static bool BlockResolve(const char *query, BlockType *outType)
{
    if (!query || !outType) return false;
    errno = 0;
    char *end = NULL;
    long numeric = strtol(query, &end, 10);
    if (end != query && *end == '\0' && errno == 0 &&
        numeric >= 0 && numeric <= BLOCK_COLOR_END &&
        IsValidBlockType((BlockType)numeric)) {
        *outType = (BlockType)numeric;
        return true;
    }
    for (int value = BLOCK_AIR; value <= BLOCK_NATURAL_END; value++) {
        BlockType type = (BlockType)value;
        if (BlockNamesEqual(query, BlockName(type))) {
            *outType = type;
            return true;
        }
    }
    for (int value = BLOCK_COLOR_START; value <= BLOCK_COLOR_END; value++) {
        BlockType type = (BlockType)value;
        if (BlockNamesEqual(query, BlockName(type))) {
            *outType = type;
            return true;
        }
    }
    return false;
}

static const char *BlockShapeName(BlockRenderShape shape)
{
    switch (shape) {
    case BLOCK_RENDER_CROSS: return "cross";
    case BLOCK_RENDER_CARPET: return "carpet";
    case BLOCK_RENDER_CUBE:
    default: return "cube";
    }
}

static void BlockInspect(GameRuntime *game)
{
    BlockType type = BLOCK_AIR;
    if (!BlockResolve(game->debugControl.blockQuery, &type)) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL block inspect error "
                          "reason=unknown_block query=%s\n",
                          game->debugControl.blockQuery);
        BlockCommandError(game, "unknown_block");
        return;
    }
    BlockMaterialResponse material = BlockMaterialResponseFor(type);
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL block inspect ok id=%d name=\"%s\" "
        "texture_side=%d texture_top=%d texture_bottom=%d shape=%s "
        "collision=%.3f translucent=%s wind=%.3f impact=%.3f "
        "flammability=%.3f water_erodibility=%.3f stage05=%s\n",
        (int)type, BlockName(type), (int)TextureForBlockFace(type, 0),
        (int)TextureForBlockFace(type, 2),
        (int)TextureForBlockFace(type, 3),
        BlockShapeName(BlockRenderShapeFor(type)), BlockCollisionHeight(type),
        IsTranslucentBlock(type) ? "true" : "false",
        material.windResistance, material.impactResistance,
        material.flammability, material.waterErodibility,
        IsStage05Block(type) ? "true" : "false");
}

static void BlockGalleryPosition(BlockType type, int originX, int originZ,
                                 int *outX, int *outZ)
{
    int row = 0;
    int column = (int)type - BLOCK_STAGE05_START;
    if (type >= BLOCK_STAGE05_BIOGENIC_START) {
        row = 1;
        column = (int)type - BLOCK_STAGE05_BIOGENIC_START;
    }
    if (type >= BLOCK_FIRE_RESIDUE_START) {
        row = 2;
        column = (int)type - BLOCK_FIRE_RESIDUE_START;
    }
    *outX = originX + column;
    *outZ = originZ + row;
}

static void BlockGallery(GameRuntime *game)
{
    int originX = game->debugControl.blockGalleryX;
    int originY = game->debugControl.blockGalleryY;
    int originZ = game->debugControl.blockGalleryZ;
    if (!WorldIsSurfaceActive() || !WorldCanAccessBlockY(originY) ||
        originX > 1000000 - (BLOCK_GALLERY_WIDTH - 1) ||
        originZ > 1000000 - (BLOCK_GALLERY_ROWS - 1)) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL block gallery error "
                          "reason=invalid_region\n");
        BlockCommandError(game, "invalid_gallery_region");
        return;
    }
    for (int row = 0; row < BLOCK_GALLERY_ROWS; row++) {
        for (int column = 0; column < BLOCK_GALLERY_WIDTH; column++) {
            if (!SurfaceBlockReadyAt(originX + column, originY,
                                     originZ + row)) {
                DebugControlReply(
                    &game->debugControl,
                    "DEBUG_CONTROL block gallery error reason=region_unloaded "
                    "position=%d,%d,%d\n",
                    originX + column, originY, originZ + row);
                BlockCommandError(game, "gallery_region_unloaded");
                return;
            }
        }
    }

    WorldBeginUndoGroup();
    unsigned placed = 0u;
    for (int value = BLOCK_STAGE05_START; value <= BLOCK_STAGE05_END;
         value++) {
        int x = 0;
        int z = 0;
        BlockGalleryPosition((BlockType)value, originX, originZ, &x, &z);
        if (!SetBlock(x, originY, z, (BlockType)value)) {
            WorldEndUndoGroup();
            if (placed > 0u) UndoBlockEdit();
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL block gallery error reason=mutation_failed "
                "position=%d,%d,%d placed=%u\n", x, originY, z, placed);
            BlockCommandError(game, "gallery_mutation_failed");
            return;
        }
        placed++;
    }
    WorldEndUndoGroup();
    game->blockGalleryActive = true;
    game->blockGalleryOrigin = (Vector3){ originX, originY, originZ };
    game->blockGalleryPlaced = placed;
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL block gallery ok origin=%d,%d,%d placed=%u rows=3 "
        "geology=14 biogenic=9 fire_residue=3\n",
        originX, originY, originZ, placed);
}

bool GameDebugBlockDispatch(GameRuntime *game, DebugControlCommand command)
{
    if (!game) return false;
    if (command == DEBUG_CONTROL_COMMAND_BLOCK_INSPECT) {
        BlockInspect(game);
        return true;
    }
    if (command == DEBUG_CONTROL_COMMAND_BLOCK_GALLERY) {
        BlockGallery(game);
        return true;
    }
    return false;
}

static bool BlockDslNumber(DebugDslValue *outValue, double value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_NUMBER,
        .as.number = value
    };
    return true;
}

static bool BlockDslBool(DebugDslValue *outValue, bool value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_BOOL,
        .as.boolean = value
    };
    return true;
}

bool GameDebugBlockDslResolve(const GameRuntime *game, const char *name,
                              DebugDslValue *outValue)
{
    if (!game || !name || !outValue) return false;
    if (strcmp(name, "block.catalog_count") == 0) {
        return BlockDslNumber(
            outValue, BLOCK_NATURAL_END + 1 + COLOR_BLOCK_COUNT);
    }
    if (strcmp(name, "block.natural_count") == 0) {
        return BlockDslNumber(
            outValue, BLOCK_NATURAL_END - BLOCK_NATURAL_START + 1);
    }
    if (strcmp(name, "block.stage05_count") == 0) {
        return BlockDslNumber(
            outValue, BLOCK_STAGE05_END - BLOCK_STAGE05_START + 1);
    }
    if (strcmp(name, "block.gallery_active") == 0) {
        return BlockDslBool(outValue, game->blockGalleryActive);
    }
    if (strcmp(name, "block.gallery_origin") == 0) {
        *outValue = (DebugDslValue){
            .type = DEBUG_DSL_VALUE_VEC3,
            .as.vec3 = {
                game->blockGalleryOrigin.x,
                game->blockGalleryOrigin.y,
                game->blockGalleryOrigin.z
            }
        };
        return true;
    }
    if (strcmp(name, "block.gallery_placed") == 0) {
        return BlockDslNumber(outValue, game->blockGalleryPlaced);
    }
    if (strcmp(name, "block.gallery_rows") == 0) {
        return BlockDslNumber(outValue, BLOCK_GALLERY_ROWS);
    }
    return false;
}
