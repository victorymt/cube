#include "world/block_atlas_artwork_internal.h"

static Color ExpansionLayered(Color base, Color band, int y,
                              unsigned int hash, int spacing)
{
    Color color = AtlasColorWithNoise(base, 14, hash);
    if ((y + (int)(hash % 3u)) % spacing == 0) {
        color = AtlasColorWithNoise(band, 8, hash);
    }
    return color;
}

Color BlockExpansionAtlasPixel(BlockTexture texture, int x, int y,
                                unsigned int hash)
{
    switch (texture) {
    case TEX_ANDESITE: {
        Color color = AtlasColorWithNoise((Color){ 105, 108, 109, 255 }, 15,
                                          hash);
        if (hash % 13u == 0u) color = (Color){ 71, 75, 77, 255 };
        if (hash % 19u == 0u) color = (Color){ 145, 145, 139, 255 };
        return color;
    }
    case TEX_DIORITE: {
        Color color = AtlasColorWithNoise((Color){ 165, 166, 162, 255 }, 12,
                                          hash);
        if (hash % 7u == 0u) color = (Color){ 67, 71, 73, 255 };
        if (hash % 11u == 0u) color = (Color){ 220, 216, 205, 255 };
        return color;
    }
    case TEX_RHYOLITE: {
        Color color = AtlasColorWithNoise((Color){ 157, 132, 121, 255 }, 15,
                                          hash);
        if ((x + y + (int)(hash % 4u)) % 11 == 0) {
            color = (Color){ 205, 180, 165, 255 };
        }
        if (hash % 23u == 0u) color = (Color){ 104, 86, 85, 255 };
        return color;
    }
    case TEX_TUFF:
        if (hash % 9u == 0u) return (Color){ 75, 72, 67, 255 };
        if (hash % 13u == 0u) return (Color){ 187, 174, 145, 255 };
        return AtlasColorWithNoise((Color){ 132, 126, 109, 255 }, 20, hash);
    case TEX_SCHIST: {
        int foliation = (x + y * 2 + (int)(hash % 4u)) % 7;
        Color base = foliation < 2 ? (Color){ 66, 70, 66, 255 }
                                   : (Color){ 112, 115, 102, 255 };
        Color color = AtlasColorWithNoise(base, 10, hash);
        if (foliation == 4 && hash % 3u == 0u) {
            color = (Color){ 174, 166, 132, 255 };
        }
        return color;
    }
    case TEX_SLATE:
        return ExpansionLayered((Color){ 69, 79, 85, 255 },
                                (Color){ 42, 51, 57, 255 }, y, hash, 4);
    case TEX_SERPENTINITE: {
        Color color = AtlasColorWithNoise((Color){ 74, 108, 78, 255 }, 15,
                                          hash);
        int vein = (x * 2 + y + (int)(hash % 7u)) % 13;
        if (vein < 2) color = (Color){ 153, 176, 135, 255 };
        if (hash % 31u == 0u) color = (Color){ 36, 63, 49, 255 };
        return color;
    }
    case TEX_DOLOMITE:
        return ExpansionLayered((Color){ 180, 174, 159, 255 },
                                (Color){ 142, 136, 125, 255 }, y, hash, 7);
    case TEX_GYPSUM: {
        Color color = AtlasColorWithNoise((Color){ 222, 216, 205, 255 }, 7,
                                          hash);
        if ((x + y * 3 + (int)(hash % 5u)) % 11 < 2) {
            color = (Color){ 247, 241, 231, 255 };
        }
        if (hash % 29u == 0u) color = (Color){ 183, 173, 168, 255 };
        return color;
    }
    case TEX_TRAVERTINE:
        return ExpansionLayered((Color){ 198, 177, 137, 255 },
                                (Color){ 156, 130, 94, 255 }, y, hash, 5);
    case TEX_BAUXITE: {
        Color color = AtlasColorWithNoise((Color){ 156, 75, 49, 255 }, 20,
                                          hash);
        if (hash % 7u == 0u) color = (Color){ 92, 50, 39, 255 };
        if (hash % 17u == 0u) color = (Color){ 211, 119, 67, 255 };
        return color;
    }
    case TEX_HEMATITE_ORE: {
        Color color = AtlasColorWithNoise((Color){ 95, 94, 91, 255 }, 15,
                                          hash);
        if (hash % 8u == 0u) color = (Color){ 132, 65, 55, 255 };
        if (hash % 27u == 0u) color = (Color){ 192, 91, 66, 255 };
        return color;
    }
    case TEX_MAGNETITE_ORE: {
        Color color = AtlasColorWithNoise((Color){ 86, 90, 91, 255 }, 13,
                                          hash);
        if (hash % 7u == 0u) color = (Color){ 32, 38, 41, 255 };
        if (hash % 29u == 0u) color = (Color){ 132, 139, 141, 255 };
        return color;
    }
    case TEX_PHOSPHATE_ROCK:
        if (hash % 11u == 0u) return (Color){ 184, 172, 120, 255 };
        if (hash % 23u == 0u) return (Color){ 77, 76, 61, 255 };
        return AtlasColorWithNoise((Color){ 141, 132, 99, 255 }, 14, hash);
    case TEX_CHERNOZEM: {
        Color color = AtlasColorWithNoise((Color){ 48, 40, 31, 255 }, 12,
                                          hash);
        if (hash % 13u == 0u) color = (Color){ 84, 67, 44, 255 };
        return color;
    }
    case TEX_TERRA_ROSSA:
        return ExpansionLayered((Color){ 134, 51, 34, 255 },
                                (Color){ 93, 38, 30, 255 }, y, hash, 6);
    case TEX_ALLUVIUM:
        return ExpansionLayered((Color){ 126, 108, 80, 255 },
                                (Color){ 90, 83, 68, 255 }, y, hash, 4);
    case TEX_LEAF_LITTER: {
        int stem = (x + y * 3 + (int)(hash % 7u)) % 11;
        if (stem > 3) return (Color){ 0, 0, 0, 0 };
        Color base = stem == 0 ? (Color){ 162, 103, 44, 255 }
                               : (Color){ 91, 63, 35, 255 };
        return AtlasColorWithNoise(base, 16, hash);
    }
    case TEX_HUMUS:
        if (hash % 9u == 0u) return (Color){ 91, 65, 39, 255 };
        return AtlasColorWithNoise((Color){ 55, 43, 30, 255 }, 11, hash);
    case TEX_COMPOST:
        if (hash % 7u == 0u) return (Color){ 112, 82, 46, 255 };
        if (hash % 17u == 0u) return (Color){ 43, 52, 29, 255 };
        return AtlasColorWithNoise((Color){ 72, 54, 35, 255 }, 14, hash);
    case TEX_SHELL_BED: {
        Color color = AtlasColorWithNoise((Color){ 203, 192, 163, 255 }, 11,
                                          hash);
        if ((x * 2 + y + (int)(hash % 5u)) % 9 < 2) {
            color = (Color){ 239, 226, 196, 255 };
        }
        if (hash % 31u == 0u) color = (Color){ 126, 111, 91, 255 };
        return color;
    }
    case TEX_CORAL_LIMESTONE: {
        Color color = AtlasColorWithNoise((Color){ 196, 181, 154, 255 }, 12,
                                          hash);
        int fossil = (x - 8) * (x - 8) + (y - 8) * (y - 8);
        if ((fossil + (int)(hash % 11u)) % 19 < 3) {
            color = (Color){ 224, 203, 169, 255 };
        }
        return color;
    }
    case TEX_GUANO:
        if (hash % 8u == 0u) return (Color){ 218, 207, 167, 255 };
        if (hash % 19u == 0u) return (Color){ 105, 98, 73, 255 };
        return AtlasColorWithNoise((Color){ 177, 164, 121, 255 }, 18, hash);
    case TEX_CHARRED_WOOD: {
        Color color = AtlasColorWithNoise(
            x % 4 == 0 ? (Color){ 18, 20, 20, 255 }
                       : (Color){ 43, 34, 28, 255 }, 9, hash);
        if (hash % 23u == 0u) color = (Color){ 101, 55, 31, 255 };
        return color;
    }
    case TEX_CHARCOAL:
        if (hash % 7u == 0u) return (Color){ 58, 59, 56, 255 };
        if (hash % 19u == 0u) return (Color){ 11, 14, 15, 255 };
        return AtlasColorWithNoise((Color){ 31, 31, 29, 255 }, 10, hash);
    case TEX_FIRE_ASH:
        if (hash % 9u == 0u) return (Color){ 105, 102, 97, 255 };
        if (hash % 17u == 0u) return (Color){ 196, 191, 181, 255 };
        return AtlasColorWithNoise((Color){ 151, 147, 139, 255 }, 12, hash);
    default:
        return MAGENTA;
    }
}
