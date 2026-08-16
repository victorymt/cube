#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"

#define PLANET_TEXTURE_WIDTH 8
#define PLANET_TEXTURE_HEIGHT 4

static unsigned int textureResults[64];
static size_t textureResultCount;
static size_t textureResultIndex;
static int textureLoadCalls;
static unsigned int unloadedTextureIds[128];
static size_t unloadedTextureCount;
static uint32_t testWorldSeed;
static int rendererEnsureCalls;
static int rendererShutdownCalls;

#include "../src/presentation/render_planets.c"

Texture2D LoadTextureFromImage(Image image)
{
    (void)image;
    textureLoadCalls++;
    unsigned int id = textureResultIndex < textureResultCount
                          ? textureResults[textureResultIndex++]
                          : 0;
    return (Texture2D){ .id = id };
}

void SetTextureFilter(Texture2D texture, int filter)
{
    (void)texture;
    (void)filter;
}

void SetTextureWrap(Texture2D texture, int wrap)
{
    (void)texture;
    (void)wrap;
}

void UnloadTexture(Texture2D texture)
{
    if (texture.id != 0 && unloadedTextureCount <
        sizeof(unloadedTextureIds) / sizeof(unloadedTextureIds[0])) {
        unloadedTextureIds[unloadedTextureCount++] = texture.id;
    }
}

PlanetSurfaceSample PlanetSampleGlobalSurface(uint32_t seed, const PlanetProfile *profile,
                                              float longitude, float latitude)
{
    (void)seed;
    (void)profile;
    (void)longitude;
    (void)latitude;
    return (PlanetSurfaceSample){
        .continentalness = 0.52f,
        .regionalness = 0.35f,
        .climate = 0.5f,
        .detail = 0.4f,
        .temperature = 288.0f,
        .meanTemperature = 288.0f,
        .seasonalAmplitude = 0.0f,
        .moisture = 0.4f,
        .biome = PLANET_BIOME_CRATER_HIGHLANDS
    };
}

double SpaceElapsedSimulationTime(void)
{
    return 0.0;
}

double SpacePeriodicSimulationTime(double elapsedTime)
{
    return elapsedTime;
}

uint32_t WorldGetSeed(void)
{
    return testWorldSeed;
}

void PlanetRendererEnsureResources(void)
{
    rendererEnsureCalls++;
}

void PlanetRendererShutdown(void)
{
    rendererShutdownCalls++;
}

static void ResetTextureMocks(void)
{
    planetTextures = (PlanetTextureResources){ 0 };
    memset(textureResults, 0, sizeof(textureResults));
    textureResultCount = 0;
    textureResultIndex = 0;
    textureLoadCalls = 0;
    memset(unloadedTextureIds, 0, sizeof(unloadedTextureIds));
    unloadedTextureCount = 0;
    testWorldSeed = 1;
    rendererEnsureCalls = 0;
    rendererShutdownCalls = 0;
}

static void QueueTextures(const unsigned int *ids, size_t count)
{
    assert(count <= sizeof(textureResults) / sizeof(textureResults[0]));
    memcpy(textureResults, ids, count * sizeof(*ids));
    textureResultCount = count;
    textureResultIndex = 0;
}

static PlanetProfile TestProfile(bool clouds)
{
    return (PlanetProfile){
        .style = SOLAR_STYLE_CRATER,
        .atmosphereType = clouds ? PLANET_ATMOSPHERE_BREATHABLE : PLANET_ATMOSPHERE_NONE,
        .hasSolidSurface = true,
        .equilibriumTempK = 288.0f,
        .oceanCoverage = 0.2f,
        .cloudCoverage = clouds ? 0.6f : 0.0f,
        .windStrength = 0.2f,
        .yearLength = 0.0f
    };
}

static SpaceBodyInfo TestBody(uint32_t seed, bool clouds)
{
    SpaceBodyInfo body = { 0 };
    body.worldSeed = seed;
    body.style = SOLAR_STYLE_CRATER;
    body.profile = TestProfile(clouds);
    return body;
}

static int ColorDistance(Color left, Color right)
{
    return abs((int)left.r - (int)right.r) +
           abs((int)left.g - (int)right.g) +
           abs((int)left.b - (int)right.b);
}

static Color CanonicalTexturePixel(uint32_t bodyId)
{
    PlanetProfile profile = TestProfile(true);
    profile.canonicalBodyId = bodyId;
    profile.style = bodyId >= 5u ? SOLAR_STYLE_GAS :
                    bodyId == 2u ? SOLAR_STYLE_LAVA :
                    bodyId == 3u ? SOLAR_STYLE_TEMPERATE :
                    bodyId == 4u ? SOLAR_STYLE_DESERT : SOLAR_STYLE_CRATER;
    PlanetSurfaceSample surface = {
        .continentalness = 0.52f,
        .regionalness = 0.35f,
        .detail = 0.40f,
        .temperature = bodyId == 2u ? 737.0f : 288.0f,
        .moisture = 0.4f,
        .biome = bodyId == 3u ? PLANET_BIOME_OCEAN
                              : PLANET_BIOME_CRATER_HIGHLANDS
    };
    return StyledPlanetPixel(&profile, 0.82f, 0.18f, 0.54f,
                             0.41f, 0.44f, 0x39217u, &surface);
}

static void TestCanonicalTexturesAreDistinct(void)
{
    Color jupiter = CanonicalTexturePixel(5u);
    Color saturn = CanonicalTexturePixel(6u);
    Color uranus = CanonicalTexturePixel(7u);
    Color neptune = CanonicalTexturePixel(8u);
    assert(ColorDistance(jupiter, saturn) > 18);
    assert(ColorDistance(saturn, uranus) > 40);
    assert(ColorDistance(uranus, neptune) > 80);
    assert(jupiter.r > jupiter.b);
    assert(saturn.r > saturn.b);
    assert(uranus.g > uranus.r && uranus.b > uranus.r);
    assert(neptune.b > neptune.r * 2);

    PlanetProfile venus = TestProfile(true);
    venus.canonicalBodyId = 2u;
    venus.style = SOLAR_STYLE_LAVA;
    venus.atmosphereType = PLANET_ATMOSPHERE_CORROSIVE;
    venus.cloudCoverage = 0.98f;
    Color venusCloud = PlanetCloudPixel(&venus, 0.82f, 0.18f, 0.54f, 17u);
    assert(venusCloud.a > 220);
    assert(venusCloud.r > venusCloud.g && venusCloud.g > venusCloud.b);
}

static void TestCanonicalIdentityIsPartOfTextureCacheKey(void)
{
    ResetTextureMocks();
    SpaceBodyInfo body = TestBody(731u, false);
    body.style = SOLAR_STYLE_GAS;
    body.profile.style = SOLAR_STYLE_GAS;
    body.profile.canonicalBodyId = 5u;
    const unsigned int ids[] = { 701, 702, 703, 704 };
    QueueTextures(ids, 4);
    PlanetTextureSet jupiter = PlanetTextureForBody(&body);
    assert(jupiter.albedo.id == 701 && jupiter.material.id == 702);

    body.profile.canonicalBodyId = 6u;
    PlanetTextureSet saturn = PlanetTextureForBody(&body);
    assert(saturn.albedo.id == 703 && saturn.material.id == 704);
    assert(textureLoadCalls == 4);
}

static void TestPartialSurfaceCreationRollsBack(void)
{
    ResetTextureMocks();
    PlanetProfile profile = TestProfile(false);
    const unsigned int ids[] = { 101, 0 };
    QueueTextures(ids, 2);

    PlanetTextureSet textures = MakePlanetSurfaceTextures(&profile, 3);
    assert(textures.albedo.id == 0);
    assert(textures.material.id == 0);
    assert(unloadedTextureCount == 1);
    assert(unloadedTextureIds[0] == 101);
}

static void FillSurfaceCache(void)
{
    for (int i = 0; i < PLANET_TEXTURE_CACHE_CAPACITY; i++) {
        planetTextures.planetTextures[i] = (PlanetTextureCacheEntry){
            .valid = true,
            .seed = (uint32_t)(100 + i),
            .style = SOLAR_STYLE_LAVA,
            .oceanKey = 77,
            .seasonKey = 0,
            .lastUse = (uint64_t)(i + 1),
            .textures = {
                .albedo = { .id = (unsigned int)(1000 + i) },
                .material = { .id = (unsigned int)(2000 + i) }
            }
        };
    }
}

static void TestSurfaceCacheKeepsOldEntryOnFailure(void)
{
    ResetTextureMocks();
    FillSurfaceCache();
    SpaceBodyInfo body = TestBody(9000, false);
    const unsigned int failed[] = { 0, 0 };
    QueueTextures(failed, 2);

    PlanetTextureSet textures = PlanetTextureForBody(&body);
    assert(textures.albedo.id == 0);
    assert(textures.material.id == 0);
    assert(planetTextures.planetTextures[0].valid);
    assert(planetTextures.planetTextures[0].textures.albedo.id == 1000);
    assert(unloadedTextureCount == 0);

    const unsigned int created[] = { 301, 302 };
    QueueTextures(created, 2);
    textures = PlanetTextureForBody(&body);
    assert(textures.albedo.id == 301);
    assert(textures.material.id == 302);
    assert(unloadedTextureCount == 2);
    assert(unloadedTextureIds[0] == 1000);
    assert(unloadedTextureIds[1] == 2000);
}

static void FillCloudCache(void)
{
    for (int i = 0; i < PLANET_CLOUD_CACHE_CAPACITY; i++) {
        planetTextures.cloudTextures[i] = (PlanetCloudCacheEntry){
            .valid = true,
            .seed = (uint32_t)(100 + i),
            .profileKey = (uint32_t)(200 + i),
            .lastUse = (uint64_t)(i + 1),
            .texture = { .id = (unsigned int)(3000 + i) }
        };
    }
}

static void TestCloudCacheKeepsOldEntryOnFailure(void)
{
    ResetTextureMocks();
    FillCloudCache();
    SpaceBodyInfo body = TestBody(9001, true);
    const unsigned int failed[] = { 0 };
    QueueTextures(failed, 1);

    Texture2D texture = PlanetCloudTextureForBody(&body);
    assert(texture.id == 0);
    assert(planetTextures.cloudTextures[0].valid);
    assert(planetTextures.cloudTextures[0].texture.id == 3000);
    assert(unloadedTextureCount == 0);

    const unsigned int created[] = { 401 };
    QueueTextures(created, 1);
    texture = PlanetCloudTextureForBody(&body);
    assert(texture.id == 401);
    assert(unloadedTextureCount == 1);
    assert(unloadedTextureIds[0] == 3000);
}

static void TestHomeResourcesRetryIndependently(void)
{
    ResetTextureMocks();
    testWorldSeed = 7;
    const unsigned int first[] = { 501, 0, 502 };
    QueueTextures(first, 3);
    EnsurePlanetRenderResources();
    assert(!planetTextures.initialized);
    assert(planetTextures.home.albedo.id == 0);
    assert(planetTextures.homeClouds.id == 502);
    assert(planetTextures.homeCloudSeed == 7);
    assert(rendererEnsureCalls == 1);

    const unsigned int second[] = { 503, 504 };
    QueueTextures(second, 2);
    EnsurePlanetRenderResources();
    assert(planetTextures.initialized);
    assert(planetTextures.home.albedo.id == 503);
    assert(planetTextures.home.material.id == 504);
    assert(planetTextures.homeClouds.id == 502);
    assert(textureLoadCalls == 5);

    UnloadPlanetRenderResources();
    assert(rendererShutdownCalls == 1);
    assert(unloadedTextureCount == 4);
    assert(unloadedTextureIds[0] == 501);
    assert(unloadedTextureIds[1] == 503);
    assert(unloadedTextureIds[2] == 504);
    assert(unloadedTextureIds[3] == 502);
    UnloadPlanetRenderResources();
    assert(rendererShutdownCalls == 2);
    assert(unloadedTextureCount == 4);
}

static void TestHomeCloudSeedReplacementIsAtomic(void)
{
    ResetTextureMocks();
    testWorldSeed = 11;
    const unsigned int initial[] = { 601, 602, 603 };
    QueueTextures(initial, 3);
    EnsurePlanetRenderResources();
    assert(planetTextures.homeClouds.id == 603);

    testWorldSeed = 12;
    const unsigned int failed[] = { 0 };
    QueueTextures(failed, 1);
    EnsurePlanetRenderResources();
    assert(planetTextures.homeClouds.id == 603);
    assert(planetTextures.homeCloudSeed == 11);
    assert(unloadedTextureCount == 0);

    const unsigned int replacement[] = { 604 };
    QueueTextures(replacement, 1);
    EnsurePlanetRenderResources();
    assert(planetTextures.homeClouds.id == 604);
    assert(planetTextures.homeCloudSeed == 12);
    assert(unloadedTextureCount == 1);
    assert(unloadedTextureIds[0] == 603);
    UnloadPlanetRenderResources();
}

int main(void)
{
    TestCanonicalTexturesAreDistinct();
    TestCanonicalIdentityIsPartOfTextureCacheKey();
    TestPartialSurfaceCreationRollsBack();
    TestSurfaceCacheKeepsOldEntryOnFailure();
    TestCloudCacheKeepsOldEntryOnFailure();
    TestHomeResourcesRetryIndependently();
    TestHomeCloudSeedReplacementIsAtomic();
    puts("planet texture resource tests passed");
    return 0;
}
