#include "world/block_atlas_artwork_internal.h"

#include <stdbool.h>
#include <stdlib.h>

Color EcologyAtlasPixel(BlockTexture texture, int x, int y,
                         unsigned int hash)
{
    Color transparent = (Color){ 0, 0, 0, 0 };
    Color color = WHITE;
    int center = ATLAS_TILE_SIZE / 2;
    switch (texture) {
    case TEX_TALL_GRASS: {
        int blade = abs(x - center);
        int reach = 2 + (ATLAS_TILE_SIZE - 1 - y) / 5;
        if (y < 2 || blade > reach || ((x + y) % 5 == 0 && blade > 1)) {
            return transparent;
        }
        return AtlasColorWithNoise((Color){ 83, 151, 63, 255 }, 17, hash);
    }
    case TEX_FERN: {
        int stem = abs(x - center);
        int frond = abs(y - center);
        bool leaf = stem <= 1 ||
                    (frond <= 6 && abs(x - center) <= 7 - frond / 2 &&
                     ((x + y) & 1) == 0);
        if (!leaf || y < 2) return transparent;
        color = AtlasColorWithNoise((Color){ 53, 126, 66, 255 }, 16, hash);
        if (stem == 0) color = (Color){ 111, 154, 73, 255 };
        return color;
    }
    case TEX_REED:
        if (!((x >= 5 && x <= 7) || (x >= 10 && x <= 11) ||
              (x == 8 && y > 5))) return transparent;
        color = AtlasColorWithNoise((Color){ 116, 153, 70, 255 }, 12, hash);
        if (y < 4 && x >= 10) color = (Color){ 112, 80, 42, 255 };
        return color;
    case TEX_LICHEN: {
        int dx = x - center;
        int dy = y - center;
        if (dx * dx + dy * dy > 48 || hash % 7u == 0u) return transparent;
        color = AtlasColorWithNoise((Color){ 137, 151, 92, 255 }, 14, hash);
        if ((hash % 11u) == 0u) color = (Color){ 198, 188, 116, 255 };
        return color;
    }
    case TEX_MOSS_CARPET:
        color = AtlasColorWithNoise((Color){ 62, 111, 53, 255 }, 16, hash);
        if ((hash % 9u) == 0u) color = (Color){ 100, 143, 67, 255 };
        return color;
    case TEX_MICROBIAL_MAT:
        color = AtlasColorWithNoise((Color){ 96, 126, 105, 255 }, 13, hash);
        if ((x + y + (int)(hash % 3u)) % 7 < 2) {
            color = (Color){ 72, 151, 137, 255 };
        }
        return color;
    case TEX_MYCELIUM:
        color = AtlasColorWithNoise((Color){ 94, 84, 91, 255 }, 13, hash);
        if ((x * 2 + y + (int)(hash % 5u)) % 9 < 2) {
            color = (Color){ 190, 178, 194, 255 };
        }
        return color;
    case TEX_LIVING_STEM:
        color = AtlasColorWithNoise(
            x % 5 == 0 ? (Color){ 61, 75, 53, 255 }
                       : (Color){ 99, 91, 67, 255 },
            12, hash);
        if ((hash % 17u) == 0u) color = (Color){ 119, 151, 91, 255 };
        return color;
    case TEX_CANOPY_FROND:
        if ((hash % 13u) < 2u) return transparent;
        color = AtlasColorWithNoise((Color){ 63, 137, 101, 245 }, 18, hash);
        if ((x + y) % 9 == 0) color = (Color){ 103, 183, 126, 245 };
        return color;
    case TEX_LUMINOUS_POD: {
        int dx = x - center;
        int dy = y - center;
        if (dx * dx + dy * dy > 50) return transparent;
        color = AtlasColorWithNoise((Color){ 126, 220, 174, 250 }, 10,
                                    hash);
        if (dx * dx + dy * dy < 14) color = (Color){ 221, 255, 196, 255 };
        return color;
    }
    case TEX_FUNGAL_STEM:
        color = AtlasColorWithNoise((Color){ 174, 164, 152, 255 }, 11,
                                    hash);
        if (x % 6 == 0) color = (Color){ 121, 112, 109, 255 };
        return color;
    case TEX_SPORE_CAP: {
        int wave = (int)(hash % 4u);
        color = AtlasColorWithNoise((Color){ 164, 86, 151, 248 }, 17,
                                    hash);
        if ((x + y + wave) % 7 == 0) color = (Color){ 222, 155, 204, 248 };
        if ((hash % 29u) == 0u) color = (Color){ 241, 216, 169, 248 };
        return color;
    }
    case TEX_CRYSTAL_BLOOM: {
        int diagonal = (x * 2 + y + (int)(hash % 5u)) % 10;
        color = AtlasColorWithNoise((Color){ 137, 205, 220, 255 }, 13,
                                    hash);
        if (diagonal < 2) color = (Color){ 218, 245, 245, 255 };
        if ((hash % 23u) == 0u) color = (Color){ 178, 121, 214, 255 };
        return color;
    }
    case TEX_VENT_CHIMNEY:
        color = AtlasColorWithNoise((Color){ 72, 67, 65, 255 }, 18, hash);
        if ((hash % 7u) == 0u) color = (Color){ 35, 37, 39, 255 };
        if (y % 6 == 0) color = (Color){ 111, 93, 72, 255 };
        return color;
    case TEX_CHEMO_MAT:
        color = AtlasColorWithNoise((Color){ 170, 133, 54, 255 }, 15, hash);
        if ((x + y + (int)(hash % 4u)) % 8 < 2) {
            color = (Color){ 104, 128, 65, 255 };
        }
        return color;
    default:
        return MAGENTA;
    }
}
