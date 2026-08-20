#include "presentation/homeworld_map_model.h"
#include "world/terrain.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void TestZoomLevels(void)
{
    assert(HomeWorldMapSpanForLevel(-1) == 256.0f);
    assert(HomeWorldMapSpanForLevel(0) == 256.0f);
    assert(HomeWorldMapSpanForLevel(1) == 512.0f);
    assert(HomeWorldMapSpanForLevel(2) == 1024.0f);
    assert(HomeWorldMapSpanForLevel(3) == 2048.0f);
    assert(HomeWorldMapSpanForLevel(99) == 2048.0f);
}

static void TestCoordinateRoundTrip(void)
{
    HomeWorldMapBounds bounds = { -320.0f, 144.0f, 512.0f };
    Rectangle map = { 40.0f, 60.0f, 640.0f, 480.0f };
    const Vector2 positions[] = {
        { -320.0f, 144.0f },
        { -576.0f, -112.0f },
        { -64.0f, 400.0f },
        { -417.5f, 237.25f }
    };
    for (size_t i = 0; i < sizeof(positions) / sizeof(positions[0]); i++) {
        Vector2 screen = HomeWorldMapWorldToScreen(
            bounds, map, positions[i].x, positions[i].y);
        Vector2 world = HomeWorldMapScreenToWorld(bounds, map, screen);
        assert(fabsf(world.x - positions[i].x) < 0.001f);
        assert(fabsf(world.y - positions[i].y) < 0.001f);
    }
    assert(HomeWorldMapWorldVisible(bounds, -576.0f, -112.0f));
    assert(!HomeWorldMapWorldVisible(bounds, -576.1f, -112.0f));
}

static void TestSphericalSeamVisibility(void)
{
    HomeWorldMapBounds bounds = {
        (float)SURFACE_EQUATOR_BLOCKS * 0.5f - 4.0f, 0.0f, 256.0f
    };
    float wrappedX = -(float)SURFACE_EQUATOR_BLOCKS * 0.5f + 4.0f;
    Vector2 screen = HomeWorldMapWorldToScreen(
        bounds, (Rectangle){ 0.0f, 0.0f, 256.0f, 256.0f }, wrappedX, 0.0f);
    assert(screen.x > 120.0f && screen.x < 136.0f);
    assert(HomeWorldMapWorldVisible(bounds, wrappedX, 0.0f));
}

static void TestTerrainPalette(void)
{
    Color colors[BIOME_SWAMP + 1];
    for (int biome = BIOME_PLAINS; biome <= BIOME_SWAMP; biome++) {
        HomeWorldMapTerrainCell cell = {
            .biome = (Biome)biome,
            .elevation = 83.0f,
            .seaLevel = 80.0f,
            .slope = 1.0f
        };
        colors[biome] = HomeWorldMapTerrainColor(cell);
        assert(colors[biome].a == 255);
        assert(strlen(HomeWorldMapBiomeName((Biome)biome, false)) > 0u);
    }
    for (int a = 0; a <= BIOME_SWAMP; a++) {
        for (int b = a + 1; b <= BIOME_SWAMP; b++) {
            assert(colors[a].r != colors[b].r ||
                   colors[a].g != colors[b].g ||
                   colors[a].b != colors[b].b);
        }
    }
    HomeWorldMapTerrainCell water = {
        .biome = BIOME_PLAINS,
        .elevation = 64.0f,
        .seaLevel = 80.0f,
        .waterDepth = 16
    };
    Color waterColor = HomeWorldMapTerrainColor(water);
    assert(waterColor.b > waterColor.r);
    water.elevation = (float)HOME_BATHYMETRY_MIN_SEABED_Y;
    water.waterDepth = HOME_BATHYMETRY_MAX_WATER_DEPTH;
    Color trenchColor = HomeWorldMapTerrainColor(water);
    assert(trenchColor.r < waterColor.r);
    assert(trenchColor.g < waterColor.g);
    assert(trenchColor.b < waterColor.b);
    assert(strcmp(HomeWorldMapBiomeName(BIOME_PLAINS, true), "Water") == 0);
}

static void TestPlanetTerrainPalette(void)
{
    Color colors[PLANET_BIOME_COUNT];
    for (int biome = 0; biome < PLANET_BIOME_COUNT; biome++) {
        HomeWorldMapTerrainCell cell = {
            .planetSurface = true,
            .planetBiome = (PlanetBiome)biome,
            .elevation = 84.0f,
            .seaLevel = 80.0f,
            .slope = 1.0f
        };
        colors[biome] = HomeWorldMapTerrainColor(cell);
        assert(colors[biome].a == 255);
    }
    for (int a = 0; a < PLANET_BIOME_COUNT; a++) {
        for (int b = a + 1; b < PLANET_BIOME_COUNT; b++) {
            assert(colors[a].r != colors[b].r ||
                   colors[a].g != colors[b].g ||
                   colors[a].b != colors[b].b);
        }
    }

    HomeWorldMapTerrainCell ocean = {
        .planetSurface = true,
        .planetBiome = PLANET_BIOME_OCEAN,
        .elevation = 72.0f,
        .seaLevel = 80.0f,
        .waterDepth = 8
    };
    Color shelfColor = HomeWorldMapTerrainColor(ocean);
    ocean.elevation = (float)BATHYMETRY_MIN_SEABED_Y;
    ocean.waterDepth = BATHYMETRY_MAX_WATER_DEPTH;
    Color trenchColor = HomeWorldMapTerrainColor(ocean);
    assert(trenchColor.r < shelfColor.r);
    assert(trenchColor.g < shelfColor.g);
    assert(trenchColor.b < shelfColor.b);

    HomeWorldMapTerrainCell lava = ocean;
    lava.planetBiome = PLANET_BIOME_LAVA_SEA;
    Color lavaColor = HomeWorldMapTerrainColor(lava);
    assert(lavaColor.r > lavaColor.g);
    assert(lavaColor.g > lavaColor.b);

    HomeWorldMapTerrainCell ice = ocean;
    ice.planetBiome = PLANET_BIOME_GLACIER;
    Color iceColor = HomeWorldMapTerrainColor(ice);
    assert(iceColor.b > iceColor.r);
}

static void TestHeatInterpolation(void)
{
    float heat[HOMEWORLD_MAP_HEAT_SIZE * HOMEWORLD_MAP_HEAT_SIZE] = { 0 };
    heat[0] = 0.25f;
    heat[HOMEWORLD_MAP_HEAT_SIZE - 1] = 0.5f;
    heat[(HOMEWORLD_MAP_HEAT_SIZE - 1) * HOMEWORLD_MAP_HEAT_SIZE] = 0.75f;
    heat[HOMEWORLD_MAP_HEAT_SIZE * HOMEWORLD_MAP_HEAT_SIZE - 1] = 1.0f;
    assert(fabsf(HomeWorldMapHeatSample(heat, 0.0f, 0.0f) - 0.25f) < 0.001f);
    assert(fabsf(HomeWorldMapHeatSample(heat, 1.0f, 1.0f) - 1.0f) < 0.001f);
    assert(HomeWorldMapHeatSample(NULL, 0.5f, 0.5f) == 0.0f);
}

static bool HasLandmark(const HomeWorldMapLandmark *landmarks, int count,
                        HomeWorldMapLandmarkKind kind)
{
    for (int i = 0; i < count; i++) {
        if (landmarks[i].kind == kind) return true;
    }
    return false;
}

static void TestLandmarkSelection(void)
{
    HomeWorldMapTerrainCell cells[
        HOMEWORLD_MAP_RASTER_SIZE * HOMEWORLD_MAP_RASTER_SIZE];
    for (int i = 0;
         i < HOMEWORLD_MAP_RASTER_SIZE * HOMEWORLD_MAP_RASTER_SIZE; i++) {
        cells[i] = (HomeWorldMapTerrainCell){
            .biome = BIOME_PLAINS,
            .elevation = 84.0f,
            .seaLevel = 80.0f
        };
    }
    int peak = 15 * HOMEWORLD_MAP_RASTER_SIZE + 15;
    cells[peak].elevation = 126.0f;
    for (int z = 30; z <= 34; z++) {
        for (int x = 30; x <= 34; x++) {
            cells[z * HOMEWORLD_MAP_RASTER_SIZE + x].biome = BIOME_FOREST;
        }
    }
    int fauna = 49 * HOMEWORLD_MAP_RASTER_SIZE + 49;
    cells[fauna].faunaActivity = 0.95f;
    for (int z = 8; z <= 10; z++) {
        cells[z * HOMEWORLD_MAP_RASTER_SIZE + 40].waterDepth = 12;
    }

    HomeWorldMapLandmark landmarks[HOMEWORLD_MAP_MAX_LANDMARKS];
    int count = HomeWorldMapSelectLandmarks(
        cells, (HomeWorldMapBounds){ 0.0f, 0.0f, 512.0f },
        landmarks, HOMEWORLD_MAP_MAX_LANDMARKS);
    assert(count > 0 && count <= HOMEWORLD_MAP_MAX_LANDMARKS);
    assert(HasLandmark(landmarks, count, HOMEWORLD_MAP_LANDMARK_PEAK));
    assert(HasLandmark(landmarks, count, HOMEWORLD_MAP_LANDMARK_SHORE));
    assert(HasLandmark(landmarks, count, HOMEWORLD_MAP_LANDMARK_FOREST));
    assert(HasLandmark(landmarks, count, HOMEWORLD_MAP_LANDMARK_FAUNA));
}

static void TestPlanetForestLandmark(void)
{
    HomeWorldMapTerrainCell cells[
        HOMEWORLD_MAP_RASTER_SIZE * HOMEWORLD_MAP_RASTER_SIZE];
    for (int i = 0;
         i < HOMEWORLD_MAP_RASTER_SIZE * HOMEWORLD_MAP_RASTER_SIZE; i++) {
        cells[i] = (HomeWorldMapTerrainCell){
            .planetSurface = true,
            .planetBiome = PLANET_BIOME_PLAINS,
            .elevation = 84.0f,
            .seaLevel = 80.0f
        };
    }
    for (int z = 30; z <= 34; z++) {
        for (int x = 30; x <= 34; x++) {
            cells[z * HOMEWORLD_MAP_RASTER_SIZE + x].planetBiome =
                PLANET_BIOME_FOREST;
        }
    }

    HomeWorldMapLandmark landmarks[HOMEWORLD_MAP_MAX_LANDMARKS];
    int count = HomeWorldMapSelectLandmarks(
        cells, (HomeWorldMapBounds){ 0.0f, 0.0f, 512.0f },
        landmarks, HOMEWORLD_MAP_MAX_LANDMARKS);
    assert(HasLandmark(landmarks, count, HOMEWORLD_MAP_LANDMARK_FOREST));
}

int main(void)
{
    TestZoomLevels();
    TestCoordinateRoundTrip();
    TestSphericalSeamVisibility();
    TestTerrainPalette();
    TestPlanetTerrainPalette();
    TestHeatInterpolation();
    TestLandmarkSelection();
    TestPlanetForestLandmark();
    puts("homeworld map model tests passed");
    return 0;
}
