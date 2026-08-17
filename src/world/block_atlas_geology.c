#include "world/block_atlas_artwork_internal.h"

Color GeologyAtlasPixel(BlockTexture texture, int x, int y,
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
