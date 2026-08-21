#include "world/block_atlas_artwork_internal.h"

#include <math.h>
#include <stdbool.h>

static Color TransparentPixel(void)
{
    return (Color){ 0, 0, 0, 0 };
}

static bool PixelInEllipse(int x, int y, float cx, float cy,
                           float radiusX, float radiusY)
{
    float dx = ((float)x + 0.5f - cx) / radiusX;
    float dy = ((float)y + 0.5f - cy) / radiusY;
    return dx * dx + dy * dy <= 1.0f;
}

static bool PixelNearSegment(int x, int y, float x0, float y0,
                             float x1, float y1, float radius)
{
    float px = (float)x + 0.5f;
    float py = (float)y + 0.5f;
    float vx = x1 - x0;
    float vy = y1 - y0;
    float lengthSquared = vx * vx + vy * vy;
    float projection = lengthSquared > 0.0f
        ? ((px - x0) * vx + (py - y0) * vy) / lengthSquared
        : 0.0f;
    if (projection < 0.0f) projection = 0.0f;
    if (projection > 1.0f) projection = 1.0f;
    float dx = px - (x0 + projection * vx);
    float dy = py - (y0 + projection * vy);
    return dx * dx + dy * dy <= radius * radius;
}

static Color OrganicColor(Color base, int noise, unsigned int hash)
{
    return AtlasColorWithNoise(base, noise, hash);
}

static Color TallGrassPixel(int x, int y, unsigned int hash)
{
    static const float blades[][4] = {
        { 7.5f, 15.8f, 2.4f, 4.0f }, { 7.5f, 15.8f, 5.1f, 1.5f },
        { 7.5f, 15.8f, 7.7f, 2.6f }, { 7.5f, 15.8f, 10.2f, 1.0f },
        { 7.5f, 15.8f, 13.2f, 4.5f }, { 7.5f, 15.8f, 4.0f, 8.0f },
        { 7.5f, 15.8f, 11.8f, 8.2f }
    };
    for (int index = 0; index < 7; index++) {
        if (!PixelNearSegment(x, y, blades[index][0], blades[index][1],
                              blades[index][2], blades[index][3], 0.54f)) {
            continue;
        }
        Color base = index % 3 == 0 ? (Color){ 56, 105, 43, 255 }
                     : index % 3 == 1 ? (Color){ 91, 143, 58, 255 }
                                      : (Color){ 119, 157, 69, 255 };
        return OrganicColor(base, 8, hash);
    }
    return TransparentPixel();
}

static Color FernPixel(int x, int y, unsigned int hash, bool bracken)
{
    float center = 7.5f;
    if (PixelNearSegment(x, y, center, 15.8f, center, 2.4f, 0.48f)) {
        return OrganicColor(bracken ? (Color){ 92, 106, 48, 255 }
                                    : (Color){ 115, 143, 68, 255 }, 6, hash);
    }
    static const float levels[] = { 4.5f, 6.6f, 8.7f, 10.8f, 12.9f };
    static const float reaches[] = { 2.3f, 3.8f, 5.3f, 6.1f, 5.0f };
    for (int level = 0; level < 5; level++) {
        float outerY = levels[level] - (bracken ? 0.4f : 1.3f);
        float radius = bracken ? 0.72f : 0.58f;
        bool left = PixelNearSegment(x, y, center, levels[level],
                                     center - reaches[level], outerY, radius);
        bool right = PixelNearSegment(x, y, center, levels[level] + 0.3f,
                                      center + reaches[level], outerY, radius);
        if (left || right) {
            Color base = bracken
                ? (level < 2 ? (Color){ 114, 129, 55, 255 }
                             : (Color){ 57, 101, 43, 255 })
                : (level < 2 ? (Color){ 81, 142, 66, 255 }
                             : (Color){ 43, 108, 58, 255 });
            if (((x + y + level) & 3) == 0) {
                base = bracken ? (Color){ 151, 139, 66, 255 }
                               : (Color){ 112, 165, 81, 255 };
            }
            return OrganicColor(base, 7, hash);
        }
    }
    return TransparentPixel();
}

static Color ReedPixel(int x, int y, unsigned int hash, bool commonReed)
{
    static const float stems[][4] = {
        { 4.2f, 15.8f, 3.8f, 3.4f }, { 7.0f, 15.8f, 7.3f, 1.8f },
        { 9.5f, 15.8f, 9.0f, 4.1f }, { 12.0f, 15.8f, 12.4f, 2.8f }
    };
    for (int index = 0; index < 4; index++) {
        if (PixelNearSegment(x, y, stems[index][0], stems[index][1],
                             stems[index][2], stems[index][3], 0.46f)) {
            return OrganicColor(index & 1 ? (Color){ 130, 150, 73, 255 }
                                          : (Color){ 83, 125, 58, 255 },
                                6, hash);
        }
        float headY = stems[index][3] - 1.0f;
        if (commonReed && PixelInEllipse(x, y, stems[index][2], headY,
                                         1.15f, 2.1f)) {
            Color plume = index & 1 ? (Color){ 152, 114, 72, 255 }
                                    : (Color){ 118, 83, 54, 255 };
            return OrganicColor(plume, 8, hash);
        }
        if (!commonReed && index != 2 &&
            PixelInEllipse(x, y, stems[index][2], headY, 0.72f, 1.7f)) {
            return OrganicColor((Color){ 104, 71, 43, 255 }, 6, hash);
        }
    }
    static const float leaves[][4] = {
        { 4.3f, 11.8f, 1.4f, 7.7f }, { 7.1f, 12.4f, 10.4f, 7.2f },
        { 9.4f, 13.2f, 6.2f, 8.5f }, { 12.0f, 11.0f, 14.3f, 6.8f }
    };
    for (int index = 0; index < 4; index++) {
        if (PixelNearSegment(x, y, leaves[index][0], leaves[index][1],
                             leaves[index][2], leaves[index][3], 0.52f)) {
            return OrganicColor((Color){ 89, 125, 54, 255 }, 7, hash);
        }
    }
    return TransparentPixel();
}

static Color LichenPixel(int x, int y, unsigned int hash)
{
    static const float branches[][4] = {
        { 7.5f, 15.8f, 7.5f, 7.0f }, { 7.5f, 12.5f, 3.2f, 7.8f },
        { 7.5f, 11.0f, 11.7f, 5.8f }, { 5.2f, 10.0f, 2.6f, 5.1f },
        { 9.8f, 8.2f, 13.2f, 4.0f }, { 7.5f, 8.0f, 5.5f, 3.8f }
    };
    for (int index = 0; index < 6; index++) {
        if (PixelNearSegment(x, y, branches[index][0], branches[index][1],
                             branches[index][2], branches[index][3], 0.72f)) {
            Color base = index & 1 ? (Color){ 174, 178, 116, 255 }
                                   : (Color){ 112, 139, 92, 255 };
            if ((hash % 7u) == 0u) base = (Color){ 204, 195, 137, 255 };
            return OrganicColor(base, 6, hash);
        }
    }
    return TransparentPixel();
}

static Color GroundCoverPixel(BlockTexture texture, int x, int y,
                              unsigned int hash)
{
    int pattern = (x * 5 + y * 7 + (int)(hash % 11u)) % 13;
    switch (texture) {
    case TEX_MOSS_CARPET: {
        Color base = pattern < 3 ? (Color){ 105, 139, 66, 255 }
                   : pattern < 8 ? (Color){ 52, 99, 48, 255 }
                                 : (Color){ 72, 116, 50, 255 };
        if ((x % 5 == 2 && y % 5 == 2) || (x + y) % 11 == 0) {
            base = (Color){ 135, 158, 76, 255 };
        }
        return OrganicColor(base, 5, hash);
    }
    case TEX_MICROBIAL_MAT: {
        Color base = pattern < 4 ? (Color){ 47, 116, 105, 255 }
                   : pattern < 8 ? (Color){ 86, 127, 91, 255 }
                                 : (Color){ 123, 143, 84, 255 };
        if ((x - 4) * (x - 4) + (y - 11) * (y - 11) < 6 ||
            (x - 12) * (x - 12) + (y - 4) * (y - 4) < 5) {
            base = (Color){ 169, 132, 77, 255 };
        }
        return OrganicColor(base, 4, hash);
    }
    case TEX_MYCELIUM: {
        bool hypha = ((x + y * 2) % 9 == 0) || ((x * 3 - y + 32) % 11 == 0);
        Color base = hypha ? (Color){ 205, 195, 202, 255 }
                           : (pattern < 6 ? (Color){ 88, 77, 81, 255 }
                                          : (Color){ 117, 101, 105, 255 });
        return OrganicColor(base, hypha ? 3 : 6, hash);
    }
    case TEX_SPHAGNUM: {
        bool tip = (x % 4 == 1 && y % 4 == 1) ||
                   (x % 5 == 3 && y % 5 == 2);
        Color base = tip ? (Color){ 178, 150, 72, 255 }
                         : (pattern < 5 ? (Color){ 112, 143, 67, 255 }
                                        : (Color){ 68, 112, 58, 255 });
        return OrganicColor(base, 6, hash);
    }
    case TEX_CHEMO_MAT: {
        bool filament = ((x + y) % 7 == 0) || ((x * 2 + y) % 13 == 0);
        Color base = filament ? (Color){ 189, 158, 74, 255 }
                              : (pattern < 6 ? (Color){ 94, 117, 61, 255 }
                                             : (Color){ 137, 101, 46, 255 });
        return OrganicColor(base, 5, hash);
    }
    default:
        return MAGENTA;
    }
}

static Color CanopyFrondPixel(int x, int y, unsigned int hash)
{
    if (PixelNearSegment(x, y, 1.0f, 14.5f, 14.8f, 2.0f, 0.55f)) {
        return OrganicColor((Color){ 91, 151, 102, 248 }, 5, hash);
    }
    for (int index = 0; index < 5; index++) {
        float veinX = 3.8f + index * 2.2f;
        float veinY = 12.0f - index * 2.0f;
        if (PixelNearSegment(x, y, veinX, veinY, veinX - 1.9f,
                             veinY - 3.0f, 0.72f) ||
            PixelNearSegment(x, y, veinX + 0.5f, veinY + 0.3f,
                             veinX + 3.1f, veinY + 1.3f, 0.72f)) {
            Color base = index & 1 ? (Color){ 53, 126, 88, 245 }
                                   : (Color){ 78, 158, 112, 245 };
            return OrganicColor(base, 8, hash);
        }
    }
    return TransparentPixel();
}

static Color TreeFoliagePixel(BlockTexture texture, int x, int y,
                              unsigned int hash)
{
    bool twig = false;
    bool gap = false;
    Color dark = { 35, 81, 42, 245 };
    Color mid = { 55, 118, 48, 245 };
    Color light = { 91, 145, 60, 245 };
    switch (texture) {
    case TEX_OAK_LEAVES:
        twig = PixelNearSegment(x, y, 1.0f, 13.5f, 14.5f, 3.0f, 0.42f);
        gap = ((x * 3 + y * 5 + (int)(hash % 7u)) % 17) < 2;
        dark = (Color){ 37, 87, 40, 245 };
        mid = (Color){ 55, 118, 48, 245 };
        light = (Color){ 91, 145, 57, 245 };
        break;
    case TEX_BIRCH_LEAVES:
        twig = ((x + y * 2) % 13 == 0);
        gap = ((x * 5 + y * 3 + (int)(hash % 5u)) % 13) < 2;
        dark = (Color){ 63, 113, 43, 245 };
        mid = (Color){ 93, 151, 56, 245 };
        light = (Color){ 142, 177, 75, 245 };
        break;
    case TEX_ASPEN_LEAVES:
        twig = PixelNearSegment(x, y, 2.0f, 12.5f, 14.0f, 4.5f, 0.35f);
        gap = ((x * 2 + y * 7 + (int)(hash % 11u)) % 19) < 3;
        dark = (Color){ 67, 112, 45, 245 };
        mid = (Color){ 105, 151, 59, 245 };
        light = (Color){ 155, 178, 77, 245 };
        break;
    case TEX_SPRUCE_NEEDLES:
        twig = y % 5 == 3 && x > 1;
        gap = ((x + y * 3 + (int)(hash % 3u)) % 9) < 2;
        dark = (Color){ 24, 61, 43, 245 };
        mid = (Color){ 36, 83, 54, 245 };
        light = (Color){ 62, 111, 70, 245 };
        break;
    case TEX_PINE_NEEDLES:
        twig = PixelNearSegment(x, y, 0.5f, 13.5f, 15.0f, 5.0f, 0.34f);
        gap = ((x * 5 + y + (int)(hash % 7u)) % 11) < 3;
        dark = (Color){ 31, 76, 48, 245 };
        mid = (Color){ 48, 104, 59, 245 };
        light = (Color){ 79, 128, 72, 245 };
        break;
    case TEX_WILLOW_LEAVES:
        twig = x % 5 == 2 && y > 2;
        gap = ((x * 7 + y * 2 + (int)(hash % 5u)) % 15) < 3;
        dark = (Color){ 56, 103, 51, 245 };
        mid = (Color){ 87, 139, 66, 245 };
        light = (Color){ 133, 162, 83, 245 };
        break;
    default:
        return MAGENTA;
    }
    if (gap && !twig) return TransparentPixel();
    if (twig) return OrganicColor((Color){ 82, 73, 46, 245 }, 5, hash);
    int tone = (x * 3 + y * 5 + (int)(hash % 5u)) % 9;
    return OrganicColor(tone < 2 ? light : tone < 6 ? mid : dark, 6, hash);
}

static Color BigBluestemPixel(int x, int y, unsigned int hash)
{
    static const float stems[][4] = {
        { 5.2f, 15.8f, 5.0f, 4.5f }, { 7.5f, 15.8f, 7.5f, 2.0f },
        { 9.8f, 15.8f, 10.2f, 5.0f }
    };
    for (int index = 0; index < 3; index++) {
        if (PixelNearSegment(x, y, stems[index][0], stems[index][1],
                             stems[index][2], stems[index][3], 0.52f)) {
            return OrganicColor(index == 1 ? (Color){ 78, 117, 76, 255 }
                                            : (Color){ 93, 132, 64, 255 },
                                6, hash);
        }
    }
    bool seedHead = PixelNearSegment(x, y, 7.5f, 3.2f, 4.2f, 1.0f, 0.58f) ||
                    PixelNearSegment(x, y, 7.5f, 3.2f, 7.5f, 0.4f, 0.58f) ||
                    PixelNearSegment(x, y, 7.5f, 3.2f, 11.0f, 1.2f, 0.58f);
    if (seedHead) {
        return OrganicColor((Color){ 136, 77, 61, 255 }, 7, hash);
    }
    if (PixelNearSegment(x, y, 6.0f, 12.8f, 2.0f, 8.0f, 0.48f) ||
        PixelNearSegment(x, y, 9.2f, 13.0f, 13.5f, 7.5f, 0.48f)) {
        return OrganicColor((Color){ 108, 139, 66, 255 }, 6, hash);
    }
    return TransparentPixel();
}

static Color HeatherPixel(int x, int y, unsigned int hash)
{
    static const float stems[][4] = {
        { 7.5f, 15.8f, 7.4f, 4.0f }, { 7.0f, 13.5f, 3.0f, 6.0f },
        { 8.0f, 12.8f, 12.5f, 5.4f }, { 6.1f, 10.8f, 4.8f, 3.0f },
        { 9.5f, 9.5f, 10.4f, 2.5f }
    };
    for (int index = 0; index < 5; index++) {
        if (PixelNearSegment(x, y, stems[index][0], stems[index][1],
                             stems[index][2], stems[index][3], 0.48f)) {
            return OrganicColor(index == 0 ? (Color){ 88, 67, 43, 255 }
                                            : (Color){ 67, 94, 48, 255 },
                                5, hash);
        }
    }
    static const float flowerCenters[][2] = {
        { 3.0f, 5.5f }, { 4.4f, 7.8f }, { 4.8f, 3.2f },
        { 10.4f, 2.6f }, { 11.4f, 4.5f }, { 12.3f, 5.8f },
        { 7.2f, 4.3f }, { 6.1f, 7.1f }, { 9.5f, 7.0f }
    };
    for (int index = 0; index < 9; index++) {
        if (PixelInEllipse(x, y, flowerCenters[index][0],
                           flowerCenters[index][1], 1.05f, 1.1f)) {
            Color flower = index % 3 == 0 ? (Color){ 206, 126, 176, 255 }
                           : index % 3 == 1 ? (Color){ 167, 82, 148, 255 }
                                            : (Color){ 224, 157, 194, 255 };
            return OrganicColor(flower, 6, hash);
        }
    }
    if ((x + y) % 4 == 0 && y > 5 && y < 14 && x > 3 && x < 12) {
        return OrganicColor((Color){ 87, 116, 55, 255 }, 6, hash);
    }
    return TransparentPixel();
}

static Color FireweedPixel(int x, int y, unsigned int hash)
{
    if (PixelNearSegment(x, y, 7.5f, 15.8f, 7.5f, 2.0f, 0.48f)) {
        return OrganicColor((Color){ 74, 113, 54, 255 }, 5, hash);
    }
    static const float leaves[][4] = {
        { 7.5f, 13.3f, 3.0f, 10.8f }, { 7.5f, 11.0f, 12.0f, 8.6f },
        { 7.5f, 9.0f, 4.2f, 6.8f }, { 7.5f, 7.0f, 10.3f, 5.2f }
    };
    for (int index = 0; index < 4; index++) {
        if (PixelNearSegment(x, y, leaves[index][0], leaves[index][1],
                             leaves[index][2], leaves[index][3], 0.65f)) {
            return OrganicColor(index & 1 ? (Color){ 72, 127, 57, 255 }
                                          : (Color){ 96, 145, 69, 255 },
                                7, hash);
        }
    }
    static const float flowers[][2] = {
        { 7.5f, 1.2f }, { 6.3f, 2.8f }, { 8.7f, 3.3f },
        { 6.0f, 4.5f }, { 8.8f, 5.0f }, { 7.1f, 6.0f }
    };
    for (int index = 0; index < 6; index++) {
        if (PixelInEllipse(x, y, flowers[index][0], flowers[index][1],
                           1.0f, 1.15f)) {
            Color flower = index & 1 ? (Color){ 205, 71, 129, 255 }
                                     : (Color){ 231, 104, 151, 255 };
            return OrganicColor(flower, 6, hash);
        }
    }
    return TransparentPixel();
}

Color EcologyAtlasPixel(BlockTexture texture, int x, int y,
                         unsigned int hash)
{
    Color color = WHITE;
    int center = ATLAS_TILE_SIZE / 2;
    switch (texture) {
    case TEX_TALL_GRASS: return TallGrassPixel(x, y, hash);
    case TEX_FERN: return FernPixel(x, y, hash, false);
    case TEX_REED: return ReedPixel(x, y, hash, false);
    case TEX_LICHEN: return LichenPixel(x, y, hash);
    case TEX_MOSS_CARPET:
    case TEX_MICROBIAL_MAT:
    case TEX_MYCELIUM:
    case TEX_SPHAGNUM:
    case TEX_CHEMO_MAT:
        return GroundCoverPixel(texture, x, y, hash);
    case TEX_LIVING_STEM:
        color = OrganicColor(x % 5 == 0 ? (Color){ 50, 67, 43, 255 }
                                        : (Color){ 91, 82, 57, 255 },
                             8, hash);
        if ((x * 3 + y) % 17 == 0) color = (Color){ 123, 148, 82, 255 };
        return color;
    case TEX_CANOPY_FROND:
        return CanopyFrondPixel(x, y, hash);
    case TEX_LUMINOUS_POD: {
        int dx = x - center;
        int dy = y - center;
        if (!PixelInEllipse(x, y, 8.0f, 8.0f, 5.2f, 6.2f)) {
            return TransparentPixel();
        }
        color = OrganicColor((Color){ 92, 188, 145, 250 }, 8, hash);
        if (dx * dx + dy * dy < 13) color = (Color){ 218, 247, 181, 255 };
        if (x <= 5 && y <= 7) color = (Color){ 183, 232, 169, 255 };
        return color;
    }
    case TEX_FUNGAL_STEM:
        color = OrganicColor((Color){ 168, 157, 143, 255 }, 7, hash);
        if (x % 6 == 0) color = (Color){ 111, 102, 98, 255 };
        if ((x + y * 3) % 19 == 0) color = (Color){ 205, 194, 173, 255 };
        return color;
    case TEX_SPORE_CAP: {
        int radial = (x - center) * (x - center) +
                     (y - center) * (y - center);
        color = OrganicColor(radial < 18 ? (Color){ 182, 99, 160, 248 }
                                         : (Color){ 125, 62, 126, 248 },
                             9, hash);
        if ((x * 5 + y * 3) % 17 < 2) color = (Color){ 232, 183, 204, 248 };
        return color;
    }
    case TEX_CRYSTAL_BLOOM: {
        int diagonal = (x * 2 + y + (int)(hash % 5u)) % 10;
        color = OrganicColor((Color){ 116, 190, 207, 255 }, 8, hash);
        if (diagonal < 2) color = (Color){ 218, 245, 245, 255 };
        if ((x - y + 16) % 9 == 0) color = (Color){ 164, 110, 205, 255 };
        return color;
    }
    case TEX_VENT_CHIMNEY:
        color = OrganicColor((Color){ 67, 63, 61, 255 }, 12, hash);
        if ((x + y * 2) % 11 == 0) color = (Color){ 33, 35, 36, 255 };
        if (y % 6 == 0) color = (Color){ 105, 87, 68, 255 };
        return color;
    case TEX_OAK_BARK:
        color = OrganicColor(x % 4 == 0 ? (Color){ 67, 47, 31, 255 }
                                        : (Color){ 109, 77, 46, 255 }, 8, hash);
        if ((x + y * 2) % 13 == 0) color = (Color){ 48, 38, 29, 255 };
        return color;
    case TEX_OAK_RING:
    case TEX_BIRCH_RING:
    case TEX_ASPEN_RING:
    case TEX_SPRUCE_RING:
    case TEX_PINE_RING:
    case TEX_WILLOW_RING: {
        int dx = x - center;
        int dy = y - center;
        int divisor = texture == TEX_BIRCH_RING || texture == TEX_SPRUCE_RING
            ? 8 : texture == TEX_ASPEN_RING || texture == TEX_PINE_RING
            ? 10 : 9;
        int ring = (dx * dx + dy * dy) / divisor;
        Color inner = texture == TEX_BIRCH_RING ? (Color){ 225, 212, 174, 255 }
                    : texture == TEX_ASPEN_RING ? (Color){ 220, 185, 117, 255 }
                    : texture == TEX_SPRUCE_RING ? (Color){ 164, 107, 60, 255 }
                    : texture == TEX_PINE_RING ? (Color){ 202, 143, 82, 255 }
                    : texture == TEX_WILLOW_RING ? (Color){ 210, 174, 102, 255 }
                                                : (Color){ 188, 142, 82, 255 };
        Color outer = texture == TEX_BIRCH_RING ? (Color){ 171, 157, 125, 255 }
                    : texture == TEX_ASPEN_RING ? (Color){ 175, 143, 83, 255 }
                    : texture == TEX_SPRUCE_RING ? (Color){ 122, 77, 44, 255 }
                    : texture == TEX_PINE_RING ? (Color){ 151, 92, 50, 255 }
                    : texture == TEX_WILLOW_RING ? (Color){ 164, 128, 70, 255 }
                                                : (Color){ 145, 102, 57, 255 };
        return OrganicColor((ring & 1) ? outer : inner, 5, hash);
    }
    case TEX_BIRCH_BARK:
        if ((x * 2 + y + (int)(hash % 5u)) % 11 < 2) {
            return (Color){ 48, 44, 39, 255 };
        }
        return OrganicColor((Color){ 207, 202, 184, 255 }, 6, hash);
    case TEX_ASPEN_BARK:
        if ((x * 3 + y + (int)(hash % 7u)) % 13 == 0) {
            return (Color){ 70, 86, 65, 255 };
        }
        return OrganicColor((Color){ 164, 174, 143, 255 }, 7, hash);
    case TEX_SPRUCE_BARK:
        color = OrganicColor(x % 3 == 0 ? (Color){ 55, 39, 29, 255 }
                                        : (Color){ 88, 62, 40, 255 }, 7, hash);
        if ((x + y) % 9 == 0) color = (Color){ 113, 77, 47, 255 };
        return color;
    case TEX_PINE_BARK:
        color = OrganicColor(y % 5 == 0 ? (Color){ 173, 103, 59, 255 }
                                        : (Color){ 123, 68, 40, 255 }, 7, hash);
        if ((x * 2 + y) % 13 == 0) color = (Color){ 77, 51, 36, 255 };
        return color;
    case TEX_WILLOW_BARK:
        color = OrganicColor((x + y) % 6 == 0 ? (Color){ 82, 76, 54, 255 }
                                               : (Color){ 132, 117, 78, 255 },
                             7, hash);
        if (x % 5 == 0) color = (Color){ 101, 91, 61, 255 };
        return color;
    case TEX_OAK_LEAVES:
    case TEX_BIRCH_LEAVES:
    case TEX_ASPEN_LEAVES:
    case TEX_SPRUCE_NEEDLES:
    case TEX_PINE_NEEDLES:
    case TEX_WILLOW_LEAVES:
        return TreeFoliagePixel(texture, x, y, hash);
    case TEX_BIG_BLUESTEM:
        return BigBluestemPixel(x, y, hash);
    case TEX_BRACKEN:
        return FernPixel(x, y, hash, true);
    case TEX_COMMON_REED:
        return ReedPixel(x, y, hash, true);
    case TEX_HEATHER:
        return HeatherPixel(x, y, hash);
    case TEX_FIREWEED:
        return FireweedPixel(x, y, hash);
    case TEX_SAGUARO:
        color = OrganicColor((x % 4 == 0 || x % 4 == 3)
                                 ? (Color){ 45, 104, 54, 255 }
                                 : (Color){ 71, 137, 72, 255 },
                             6, hash);
        if ((x * 5 + y * 7) % 23 == 0) color = (Color){ 198, 205, 132, 255 };
        return color;
    default:
        return MAGENTA;
    }
}
