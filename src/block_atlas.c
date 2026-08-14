#include "block_atlas.h"

#include "raymath.h"
#include "rlgl.h"
#include "world.h"

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

static Color AtlasColorWithNoise(Color base, int amount, unsigned int hash)
{
    int delta = (int)(hash % (unsigned int)(amount * 2 + 1)) - amount;
    return (Color){
        (unsigned char)Clamp((float)((int)base.r + delta), 0.0f, 255.0f),
        (unsigned char)Clamp((float)((int)base.g + delta), 0.0f, 255.0f),
        (unsigned char)Clamp((float)((int)base.b + delta), 0.0f, 255.0f),
        base.a
    };
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
    case TEX_LEAVES: {
        Color color = AtlasColorWithNoise((Color){ 46, 128, 55, 255 }, 24,
                                          hash);
        if ((hash % 7u) == 0u) color = (Color){ 31, 95, 43, 255 };
        if ((x + y + (int)(hash % 4u)) % 9 == 0) {
            color = (Color){ 82, 158, 68, 255 };
        }
        return color;
    }
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
        color = AtlasColorWithNoise(
            (x % 3 == 0) ? (Color){ 52, 122, 54, 255 }
                         : (Color){ 78, 152, 62, 255 },
            14, hash);
        if ((y % 6) == 0) {
            color = AtlasColorWithNoise((Color){ 148, 196, 92, 255 }, 10,
                                        hash);
        }
        return color;
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

static Color ItemAtlasPixel(BlockTexture texture, int x, int y,
                            unsigned int hash)
{
    Color color = WHITE;
    switch (texture) {
    case TEX_TORCH:
        if (y < 3) {
            color = AtlasColorWithNoise((Color){ 255, 186, 62, 255 }, 22,
                                        hash);
            if ((hash % 5u) == 0u) color = (Color){ 255, 236, 130, 255 };
        } else if (y < 6) {
            color = AtlasColorWithNoise((Color){ 226, 110, 36, 255 }, 20,
                                        hash);
        } else {
            color = AtlasColorWithNoise(
                (x % 4 == 0 || y % 5 == 0)
                    ? (Color){ 92, 60, 32, 255 }
                    : (Color){ 128, 82, 42, 255 },
                10, hash);
        }
        return color;
    case TEX_ALBUM:
        if (x == 0 || x == ATLAS_TILE_SIZE - 1 || y == 0 ||
            y == ATLAS_TILE_SIZE - 1 || x == 1 ||
            x == ATLAS_TILE_SIZE - 2) {
            color = AtlasColorWithNoise((Color){ 150, 112, 52, 255 }, 12,
                                        hash);
            if ((hash % 9u) == 0u) color = (Color){ 196, 156, 70, 255 };
        } else if (x >= 4 && x <= 11 && y >= 4 && y <= 11) {
            if (y == 7 || y == 8) {
                color = AtlasColorWithNoise((Color){ 74, 52, 30, 255 }, 8,
                                            hash);
            } else {
                color = AtlasColorWithNoise((Color){ 206, 196, 176, 255 }, 12,
                                            hash);
            }
        } else {
            color = AtlasColorWithNoise((Color){ 118, 76, 42, 255 }, 14,
                                        hash);
            if ((hash % 13u) == 0u) {
                color = AtlasColorWithNoise((Color){ 150, 100, 56, 255 }, 8,
                                            hash);
            }
        }
        return color;
    case TEX_DOOR:
        if (x % 4 == 0 || x == ATLAS_TILE_SIZE - 1) {
            color = AtlasColorWithNoise((Color){ 104, 66, 32, 255 }, 10,
                                        hash);
        } else if (y == 5 || y == 6 || y == 10 || y == 11) {
            color = AtlasColorWithNoise((Color){ 122, 80, 40, 255 }, 12,
                                        hash);
        } else {
            color = AtlasColorWithNoise((Color){ 156, 104, 52, 255 }, 12,
                                        hash);
        }
        if (x == 12 && y == 8) {
            color = AtlasColorWithNoise((Color){ 216, 190, 96, 255 }, 8,
                                        hash);
        }
        if (x == 12 && (y == 7 || y == 9)) {
            color = AtlasColorWithNoise((Color){ 96, 62, 30, 255 }, 6,
                                        hash);
        }
        return color;
    case TEX_FENCE:
        if (x == 7 || x == 8) {
            color = AtlasColorWithNoise((Color){ 128, 82, 42, 255 }, 10,
                                        hash);
            if ((hash % 9u) == 0u) {
                color = AtlasColorWithNoise((Color){ 156, 104, 54, 255 }, 6,
                                            hash);
            }
        } else if (y == 7 || y == 8) {
            color = AtlasColorWithNoise((Color){ 138, 90, 46, 255 }, 10,
                                        hash);
            if ((hash % 11u) == 0u) {
                color = AtlasColorWithNoise((Color){ 166, 112, 58, 255 }, 6,
                                            hash);
            }
        } else {
            color = AtlasColorWithNoise((Color){ 150, 98, 50, 255 }, 10,
                                        hash);
            if ((hash % 17u) == 0u) color = (Color){ 110, 70, 36, 255 };
        }
        return color;
    case TEX_LAVA:
        color = AtlasColorWithNoise((Color){ 224, 96, 24, 255 }, 22, hash);
        if ((hash % 7u) == 0u) {
            color = AtlasColorWithNoise((Color){ 255, 196, 48, 255 }, 14,
                                        hash);
        }
        if ((hash % 11u) == 0u) {
            color = AtlasColorWithNoise((Color){ 168, 44, 12, 255 }, 12,
                                        hash);
        }
        if ((hash % 19u) == 0u) {
            color = AtlasColorWithNoise((Color){ 255, 140, 40, 255 }, 10,
                                        hash);
        }
        return color;
    case TEX_FLOWER:
        color = (Color){ 0, 0, 0, 0 };
        if (x >= 7 && x <= 8 && y >= 10 && y <= 14) {
            color = AtlasColorWithNoise((Color){ 62, 148, 54, 255 }, 10,
                                        hash);
        }
        if (x >= 5 && x <= 10 && y >= 6 && y <= 9) {
            color = AtlasColorWithNoise((Color){ 208, 62, 54, 255 }, 14,
                                        hash);
        }
        if (x >= 7 && x <= 8 && y >= 7 && y <= 8) {
            color = AtlasColorWithNoise((Color){ 250, 224, 96, 255 }, 10,
                                        hash);
        }
        return color;
    case TEX_MUSHROOM:
        color = (Color){ 0, 0, 0, 0 };
        if (x >= 7 && x <= 8 && y >= 12 && y <= 14) {
            color = AtlasColorWithNoise((Color){ 226, 224, 216, 255 }, 8,
                                        hash);
        }
        if (x >= 4 && x <= 11 && y >= 5 && y <= 11) {
            color = AtlasColorWithNoise((Color){ 196, 52, 46, 255 }, 14,
                                        hash);
            if (((x + y) % 5) == 0) {
                color = AtlasColorWithNoise((Color){ 240, 238, 230, 255 }, 8,
                                            hash);
            }
        }
        return color;
    case TEX_BOOKSHELF:
        if (y == 0 || y == 15 || x == 0 || x == 15) {
            color = AtlasColorWithNoise((Color){ 148, 96, 48, 255 }, 10,
                                        hash);
        } else if (x % 3 == 1) {
            color = AtlasColorWithNoise((Color){ 118, 76, 40, 255 }, 10,
                                        hash);
        } else {
            color = AtlasColorWithNoise((Color){ 84, 54, 30, 255 }, 10,
                                        hash);
            if ((hash % 9u) == 0u) {
                color = AtlasColorWithNoise((Color){ 158, 90, 60, 255 }, 10,
                                            hash);
            }
            if ((hash % 13u) == 0u) {
                color = AtlasColorWithNoise((Color){ 64, 110, 150, 255 }, 10,
                                            hash);
            }
            if ((hash % 17u) == 0u) {
                color = AtlasColorWithNoise((Color){ 140, 150, 60, 255 }, 10,
                                            hash);
            }
        }
        return color;
    case TEX_HAY:
        color = AtlasColorWithNoise(
            (y % 4 == 0) ? (Color){ 176, 132, 44, 255 }
                         : (Color){ 218, 172, 66, 255 },
            14, hash);
        if ((hash % 11u) == 0u) {
            color = AtlasColorWithNoise((Color){ 236, 196, 92, 255 }, 8,
                                        hash);
        }
        return color;
    case TEX_PUMPKIN:
        if (x >= 7 && x <= 8 && y <= 2) {
            color = AtlasColorWithNoise((Color){ 96, 128, 52, 255 }, 10,
                                        hash);
        } else {
            color = AtlasColorWithNoise((Color){ 224, 138, 42, 255 }, 16,
                                        hash);
            if ((hash % 9u) == 0u) {
                color = AtlasColorWithNoise((Color){ 246, 168, 62, 255 }, 10,
                                            hash);
            }
            if ((hash % 15u) == 0u) {
                color = AtlasColorWithNoise((Color){ 182, 98, 26, 255 }, 10,
                                            hash);
            }
        }
        return color;
    default:
        return MAGENTA;
    }
}

static Color SpaceAtlasPixel(BlockTexture texture, int x, int y,
                             unsigned int hash)
{
    Color color = WHITE;
    switch (texture) {
    case TEX_MOON_ROCK:
        color = AtlasColorWithNoise((Color){ 138, 142, 148, 255 }, 14,
                                    hash);
        if ((hash % 13u) == 0u) {
            color = AtlasColorWithNoise((Color){ 164, 168, 174, 255 }, 8,
                                        hash);
        }
        if ((hash % 23u) == 0u) color = (Color){ 104, 108, 114, 255 };
        return color;
    case TEX_METEORITE:
        color = AtlasColorWithNoise((Color){ 92, 78, 70, 255 }, 16, hash);
        if ((hash % 11u) == 0u) {
            color = AtlasColorWithNoise((Color){ 150, 130, 90, 255 }, 12,
                                        hash);
        }
        if ((hash % 17u) == 0u) {
            color = AtlasColorWithNoise((Color){ 60, 52, 48, 255 }, 10,
                                        hash);
        }
        return color;
    case TEX_MOON_SAND:
        color = AtlasColorWithNoise((Color){ 190, 186, 176, 255 }, 12,
                                    hash);
        if ((hash % 15u) == 0u) {
            color = AtlasColorWithNoise((Color){ 164, 158, 146, 255 }, 8,
                                        hash);
        }
        if ((hash % 29u) == 0u) {
            color = AtlasColorWithNoise((Color){ 210, 208, 200, 255 }, 6,
                                        hash);
        }
        return color;
    case TEX_STAR_MATTER:
        color = AtlasColorWithNoise((Color){ 238, 236, 222, 255 }, 8, hash);
        if ((hash % 7u) == 0u) {
            color = AtlasColorWithNoise((Color){ 255, 240, 150, 255 }, 10,
                                        hash);
        }
        if ((hash % 13u) == 0u) {
            color = AtlasColorWithNoise((Color){ 190, 210, 245, 255 }, 8,
                                        hash);
        }
        if ((hash % 31u) == 0u) color = (Color){ 255, 255, 235, 255 };
        return color;
    case TEX_SPACESHIP: {
        color = AtlasColorWithNoise((Color){ 50, 57, 66, 255 }, 7, hash);
        if (x == 0 || y == 0 || x == ATLAS_TILE_SIZE - 1 ||
            y == ATLAS_TILE_SIZE - 1) {
            color = AtlasColorWithNoise((Color){ 24, 29, 36, 255 }, 5,
                                        hash);
        }
        if ((x == 2 || x == 13) && (y == 2 || y == 13)) {
            color = (Color){ 224, 166, 62, 255 };
        }
        bool hull = y >= 2 && y <= 13 && x >= 6 && x <= 9;
        bool nose = (y == 2 && x >= 7 && x <= 8) ||
                    (y == 3 && x >= 6 && x <= 9);
        bool leftWing = y >= 7 && y <= 11 && x >= y - 6 && x <= 6;
        bool rightWing = y >= 7 && y <= 11 && x <= 21 - y && x >= 9;
        if (hull || nose || leftWing || rightWing) {
            color = AtlasColorWithNoise((Color){ 202, 211, 220, 255 }, 7,
                                        hash);
            if (x == 6 || x == 9) {
                color = AtlasColorWithNoise((Color){ 132, 144, 158, 255 }, 5,
                                            hash);
            }
        }
        if (y >= 5 && y <= 7 && x >= 7 && x <= 8) {
            color = AtlasColorWithNoise((Color){ 64, 137, 190, 255 }, 7,
                                        hash);
            if (y == 5) color = (Color){ 126, 202, 235, 255 };
        }
        if (y >= 9 && y <= 11 && (x == 5 || x == 10)) {
            color = AtlasColorWithNoise((Color){ 224, 134, 42, 255 }, 5,
                                        hash);
        }
        if (y >= 13 && (x == 6 || x == 9)) {
            color = AtlasColorWithNoise((Color){ 255, 174, 58, 255 }, 5,
                                        hash);
        } else if ((hash % 29u) == 0u && !hull && !nose && !leftWing &&
                   !rightWing) {
            color = (Color){ 75, 84, 96, 255 };
        }
        return color;
    }
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
    if (texture >= TEX_COLOR_START && texture < TEX_COUNT) {
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

    switch (type) {
    case BLOCK_GRASS:
        if (face == 2) return TEX_GRASS_TOP;
        if (face == 3) return TEX_DIRT;
        return TEX_GRASS_SIDE;
    case BLOCK_DIRT: return TEX_DIRT;
    case BLOCK_STONE: return TEX_STONE;
    case BLOCK_WOOD:
        if (face == 2 || face == 3) return TEX_WOOD_TOP;
        return TEX_WOOD_SIDE;
    case BLOCK_SAND: return TEX_SAND;
    case BLOCK_LEAVES: return TEX_LEAVES;
    case BLOCK_RED: return TEX_RED;
    case BLOCK_ORANGE: return TEX_ORANGE;
    case BLOCK_YELLOW: return TEX_YELLOW;
    case BLOCK_BLUE: return TEX_BLUE;
    case BLOCK_PURPLE: return TEX_PURPLE;
    case BLOCK_GREEN: return TEX_GREEN;
    case BLOCK_CYAN: return TEX_CYAN;
    case BLOCK_PINK: return TEX_PINK;
    case BLOCK_WHITE: return TEX_WHITE;
    case BLOCK_GRAY: return TEX_GRAY;
    case BLOCK_BLACK: return TEX_BLACK;
    case BLOCK_PLANK: return TEX_PLANK;
    case BLOCK_BRICK: return TEX_BRICK;
    case BLOCK_GLASS: return TEX_GLASS;
    case BLOCK_WATER: return TEX_WATER;
    case BLOCK_SNOW: return TEX_SNOW;
    case BLOCK_ICE: return TEX_ICE;
    case BLOCK_CACTUS: return TEX_CACTUS;
    case BLOCK_BEDROCK: return TEX_BEDROCK;
    case BLOCK_COAL_ORE: return TEX_COAL_ORE;
    case BLOCK_IRON_ORE: return TEX_IRON_ORE;
    case BLOCK_GOLD_ORE: return TEX_GOLD_ORE;
    case BLOCK_DIAMOND_ORE: return TEX_DIAMOND_ORE;
    case BLOCK_TORCH: return TEX_TORCH;
    case BLOCK_ALBUM: return TEX_ALBUM;
    case BLOCK_SLAB: return TEX_STONE;
    case BLOCK_DOOR:
    case BLOCK_DOOR_OPEN: return TEX_DOOR;
    case BLOCK_MOON_ROCK: return TEX_MOON_ROCK;
    case BLOCK_METEORITE: return TEX_METEORITE;
    case BLOCK_MOON_SAND: return TEX_MOON_SAND;
    case BLOCK_STAR_MATTER: return TEX_STAR_MATTER;
    case BLOCK_SPACESHIP: return TEX_SPACESHIP;
    case BLOCK_STONE_STAIRS: return TEX_STONE;
    case BLOCK_WOOD_STAIRS: return TEX_PLANK;
    case BLOCK_FENCE:
    case BLOCK_FENCE_GATE:
    case BLOCK_FENCE_GATE_OPEN: return TEX_FENCE;
    case BLOCK_GLASS_PANE: return TEX_GLASS;
    case BLOCK_LAVA: return TEX_LAVA;
    case BLOCK_FLOWER: return TEX_FLOWER;
    case BLOCK_MUSHROOM: return TEX_MUSHROOM;
    case BLOCK_BOOKSHELF: return TEX_BOOKSHELF;
    case BLOCK_HAY_BALE: return TEX_HAY;
    case BLOCK_PUMPKIN: return TEX_PUMPKIN;
    case BLOCK_NETHERRACK: return TEX_NETHERRACK;
    case BLOCK_SOUL_SAND: return TEX_SOUL_SAND;
    case BLOCK_GLOWSTONE: return TEX_GLOWSTONE;
    case BLOCK_STONE_BRICKS: return TEX_STONE_BRICKS;
    case BLOCK_SANDSTONE: return TEX_SANDSTONE;
    case BLOCK_OBSIDIAN: return TEX_OBSIDIAN;
    case BLOCK_NETHER_PORTAL: return TEX_NETHER_PORTAL;
    case BLOCK_SPACESHIP_CORE_NORTH:
    case BLOCK_SPACESHIP_CORE_EAST:
    case BLOCK_SPACESHIP_CORE_SOUTH:
    case BLOCK_SPACESHIP_CORE_WEST:
    case BLOCK_SPACESHIP_OCCUPIED: return TEX_SPACESHIP;
    default: return TEX_DIRT;
    }
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
