#include "world/block_atlas_artwork_internal.h"

Color ItemAtlasPixel(BlockTexture texture, int x, int y,
                     unsigned int hash)
{
    Color color = WHITE;
    switch (texture) {
    /* Seamless 16-pixel architectural motifs; lattice holes use cutout alpha. */
    case TEX_BLUE_BRICK: {
        int joint = (x + ((y / 4) % 2) * 4) % 8;
        Color base = y % 4 == 0 || joint == 0
            ? (Color){ 57, 65, 67, 255 } : (Color){ 91, 103, 108, 255 };
        if (y % 4 == 1 && joint != 0) base = (Color){ 108, 119, 122, 255 };
        return AtlasColorWithNoise(base, 5, hash);
    }
    case TEX_BLUE_ROOF_TILE: {
        static const unsigned char ribs[8] = { 35, 49, 67, 81, 86, 76, 60, 43 };
        int shade = ribs[x % 8] - (y % 8 == 0 ? 17 : 0);
        return AtlasColorWithNoise((Color){ shade, shade + 12, shade + 18, 255 },
                                   3, hash);
    }
    case TEX_RED_LACQUER_WOOD: {
        Color base = (x == 3 || x == 11) && y % 7 != 0
            ? (Color){ 123, 33, 26, 255 } : (Color){ 143, 42, 32, 255 };
        return AtlasColorWithNoise(base, 3, hash);
    }
    case TEX_WHITE_PLASTER:
        return AtlasColorWithNoise((Color){ 230, 225, 208, 255 }, 4, hash);
    case TEX_DRESSED_STONE: {
        bool seam = y % 8 == 0 || (x + ((y / 8) % 2) * 8) % 16 == 0;
        Color base = seam ? (Color){ 122, 129, 125, 255 }
                          : (Color){ 174, 178, 170, 255 };
        if (!seam && y % 8 == 1) base = (Color){ 195, 198, 187, 255 };
        return AtlasColorWithNoise(base, 5, hash);
    }
    case TEX_WOOD_LATTICE: {
        bool frame = x < 2 || x >= 14 || y < 2 || y >= 14;
        bool bars = x % 6 < 2 || y % 6 < 2;
        if (!frame && !bars) return (Color){ 0, 0, 0, 0 };
        Color base = x % 6 == 0 || y % 6 == 0
            ? (Color){ 120, 80, 45, 255 } : (Color){ 79, 48, 29, 255 };
        return AtlasColorWithNoise(base, 3, hash);
    }
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
        if ((x == 7 || x == 8) && y >= 7 && y <= 15) {
            color = AtlasColorWithNoise((Color){ 52, 119, 48, 255 }, 6,
                                        hash);
        }
        if ((y == 10 && x >= 4 && x <= 7) ||
            (y == 11 && x >= 5 && x <= 7) ||
            (y == 12 && x >= 8 && x <= 11) ||
            (y == 13 && x >= 8 && x <= 10)) {
            color = AtlasColorWithNoise((Color){ 76, 139, 57, 255 }, 7,
                                        hash);
        }
        static const int petals[5][2] = {
            { 8, 2 }, { 5, 5 }, { 11, 5 }, { 6, 7 }, { 10, 7 }
        };
        for (int petal = 0; petal < 5; petal++) {
            int dx = x - petals[petal][0];
            int dy = y - petals[petal][1];
            if (dx * dx + dy * dy <= 3) {
                color = AtlasColorWithNoise(
                    petal & 1 ? (Color){ 190, 42, 48, 255 }
                              : (Color){ 222, 65, 58, 255 },
                    7, hash);
            }
        }
        if ((x - 8) * (x - 8) + (y - 5) * (y - 5) <= 2) {
            color = AtlasColorWithNoise((Color){ 242, 198, 65, 255 }, 5,
                                        hash);
        }
        return color;
    case TEX_MUSHROOM:
        color = (Color){ 0, 0, 0, 0 };
        if (y >= 8 && y <= 15 && x >= 6 && x <= 9 &&
            !(y >= 13 && (x == 6 || x == 9))) {
            color = AtlasColorWithNoise(
                x <= 7 ? (Color){ 215, 205, 181, 255 }
                       : (Color){ 175, 161, 141, 255 },
                5, hash);
        }
        {
            int dx = x - 8;
            int dy = y - 7;
            bool cap = y >= 3 && y <= 9 && dx * dx * 2 + dy * dy * 3 <= 55;
            if (cap) {
                color = AtlasColorWithNoise(
                    y >= 8 ? (Color){ 116, 45, 36, 255 }
                           : (Color){ 178, 49, 42, 255 },
                    8, hash);
                if ((x == 5 && y == 6) || (x == 8 && y == 4) ||
                    (x == 10 && y == 6) || (x == 7 && y == 7)) {
                    color = (Color){ 231, 218, 190, 255 };
                }
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
