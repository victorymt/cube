#include "planet_material.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static PlanetProfile TestProfile(SolarBodyStyle style)
{
    PlanetProfile profile = { 0 };
    profile.style = style;
    profile.hasSolidSurface = style != SOLAR_STYLE_GAS;
    return profile;
}

static void AssertEncodedType(Color pixel, PlanetSurfaceType type)
{
    unsigned char expected = (unsigned char)((float)type /
                                              (float)(PLANET_SURFACE_TYPE_COUNT - 1) *
                                              255.0f);
    assert(pixel.a == expected);
}

static void TestColorChannel(void)
{
    assert(PlanetColorChannel(-1.0f) == 0);
    assert(PlanetColorChannel(0.0f) == 0);
    assert(PlanetColorChannel(42.9f) == 42);
    assert(PlanetColorChannel(255.0f) == 255);
    assert(PlanetColorChannel(300.0f) == 255);
}

static void TestLavaFissure(void)
{
    PlanetSurfaceSample surface = { 0 };
    surface.continentalness = 0.53f;
    assert(fabsf(PlanetLavaFissure(&surface) - 1.0f) < 0.0001f);

    surface.continentalness = 0.0f;
    surface.detail = 0.66f;
    assert(fabsf(PlanetLavaFissure(&surface) - 1.0f) < 0.0001f);

    surface.detail = 0.0f;
    assert(PlanetLavaFissure(&surface) == 0.0f);
}

static void TestSurfaceTypeClassification(void)
{
    PlanetProfile profile = TestProfile(SOLAR_STYLE_TEMPERATE);
    PlanetSurfaceSample surface = { .biome = PLANET_BIOME_OCEAN };
    assert(PlanetSurfaceTypeFor(&profile, &surface) == PLANET_SURFACE_OCEAN);

    surface = (PlanetSurfaceSample){ .biome = PLANET_BIOME_DUNES };
    assert(PlanetSurfaceTypeFor(&profile, &surface) == PLANET_SURFACE_SAND);

    surface = (PlanetSurfaceSample){ .biome = PLANET_BIOME_BADLANDS,
                                     .duneBand = 0.43f };
    assert(PlanetSurfaceTypeFor(&profile, &surface) == PLANET_SURFACE_SAND);
    surface.duneBand = 0.42f;
    assert(PlanetSurfaceTypeFor(&profile, &surface) == PLANET_SURFACE_ROCK);

    surface = (PlanetSurfaceSample){ .biome = PLANET_BIOME_FOREST,
                                     .iceCoverage = 0.35f };
    assert(PlanetSurfaceTypeFor(&profile, &surface) == PLANET_SURFACE_ICE);

    profile = TestProfile(SOLAR_STYLE_LAVA);
    surface = (PlanetSurfaceSample){ .biome = PLANET_BIOME_LAVA_SEA };
    assert(PlanetSurfaceTypeFor(&profile, &surface) == PLANET_SURFACE_LAVA);

    profile = TestProfile(SOLAR_STYLE_GAS);
    surface = (PlanetSurfaceSample){ .biome = PLANET_BIOME_STORM_BANDS };
    assert(PlanetSurfaceTypeFor(&profile, &surface) == PLANET_SURFACE_GAS);

    profile = TestProfile(SOLAR_STYLE_CRATER);
    surface = (PlanetSurfaceSample){ .biome = PLANET_BIOME_PLAINS };
    assert(PlanetSurfaceTypeFor(&profile, &surface) == PLANET_SURFACE_ROCK);
}

static void TestMaterialEncoding(void)
{
    PlanetProfile profile = TestProfile(SOLAR_STYLE_TEMPERATE);
    PlanetSurfaceSample surface = {
        .detail = 0.50f,
        .biome = PLANET_BIOME_OCEAN
    };
    Color ocean = PlanetMaterialPixel(&profile, &surface);
    assert(ocean.r == 16);
    assert(ocean.g == 237);
    assert(ocean.b == 0);
    AssertEncodedType(ocean, PLANET_SURFACE_OCEAN);

    profile = TestProfile(SOLAR_STYLE_LAVA);
    surface = (PlanetSurfaceSample){
        .continentalness = 0.0f,
        .detail = 0.0f,
        .biome = PLANET_BIOME_LAVA_SEA
    };
    Color lava = PlanetMaterialPixel(&profile, &surface);
    assert(lava.r == 96);
    assert(lava.g == 139);
    assert(lava.b == 207);
    AssertEncodedType(lava, PLANET_SURFACE_LAVA);

    profile = TestProfile(SOLAR_STYLE_GAS);
    surface = (PlanetSurfaceSample){
        .detail = 0.50f,
        .regionalness = 0.50f,
        .biome = PLANET_BIOME_STORM_BANDS
    };
    Color gas = PlanetMaterialPixel(&profile, &surface);
    assert(gas.b == 0);
    AssertEncodedType(gas, PLANET_SURFACE_GAS);
}

int main(void)
{
    TestColorChannel();
    TestLavaFissure();
    TestSurfaceTypeClassification();
    TestMaterialEncoding();
    puts("planet_material tests passed");
    return 0;
}
