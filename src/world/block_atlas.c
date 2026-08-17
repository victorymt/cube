#include "world/block_atlas.h"

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

static Color GeologyAtlasPixel(BlockTexture texture, int x, int y,
                               unsigned int hash)
{
    Color color = WHITE;
    switch (texture) {
    case TEX_GRAVEL:
        color = AtlasColorWithNoise((Color){ 112, 108, 104, 255 }, 24,
                                    hash);
        if ((hash % 7u) == 0u) color = (Color){ 78, 76, 74, 255 };
        if ((hash % 11u) == 0u) color = (Color){ 151, 145, 137, 255 };
        return color;
    case TEX_CLAY:
        color = AtlasColorWithNoise((Color){ 151, 164, 170, 255 }, 12,
                                    hash);
        if ((y + (int)(hash % 2u)) % 5 == 0) {
            color = AtlasColorWithNoise((Color){ 128, 143, 151, 255 }, 8,
                                        hash);
        }
        return color;
    case TEX_MUD:
        color = AtlasColorWithNoise((Color){ 91, 68, 48, 255 }, 18, hash);
        if ((hash % 9u) == 0u) color = (Color){ 61, 49, 39, 255 };
        if ((x + y + (int)(hash % 3u)) % 11 == 0) {
            color = (Color){ 119, 91, 62, 255 };
        }
        return color;
    case TEX_MOSSY_STONE:
        color = AtlasColorWithNoise((Color){ 111, 116, 112, 255 }, 18,
                                    hash);
        if ((hash % 5u) == 0u ||
            ((x + y + (int)(hash % 4u)) % 9 == 0)) {
            color = AtlasColorWithNoise((Color){ 78, 111, 62, 255 }, 16,
                                        hash);
        }
        return color;
    case TEX_RED_SAND:
        color = AtlasColorWithNoise((Color){ 184, 96, 54, 255 }, 18, hash);
        if ((y + (int)(hash % 3u)) % 6 == 0) {
            color = AtlasColorWithNoise((Color){ 215, 126, 70, 255 }, 10,
                                        hash);
        }
        return color;
    case TEX_BASALT:
        color = AtlasColorWithNoise(
            (x % 4 == 0) ? (Color){ 42, 44, 49, 255 }
                         : (Color){ 66, 69, 74, 255 },
            13, hash);
        if ((hash % 17u) == 0u) color = (Color){ 91, 91, 94, 255 };
        return color;
    case TEX_COPPER_ORE:
        color = AtlasColorWithNoise((Color){ 116, 120, 122, 255 }, 18,
                                    hash);
        if ((hash % 11u) == 0u) {
            color = AtlasColorWithNoise((Color){ 184, 112, 72, 255 }, 14,
                                        hash);
        }
        if ((hash % 29u) == 0u) color = (Color){ 85, 151, 126, 255 };
        return color;
    case TEX_CRYSTAL: {
        int diagonal = (x + y * 2 + (int)(hash % 5u)) % 9;
        color = AtlasColorWithNoise((Color){ 104, 170, 202, 255 }, 16,
                                    hash);
        if (diagonal < 2) color = (Color){ 181, 229, 239, 255 };
        if ((hash % 13u) == 0u) color = (Color){ 151, 112, 201, 255 };
        return color;
    }
    case TEX_GRANITE:
        color = AtlasColorWithNoise((Color){ 126, 113, 108, 255 }, 17,
                                    hash);
        if ((hash % 7u) == 0u) color = (Color){ 91, 91, 94, 255 };
        if ((hash % 11u) == 0u) color = (Color){ 169, 139, 131, 255 };
        if ((hash % 19u) == 0u) color = (Color){ 218, 211, 199, 255 };
        return color;
    case TEX_LIMESTONE:
        color = AtlasColorWithNoise((Color){ 188, 183, 157, 255 }, 12,
                                    hash);
        if ((y + (int)(hash % 2u)) % 6 == 0) {
            color = AtlasColorWithNoise((Color){ 157, 153, 133, 255 }, 8,
                                        hash);
        }
        if ((hash % 29u) == 0u) color = (Color){ 220, 215, 188, 255 };
        return color;
    case TEX_SHALE:
        color = AtlasColorWithNoise((Color){ 75, 82, 86, 255 }, 12, hash);
        if (y % 4 == 0 || (y + (int)(hash % 3u)) % 7 == 0) {
            color = AtlasColorWithNoise((Color){ 48, 55, 59, 255 }, 7,
                                        hash);
        }
        return color;
    case TEX_MARBLE: {
        int vein = (x * 2 + y + (int)(hash % 7u)) % 13;
        color = AtlasColorWithNoise((Color){ 211, 211, 205, 255 }, 8,
                                    hash);
        if (vein < 2) {
            color = AtlasColorWithNoise((Color){ 137, 145, 151, 255 }, 10,
                                        hash);
        }
        return color;
    }
    case TEX_PEAT:
        color = AtlasColorWithNoise((Color){ 67, 50, 38, 255 }, 15, hash);
        if ((hash % 8u) == 0u) color = (Color){ 91, 72, 48, 255 };
        if ((x + y + (int)(hash % 4u)) % 10 == 0) {
            color = (Color){ 43, 39, 31, 255 };
        }
        return color;
    case TEX_PERMAFROST:
        color = AtlasColorWithNoise((Color){ 126, 137, 139, 255 }, 13,
                                    hash);
        if ((hash % 9u) == 0u) color = (Color){ 175, 190, 194, 255 };
        if ((hash % 17u) == 0u) color = (Color){ 90, 78, 67, 255 };
        return color;
    case TEX_ROCK_SALT:
        color = AtlasColorWithNoise((Color){ 218, 207, 198, 255 }, 9,
                                    hash);
        if ((hash % 11u) == 0u) color = (Color){ 241, 231, 221, 255 };
        if ((x + y) % 9 == 0) color = (Color){ 181, 170, 166, 255 };
        return color;
    case TEX_VOLCANIC_ASH:
        color = AtlasColorWithNoise((Color){ 73, 68, 67, 255 }, 18, hash);
        if ((hash % 7u) == 0u) color = (Color){ 39, 38, 40, 255 };
        if ((hash % 13u) == 0u) color = (Color){ 104, 94, 89, 255 };
        return color;
    case TEX_PUMICE:
        color = AtlasColorWithNoise((Color){ 167, 157, 143, 255 }, 16,
                                    hash);
        if ((hash % 5u) == 0u) color = (Color){ 104, 99, 94, 255 };
        if ((hash % 17u) == 0u) color = (Color){ 205, 196, 180, 255 };
        return color;
    case TEX_SULFUR_ORE:
        color = AtlasColorWithNoise((Color){ 72, 70, 69, 255 }, 17, hash);
        if ((hash % 9u) == 0u) {
            color = AtlasColorWithNoise((Color){ 206, 183, 50, 255 }, 14,
                                        hash);
        }
        if ((hash % 31u) == 0u) color = (Color){ 240, 219, 79, 255 };
        return color;
    case TEX_PACKED_ICE:
        color = AtlasColorWithNoise((Color){ 101, 164, 193, 255 }, 10,
                                    hash);
        if ((x + y + (int)(hash % 5u)) % 11 < 2) {
            color = (Color){ 170, 215, 228, 255 };
        }
        return color;
    case TEX_QUARTZ_ORE:
        color = AtlasColorWithNoise((Color){ 103, 106, 110, 255 }, 17,
                                    hash);
        if ((hash % 9u) == 0u) {
            color = AtlasColorWithNoise((Color){ 205, 196, 205, 255 }, 10,
                                        hash);
        }
        if ((hash % 37u) == 0u) color = (Color){ 239, 231, 240, 255 };
        return color;
    case TEX_LOAM:
        color = AtlasColorWithNoise((Color){ 112, 76, 48, 255 }, 17, hash);
        if ((hash % 9u) == 0u) color = (Color){ 78, 55, 38, 255 };
        if ((hash % 17u) == 0u) color = (Color){ 145, 102, 65, 255 };
        return color;
    case TEX_PODZOL:
        color = AtlasColorWithNoise((Color){ 91, 67, 43, 255 }, 15, hash);
        if ((y + (int)(hash % 3u)) % 6 == 0) {
            color = (Color){ 55, 48, 34, 255 };
        }
        if ((hash % 13u) == 0u) color = (Color){ 126, 88, 48, 255 };
        return color;
    case TEX_SILT:
        color = AtlasColorWithNoise((Color){ 132, 121, 103, 255 }, 11,
                                    hash);
        if ((y + (int)(hash % 2u)) % 5 == 0) {
            color = (Color){ 108, 101, 88, 255 };
        }
        return color;
    case TEX_CHALK:
        color = AtlasColorWithNoise((Color){ 218, 216, 199, 255 }, 7,
                                    hash);
        if ((hash % 19u) == 0u) color = (Color){ 185, 188, 177, 255 };
        if ((x + y + (int)(hash % 5u)) % 13 == 0) {
            color = (Color){ 239, 236, 217, 255 };
        }
        return color;
    case TEX_GNEISS: {
        int band = (x + y * 2 + (int)(hash % 4u)) % 8;
        color = AtlasColorWithNoise((Color){ 116, 112, 119, 255 }, 12,
                                    hash);
        if (band < 2) color = (Color){ 72, 72, 79, 255 };
        if (band == 4) color = (Color){ 168, 161, 159, 255 };
        return color;
    }
    case TEX_LATERITE:
        color = AtlasColorWithNoise((Color){ 154, 74, 45, 255 }, 18, hash);
        if ((hash % 7u) == 0u) color = (Color){ 102, 54, 39, 255 };
        if ((hash % 17u) == 0u) color = (Color){ 197, 105, 57, 255 };
        return color;
    case TEX_SCORIA:
        color = AtlasColorWithNoise((Color){ 73, 48, 45, 255 }, 17, hash);
        if ((hash % 5u) == 0u) color = (Color){ 31, 29, 31, 255 };
        if ((hash % 13u) == 0u) color = (Color){ 119, 60, 45, 255 };
        return color;
    case TEX_REGOLITH:
        color = AtlasColorWithNoise((Color){ 143, 136, 127, 255 }, 16,
                                    hash);
        if ((hash % 11u) == 0u) color = (Color){ 102, 99, 96, 255 };
        if ((hash % 23u) == 0u) color = (Color){ 180, 169, 153, 255 };
        return color;
    case TEX_SALT_CRUST:
        color = AtlasColorWithNoise((Color){ 232, 224, 207, 255 }, 7,
                                    hash);
        if (x % 7 == 0 || y % 7 == 0) color = (Color){ 188, 178, 166, 255 };
        if ((hash % 17u) == 0u) color = (Color){ 248, 243, 229, 255 };
        return color;
    case TEX_TIN_ORE:
    case TEX_SILVER_ORE:
    case TEX_NICKEL_ORE: {
        Color ore = texture == TEX_TIN_ORE
            ? (Color){ 166, 174, 176, 255 }
            : (texture == TEX_SILVER_ORE
                   ? (Color){ 196, 202, 207, 255 }
                   : (Color){ 151, 166, 132, 255 });
        color = AtlasColorWithNoise((Color){ 104, 108, 111, 255 }, 17,
                                    hash);
        if ((hash % 9u) == 0u) color = AtlasColorWithNoise(ore, 10, hash);
        if ((hash % 31u) == 0u) color = ore;
        return color;
    }
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
