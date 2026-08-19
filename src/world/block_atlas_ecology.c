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
    case TEX_OAK_BARK:
        return AtlasColorWithNoise((x % 4 == 0) ?
                                   (Color){ 78, 52, 34, 255 } :
                                   (Color){ 116, 82, 49, 255 }, 12, hash);
    case TEX_OAK_RING: {
        int dx = x - center;
        int dy = y - center;
        int ring = (dx * dx + dy * dy) / 9;
        return AtlasColorWithNoise((ring & 1) ?
                                   (Color){ 145, 102, 57, 255 } :
                                   (Color){ 188, 142, 82, 255 }, 8, hash);
    }
    case TEX_OAK_LEAVES:
        if ((x + y + (int)(hash % 5u)) % 11 == 0) return transparent;
        return AtlasColorWithNoise((Color){ 55, 118, 48, 245 }, 20, hash);
    case TEX_BIRCH_BARK:
        if ((x + y + (int)(hash % 5u)) % 9 == 0) {
            return (Color){ 42, 39, 36, 255 };
        }
        return AtlasColorWithNoise((Color){ 207, 202, 184, 255 }, 10, hash);
    case TEX_BIRCH_RING: {
        int dx = x - center;
        int dy = y - center;
        int ring = (dx * dx + dy * dy) / 8;
        return AtlasColorWithNoise((ring & 1) ?
                                   (Color){ 171, 157, 125, 255 } :
                                   (Color){ 225, 212, 174, 255 }, 7, hash);
    }
    case TEX_BIRCH_LEAVES:
        if ((x * 2 + y + (int)(hash % 4u)) % 10 == 0) return transparent;
        return AtlasColorWithNoise((Color){ 92, 150, 58, 245 }, 18, hash);
    case TEX_ASPEN_BARK:
        if ((x * 3 + y + (int)(hash % 7u)) % 11 == 0) {
            return (Color){ 73, 90, 67, 255 };
        }
        return AtlasColorWithNoise((Color){ 166, 174, 145, 255 }, 12, hash);
    case TEX_ASPEN_RING: {
        int dx = x - center;
        int dy = y - center;
        int ring = (dx * dx + dy * dy) / 10;
        return AtlasColorWithNoise((ring & 1) ?
                                   (Color){ 175, 143, 83, 255 } :
                                   (Color){ 220, 185, 117, 255 }, 8, hash);
    }
    case TEX_ASPEN_LEAVES:
        if ((x + y * 2 + (int)(hash % 5u)) % 12 == 0) return transparent;
        return AtlasColorWithNoise((Color){ 104, 151, 62, 245 }, 18, hash);
    case TEX_SPRUCE_BARK:
        return AtlasColorWithNoise((x % 3 == 0) ?
                                   (Color){ 61, 43, 31, 255 } :
                                   (Color){ 91, 65, 42, 255 }, 10, hash);
    case TEX_SPRUCE_RING: {
        int dx = x - center;
        int dy = y - center;
        int ring = (dx * dx + dy * dy) / 8;
        return AtlasColorWithNoise((ring & 1) ?
                                   (Color){ 122, 77, 44, 255 } :
                                   (Color){ 164, 107, 60, 255 }, 7, hash);
    }
    case TEX_SPRUCE_NEEDLES:
        if ((x + y + (int)(hash % 3u)) % 7 == 0) return transparent;
        return AtlasColorWithNoise((Color){ 35, 82, 53, 245 }, 14, hash);
    case TEX_PINE_BARK:
        return AtlasColorWithNoise((y % 5 == 0) ?
                                   (Color){ 175, 105, 61, 255 } :
                                   (Color){ 128, 73, 43, 255 }, 11, hash);
    case TEX_PINE_RING: {
        int dx = x - center;
        int dy = y - center;
        int ring = (dx * dx + dy * dy) / 10;
        return AtlasColorWithNoise((ring & 1) ?
                                   (Color){ 151, 92, 50, 255 } :
                                   (Color){ 202, 143, 82, 255 }, 8, hash);
    }
    case TEX_PINE_NEEDLES:
        if ((x * 2 + y + (int)(hash % 4u)) % 8 == 0) return transparent;
        return AtlasColorWithNoise((Color){ 48, 104, 59, 245 }, 14, hash);
    case TEX_WILLOW_BARK:
        return AtlasColorWithNoise((x + y) % 6 == 0 ?
                                   (Color){ 91, 83, 57, 255 } :
                                   (Color){ 139, 122, 80, 255 }, 12, hash);
    case TEX_WILLOW_RING: {
        int dx = x - center;
        int dy = y - center;
        int ring = (dx * dx + dy * dy) / 9;
        return AtlasColorWithNoise((ring & 1) ?
                                   (Color){ 164, 128, 70, 255 } :
                                   (Color){ 210, 174, 102, 255 }, 8, hash);
    }
    case TEX_WILLOW_LEAVES:
        if ((x * 3 + y + (int)(hash % 4u)) % 10 == 0) return transparent;
        return AtlasColorWithNoise((Color){ 86, 139, 67, 245 }, 18, hash);
    case TEX_BIG_BLUESTEM:
        if (y < 2 || abs(x - center) > 2 + (ATLAS_TILE_SIZE - y) / 5) {
            return transparent;
        }
        return AtlasColorWithNoise((Color){ 93, 137, 67, 255 }, 16, hash);
    case TEX_BRACKEN:
        if (y < 2 || (abs(x - center) > 1 &&
                      (x + y + (int)(hash % 3u)) % 3 != 0)) {
            return transparent;
        }
        return AtlasColorWithNoise((Color){ 66, 116, 52, 255 }, 15, hash);
    case TEX_COMMON_REED:
        if (!((x >= 5 && x <= 6) || (x >= 10 && x <= 11) ||
              (x == 8 && y > 6))) return transparent;
        return AtlasColorWithNoise((Color){ 124, 148, 72, 255 }, 12, hash);
    case TEX_SPHAGNUM:
        return AtlasColorWithNoise((Color){ 91, 130, 67, 245 }, 16, hash);
    case TEX_HEATHER:
        if (y < 3 || (x + y + (int)(hash % 5u)) % 6 == 0) return transparent;
        return AtlasColorWithNoise((Color){ 103, 111, 61, 255 }, 15, hash);
    case TEX_FIREWEED:
        if (y < 2 || (abs(x - center) > 3 && y < center)) return transparent;
        if ((x + y + (int)(hash % 7u)) % 11 == 0) {
            return (Color){ 219, 75, 111, 255 };
        }
        return AtlasColorWithNoise((Color){ 94, 139, 70, 255 }, 14, hash);
    case TEX_SAGUARO:
        color = AtlasColorWithNoise((Color){ 65, 132, 69, 255 }, 13, hash);
        if (x % 4 == 0) color = (Color){ 43, 105, 53, 255 };
        if (y % 7 == 0 && (x == 2 || x == 13)) {
            color = (Color){ 183, 195, 112, 255 };
        }
        return color;
    default:
        return MAGENTA;
    }
}
