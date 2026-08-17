#include "presentation/render.h"
#include "presentation/render_dependencies.h"
#include "presentation/render_internal.h"

#ifndef PLANET_TEXTURE_WIDTH
#define PLANET_TEXTURE_WIDTH 384
#endif
#ifndef PLANET_TEXTURE_HEIGHT
#define PLANET_TEXTURE_HEIGHT 192
#endif
#define PLANET_TEXTURE_CACHE_CAPACITY 24
#define PLANET_CLOUD_CACHE_CAPACITY 24

typedef struct PlanetTextureCacheEntry {
    bool valid;
    uint32_t seed;
    uint32_t canonicalBodyId;
    SolarBodyStyle style;
    uint32_t oceanKey;
    uint32_t seasonKey;
    uint64_t lastUse;
    PlanetTextureSet textures;
} PlanetTextureCacheEntry;

typedef struct PlanetCloudCacheEntry {
    bool valid;
    uint32_t seed;
    uint32_t profileKey;
    uint64_t lastUse;
    Texture2D texture;
} PlanetCloudCacheEntry;

typedef struct PlanetTextureResources {
    bool initialized;
    PlanetTextureSet home;
    Texture2D homeClouds;
    uint32_t homeCloudSeed;
    uint64_t textureCacheTick;
    PlanetTextureCacheEntry planetTextures[PLANET_TEXTURE_CACHE_CAPACITY];
    PlanetCloudCacheEntry cloudTextures[PLANET_CLOUD_CACHE_CAPACITY];
} PlanetTextureResources;

static PlanetTextureResources planetTextures = { 0 };

static Color HomePlanetColor(void);

static uint32_t PlanetTextureHash(int x, int y, int z, uint32_t seed)
{
    uint32_t hash = seed;
    hash ^= (uint32_t)x * 0x8da6b343u;
    hash ^= (uint32_t)y * 0xd8163841u;
    hash ^= (uint32_t)z * 0xcb1ab31fu;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    return hash ^ (hash >> 16);
}

static float PlanetHashUnit(int x, int y, int z, uint32_t seed)
{
    return (float)(PlanetTextureHash(x, y, z, seed) & 0x00ffffffu) / 16777215.0f;
}

static float PlanetNoiseSmooth(float value)
{
    return value * value * (3.0f - 2.0f * value);
}

static float PlanetValueNoise(float x, float y, float z, uint32_t seed)
{
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int z0 = (int)floorf(z);
    float tx = PlanetNoiseSmooth(x - (float)x0);
    float ty = PlanetNoiseSmooth(y - (float)y0);
    float tz = PlanetNoiseSmooth(z - (float)z0);

    float x00 = Lerp(PlanetHashUnit(x0, y0, z0, seed),
                     PlanetHashUnit(x0 + 1, y0, z0, seed), tx);
    float x10 = Lerp(PlanetHashUnit(x0, y0 + 1, z0, seed),
                     PlanetHashUnit(x0 + 1, y0 + 1, z0, seed), tx);
    float x01 = Lerp(PlanetHashUnit(x0, y0, z0 + 1, seed),
                     PlanetHashUnit(x0 + 1, y0, z0 + 1, seed), tx);
    float x11 = Lerp(PlanetHashUnit(x0, y0 + 1, z0 + 1, seed),
                     PlanetHashUnit(x0 + 1, y0 + 1, z0 + 1, seed), tx);
    return Lerp(Lerp(x00, x10, ty), Lerp(x01, x11, ty), tz);
}

static float PlanetFractalNoise(float x, float y, float z, uint32_t seed)
{
    float value = 0.0f;
    float amplitude = 0.55f;
    float total = 0.0f;
    for (int octave = 0; octave < 4; octave++) {
        value += PlanetValueNoise(x, y, z, seed + (uint32_t)octave * 1013u) * amplitude;
        total += amplitude;
        x = x * 2.03f + 7.1f;
        y = y * 2.03f - 3.7f;
        z = z * 2.03f + 5.3f;
        amplitude *= 0.5f;
    }
    return value / total;
}

static Color ShadePlanetColor(Color color, float shade)
{
    return (Color){
        PlanetColorChannel((float)color.r * shade),
        PlanetColorChannel((float)color.g * shade),
        PlanetColorChannel((float)color.b * shade),
        color.a
    };
}

static Color ApplyPlanetClimateColor(Color color, const PlanetProfile *profile,
                                     const PlanetSurfaceSample *surface)
{
    if (!profile || !surface) return color;

    float cold = Clamp((248.0f - surface->temperature) / 62.0f, 0.0f, 1.0f);
    float warm = Clamp((surface->temperature - 304.0f) / 92.0f, 0.0f, 1.0f);
    color = ColorLerp(color, (Color){ 178, 211, 235, 255 }, cold * 0.16f);
    color = ColorLerp(color, (Color){ 224, 111, 54, 255 }, warm *
                      (0.10f + profile->greenhouseEffect * 0.10f));
    if (surface->iceCoverage > 0.01f) {
        color = ColorLerp(color, (Color){ 218, 240, 247, 255 },
                          Clamp(surface->iceCoverage * 0.86f, 0.0f, 0.92f));
    }
    color = ColorLerp(color, (Color){ 38, 43, 49, 255 }, surface->impactDepth * 0.42f);
    color = ColorLerp(color, (Color){ 177, 167, 145, 255 }, surface->ejecta * 0.24f);
    color = ColorLerp(color, (Color){ 218, 204, 166, 255 }, surface->impactRim * 0.28f);
    if (surface->lavaFlow > 0.05f) {
        color = ColorLerp(color, (Color){ 255, 82, 19, 255 }, surface->lavaFlow * 0.42f);
    }
    if (surface->glacierCracks > 0.05f) {
        color = ColorLerp(color, (Color){ 25, 71, 119, 255 },
                          surface->glacierCracks * 0.48f);
    }
    float albedoShade = 0.88f + Clamp(profile->albedo, 0.0f, 1.0f) * 0.28f;
    return ShadePlanetColor(color, albedoShade);
}

static Color TemperatePlanetPixel(const PlanetProfile *profile, float ny,
                                  const PlanetSurfaceSample *surfaceSample)
{
    PlanetSurfaceSample surface = *surfaceSample;
    float continents = surface.continentalness;
    float detail = surface.detail;
    Color color;

    if (surface.biome == PLANET_BIOME_OCEAN) {
        float waterline = 0.27f + Clamp(profile->oceanCoverage, 0.0f, 1.0f) * 0.36f;
        float depth = Clamp((waterline - continents) * 6.0f, 0.0f, 1.0f);
        color = ColorLerp((Color){ 35, 139, 176, 255 },
                          (Color){ 9, 43, 103, 255 }, depth * 0.82f);
    } else if (surface.biome == PLANET_BIOME_COAST) {
        color = ColorLerp((Color){ 82, 158, 166, 255 },
                          (Color){ 202, 181, 120, 255 }, detail * 0.64f);
    } else {
        float height = Clamp((continents - 0.42f) * 4.6f + detail * 0.18f, 0.0f, 1.0f);
        Color lowland = surface.biome == PLANET_BIOME_FOREST
                            ? (Color){ 39, 112, 61, 255 }
                            : (Color){ 91, 140, 76, 255 };
        if (surface.biome == PLANET_BIOME_ALPINE) {
            lowland = (Color){ 118, 120, 98, 255 };
            height = fmaxf(height, 0.54f + surface.regionalness * 0.36f);
        }
        if (fabsf(ny) > 0.63f) {
            lowland = ColorLerp(lowland, (Color){ 104, 130, 102, 255 }, 0.45f);
        }
        color = ColorLerp(lowland, (Color){ 126, 112, 82, 255 }, height);
        if (height > 0.86f) {
            color = ColorLerp(color, (Color){ 193, 201, 198, 255 },
                              (height - 0.86f) / 0.14f);
        }
    }

    color = ApplyPlanetClimateColor(color, profile, &surface);
    return color;
}

static float PlanetCloudAmountFor(const PlanetProfile *profile)
{
    if (!profile || profile->atmosphereType == PLANET_ATMOSPHERE_NONE) return 0.0f;
    return Clamp(profile->cloudCoverage, 0.0f, 1.0f);
}

static Color PlanetCloudColorFor(const PlanetProfile *profile)
{
    if (profile->atmosphereType == PLANET_ATMOSPHERE_CORROSIVE) {
        return ColorLerp((Color){ 244, 218, 132, 255 },
                         PlanetAtmosphereBaseColor(profile), 0.22f);
    }
    if (profile->style == SOLAR_STYLE_ICE || profile->equilibriumTempK < 238.0f) {
        return (Color){ 222, 240, 248, 255 };
    }
    if (profile->atmosphereType == PLANET_ATMOSPHERE_DENSE) {
        return ColorLerp((Color){ 239, 240, 235, 255 },
                         PlanetAtmosphereBaseColor(profile), 0.12f);
    }
    return (Color){ 246, 250, 255, 255 };
}

static float PlanetCloudStorm(Vector3 point, uint32_t seed, int index, float cloudAmount)
{
    if (index > 0 && cloudAmount < 0.38f) return 0.0f;

    float longitude = PlanetHashUnit(23 + index * 17, 41, 67, seed) * 2.0f * PI;
    float latitude = (PlanetHashUnit(71, 13 + index * 19, 37, seed) - 0.5f) * 1.45f;
    float cosLatitude = cosf(latitude);
    Vector3 center = { cosLatitude * cosf(longitude), sinf(latitude),
                       cosLatitude * sinf(longitude) };
    float angularDistance = sqrtf(fmaxf(0.0f,
                                         2.0f * (1.0f - Vector3DotProduct(point, center))));
    float radius = 0.16f + PlanetHashUnit(89, 29 + index * 31, 11, seed) * 0.11f;
    if (angularDistance >= radius) return 0.0f;

    Vector3 east = Vector3Normalize(Vector3CrossProduct((Vector3){ 0.0f, 1.0f, 0.0f },
                                                                  center));
    Vector3 north = Vector3Normalize(Vector3CrossProduct(center, east));
    float azimuth = atan2f(Vector3DotProduct(point, north),
                           Vector3DotProduct(point, east));
    float winding = index == 0 ? 3.0f : -2.0f;
    float phase = PlanetHashUnit(43, 97, 17 + index * 13, seed) * 2.0f * PI;
    float spiral = 0.5f + 0.5f * sinf(angularDistance * 82.0f + azimuth * winding + phase);
    float envelope = PlanetNoiseSmooth(1.0f - angularDistance / radius);
    float eye = PlanetNoiseSmooth(Clamp((angularDistance - radius * 0.10f) /
                                        (radius * 0.18f), 0.0f, 1.0f));
    return envelope * eye * (0.42f + spiral * 0.58f);
}

static Color PlanetCloudPixel(const PlanetProfile *profile, float nx, float ny,
                              float nz, uint32_t seed)
{
    Vector3 point = { nx, ny, nz };
    float latitude = asinf(Clamp(ny, -1.0f, 1.0f));
    float absLatitude = fabsf(latitude);
    float longitude = atan2f(nz, nx);
    float cloudAmount = PlanetCloudAmountFor(profile);
    float phase = PlanetHashUnit(31, 47, 59, seed) * 2.0f * PI;
    float warp = PlanetFractalNoise(nx * 2.1f + 4.7f, ny * 2.4f - 1.3f,
                                    nz * 2.1f + 7.9f, seed ^ 0x6d2b79u);
    float broad = PlanetFractalNoise(nx * 4.0f + ny * 0.9f,
                                     ny * 3.0f, nz * 4.0f - ny * 0.9f, seed);
    float detail = PlanetFractalNoise(nx * 10.5f - 3.1f, ny * 8.0f + 5.7f,
                                      nz * 10.5f + 2.3f, seed ^ 0x9e3779u);

    if (profile) {
        switch (profile->canonicalBodyId) {
        case 2u: {
            float bands = 0.5f + 0.5f * sinf(latitude * 32.0f + warp * 5.0f);
            Color color = ColorLerp((Color){ 246, 218, 142, 255 },
                                    (Color){ 205, 161, 82, 255 },
                                    broad * 0.24f + bands * 0.18f);
            color.a = PlanetColorChannel((0.88f + detail * 0.10f) * 255.0f);
            return color;
        }
        case 4u: {
            float polarDust = Clamp((fabsf(ny) - 0.56f) / 0.44f, 0.0f, 1.0f);
            float dust = broad * 0.54f + detail * 0.18f + polarDust * 0.28f;
            Color color = ColorLerp((Color){ 211, 153, 116, 255 },
                                    (Color){ 237, 193, 151, 255 }, detail);
            color.a = PlanetColorChannel(Clamp((dust - 0.61f) * 1.35f,
                                               0.0f, 0.28f) * 255.0f);
            return color;
        }
        case 5u: {
            float lanes = 0.5f + 0.5f * sinf(latitude * 76.0f + warp * 6.0f);
            Color color = ColorLerp((Color){ 248, 237, 211, 255 },
                                    (Color){ 219, 185, 145, 255 }, lanes);
            color.a = PlanetColorChannel((0.10f + lanes * detail * 0.24f) * 255.0f);
            return color;
        }
        case 6u: {
            float lanes = 0.5f + 0.5f * sinf(latitude * 64.0f + warp * 4.0f);
            Color color = ColorLerp((Color){ 249, 239, 205, 255 },
                                    (Color){ 226, 204, 162, 255 }, lanes);
            color.a = PlanetColorChannel((0.08f + lanes * detail * 0.17f) * 255.0f);
            return color;
        }
        case 7u: {
            float polarHaze = Clamp((ny - 0.38f) / 0.62f, 0.0f, 1.0f);
            Color color = ColorLerp((Color){ 199, 237, 236, 255 },
                                    (Color){ 224, 245, 239, 255 }, polarHaze);
            color.a = PlanetColorChannel((0.05f + polarHaze * 0.12f +
                                          detail * 0.04f) * 255.0f);
            return color;
        }
        case 8u: {
            float lanes = 0.5f + 0.5f * sinf(latitude * 49.0f + warp * 7.0f);
            float storm = PlanetCloudStorm(point, seed, 0, 1.0f);
            float opacity = 0.05f + lanes * detail * 0.16f + storm * 0.62f;
            Color color = ColorLerp((Color){ 190, 223, 247, 255 },
                                    (Color){ 242, 249, 251, 255 }, storm);
            color.a = PlanetColorChannel(Clamp(opacity, 0.0f, 0.78f) * 255.0f);
            return color;
        }
        default:
            break;
        }
    }

    float equatorialConvergence = expf(-powf(latitude / 0.24f, 2.0f));
    float midLatitudeTracks = expf(-powf((absLatitude - 0.74f) / 0.22f, 2.0f));
    float subtropicalDryBand = expf(-powf((absLatitude - 0.43f) / 0.15f, 2.0f));
    float bandWave = 0.5f + 0.5f * sinf(latitude * 18.0f + phase + warp * 3.2f);
    float circulation = Clamp(0.42f + equatorialConvergence * 0.25f +
                              midLatitudeTracks * 0.31f - subtropicalDryBand * 0.22f +
                              (bandWave - 0.5f) * 0.20f, 0.0f, 1.0f);

    float polarMask = PlanetNoiseSmooth(Clamp((fabsf(ny) - 0.68f) / 0.30f, 0.0f, 1.0f));
    float vortexDirection = ny >= 0.0f ? 3.0f : -3.0f;
    float vortexArms = 0.5f + 0.5f * sinf(longitude * vortexDirection +
                                           (1.0f - fabsf(ny)) * 34.0f + phase);
    float polarVortex = polarMask * vortexArms;
    float storms = PlanetCloudStorm(point, seed, 0, cloudAmount);
    storms = fmaxf(storms, PlanetCloudStorm(point, seed, 1, cloudAmount));

    float cloudField = broad * 0.68f + detail * 0.12f + circulation * 0.16f +
                       polarVortex * 0.10f + storms * 0.48f + cloudAmount * 0.12f;
    float threshold = 0.63f - cloudAmount * 0.17f;
    float maxOpacity = 0.38f + cloudAmount * 0.50f;
    float opacity = Clamp((cloudField - threshold) * (4.1f + cloudAmount * 4.8f),
                          0.0f, maxOpacity);
    Color color = PlanetCloudColorFor(profile);
    color.a = PlanetColorChannel(opacity * 255.0f);
    return color;
}

static Color CraterPlanetPixel(float nx, float ny, float nz, float noise, uint32_t seed)
{
    static const Vector3 centers[] = {
        { 1.0f, 0.0f, 0.0f }, { -0.60f, 0.42f, 0.68f },
        { 0.18f, -0.82f, 0.54f }, { 0.44f, 0.76f, -0.47f },
        { -0.22f, -0.31f, -0.92f }, { 0.72f, -0.48f, -0.50f },
        { -0.82f, -0.54f, 0.20f }, { -0.35f, 0.86f, 0.36f }
    };
    float tone = 82.0f + noise * 64.0f;
    Vector3 point = { nx, ny, nz };
    for (int i = 0; i < (int)(sizeof(centers) / sizeof(centers[0])); i++) {
        Vector3 center = Vector3Normalize(centers[i]);
        float radial = sqrtf(fmaxf(0.0f, 2.0f * (1.0f - Vector3DotProduct(point, center))));
        float radius = 0.075f + 0.025f * (float)((i + (int)(seed & 3u)) % 4);
        if (radial < radius) tone -= 30.0f * (1.0f - radial / radius);
        else if (radial < radius * 1.16f) tone += 36.0f * (1.0f - (radial - radius) / (radius * 0.16f));
    }
    return (Color){ PlanetColorChannel(tone * 0.94f),
                    PlanetColorChannel(tone * 0.96f),
                    PlanetColorChannel(tone), 255 };
}

static float PlanetTextureWrappedDistance(float left, float right)
{
    float distance = fabsf(left - right);
    return fminf(distance, 1.0f - distance);
}

static float PlanetTextureEllipse(float u, float v, float centerU, float centerV,
                                  float radiusU, float radiusV)
{
    float du = PlanetTextureWrappedDistance(u, centerU) / radiusU;
    float dv = (v - centerV) / radiusV;
    return sqrtf(du * du + dv * dv);
}

static bool CanonicalSolarPlanetPixel(
    const PlanetProfile *profile, float nx, float ny, float nz, float u, float v,
    uint32_t seed, const PlanetSurfaceSample *surface, Color *outColor)
{
    if (!profile || !surface || !outColor || profile->canonicalBodyId == 0u) {
        return false;
    }

    float broad = surface->continentalness;
    float fine = surface->detail;
    switch (profile->canonicalBodyId) {
    case 1u: {
        Color color = CraterPlanetPixel(nx, ny, nz, broad, seed);
        color = ColorLerp(color, (Color){ 103, 98, 91, 255 },
                          surface->regionalness * 0.22f);
        color = ColorLerp(color, (Color){ 55, 57, 61, 255 },
                          surface->impactDepth * 0.52f);
        color = ColorLerp(color, (Color){ 183, 180, 169, 255 },
                          surface->ejecta * 0.22f);
        if (surface->iceCoverage > 0.56f) {
            color = ColorLerp(color, (Color){ 198, 209, 211, 255 },
                              (surface->iceCoverage - 0.56f) * 0.72f);
        }
        *outColor = color;
        return true;
    }
    case 2u: {
        float waves = 0.5f + 0.5f * sinf(ny * 24.0f + broad * 5.0f);
        Color color = ColorLerp((Color){ 170, 112, 43, 255 },
                                (Color){ 226, 183, 91, 255 },
                                broad * 0.54f + waves * 0.18f);
        color = ColorLerp(color, (Color){ 115, 76, 43, 255 },
                          surface->ridge * 0.28f);
        color = ColorLerp(color, (Color){ 238, 205, 126, 255 },
                          surface->volcanicCone * 0.16f + fine * 0.08f);
        *outColor = color;
        return true;
    }
    case 3u:
        *outColor = TemperatePlanetPixel(profile, ny, surface);
        return true;
    case 4u: {
        float highland = Clamp(surface->regionalness * 0.58f +
                               surface->ridge * 0.42f, 0.0f, 1.0f);
        Color color = ColorLerp((Color){ 125, 57, 34, 255 },
                                (Color){ 204, 111, 61, 255 },
                                broad * 0.62f + fine * 0.16f);
        color = ColorLerp(color, (Color){ 79, 51, 45, 255 }, highland * 0.34f);
        color = ColorLerp(color, (Color){ 63, 49, 47, 255 },
                          surface->impactDepth * 0.38f);
        color = ColorLerp(color, (Color){ 231, 225, 204, 255 },
                          Clamp((surface->iceCoverage - 0.42f) * 1.52f,
                                0.0f, 0.82f));
        *outColor = color;
        return true;
    }
    case 5u: {
        float narrow = 0.5f + 0.5f * sinf(ny * 78.0f + broad * 9.0f);
        float wide = 0.5f + 0.5f * sinf(ny * 22.0f - fine * 4.0f);
        Color color = ColorLerp((Color){ 232, 218, 185, 255 },
                                (Color){ 151, 91, 59, 255 },
                                narrow * 0.38f + wide * 0.24f);
        float zone = expf(-powf(ny / 0.22f, 2.0f));
        color = ColorLerp(color, (Color){ 220, 174, 116, 255 }, zone * 0.28f);
        float spot = PlanetTextureEllipse(u, v, 0.68f, 0.62f, 0.105f, 0.045f);
        if (spot < 1.0f) {
            float swirl = 0.5f + 0.5f * sinf(spot * 31.0f + broad * 8.0f);
            Color redSpot = ColorLerp((Color){ 160, 62, 43, 255 },
                                      (Color){ 230, 142, 104, 255 }, swirl);
            color = ColorLerp(color, redSpot, (1.0f - spot) * 0.92f);
        }
        *outColor = color;
        return true;
    }
    case 6u: {
        float narrow = 0.5f + 0.5f * sinf(ny * 68.0f + broad * 5.0f);
        float wide = 0.5f + 0.5f * sinf(ny * 18.0f - fine * 3.0f);
        Color color = ColorLerp((Color){ 235, 222, 184, 255 },
                                (Color){ 183, 145, 91, 255 },
                                narrow * 0.22f + wide * 0.18f);
        float polar = Clamp((fabsf(ny) - 0.72f) / 0.28f, 0.0f, 1.0f);
        color = ColorLerp(color, (Color){ 192, 163, 116, 255 }, polar * 0.20f);
        *outColor = color;
        return true;
    }
    case 7u: {
        float bands = 0.5f + 0.5f * sinf(ny * 31.0f + broad * 2.0f);
        Color color = ColorLerp((Color){ 111, 198, 207, 255 },
                                (Color){ 151, 220, 219, 255 }, bands * 0.18f);
        float hood = Clamp((ny - 0.48f) / 0.52f, 0.0f, 1.0f);
        color = ColorLerp(color, (Color){ 177, 226, 219, 255 }, hood * 0.30f);
        *outColor = color;
        return true;
    }
    case 8u: {
        float bands = 0.5f + 0.5f * sinf(ny * 54.0f + broad * 5.0f);
        Color color = ColorLerp((Color){ 35, 72, 171, 255 },
                                (Color){ 59, 120, 224, 255 },
                                bands * 0.30f + fine * 0.12f);
        float spot = PlanetTextureEllipse(u, v, 0.60f, 0.43f, 0.115f, 0.060f);
        if (spot < 1.0f) {
            color = ColorLerp(color, (Color){ 24, 38, 105, 255 },
                              (1.0f - spot) * 0.76f);
        }
        float brightBand = expf(-powf((ny + 0.34f) / 0.065f, 2.0f));
        color = ColorLerp(color, (Color){ 138, 190, 242, 255 },
                          brightBand * (0.22f + broad * 0.18f));
        *outColor = color;
        return true;
    }
    default:
        return false;
    }
}

static Color StyledPlanetPixel(const PlanetProfile *profile, float nx, float ny, float nz,
                               float u, float v, uint32_t seed,
                               const PlanetSurfaceSample *surfaceSample)
{
    SolarBodyStyle style = profile->style;
    PlanetSurfaceSample surface = *surfaceSample;
    float noise = surface.continentalness;
    float fine = surface.detail;
    Color color = (Color){ 120, 120, 120, 255 };

    if (CanonicalSolarPlanetPixel(profile, nx, ny, nz, u, v, seed,
                                  &surface, &color)) {
        return color;
    }

    switch (style) {
    case SOLAR_STYLE_LAVA: {
        color = ColorLerp((Color){ 23, 18, 21, 255 }, (Color){ 82, 38, 25, 255 }, noise);
        if (surface.biome == PLANET_BIOME_LAVA_SEA) {
            color = ColorLerp((Color){ 180, 48, 16, 255 }, (Color){ 255, 154, 32, 255 },
                              fine);
        }
        float fissure = PlanetLavaFissure(&surface);
        color = ColorLerp(color, (Color){ 255, 115, 18, 255 }, fissure);
        if (fissure > 0.72f) color = ColorLerp(color, (Color){ 255, 225, 88, 255 },
                                               (fissure - 0.72f) / 0.28f);
        break;
    }
    case SOLAR_STYLE_ICE: {
        color = ColorLerp((Color){ 78, 139, 176, 255 },
                          (Color){ 219, 240, 246, 255 }, noise * 0.82f + fabsf(ny) * 0.18f);
        color = ColorLerp(color, (Color){ 24, 76, 126, 255 }, surface.glacierCracks * 0.72f);
        break;
    }
    case SOLAR_STYLE_DESERT: {
        float dunes = surface.duneBand;
        color = ColorLerp((Color){ 139, 72, 36, 255 },
                          (Color){ 238, 183, 91, 255 }, noise * 0.74f + dunes * 0.10f);
        if (fine > 0.72f) color = ColorLerp(color, (Color){ 91, 48, 37, 255 },
                                            (fine - 0.72f) * 2.2f);
        if (surface.biome == PLANET_BIOME_OASIS) {
            color = ColorLerp(color, (Color){ 58, 132, 112, 255 }, 0.66f);
        }
        break;
    }
    case SOLAR_STYLE_GAS: {
        float bands = 0.5f + 0.5f * sinf(ny * 56.0f + noise * 7.0f);
        Color cool = ColorLerp((Color){ 76, 51, 109, 255 },
                               (Color){ 174, 105, 141, 255 }, noise);
        color = ColorLerp(cool, (Color){ 228, 181, 139, 255 }, bands * 0.48f);
        float stormU = 0.28f + PlanetHashUnit(3, 5, 7, seed) * 0.44f;
        float stormV = 0.38f + PlanetHashUnit(11, 2, 9, seed) * 0.24f;
        float du = fabsf(u - stormU);
        du = fminf(du, 1.0f - du);
        float ellipse = sqrtf((du * du) / (0.115f * 0.115f) +
                              ((v - stormV) * (v - stormV)) / (0.043f * 0.043f));
        if (ellipse < 1.0f) {
            float swirl = 0.5f + 0.5f * sinf(ellipse * 28.0f + noise * 8.0f);
            color = ColorLerp(color, (Color){ 242, 204, 174, 255 }, swirl * (1.0f - ellipse));
        }
        break;
    }
    case SOLAR_STYLE_CRATER:
        color = CraterPlanetPixel(nx, ny, nz, noise, seed);
        if (surface.biome == PLANET_BIOME_IMPACT_BASIN) {
            color = ColorLerp(color, (Color){ 51, 56, 63, 255 }, 0.46f);
        }
        break;
    case SOLAR_STYLE_TEMPERATE:
        return TemperatePlanetPixel(profile, ny, &surface);
    default:
        break;
    }

    color = ApplyPlanetClimateColor(color, profile, &surface);
    return color;
}

static Texture2D LoadPlanetTexturePixels(Color *pixels)
{
    Image image = {
        .data = pixels,
        .width = PLANET_TEXTURE_WIDTH,
        .height = PLANET_TEXTURE_HEIGHT,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };
    Texture2D texture = LoadTextureFromImage(image);
    if (texture.id != 0) {
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
    }
    return texture;
}

static void UnloadPlanetTextureSet(PlanetTextureSet *textures)
{
    if (!textures) return;
    if (textures->albedo.id != 0) UnloadTexture(textures->albedo);
    if (textures->material.id != 0) UnloadTexture(textures->material);
    *textures = (PlanetTextureSet){ 0 };
}

static bool PlanetTextureSetIsReady(const PlanetTextureSet *textures)
{
    return textures && textures->albedo.id != 0 && textures->material.id != 0;
}

static PlanetTextureSet MakePlanetSurfaceTextures(const PlanetProfile *profile,
                                                  uint32_t seed)
{
    PlanetTextureSet textures = { 0 };
    size_t pixelCount = (size_t)PLANET_TEXTURE_WIDTH * PLANET_TEXTURE_HEIGHT;
    Color *albedoPixels = malloc(pixelCount * sizeof(*albedoPixels));
    Color *materialPixels = malloc(pixelCount * sizeof(*materialPixels));
    if (!albedoPixels || !materialPixels) {
        free(albedoPixels);
        free(materialPixels);
        return textures;
    }

    for (int y = 0; y < PLANET_TEXTURE_HEIGHT; y++) {
        float v = (float)y / (float)(PLANET_TEXTURE_HEIGHT - 1);
        float latitude = (0.5f - v) * PI;
        float cosLatitude = cosf(latitude);
        for (int x = 0; x < PLANET_TEXTURE_WIDTH; x++) {
            float u = (float)x / (float)PLANET_TEXTURE_WIDTH;
            float longitude = u * 2.0f * PI;
            float nx = cosLatitude * cosf(longitude);
            float ny = sinf(latitude);
            float nz = cosLatitude * sinf(longitude);
            PlanetSurfaceSample surface = PlanetSampleGlobalSurface(seed, profile,
                                                                     longitude, latitude);
            size_t index = (size_t)y * PLANET_TEXTURE_WIDTH + x;
            albedoPixels[index] = StyledPlanetPixel(profile, nx, ny, nz, u, v, seed,
                                                     &surface);
            materialPixels[index] = PlanetMaterialPixel(profile, &surface);
        }
    }

    textures.albedo = LoadPlanetTexturePixels(albedoPixels);
    textures.material = LoadPlanetTexturePixels(materialPixels);
    free(albedoPixels);
    free(materialPixels);
    if (!PlanetTextureSetIsReady(&textures)) {
        UnloadPlanetTextureSet(&textures);
    }
    return textures;
}

static Texture2D MakePlanetCloudTexture(const PlanetProfile *profile, uint32_t seed)
{
    size_t pixelCount = (size_t)PLANET_TEXTURE_WIDTH * PLANET_TEXTURE_HEIGHT;
    Color *pixels = malloc(pixelCount * sizeof(*pixels));
    if (!pixels) return (Texture2D){ 0 };

    for (int y = 0; y < PLANET_TEXTURE_HEIGHT; y++) {
        float v = (float)y / (float)(PLANET_TEXTURE_HEIGHT - 1);
        float latitude = (0.5f - v) * PI;
        float cosLatitude = cosf(latitude);
        for (int x = 0; x < PLANET_TEXTURE_WIDTH; x++) {
            float u = (float)x / (float)PLANET_TEXTURE_WIDTH;
            float longitude = u * 2.0f * PI;
            float nx = cosLatitude * cosf(longitude);
            float ny = sinf(latitude);
            float nz = cosLatitude * sinf(longitude);
            pixels[(size_t)y * PLANET_TEXTURE_WIDTH + x] =
                PlanetCloudPixel(profile, nx, ny, nz, seed);
        }
    }

    Texture2D texture = LoadPlanetTexturePixels(pixels);
    free(pixels);
    return texture;
}

static uint32_t PlanetTextureOceanKey(const PlanetProfile *profile)
{
    return (uint32_t)lroundf(Clamp(profile->oceanCoverage, 0.0f, 1.0f) * 1000.0f);
}

static uint32_t PlanetTextureSeasonKey(const PlanetProfile *profile)
{
    if (!profile || profile->yearLength <= 0.0f) return 0u;
    double cycle = fmod(SpacePeriodicSimulationTime(
                            SpaceElapsedSimulationTime()) /
                        (double)profile->yearLength, 1.0);
    if (cycle < 0.0) cycle += 1.0;
    return (uint32_t)floor(cycle * 8.0);
}

static PlanetTextureSet PlanetTextureForBody(const SpaceBodyInfo *body)
{
    uint32_t oceanKey = PlanetTextureOceanKey(&body->profile);
    uint32_t seasonKey = PlanetTextureSeasonKey(&body->profile);
    planetTextures.textureCacheTick++;
    for (int i = 0; i < PLANET_TEXTURE_CACHE_CAPACITY; i++) {
        PlanetTextureCacheEntry *entry = &planetTextures.planetTextures[i];
        if (!entry->valid || entry->seed != body->worldSeed ||
            entry->canonicalBodyId != body->profile.canonicalBodyId ||
            entry->style != body->style || entry->oceanKey != oceanKey ||
            entry->seasonKey != seasonKey) {
            continue;
        }
        entry->lastUse = planetTextures.textureCacheTick;
        return entry->textures;
    }

    int replacement = 0;
    uint64_t oldestUse = UINT64_MAX;
    for (int i = 0; i < PLANET_TEXTURE_CACHE_CAPACITY; i++) {
        PlanetTextureCacheEntry *entry = &planetTextures.planetTextures[i];
        if (!entry->valid) {
            replacement = i;
            break;
        }
        if (entry->lastUse < oldestUse) {
            oldestUse = entry->lastUse;
            replacement = i;
        }
    }

    PlanetTextureSet textures = MakePlanetSurfaceTextures(&body->profile,
                                                          body->worldSeed);
    if (!PlanetTextureSetIsReady(&textures)) return (PlanetTextureSet){ 0 };

    PlanetTextureCacheEntry *entry = &planetTextures.planetTextures[replacement];
    if (entry->valid) UnloadPlanetTextureSet(&entry->textures);
    *entry = (PlanetTextureCacheEntry){
        .valid = true,
        .seed = body->worldSeed,
        .canonicalBodyId = body->profile.canonicalBodyId,
        .style = body->style,
        .oceanKey = oceanKey,
        .seasonKey = seasonKey,
        .lastUse = planetTextures.textureCacheTick,
        .textures = textures
    };
    return entry->textures;
}

static uint32_t PlanetCloudProfileKey(const PlanetProfile *profile)
{
    if (!profile) return 0u;
    int cloudKey = (int)lroundf(Clamp(profile->cloudCoverage, 0.0f, 1.0f) * 1023.0f);
    int temperatureKey = (int)lroundf(Clamp(profile->equilibriumTempK, 80.0f, 900.0f));
    int windKey = (int)lroundf(Clamp(profile->windStrength, 0.0f, 1.0f) * 1023.0f);
    uint32_t lanes = (uint32_t)profile->style * 0x9e3779b9u ^
                     (uint32_t)profile->atmosphereType * 0x85ebca6bu ^
                     profile->canonicalBodyId * 0xc2b2ae35u;
    return PlanetTextureHash(cloudKey, temperatureKey, windKey, lanes);
}

static bool PlanetHasCloudLayer(const PlanetProfile *profile)
{
    if (!profile || profile->atmosphereType == PLANET_ATMOSPHERE_NONE) return false;
    return PlanetCloudAmountFor(profile) > 0.055f;
}

static Texture2D PlanetCloudTextureForBody(const SpaceBodyInfo *body)
{
    if (!body || !PlanetHasCloudLayer(&body->profile)) return (Texture2D){ 0 };

    uint32_t profileKey = PlanetCloudProfileKey(&body->profile);
    planetTextures.textureCacheTick++;
    for (int i = 0; i < PLANET_CLOUD_CACHE_CAPACITY; i++) {
        PlanetCloudCacheEntry *entry = &planetTextures.cloudTextures[i];
        if (!entry->valid || entry->seed != body->worldSeed ||
            entry->profileKey != profileKey) continue;
        entry->lastUse = planetTextures.textureCacheTick;
        return entry->texture;
    }

    int replacement = 0;
    uint64_t oldestUse = UINT64_MAX;
    for (int i = 0; i < PLANET_CLOUD_CACHE_CAPACITY; i++) {
        PlanetCloudCacheEntry *entry = &planetTextures.cloudTextures[i];
        if (!entry->valid) {
            replacement = i;
            break;
        }
        if (entry->lastUse < oldestUse) {
            oldestUse = entry->lastUse;
            replacement = i;
        }
    }

    Texture2D texture = MakePlanetCloudTexture(&body->profile,
                                               body->worldSeed ^ 0x8392f5u);
    if (texture.id == 0) return (Texture2D){ 0 };

    PlanetCloudCacheEntry *entry = &planetTextures.cloudTextures[replacement];
    if (entry->valid && entry->texture.id != 0) UnloadTexture(entry->texture);
    *entry = (PlanetCloudCacheEntry){
        .valid = true,
        .seed = body->worldSeed,
        .profileKey = profileKey,
        .lastUse = planetTextures.textureCacheTick,
        .texture = texture
    };
    return entry->texture;
}

static float PlanetCloudRotation(const PlanetProfile *profile, uint32_t seed)
{
    float wind = Clamp(profile ? profile->windStrength : 0.0f, 0.0f, 1.0f);
    float phase = PlanetHashUnit(17, 73, 101, seed) * 360.0f;
    float baseRate = profile ? fmaxf(profile->rotationRate, 0.05f) : 1.0f;
    float speed = baseRate * (0.25f + wind * 0.35f) +
                  (0.08f + wind * 1.20f) *
                  (0.82f + PlanetHashUnit(107, 19, 53, seed) * 0.36f);
    float direction = PlanetHashUnit(61, 83, 7, seed) < 0.5f ? -1.0f : 1.0f;
    double angle = (double)phase +
                   SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()) *
                   (double)speed * (double)direction;
    angle = fmod(angle, 360.0);
    if (angle < 0.0) angle += 360.0;
    return (float)angle;
}

static float PlanetCloudShadowStrength(const PlanetProfile *profile)
{
    if (!profile || !profile->hasSolidSurface) return 0.0f;
    float amount = PlanetCloudAmountFor(profile);
    float density = Clamp(profile->atmosphereDensity, 0.0f, 1.0f);
    return Clamp(amount * (0.82f + density * 0.42f), 0.0f, 0.78f);
}

static PlanetProfile HomePlanetRenderProfile(void)
{
    return (PlanetProfile){
        .style = SOLAR_STYLE_TEMPERATE,
        .atmosphereType = PLANET_ATMOSPHERE_BREATHABLE,
        .physicalRadiusKm = SPACE_UNITS_EARTH_RADIUS_KM,
        .massKg = SPACE_UNITS_EARTH_MASS_KG,
        .spaceProxyRadius = 62.0f,
        .hasSolidSurface = true,
        .surfaceGravity = 1.0f,
        .receivedIrradiance = 1.0,
        .radiativeTempK = 255.0f,
        .equilibriumTempK = 288.0f,
        .surfacePressureAtm = 1.0f,
        .atmosphereDensity = 0.78f,
        .oceanCoverage = 0.48f,
        .iceCoverage = 0.10f,
        .cloudCoverage = 0.58f,
        .rotationRate = 1.2f,
        .albedo = 0.30f,
        .greenhouseEffect = 0.84f,
        .axialTilt = 23.4f * DEG2RAD,
        .yearLength = 6400.0f,
        .windStrength = 0.42f
    };
}

static void EnsurePlanetRenderResources(void)
{
    PlanetRendererEnsureResources();

    uint32_t homeSeed = WorldGetSeed();
    PlanetProfile homeProfile = HomePlanetRenderProfile();
    if (!PlanetTextureSetIsReady(&planetTextures.home)) {
        PlanetTextureSet home = MakePlanetSurfaceTextures(&homeProfile, 0x48a1c3u);
        if (PlanetTextureSetIsReady(&home)) {
            UnloadPlanetTextureSet(&planetTextures.home);
            planetTextures.home = home;
        }
    }

    if (planetTextures.homeClouds.id == 0 ||
        planetTextures.homeCloudSeed != homeSeed) {
        Texture2D clouds = MakePlanetCloudTexture(&homeProfile,
                                                  homeSeed ^ 0x8392f5u);
        if (clouds.id != 0) {
            if (planetTextures.homeClouds.id != 0) {
                UnloadTexture(planetTextures.homeClouds);
            }
            planetTextures.homeClouds = clouds;
            planetTextures.homeCloudSeed = homeSeed;
        }
    }
    planetTextures.initialized = PlanetTextureSetIsReady(&planetTextures.home);
}

void UnloadPlanetRenderResources(void)
{
    PlanetRendererShutdown();

    UnloadPlanetTextureSet(&planetTextures.home);
    if (planetTextures.homeClouds.id != 0) {
        UnloadTexture(planetTextures.homeClouds);
    }
    for (int i = 0; i < PLANET_TEXTURE_CACHE_CAPACITY; i++) {
        if (planetTextures.planetTextures[i].valid) {
            UnloadPlanetTextureSet(&planetTextures.planetTextures[i].textures);
        }
    }
    for (int i = 0; i < PLANET_CLOUD_CACHE_CAPACITY; i++) {
        if (planetTextures.cloudTextures[i].valid &&
            planetTextures.cloudTextures[i].texture.id != 0) {
            UnloadTexture(planetTextures.cloudTextures[i].texture);
        }
    }
    planetTextures = (PlanetTextureResources){ 0 };
}

#ifdef RENDER_PLANETS_TESTING
void RenderPlanetsTestResetState(void) { planetTextures = (PlanetTextureResources){ 0 }; }
bool RenderPlanetsTestInitialized(void) { return planetTextures.initialized; }
Color RenderPlanetsTestStyledPixel(const PlanetProfile *p, float nx, float ny, float nz, float u, float v, uint32_t seed, const PlanetSurfaceSample *s) { return StyledPlanetPixel(p, nx, ny, nz, u, v, seed, s); }
Color RenderPlanetsTestCloudPixel(const PlanetProfile *p, float nx, float ny, float nz, uint32_t seed) { return PlanetCloudPixel(p, nx, ny, nz, seed); }
PlanetTextureSet RenderPlanetsTestMakeSurfaceTextures(const PlanetProfile *p, uint32_t seed) { return MakePlanetSurfaceTextures(p, seed); }
PlanetTextureSet RenderPlanetsTestTextureForBody(const SpaceBodyInfo *body)
{ return PlanetTextureForBody(body); }
Texture2D RenderPlanetsTestCloudTextureForBody(const SpaceBodyInfo *body)
{ return PlanetCloudTextureForBody(body); }
void RenderPlanetsTestEnsureResources(void) { EnsurePlanetRenderResources(); }
void RenderPlanetsTestGetHomeResources(PlanetTextureSet *home, Texture2D *clouds,
                                       uint32_t *seed)
{
    if (home) *home = planetTextures.home;
    if (clouds) *clouds = planetTextures.homeClouds;
    if (seed) *seed = planetTextures.homeCloudSeed;
}
void RenderPlanetsTestSetSurfaceCacheEntry(int i, bool valid, PlanetTextureSet t)
{
    if (i >= 0 && i < PLANET_TEXTURE_CACHE_CAPACITY)
        planetTextures.planetTextures[i] = (PlanetTextureCacheEntry){ valid, 0, 0, 0, 0, 0, 0, t };
}
bool RenderPlanetsTestGetSurfaceCacheEntry(int i, PlanetTextureSet *t)
{
    if (i < 0 || i >= PLANET_TEXTURE_CACHE_CAPACITY) return false;
    if (t) *t = planetTextures.planetTextures[i].textures;
    return planetTextures.planetTextures[i].valid;
}
void RenderPlanetsTestSetCloudCacheEntry(int i, bool valid, Texture2D t)
{
    if (i >= 0 && i < PLANET_CLOUD_CACHE_CAPACITY)
        planetTextures.cloudTextures[i] = (PlanetCloudCacheEntry){ valid, 0, 0, 0, t };
}
bool RenderPlanetsTestGetCloudCacheEntry(int i, Texture2D *t)
{
    if (i < 0 || i >= PLANET_CLOUD_CACHE_CAPACITY) return false;
    if (t) *t = planetTextures.cloudTextures[i].texture;
    return planetTextures.cloudTextures[i].valid;
}
#endif

bool PlanetRenderSurfaceVisual(bool planetSurface,
                               PlanetTextureSet *outTextures,
                               Color *outFallback)
{
    if (!outTextures || !outFallback) return false;
    EnsurePlanetRenderResources();
    if (!planetSurface) {
        *outTextures = planetTextures.home;
        *outFallback = HomePlanetColor();
        return PlanetTextureSetIsReady(outTextures);
    }
    if (!PlanetWorldIsActive()) return false;

    const PlanetProfile *activeProfile = PlanetWorldProfile();
    if (!activeProfile) return false;
    PlanetProfile profile = *activeProfile;
    SpaceBodyInfo body = {
        .profile = profile,
        .style = PlanetWorldStyle(),
        .worldSeed = PlanetWorldSeed()
    };
    *outTextures = PlanetTextureForBody(&body);
    switch (body.style) {
        case SOLAR_STYLE_LAVA: *outFallback = (Color){ 187, 70, 34, 255 }; break;
        case SOLAR_STYLE_ICE: *outFallback = (Color){ 151, 191, 219, 255 }; break;
        case SOLAR_STYLE_DESERT: *outFallback = (Color){ 190, 151, 87, 255 }; break;
        case SOLAR_STYLE_CRATER: *outFallback = (Color){ 139, 137, 132, 255 }; break;
        case SOLAR_STYLE_TEMPERATE: *outFallback = (Color){ 67, 132, 101, 255 }; break;
        case SOLAR_STYLE_GAS: *outFallback = (Color){ 176, 139, 99, 255 }; break;
        case SOLAR_STYLE_SUN:
        default: *outFallback = (Color){ 216, 181, 104, 255 }; break;
    }
    return PlanetTextureSetIsReady(outTextures);
}

static Vector3 PlanetShaderColor(Color color)
{
    return (Vector3){ (float)color.r / 255.0f,
                      (float)color.g / 255.0f,
                      (float)color.b / 255.0f };
}

static PlanetMaterialResponse PlanetMaterialResponseFor(const PlanetProfile *profile,
                                                        bool cloudLayer)
{
    if (cloudLayer) {
        return (PlanetMaterialResponse){
            .roughness = 0.82f,
            .specular = 0.36f,
            .metallic = 0.0f,
            .model = 7
        };
    }

    PlanetMaterialResponse response = {
        .roughness = 0.78f,
        .specular = 0.24f,
        .metallic = 0.0f,
        .model = profile ? (int)profile->style : 0
    };
    if (!profile) return response;

    switch (profile->style) {
    case SOLAR_STYLE_LAVA:
        response.roughness = 0.58f;
        response.specular = 0.42f;
        response.metallic = 0.08f;
        break;
    case SOLAR_STYLE_ICE:
        response.roughness = 0.28f;
        response.specular = 0.78f;
        response.metallic = 0.02f;
        break;
    case SOLAR_STYLE_DESERT:
        response.roughness = 0.86f;
        response.specular = 0.18f;
        break;
    case SOLAR_STYLE_GAS:
        response.roughness = 0.41f;
        response.specular = 0.72f;
        break;
    case SOLAR_STYLE_CRATER:
        response.roughness = 0.92f;
        response.specular = 0.14f;
        break;
    case SOLAR_STYLE_TEMPERATE:
        response.roughness = 0.64f;
        response.specular = 0.38f;
        break;
    default:
        break;
    }

    if (profile->atmosphereType == PLANET_ATMOSPHERE_NONE) {
        response.roughness = Clamp(response.roughness + 0.035f, 0.045f, 1.0f);
    }
    return response;
}

static PlanetSpaceLighting PlanetSpaceLightingFor(int systemAnchorX, int systemAnchorZ,
                                                   Vector3 planetCenter)
{
    PlanetSpaceLighting lighting = { 0 };
    SolarSystemDef system = { 0 };
    SolarLightSource sources[MAX_SOLAR_LIGHTS];
    if (!StarSystemAt(systemAnchorX, systemAnchorZ, &system)) return lighting;

    lighting.count = SolarSystemLightSources(&system, sources, MAX_SOLAR_LIGHTS);
    if (lighting.count <= 0) return lighting;

    lighting.exposure = planetSceneExposure;
    for (int i = 0; i < lighting.count; i++) {
        Color color = SpectrumColor(sources[i].spectrum);
        lighting.positions[i] = sources[i].center;
        lighting.colors[i] = PlanetShaderColor(color);
        lighting.intensities[i] = SolarLightIrradianceAt(&sources[i], planetCenter);
    }
    return lighting;
}

static void DrawPlanetAtmosphere(const Camera3D *camera, Vector3 center, float radius,
                                 const PlanetProfile *profile,
                                 const PlanetSpaceLighting *lighting, float alpha)
{
    if (!camera || !profile ||
        profile->atmosphereType == PLANET_ATMOSPHERE_NONE || alpha <= 0.0f) {
        return;
    }

    PlanetAtmosphereVisual visual = PlanetAtmosphereVisualFor(profile);
    PlanetRendererDrawAtmosphere(&(PlanetAtmosphereDrawParams){
        .center = center,
        .radius = radius,
        .cameraPosition = camera->position,
        .rayleighColor = visual.haze,
        .horizonColor = visual.horizon,
        .density = profile->atmosphereDensity,
        .opticalDepth = visual.opticalDepth,
        .mieStrength = visual.mieStrength,
        .scaleHeight = visual.scaleHeight,
        .alpha = alpha,
        .sceneExposure = planetSceneExposure,
        .lighting = lighting
    });
}

static PlanetRingLayer PlanetRingLayerFor(Vector3 center, float radius, float tilt,
                                          uint32_t seed)
{
    return (PlanetRingLayer){
        .center = center,
        .normal = { 0.0f, cosf(tilt), sinf(tilt) },
        .radii = { radius * 1.30f, radius * 1.86f },
        .shadowParams = {
            PlanetHashUnit(131, 47, 19, seed) * 2.0f * PI,
            0.78f
        }
    };
}

static Vector3 PlanetRingPoint(const PlanetRingLayer *ring, float radius, float angle)
{
    float c = cosf(angle);
    float s = sinf(angle);
    return (Vector3){ ring->center.x + c * radius,
                      ring->center.y - s * radius * ring->normal.z,
                      ring->center.z + s * radius * ring->normal.y };
}

static float PlanetRingDensity(float radialFraction, float phase)
{
    float broad = 0.5f + 0.5f * sinf(radialFraction * 29.0f + phase);
    float fine = 0.5f + 0.5f * sinf(radialFraction * 73.0f + phase * 1.73f);
    float gapCenterA = 0.22f + 0.18f * (0.5f + 0.5f * sinf(phase * 0.71f));
    float gapCenterB = 0.62f + 0.18f * (0.5f + 0.5f * sinf(phase * 1.13f + 1.7f));
    float gapACoord = (radialFraction - gapCenterA) / 0.028f;
    float gapBCoord = (radialFraction - gapCenterB) / 0.045f;
    float gapA = expf(-gapACoord * gapACoord);
    float gapB = expf(-gapBCoord * gapBCoord);
    return Clamp(0.10f + broad * 0.54f + fine * 0.24f -
                 fmaxf(gapA, gapB) * 0.72f, 0.008f, 0.94f);
}

static Color PlanetRingParticleColor(SolarBodyStyle style, float density,
                                     float radialFraction, float phase)
{
    Color sparse = { 132, 122, 126, 255 };
    Color dense = { 224, 207, 184, 255 };
    if (style == SOLAR_STYLE_ICE) {
        sparse = (Color){ 132, 150, 166, 255 };
        dense = (Color){ 221, 233, 236, 255 };
    } else if (style == SOLAR_STYLE_CRATER) {
        sparse = (Color){ 128, 127, 125, 255 };
        dense = (Color){ 211, 205, 194, 255 };
    } else if (style == SOLAR_STYLE_LAVA) {
        sparse = (Color){ 116, 101, 101, 255 };
        dense = (Color){ 195, 164, 142, 255 };
    }
    float mineralVariation = 0.5f + 0.5f * sinf(radialFraction * 23.0f + phase * 0.61f);
    float colorMix = Clamp(0.12f + density * 0.74f + mineralVariation * 0.14f,
                           0.0f, 1.0f);
    return ColorLerp(sparse, dense, colorMix);
}

static float PlanetRingPlanetTransmission(Vector3 ringPoint, Vector3 lightPosition,
                                           Vector3 planetCenter, float planetRadius)
{
    Vector3 toLight = Vector3Subtract(lightPosition, ringPoint);
    float lightDistanceSqr = Vector3LengthSqr(toLight);
    if (lightDistanceSqr <= 0.0001f) return 1.0f;
    Vector3 lightDirection = Vector3Scale(toLight, 1.0f / sqrtf(lightDistanceSqr));
    Vector3 toCenter = Vector3Subtract(planetCenter, ringPoint);
    float alongRay = Vector3DotProduct(toCenter, lightDirection);
    if (alongRay <= 0.0f) return 1.0f;

    Vector3 closestPoint = Vector3Add(ringPoint,
                                      Vector3Scale(lightDirection, alongRay));
    float closestDistanceSqr = Vector3DistanceSqr(closestPoint, planetCenter);
    float radiusSqr = planetRadius * planetRadius;
    if (closestDistanceSqr >= radiusSqr) return 1.0f;
    float halfChord = sqrtf(fmaxf(radiusSqr - closestDistanceSqr, 0.0f));
    return alongRay - halfChord > 0.0f ? 0.025f : 1.0f;
}

static Color PlanetRingLitColor(Color albedo, Vector3 ringPoint, Vector3 ringNormal,
                                Vector3 planetCenter, float planetRadius, float density,
                                float alpha, const PlanetSpaceLighting *lighting)
{
    Vector3 illumination = { 0.055f, 0.058f, 0.064f };
    if (lighting) {
        for (int i = 0; i < lighting->count; i++) {
            Vector3 toLight = Vector3Subtract(lighting->positions[i], ringPoint);
            float distanceSqr = Vector3LengthSqr(toLight);
            if (distanceSqr <= 0.0001f) continue;
            Vector3 lightDirection = Vector3Scale(toLight, 1.0f / sqrtf(distanceSqr));
            float incidence = fabsf(Vector3DotProduct(ringNormal, lightDirection));
            float scattering = 0.24f + incidence * 0.76f;
            float transmission = PlanetRingPlanetTransmission(
                ringPoint, lighting->positions[i], planetCenter, planetRadius);
            float strength = lighting->intensities[i] * scattering * transmission;
            illumination.x += lighting->colors[i].x * strength;
            illumination.y += lighting->colors[i].y * strength;
            illumination.z += lighting->colors[i].z * strength;
        }
    }

    float opacity = alpha * (0.015f + density * 0.84f);
    float exposure = lighting && lighting->exposure > 0.0f ?
                     lighting->exposure : planetSceneExposure;
    float mappedR = 1.0f - expf(-Clamp(illumination.x, 0.0f, 8.0f) * exposure);
    float mappedG = 1.0f - expf(-Clamp(illumination.y, 0.0f, 8.0f) * exposure);
    float mappedB = 1.0f - expf(-Clamp(illumination.z, 0.0f, 8.0f) * exposure);
    return (Color){
        PlanetColorChannel((float)albedo.r * mappedR),
        PlanetColorChannel((float)albedo.g * mappedG),
        PlanetColorChannel((float)albedo.b * mappedB),
        PlanetColorChannel(Clamp(opacity, 0.0f, 0.94f) * 255.0f)
    };
}

static void DrawPlanetRings(const PlanetRingLayer *ring, float planetRadius,
                            SolarBodyStyle style, float alpha,
                            const PlanetSpaceLighting *lighting)
{
    if (!ring || alpha <= 0.0f) return;
    const int angularSegments = 72;
    const int radialSegments = 24;
    float ringWidth = ring->radii.y - ring->radii.x;

    BeginBlendMode(BLEND_ALPHA);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    for (int radial = 0; radial < radialSegments; radial++) {
        float radial0 = (float)radial / (float)radialSegments;
        float radial1 = (float)(radial + 1) / (float)radialSegments;
        float radialCenter = (radial0 + radial1) * 0.5f;
        float density = PlanetRingDensity(radialCenter, ring->shadowParams.x);
        float innerRadius = ring->radii.x + ringWidth * radial0;
        float outerRadius = ring->radii.x + ringWidth * radial1;
        Color albedo = PlanetRingParticleColor(style, density, radialCenter,
                                               ring->shadowParams.x);
        for (int segment = 0; segment < angularSegments; segment++) {
            float a0 = (float)segment * 2.0f * PI / (float)angularSegments;
            float a1 = (float)(segment + 1) * 2.0f * PI / (float)angularSegments;
            float centerAngle = (a0 + a1) * 0.5f;
            Vector3 midpoint = PlanetRingPoint(ring,
                                                (innerRadius + outerRadius) * 0.5f,
                                                centerAngle);
            Color color = PlanetRingLitColor(albedo, midpoint, ring->normal,
                                             ring->center, planetRadius, density,
                                             alpha, lighting);
            Vector3 i0 = PlanetRingPoint(ring, innerRadius, a0);
            Vector3 i1 = PlanetRingPoint(ring, innerRadius, a1);
            Vector3 o0 = PlanetRingPoint(ring, outerRadius, a0);
            Vector3 o1 = PlanetRingPoint(ring, outerRadius, a1);
            DrawTriangle3D(i0, o0, o1, color);
            DrawTriangle3D(i0, o1, i1, color);
        }
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    EndBlendMode();
}

float CelestialRadiusPixels(const Camera3D *camera, Vector3 center,
                            float radius)
{
    float distance = Vector3Distance(camera->position, center);
    if (!(distance > 0.0f) || !(radius > 0.0f)) return 0.0f;
    float focalPixels = (float)GetScreenHeight() /
        (2.0f * tanf(camera->fovy * DEG2RAD * 0.5f));
    return radius / distance * focalPixels;
}

#define CELESTIAL_RENDER_NEAREST_SURFACE 0.075f

static float CelestialRenderScale(const Camera3D *camera, Vector3 center,
                                  float radius)
{
    float distance = Vector3Distance(camera->position, center);
    float nearestSurface = distance - radius;
    if (!(nearestSurface > 0.0f) ||
        nearestSurface >= CELESTIAL_RENDER_NEAREST_SURFACE) {
        return 1.0f;
    }
    return CELESTIAL_RENDER_NEAREST_SURFACE / nearestSurface;
}

static Vector3 CelestialRenderPoint(const Camera3D *camera, Vector3 point,
                                    float scale)
{
    return Vector3Add(camera->position,
                      Vector3Scale(Vector3Subtract(point, camera->position),
                                   scale));
}

static void CelestialRenderLighting(const Camera3D *camera, float scale,
                                    PlanetSpaceLighting *lighting)
{
    if (!lighting || scale <= 1.0f) return;
    for (int i = 0; i < lighting->count; i++) {
        lighting->positions[i] = CelestialRenderPoint(
            camera, lighting->positions[i], scale);
    }
}

void DrawSolarBodies(const Camera3D *camera, float spaceFade)
{
    if (spaceFade <= 0.05f) return;

    EnsurePlanetRenderResources();

    SpaceBodyInfo bodies[48];
    int count = SpaceBodiesNear(camera->position, SOLAR_SYSTEM_QUERY_RADIUS,
                                bodies, 48);
    for (int i = 0; i < count; i++) {
        if (!bodies[i].isStar && bodies[i].systemAnchorX == 0 &&
            bodies[i].systemAnchorZ == 0 && bodies[i].bodyId == 3u) {
            continue;
        }
        Color color = bodies[i].isStar ? SpectrumColor(bodies[i].spectrum)
                                      : SolarStyleColor(bodies[i].style);
        if (bodies[i].isStar) {
            float physicalRadius = bodies[i].physicalRadiusGame;
            if (CelestialRadiusPixels(camera, bodies[i].center,
                                      physicalRadius) < 0.75f) {
                continue;
            }
            float renderScale = CelestialRenderScale(
                camera, bodies[i].center, physicalRadius);
            Vector3 renderCenter = CelestialRenderPoint(
                camera, bodies[i].center, renderScale);
            float renderRadius = physicalRadius * renderScale;
            if (bodies[i].remnant.active) {
                Color remnantColor = bodies[i].remnant.blackHole
                    ? (Color){ 120, 150, 255, 255 }
                    : (Color){ 255, 120, 90, 255 };
                float remnantAlpha = (0.10f +
                                      bodies[i].remnant.ejectaStrength * 0.24f) *
                                     spaceFade;
                float shockRadius =
                    (float)SpaceUnitsKilometersToGameDistance(
                        bodies[i].remnant.physicalShockRadiusKm) * renderScale;
                if (shockRadius > 0.0f) {
                    DrawSphereWires(renderCenter, shockRadius, 20, 20,
                                    Fade(remnantColor, remnantAlpha));
                }
            }
            DrawSphere(renderCenter, renderRadius * 1.08f, color);
            DrawSphere(renderCenter, renderRadius * 1.15f,
                       Fade(color, 0.12f * spaceFade));
        } else {
            float physicalRadius = bodies[i].physicalRadiusGame;
            if (CelestialRadiusPixels(camera, bodies[i].center,
                                      physicalRadius) < 0.75f) {
                continue;
            }
            float renderScale = CelestialRenderScale(
                camera, bodies[i].center, physicalRadius);
            Vector3 renderCenter = CelestialRenderPoint(
                camera, bodies[i].center, renderScale);
            float renderRadius = physicalRadius * renderScale;
            PlanetTextureSet textures = PlanetTextureForBody(&bodies[i]);
            float rotation = PlanetBodyTextureRotation(&bodies[i]);
            PlanetSpaceLighting lighting = PlanetSpaceLightingFor(
                bodies[i].systemAnchorX, bodies[i].systemAnchorZ, bodies[i].center);
            CelestialRenderLighting(camera, renderScale, &lighting);
            float atmosphereAlpha = 0.08f + bodies[i].profile.atmosphereDensity * 0.54f;
            float ambientLight = 0.025f + bodies[i].profile.atmosphereDensity * 0.040f;
            float emissiveStrength = bodies[i].style == SOLAR_STYLE_LAVA ? 0.82f : 0.0f;
            PlanetCloudLayer cloudLayer = { 0 };
            if (PlanetHasCloudLayer(&bodies[i].profile)) {
                cloudLayer.texture = PlanetCloudTextureForBody(&bodies[i]);
                cloudLayer.rotation = PlanetCloudRotation(&bodies[i].profile,
                                                          bodies[i].worldSeed ^ 0x8392f5u);
                cloudLayer.shadowStrength = PlanetCloudShadowStrength(&bodies[i].profile);
            }
            PlanetRingLayer ringLayer = { 0 };
            PlanetRingLayer *activeRing = NULL;
            if (bodies[i].profile.hasRings) {
                ringLayer = PlanetRingLayerFor(renderCenter, renderRadius,
                                               bodies[i].profile.ringTilt,
                                               bodies[i].worldSeed);
                activeRing = &ringLayer;
            }
            PlanetMaterialResponse material = PlanetMaterialResponseFor(
                &bodies[i].profile, false);
            PlanetRendererDrawSurface(&(PlanetSurfaceDrawParams){
                .center = renderCenter,
                .radius = renderRadius,
                .textures = textures,
                .rotation = rotation,
                .fallback = color,
                .cameraPosition = camera->position,
                .lighting = &lighting,
                .material = &material,
                .ambientLight = ambientLight,
                .emissiveStrength = emissiveStrength,
                .sceneExposure = planetSceneExposure,
                .cloudLayer = &cloudLayer,
                .ringLayer = activeRing
            });
            if (cloudLayer.texture.id != 0) {
                PlanetMaterialResponse cloudMaterial = PlanetMaterialResponseFor(
                    &bodies[i].profile, true);
                PlanetRendererDrawSurface(&(PlanetSurfaceDrawParams){
                    .center = renderCenter,
                    .radius = renderRadius * 1.014f,
                    .textures = { .albedo = cloudLayer.texture },
                    .rotation = cloudLayer.rotation,
                    .fallback = WHITE,
                    .cameraPosition = camera->position,
                    .lighting = &lighting,
                    .material = &cloudMaterial,
                    .ambientLight = ambientLight,
                    .emissiveStrength = 0.0f,
                    .sceneExposure = planetSceneExposure
                });
            }
            DrawPlanetAtmosphere(camera, renderCenter, renderRadius,
                                 &bodies[i].profile, &lighting,
                                 atmosphereAlpha * spaceFade);
            if (activeRing) {
                DrawPlanetRings(activeRing, renderRadius, bodies[i].style,
                                spaceFade, &lighting);
            }
        }
    }

    SpaceSatelliteInfo satellites[48];
    int satelliteCount = SpaceSatellitesNear(
        camera->position, SOLAR_SYSTEM_QUERY_RADIUS, satellites, 48);
    for (int i = 0; i < satelliteCount; i++) {
        float radius = (float)SpaceUnitsKilometersToGameDistance(
            satellites[i].physicalRadiusKm);
        if (CelestialRadiusPixels(camera, satellites[i].center, radius) <
            0.75f) continue;
        float renderScale = CelestialRenderScale(
            camera, satellites[i].center, radius);
        DrawSphere(CelestialRenderPoint(camera, satellites[i].center,
                                        renderScale),
                   radius * renderScale,
                   (Color){ 174, 180, 188, 255 });
    }
}

static Color HomePlanetColor(void)
{
    if (WorldTerrainMode() == TERRAIN_FLAT) return (Color){ 72, 138, 88, 255 };

    switch (BiomeAt(0, 0)) {
    case BIOME_DESERT:  return (Color){ 184, 140, 76, 255 };
    case BIOME_SNOW:    return (Color){ 158, 184, 210, 255 };
    case BIOME_MOUNTAIN: return (Color){ 116, 142, 158, 255 };
    case BIOME_FOREST:  return (Color){ 48, 116, 72, 255 };
    case BIOME_PLAINS:
    default:            return (Color){ 70, 142, 92, 255 };
    }
}

void DrawHomePlanet(const Camera3D *camera, float spaceFade)
{
    if (spaceFade <= 0.05f) return;

    const Vector3 physicalCenter = HomeWorldCenter();
    const float physicalRadius = (float)SpaceUnitsKilometersToGameDistance(
        SPACE_UNITS_EARTH_RADIUS_KM);
    if (CelestialRadiusPixels(camera, physicalCenter, physicalRadius) < 0.75f) {
        return;
    }
    float distance = Vector3Distance(camera->position, physicalCenter);
    if (distance <= physicalRadius * 1.05f || distance > 24000.0f) return;

    float renderScale = CelestialRenderScale(
        camera, physicalCenter, physicalRadius);
    const Vector3 center = CelestialRenderPoint(
        camera, physicalCenter, renderScale);
    const float radius = physicalRadius * renderScale;

    EnsurePlanetRenderResources();
    PlanetProfile homeAtmosphere = HomePlanetRenderProfile();
    float homeRotation = -18.0f +
                         (float)SpacePeriodicSimulationTime(
                             SpaceElapsedSimulationTime()) * 1.2f;
    PlanetSpaceLighting lighting = PlanetSpaceLightingFor(0, 0, physicalCenter);
    CelestialRenderLighting(camera, renderScale, &lighting);
    PlanetCloudLayer cloudLayer = {
        .texture = planetTextures.homeClouds,
        .rotation = PlanetCloudRotation(&homeAtmosphere,
                                         planetTextures.homeCloudSeed ^ 0x8392f5u),
        .shadowStrength = PlanetCloudShadowStrength(&homeAtmosphere)
    };
    PlanetMaterialResponse material = PlanetMaterialResponseFor(&homeAtmosphere, false);
    PlanetRendererDrawSurface(&(PlanetSurfaceDrawParams){
        .center = center,
        .radius = radius,
        .textures = planetTextures.home,
        .rotation = homeRotation,
        .fallback = HomePlanetColor(),
        .cameraPosition = camera->position,
        .lighting = &lighting,
        .material = &material,
        .ambientLight = 0.056f,
        .emissiveStrength = 0.0f,
        .sceneExposure = planetSceneExposure,
        .cloudLayer = &cloudLayer
    });
    if (cloudLayer.texture.id != 0) {
        PlanetMaterialResponse cloudMaterial = PlanetMaterialResponseFor(
            &homeAtmosphere, true);
        PlanetRendererDrawSurface(&(PlanetSurfaceDrawParams){
            .center = center,
            .radius = radius * 1.014f,
            .textures = { .albedo = cloudLayer.texture },
            .rotation = cloudLayer.rotation,
            .fallback = WHITE,
            .cameraPosition = camera->position,
            .lighting = &lighting,
            .material = &cloudMaterial,
            .ambientLight = 0.056f,
            .emissiveStrength = 0.0f,
            .sceneExposure = planetSceneExposure
        });
    }
    DrawPlanetAtmosphere(camera, center, radius, &homeAtmosphere,
                         &lighting, 0.62f * spaceFade);
}
