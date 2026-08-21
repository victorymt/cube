#include "world/block_atlas.h"
#include "world/block_atlas_artwork_internal.h"

#include "raymath.h"
#include "rlgl.h"
#include "world/block_catalog.h"
#include "world/world.h"

#include <stdbool.h>

static unsigned int AtlasHash3D(int x, int y, int z)
{
    unsigned int h = 2166136261u;
    h = (h ^ (unsigned int)x) * 16777619u;
    h = (h ^ (unsigned int)y) * 16777619u;
    h = (h ^ (unsigned int)z) * 16777619u;
    h ^= h >> 15;
    h *= 2246822519u;
    return h ^ (h >> 13);
}

Color AtlasColorWithNoise(Color base, int amount, unsigned int hash)
{
    int delta = (int)(hash % (unsigned int)(amount * 2 + 1)) - amount;
    return (Color){
        (unsigned char)Clamp((float)((int)base.r + delta), 0.0f, 255.0f),
        (unsigned char)Clamp((float)((int)base.g + delta), 0.0f, 255.0f),
        (unsigned char)Clamp((float)((int)base.b + delta), 0.0f, 255.0f),
        base.a
    };
}

static Color GenericLeavesPixel(int x, int y, unsigned int hash)
{
    static const unsigned char centers[][2] = {
        { 1, 2 }, { 4, 1 }, { 8, 2 }, { 12, 1 }, { 15, 3 },
        { 2, 6 }, { 6, 5 }, { 10, 6 }, { 14, 7 },
        { 0, 10 }, { 4, 10 }, { 8, 9 }, { 12, 11 }, { 15, 12 },
        { 2, 14 }, { 6, 13 }, { 10, 15 }, { 14, 15 }
    };
    static const Color foliage[] = {
        { 35, 89, 42, 255 },
        { 51, 117, 47, 255 },
        { 75, 139, 55, 255 },
        { 104, 155, 66, 255 }
    };
    int selectedLeaf = -1;
    int selectedDx = 0;
    int selectedDy = 0;
    for (int index = 0;
         index < (int)(sizeof(centers) / sizeof(centers[0])); index++) {
        int dx = x - (int)centers[index][0];
        int dy = y - (int)centers[index][1];
        int radiusX = 2 + (index % 3 == 0);
        int radiusY = 2 + (index % 4 == 0);
        if (dx * dx * radiusY * radiusY +
            dy * dy * radiusX * radiusX <=
            radiusX * radiusX * radiusY * radiusY) {
            selectedLeaf = index;
            selectedDx = dx;
            selectedDy = dy;
        }
    }
    if (selectedLeaf >= 0) {
        int tone = (selectedLeaf * 3 + x + y + (int)(hash % 3u)) & 3;
        Color base = foliage[tone];
        if (selectedDx == 0 || (selectedDx + selectedDy == 0 &&
                                (selectedLeaf & 1))) {
            base = (Color){ 128, 169, 78, 255 };
        }
        return AtlasColorWithNoise(base, 5, hash);
    }
    if (x + y >= 13 && x + y <= 15) {
        return AtlasColorWithNoise((Color){ 86, 65, 38, 255 }, 5, hash);
    }
    return AtlasColorWithNoise((Color){ 27, 70, 37, 255 }, 5, hash);
}

static Color CactusAtlasPixel(int x, int y, unsigned int hash)
{
    int rib = x & 3;
    Color base = rib == 0 ? (Color){ 38, 96, 49, 255 }
               : rib == 1 ? (Color){ 55, 126, 58, 255 }
               : rib == 2 ? (Color){ 82, 151, 69, 255 }
                          : (Color){ 48, 112, 52, 255 };
    int areoleY = (y + (x / 4) * 3) % 7;
    if (rib == 2 && areoleY == 1) {
        return AtlasColorWithNoise((Color){ 201, 202, 133, 255 }, 4, hash);
    }
    if ((rib == 1 || rib == 3) && areoleY == 1) {
        return AtlasColorWithNoise((Color){ 151, 170, 101, 255 }, 4, hash);
    }
    if (y == 5 + (x / 4) * 2) {
        base = (Color){ 66, 119, 57, 255 };
    }
    return AtlasColorWithNoise(base, 6, hash);
}

static Color NaturalAtlasPixel(BlockTexture texture, int x, int y,
                               unsigned int hash)
{
    switch (texture) {
    case TEX_GRASS_TOP: {
        Color color = AtlasColorWithNoise((Color){ 84, 170, 67, 255 }, 18,
                                          hash);
        if ((hash % 11u) == 0u) color = (Color){ 119, 199, 82, 255 };
        return color;
    }
    case TEX_GRASS_SIDE:
        if (y < 5 + (int)(hash % 3u)) {
            return AtlasColorWithNoise((Color){ 88, 169, 70, 255 }, 15,
                                       hash);
        }
        return AtlasColorWithNoise((Color){ 121, 79, 45, 255 }, 17, hash);
    case TEX_DIRT: {
        Color color = AtlasColorWithNoise((Color){ 121, 77, 43, 255 }, 22,
                                          hash);
        if ((hash % 17u) == 0u) color = (Color){ 89, 55, 34, 255 };
        return color;
    }
    case TEX_STONE: {
        Color color = AtlasColorWithNoise((Color){ 118, 122, 124, 255 }, 20,
                                          hash);
        if ((x + y + (int)(hash % 5u)) % 13 == 0) {
            color = (Color){ 84, 88, 91, 255 };
        }
        return color;
    }
    case TEX_WOOD_SIDE:
        return AtlasColorWithNoise(
            (x % 5 == 0) ? (Color){ 104, 67, 32, 255 }
                         : (Color){ 142, 91, 42, 255 },
            13, hash);
    case TEX_WOOD_TOP: {
        int dx = x - ATLAS_TILE_SIZE / 2;
        int dy = y - ATLAS_TILE_SIZE / 2;
        int ring = (dx * dx + dy * dy) / 11;
        return AtlasColorWithNoise(
            (ring % 2 == 0) ? (Color){ 154, 105, 55, 255 }
                            : (Color){ 118, 75, 37, 255 },
            10, hash);
    }
    case TEX_SAND: {
        Color color = AtlasColorWithNoise((Color){ 214, 197, 132, 255 }, 16,
                                          hash);
        if ((hash % 19u) == 0u) color = (Color){ 183, 165, 101, 255 };
        return color;
    }
    case TEX_LEAVES:
        return GenericLeavesPixel(x, y, hash);
    default:
        return MAGENTA;
    }
}

typedef struct NamedColorRule {
    Color base;
    Color highlight;
    int baseNoise;
    int highlightNoise;
} NamedColorRule;

static Color NamedColorAtlasPixel(BlockTexture texture, int x, int y,
                                  unsigned int hash)
{
    static const NamedColorRule rules[] = {
        { { 207, 55, 54, 255 }, { 238, 83, 75, 255 }, 18, 10 },
        { { 229, 126, 38, 255 }, { 247, 156, 55, 255 }, 18, 10 },
        { { 238, 207, 64, 255 }, { 255, 228, 86, 255 }, 16, 8 },
        { { 51, 116, 220, 255 }, { 74, 150, 244, 255 }, 18, 10 },
        { { 143, 72, 202, 255 }, { 171, 98, 231, 255 }, 18, 10 },
        { { 64, 185, 85, 255 }, { 91, 218, 108, 255 }, 18, 10 },
        { { 47, 188, 207, 255 }, { 76, 219, 235, 255 }, 18, 10 },
        { { 226, 96, 161, 255 }, { 247, 128, 188, 255 }, 18, 10 },
        { { 232, 235, 224, 255 }, { 250, 250, 241, 255 }, 10, 6 },
        { { 112, 119, 126, 255 }, { 141, 148, 154, 255 }, 14, 8 },
        { { 28, 31, 35, 255 }, { 52, 56, 62, 255 }, 10, 6 }
    };
    const NamedColorRule *rule = &rules[(int)texture - TEX_RED];
    Color color = AtlasColorWithNoise(rule->base, rule->baseNoise, hash);
    if ((x + y) % 6 == 0) {
        color = AtlasColorWithNoise(rule->highlight, rule->highlightNoise,
                                    hash);
    }
    return color;
}

static Color SurfaceAtlasPixel(BlockTexture texture, int x, int y,
                               unsigned int hash)
{
    Color color = WHITE;
    switch (texture) {
    case TEX_PLANK:
        color = AtlasColorWithNoise(
            (y % 4 == 0) ? (Color){ 118, 72, 36, 255 }
                         : (Color){ 156, 100, 48, 255 },
            12, hash);
        if ((x + (y / 4) % 2 * 4) % 8 == 0) {
            color = AtlasColorWithNoise((Color){ 108, 66, 32, 255 }, 8,
                                        hash);
        }
        return color;
    case TEX_BRICK: {
        int row = y / 4;
        int mortar = (y % 4 == 0) || ((x + (row % 2) * 4) % 8 == 0);
        if (mortar) {
            color = AtlasColorWithNoise((Color){ 205, 200, 190, 255 }, 8,
                                        hash);
        } else {
            color = AtlasColorWithNoise((Color){ 148, 62, 48, 255 }, 16,
                                        hash);
        }
        if (!mortar && (hash % 13u) == 0u) {
            color = AtlasColorWithNoise((Color){ 168, 80, 62, 255 }, 8,
                                        hash);
        }
        return color;
    }
    case TEX_GLASS:
        color = AtlasColorWithNoise((Color){ 205, 230, 235, 230 }, 10,
                                    hash);
        if ((x + y) % 7 == 0) {
            color = AtlasColorWithNoise((Color){ 240, 250, 250, 235 }, 5,
                                        hash);
        }
        if (x == 0 || y == 0 || x == ATLAS_TILE_SIZE - 1 ||
            y == ATLAS_TILE_SIZE - 1) {
            color = AtlasColorWithNoise((Color){ 165, 205, 215, 225 }, 8,
                                        hash);
        }
        return color;
    case TEX_WATER:
        color = AtlasColorWithNoise((Color){ 52, 118, 205, 195 }, 12, hash);
        if (((y + (int)(hash % 3u)) % 6) == 0) {
            color = AtlasColorWithNoise((Color){ 86, 158, 228, 210 }, 10,
                                        hash);
        }
        if (((x + (int)(hash % 2u)) % 9) == 0) {
            color = AtlasColorWithNoise((Color){ 40, 96, 178, 190 }, 8,
                                        hash);
        }
        return color;
    case TEX_SNOW:
        color = AtlasColorWithNoise((Color){ 238, 244, 246, 255 }, 8, hash);
        if ((hash % 9u) == 0u) {
            color = AtlasColorWithNoise((Color){ 218, 228, 234, 255 }, 6,
                                        hash);
        }
        return color;
    case TEX_ICE:
        color = AtlasColorWithNoise((Color){ 148, 205, 226, 235 }, 12,
                                    hash);
        if ((hash % 11u) == 0u) color = (Color){ 210, 240, 248, 235 };
        return color;
    case TEX_CACTUS:
        return CactusAtlasPixel(x, y, hash);
    case TEX_BEDROCK:
        color = AtlasColorWithNoise((Color){ 58, 58, 64, 255 }, 24, hash);
        if ((hash % 7u) == 0u) color = (Color){ 32, 32, 36, 255 };
        if ((hash % 11u) == 0u) color = (Color){ 86, 84, 88, 255 };
        return color;
    case TEX_COAL_ORE:
        color = AtlasColorWithNoise((Color){ 116, 120, 122, 255 }, 18,
                                    hash);
        if ((hash % 13u) == 0u) {
            color = AtlasColorWithNoise((Color){ 38, 40, 44, 255 }, 8, hash);
        }
        if ((hash % 31u) == 0u) color = (Color){ 62, 64, 68, 255 };
        return color;
    case TEX_IRON_ORE:
        color = AtlasColorWithNoise((Color){ 116, 120, 122, 255 }, 18,
                                    hash);
        if ((hash % 13u) == 0u) {
            color = AtlasColorWithNoise((Color){ 190, 152, 108, 255 }, 10,
                                        hash);
        }
        if ((hash % 31u) == 0u) color = (Color){ 226, 192, 150, 255 };
        return color;
    case TEX_GOLD_ORE:
        color = AtlasColorWithNoise((Color){ 116, 120, 122, 255 }, 18,
                                    hash);
        if ((hash % 11u) == 0u) {
            color = AtlasColorWithNoise((Color){ 232, 196, 64, 255 }, 12,
                                        hash);
        }
        if ((hash % 29u) == 0u) color = (Color){ 250, 226, 110, 255 };
        return color;
    case TEX_DIAMOND_ORE:
        color = AtlasColorWithNoise((Color){ 116, 120, 122, 255 }, 18,
                                    hash);
        if ((hash % 11u) == 0u) {
            color = AtlasColorWithNoise((Color){ 92, 214, 232, 255 }, 12,
                                        hash);
        }
        if ((hash % 29u) == 0u) color = (Color){ 140, 240, 250, 255 };
        return color;
    default:
        return MAGENTA;
    }
}

static Color NetherAtlasPixel(BlockTexture texture, int x, int y,
                              unsigned int hash)
{
    Color color = WHITE;
    switch (texture) {
    case TEX_NETHERRACK:
        color = AtlasColorWithNoise((Color){ 116, 48, 42, 255 }, 22, hash);
        if ((hash % 9u) == 0u) {
            color = AtlasColorWithNoise((Color){ 168, 72, 56, 255 }, 14,
                                        hash);
        }
        if ((hash % 17u) == 0u) {
            color = AtlasColorWithNoise((Color){ 76, 28, 26, 255 }, 10,
                                        hash);
        }
        return color;
    case TEX_SOUL_SAND:
        color = AtlasColorWithNoise((Color){ 124, 106, 88, 255 }, 16,
                                    hash);
        if ((hash % 11u) == 0u) {
            color = AtlasColorWithNoise((Color){ 164, 148, 120, 255 }, 10,
                                        hash);
        }
        if ((hash % 19u) == 0u) {
            color = AtlasColorWithNoise((Color){ 92, 76, 66, 255 }, 8,
                                        hash);
        }
        return color;
    case TEX_GLOWSTONE:
        color = AtlasColorWithNoise((Color){ 178, 138, 62, 255 }, 18,
                                    hash);
        if ((hash % 9u) == 0u) {
            color = AtlasColorWithNoise((Color){ 250, 220, 110, 255 }, 14,
                                        hash);
        }
        if ((hash % 13u) == 0u) {
            color = AtlasColorWithNoise((Color){ 240, 250, 190, 255 }, 10,
                                        hash);
        }
        if ((hash % 23u) == 0u) {
            color = AtlasColorWithNoise((Color){ 120, 88, 40, 255 }, 8,
                                        hash);
        }
        return color;
    case TEX_STONE_BRICKS:
        if (y % 4 == 0 || x % 8 == 0) {
            color = AtlasColorWithNoise((Color){ 118, 118, 118, 255 }, 8,
                                        hash);
        } else {
            color = AtlasColorWithNoise((Color){ 138, 140, 142, 255 }, 10,
                                        hash);
            if ((hash % 15u) == 0u) {
                color = AtlasColorWithNoise((Color){ 154, 156, 158, 255 }, 6,
                                            hash);
            }
        }
        return color;
    case TEX_SANDSTONE:
        color = AtlasColorWithNoise((Color){ 216, 200, 150, 255 }, 10,
                                    hash);
        if (y % 4 == 0) {
            color = AtlasColorWithNoise((Color){ 196, 178, 128, 255 }, 8,
                                        hash);
        }
        if ((hash % 19u) == 0u) {
            color = AtlasColorWithNoise((Color){ 230, 216, 168, 255 }, 6,
                                        hash);
        }
        return color;
    case TEX_OBSIDIAN:
        color = AtlasColorWithNoise((Color){ 22, 16, 30, 255 }, 16, hash);
        if ((hash % 13u) == 0u) {
            color = AtlasColorWithNoise((Color){ 74, 48, 104, 255 }, 12,
                                        hash);
        }
        if ((hash % 23u) == 0u) {
            color = AtlasColorWithNoise((Color){ 44, 30, 60, 255 }, 10,
                                        hash);
        }
        return color;
    case TEX_NETHER_PORTAL:
        if ((x + y) % 6 < 2) {
            return AtlasColorWithNoise((Color){ 96, 28, 110, 255 }, 20,
                                       hash);
        }
        color = AtlasColorWithNoise((Color){ 158, 52, 190, 255 }, 20, hash);
        if ((hash % 9u) == 0u) {
            color = AtlasColorWithNoise((Color){ 210, 120, 240, 255 }, 14,
                                        hash);
        }
        return color;
    default:
        return MAGENTA;
    }
}

static Color AtlasPixelColor(BlockTexture texture, int x, int y)
{
    unsigned int hash = AtlasHash3D((int)texture, x, y);
    if (texture >= TEX_COLOR_START && texture <= TEX_COLOR_END) {
        Color base = ColorPalette256((int)texture - TEX_COLOR_START);
        Color color = AtlasColorWithNoise(base, 2, hash);
        if ((x + y) % 8 == 0) {
            color = AtlasColorWithNoise(base, 1, hash);
        }
        return color;
    }
    if (texture <= TEX_LEAVES) {
        return NaturalAtlasPixel(texture, x, y, hash);
    }
    if (texture >= TEX_RED && texture <= TEX_BLACK) {
        return NamedColorAtlasPixel(texture, x, y, hash);
    }
    if (texture >= TEX_PLANK && texture <= TEX_DIAMOND_ORE) {
        return SurfaceAtlasPixel(texture, x, y, hash);
    }
    if (texture >= TEX_MOON_ROCK && texture <= TEX_SPACESHIP) {
        return SpaceAtlasPixel(texture, x, y, hash);
    }
    if (texture >= TEX_NETHERRACK && texture <= TEX_NETHER_PORTAL) {
        return NetherAtlasPixel(texture, x, y, hash);
    }
    if (texture >= TEX_GRAVEL && texture <= TEX_NICKEL_ORE) {
        return GeologyAtlasPixel(texture, x, y, hash);
    }
    if (texture >= TEX_TALL_GRASS && texture <= TEX_CHEMO_MAT) {
        return EcologyAtlasPixel(texture, x, y, hash);
    }
    if (texture >= TEX_STAGE05_START && texture <= TEX_STAGE05_END) {
        return BlockExpansionAtlasPixel(texture, x, y, hash);
    }
    if (texture >= TEX_STAGE06_START && texture <= TEX_STAGE06_END) {
        return EcologyAtlasPixel(texture, x, y, hash);
    }
    return ItemAtlasPixel(texture, x, y, hash);
}

static void DrawAtlasArtwork(Image *image, BlockTexture texture, int originX,
                             int originY)
{
    for (int y = 0; y < ATLAS_TILE_SIZE; y++) {
        for (int x = 0; x < ATLAS_TILE_SIZE; x++) {
            ImageDrawPixel(image, originX + x, originY + y,
                           AtlasPixelColor(texture, x, y));
        }
    }
}

static void ExtendAtlasTilePadding(Image *image, int cellX, int cellY,
                                   int originX, int originY)
{
    for (int y = 0; y < ATLAS_CELL_SIZE; y++) {
        int sourceY = y - ATLAS_TILE_PADDING;
        if (sourceY < 0) sourceY = 0;
        if (sourceY >= ATLAS_TILE_SIZE) sourceY = ATLAS_TILE_SIZE - 1;
        for (int x = 0; x < ATLAS_CELL_SIZE; x++) {
            bool insideTile = x >= ATLAS_TILE_PADDING &&
                              x < ATLAS_TILE_PADDING + ATLAS_TILE_SIZE &&
                              y >= ATLAS_TILE_PADDING &&
                              y < ATLAS_TILE_PADDING + ATLAS_TILE_SIZE;
            if (insideTile) continue;
            int sourceX = x - ATLAS_TILE_PADDING;
            if (sourceX < 0) sourceX = 0;
            if (sourceX >= ATLAS_TILE_SIZE) sourceX = ATLAS_TILE_SIZE - 1;
            Color edge = GetImageColor(*image, originX + sourceX,
                                       originY + sourceY);
            ImageDrawPixel(image, cellX + x, cellY + y, edge);
        }
    }
}

void DrawAtlasTile(Image *image, BlockTexture texture)
{
    int tileIndex = (int)texture;
    int cellX = (tileIndex % ATLAS_COLUMNS) * ATLAS_CELL_SIZE;
    int cellY = (tileIndex / ATLAS_COLUMNS) * ATLAS_CELL_SIZE;
    int originX = cellX + ATLAS_TILE_PADDING;
    int originY = cellY + ATLAS_TILE_PADDING;
    DrawAtlasArtwork(image, texture, originX, originY);
    ExtendAtlasTilePadding(image, cellX, cellY, originX, originY);
}

Texture2D LoadBlockAtlas(void)
{
    Image image = GenImageColor(ATLAS_CELL_SIZE * ATLAS_COLUMNS,
                                ATLAS_CELL_SIZE * ATLAS_ROWS, BLANK);
    for (int i = 0; i < TEX_COUNT; i++) {
        DrawAtlasTile(&image, (BlockTexture)i);
    }

    Texture2D texture = LoadTextureFromImage(image);
    if (texture.id != 0) {
        GenTextureMipmaps(&texture);
        SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
        SetTextureFilter(texture, TEXTURE_FILTER_ANISOTROPIC_8X);
        rlTextureParameters(texture.id, RL_TEXTURE_MAG_FILTER,
                            RL_TEXTURE_FILTER_NEAREST);
        SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
    }
    UnloadImage(image);
    return texture;
}

BlockTexture TextureForBlockFace(BlockType type, int face)
{
    if (IsColorBlock(type)) {
        return (BlockTexture)(TEX_COLOR_START + ColorBlockIndex(type));
    }
    const BlockCatalogEntry *entry = BlockCatalogGet(type);
    if (face == 2) return entry->topTexture;
    if (face == 3) return entry->bottomTexture;
    return entry->sideTexture;
}

void AtlasUVs(BlockTexture texture, Vector2 uvs[6])
{
    int tileIndex = (int)texture;
    int tileX = tileIndex % ATLAS_COLUMNS;
    int tileY = tileIndex / ATLAS_COLUMNS;
    float atlasWidth = (float)(ATLAS_CELL_SIZE * ATLAS_COLUMNS);
    float atlasHeight = (float)(ATLAS_CELL_SIZE * ATLAS_ROWS);
    float tileSize = (float)ATLAS_TILE_SIZE;
    float cellSize = (float)ATLAS_CELL_SIZE;
    float padding = (float)ATLAS_TILE_PADDING;
    float inset = 0.25f;
    float u0 = ((float)tileX * cellSize + padding + inset) / atlasWidth;
    float u1 = ((float)tileX * cellSize + padding + tileSize - inset) /
               atlasWidth;
    float v0 = ((float)tileY * cellSize + padding + inset) / atlasHeight;
    float v1 = ((float)tileY * cellSize + padding + tileSize - inset) /
               atlasHeight;

    uvs[0] = (Vector2){ u0, v1 };
    uvs[1] = (Vector2){ u1, v1 };
    uvs[2] = (Vector2){ u1, v0 };
    uvs[3] = (Vector2){ u0, v1 };
    uvs[4] = (Vector2){ u1, v0 };
    uvs[5] = (Vector2){ u0, v0 };
}

Rectangle AtlasSourceRect(BlockTexture texture)
{
    int tileIndex = (int)texture;
    return (Rectangle){
        (float)((tileIndex % ATLAS_COLUMNS) * ATLAS_CELL_SIZE +
                ATLAS_TILE_PADDING),
        (float)((tileIndex / ATLAS_COLUMNS) * ATLAS_CELL_SIZE +
                ATLAS_TILE_PADDING),
        (float)ATLAS_TILE_SIZE,
        (float)ATLAS_TILE_SIZE
    };
}
