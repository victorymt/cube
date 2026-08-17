#include "world/block_atlas_artwork_internal.h"

Color ItemAtlasPixel(BlockTexture texture, int x, int y,
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
