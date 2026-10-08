#include "world/volcano_terrain.h"
#include "raymath.h"
#include <math.h>

static uint32_t Hash(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    return x ^ (x >> 16);
}
static float Unit(uint32_t x) { return (Hash(x) & 0xffffffu) / 16777215.0f; }
static float Smooth(float a, float b, float x)
{
    float t = Clamp((x - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
}
static float Noise(uint32_t seed, float x, float y)
{
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float tx = Smooth(0, 1, x - ix), ty = Smooth(0, 1, y - iy);
    uint32_t a = (uint32_t)ix * 374761393u, b = (uint32_t)iy * 668265263u;
    return Lerp(Lerp(Unit(seed ^ a ^ b), Unit(seed ^ (a + 374761393u) ^ b), tx),
                Lerp(Unit(seed ^ a ^ (b + 668265263u)),
                     Unit(seed ^ (a + 374761393u) ^ (b + 668265263u)), tx), ty) * 2 - 1;
}
static float Fbm(uint32_t seed, float x, float y)
{
    float sum = 0, weight = .5f;
    for (unsigned i = 0; i < 4; i++) {
        sum += weight * Noise(seed + i * 101u, x, y);
        x = x * 2.03f + 13.1f; y = y * 2.03f - 7.7f; weight *= .5f;
    }
    return sum / .9375f;
}

bool VolcanoTerrainDefine(uint32_t seed, float activity, SurfaceAddress center,
                           VolcanoTerrainDefinition *out)
{
    if (!out || !isfinite(activity) || activity <= 0 || !SurfaceAddressIsValid(center) ||
        center.u % VOLCANO_TERRAIN_SPACING != VOLCANO_TERRAIN_OFFSET ||
        center.v % VOLCANO_TERRAIN_SPACING != VOLCANO_TERRAIN_OFFSET) return false;
    uint32_t key = Hash(seed ^ ((uint32_t)center.face * 73856093u) ^
                        ((uint32_t)center.u * 19349663u) ^ ((uint32_t)center.v * 83492791u));
    if (Unit(key) >= Clamp(activity, 0, 1) * .55f) return false;
    center.radial = 0;
    *out = (VolcanoTerrainDefinition){ .center = center, .seed = key,
        .radius = 110 + 35 * Unit(key + 1), .height = 62 + 26 * Unit(key + 2),
        .craterRadius = 17 + 10 * Unit(key + 3), .craterDepth = 25 + 13 * Unit(key + 4),
        .potential = .45f + .55f * Unit(key + 5) };
    return true;
}

VolcanoTerrainSample VolcanoTerrainShape(const VolcanoTerrainDefinition *d, Vector3 direction)
{
    VolcanoTerrainSample result = { 0 };
    if (!d || !SurfaceAddressIsValid(d->center) || !isfinite(d->radius) ||
        !isfinite(d->height) || !isfinite(d->craterRadius) || !isfinite(d->craterDepth) ||
        d->radius <= 0 || d->craterRadius <= 0 || d->craterRadius >= d->radius ||
        d->height <= 0 || d->craterDepth < 0 || d->craterDepth >= d->height ||
        !isfinite(direction.x) || !isfinite(direction.y) || !isfinite(direction.z) ||
        Vector3LengthSqr(direction) < .5f) return result;
    direction = Vector3Normalize(direction);
    Vector3 center = SurfaceAddressDirection(d->center);
    Vector3 delta = Vector3Subtract(direction, center);
    if (Vector3LengthSqr(delta) * SURFACE_RADIUS_BLOCKS * SURFACE_RADIUS_BLOCKS >
        d->radius * d->radius * 1.44f) return result;
    Vector3 east = Vector3Normalize(Vector3CrossProduct(
        fabsf(center.y) < .95f ? (Vector3){0,1,0} : (Vector3){1,0,0}, center));
    Vector3 north = Vector3CrossProduct(center, east);
    float x = Vector3DotProduct(delta, east) * SURFACE_RADIUS_BLOCKS;
    float y = Vector3DotProduct(delta, north) * SURFACE_RADIUS_BLOCKS;
    /* Anchor the domain warp at the vent: the crater center cannot drift. */
    float wx = Fbm(d->seed, x / d->radius * 3, y / d->radius * 3) - Fbm(d->seed, 0, 0);
    float wy = Fbm(d->seed + 71, x / d->radius * 3, y / d->radius * 3) - Fbm(d->seed + 71, 0, 0);
    x += wx * d->radius * .055f; y += wy * d->radius * .055f;
    float r = hypotf(x, y);
    if (r >= d->radius) return result;
    float angle = atan2f(y, x);
    float craterRadius = d->craterRadius * (1 + .07f * sinf(3 * angle + Unit(d->seed) * 6.28f));
    float flank = Clamp((d->radius - r) / (d->radius - craterRadius), 0, 1);
    float interior = 1 - Smooth(.18f * craterRadius, craterRadius, r);
    float foot = 1 - Smooth(.78f * d->radius, d->radius, r);
    float height = d->height * powf(flank, 1.65f) - d->craterDepth * interior;
    /* Radial gullies and bounded multi-scale roughness preserve crater/foot. */
    float detailMask = Smooth(craterRadius, craterRadius * 1.6f, r) * foot;
    float ridges = .5f + .5f * sinf(13 * angle + 1.6f * Fbm(d->seed + 11, x / 38, y / 38));
    height += detailMask * (3 * Fbm(d->seed + 19, x / 15, y / 15) - 6 * ridges * ridges);
    result = (VolcanoTerrainSample){ .definition = *d, .height = fmaxf(0, height * foot),
        .foundation = 1 - Smooth(.55f * d->radius, d->radius, r), .crater = interior };
    return result;
}

VolcanoTerrainSample VolcanoTerrainAt(uint32_t seed, float activity, Vector3 direction)
{
    VolcanoTerrainSample best = { 0 };
    if (!isfinite(activity) || activity <= 0 || !isfinite(direction.x) ||
        !isfinite(direction.y) || !isfinite(direction.z) || Vector3LengthSqr(direction) < .5f) return best;
    direction = Vector3Normalize(direction);
    /* Project onto every facing cube plane. Neighbor support is enumerated
       on both sides of edges/corners, independent of dominant-face tie breaks. */
    for (SurfaceFace face = 0; face < SURFACE_FACE_COUNT; face++) {
        float a, s, t;
        switch (face) {
        case SURFACE_FACE_POS_X: a=direction.x; s=-direction.z; t=-direction.y; break;
        case SURFACE_FACE_NEG_X: a=-direction.x; s=direction.z; t=-direction.y; break;
        case SURFACE_FACE_POS_Y: a=direction.y; s=direction.x; t=direction.z; break;
        case SURFACE_FACE_NEG_Y: a=-direction.y; s=direction.x; t=-direction.z; break;
        case SURFACE_FACE_POS_Z: a=direction.z; s=direction.x; t=-direction.y; break;
        default: a=-direction.z; s=-direction.x; t=-direction.y; break;
        }
        if (a < .5f) continue;
        int u = (int)floorf(((s/a + 1) * .5f * SURFACE_FACE_BLOCKS - VOLCANO_TERRAIN_OFFSET) / VOLCANO_TERRAIN_SPACING);
        int v = (int)floorf(((t/a + 1) * .5f * SURFACE_FACE_BLOCKS - VOLCANO_TERRAIN_OFFSET) / VOLCANO_TERRAIN_SPACING);
        for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
            SurfaceAddress address = {0, face, (u+i)*VOLCANO_TERRAIN_SPACING + VOLCANO_TERRAIN_OFFSET,
                (v+j)*VOLCANO_TERRAIN_SPACING + VOLCANO_TERRAIN_OFFSET, 0};
            VolcanoTerrainDefinition d;
            if (!VolcanoTerrainDefine(seed, activity, address, &d)) continue;
            VolcanoTerrainSample candidate = VolcanoTerrainShape(&d, direction);
            if (candidate.height > best.height) best = candidate;
        }
    }
    return best;
}
