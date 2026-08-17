#include "world/block_atlas_artwork_internal.h"

#include <stdbool.h>

Color SpaceAtlasPixel(BlockTexture texture, int x, int y,
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
