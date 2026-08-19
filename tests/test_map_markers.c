#include "gameplay/map_markers.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static MapMarkerSurface Home(void)
{
    return (MapMarkerSurface){ WORLD_DIMENSION_HOME, 0u };
}

static MapMarkerSurface Planet(uint32_t id)
{
    return (MapMarkerSurface){ WORLD_DIMENSION_PLANET, id };
}

static void TestCrudAndSurfaceLimits(void)
{
    MapMarkersReset();
    uint32_t first = 0u;
    assert(MapMarkersCreate(Home(), 12.5f, -7.0f, "Home", MAP_MARKER_RED,
                            &first));
    assert(first != 0u);
    assert(MapMarkersCreate(Planet(9u), 4.0f, 8.0f, "Planet",
                            MAP_MARKER_CYAN, NULL));
    assert(MapMarkersCount(Home()) == 1);
    assert(MapMarkersCount(Planet(9u)) == 1);
    assert(MapMarkersCount(Planet(10u)) == 0);

    MapMarker marker;
    assert(MapMarkersFind(first, &marker));
    assert(strcmp(marker.name, "Home") == 0);
    assert(MapMarkersUpdate(first, "Base Camp", MAP_MARKER_GREEN));
    assert(MapMarkersToggleTarget(first));
    assert(MapMarkersTargetOnSurface(Home(), &marker));
    assert(!MapMarkersTargetOnSurface(Planet(9u), &marker));
    assert(MapMarkersToggleTarget(first));
    assert(!MapMarkersTarget(&marker));
    assert(MapMarkersSetTarget(first));
    assert(MapMarkersTarget(&marker));
    assert(MapMarkersSetTarget(0u));
    assert(!MapMarkersSetTarget(999999u));

    for (int i = 1; i < MAP_MARKERS_PER_SURFACE; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Point %d", i);
        assert(MapMarkersCreate(Home(), (float)i, (float)-i, name,
                                MAP_MARKER_AMBER, NULL));
    }
    assert(MapMarkersCount(Home()) == MAP_MARKERS_PER_SURFACE);
    assert(!MapMarkersCreate(Home(), 1.0f, 2.0f, "Overflow",
                             MAP_MARKER_BLUE, NULL));
    assert(MapMarkersRemove(first));
    assert(!MapMarkersFind(first, NULL));
}

static void TestUtf8Names(void)
{
    char name[MAP_MARKER_NAME_SIZE] = "Base ";
    assert(MapMarkerNameAppendCodepoint(name, sizeof(name), 0x8425));
    assert(MapMarkerNameAppendCodepoint(name, sizeof(name), 0x5730));
    assert(strcmp(name, "Base \xe8\x90\xa5\xe5\x9c\xb0") == 0);
    assert(MapMarkerNameIsValid(name));
    MapMarkerNameBackspace(name);
    assert(strcmp(name, "Base \xe8\x90\xa5") == 0);
    assert(MapMarkerNameAppendUtf8(name, sizeof(name), "地 A"));
    assert(MapMarkerNameIsValid(name));
    assert(!MapMarkerNameIsValid("   "));
    assert(!MapMarkerNameIsValid("\xf0\x28\x8c\x28"));

    char full[MAP_MARKER_NAME_SIZE];
    memset(full, 'a', sizeof(full) - 1u);
    full[sizeof(full) - 1u] = '\0';
    assert(!MapMarkerNameAppendCodepoint(full, sizeof(full), 'b'));
}

static void TestSaveRoundTripAndCorruption(void)
{
    MapMarkersReset();
    uint32_t target = 0u;
    assert(MapMarkersCreate(Planet(42u), 123.25f, -456.5f,
                            "\xe8\x90\xa5\xe5\x9c\xb0", MAP_MARKER_MAGENTA,
                            &target));
    assert(MapMarkersToggleTarget(target));
    FILE *file = tmpfile();
    assert(file != NULL);
    assert(MapMarkersSaveState(file));
    rewind(file);
    MapMarkerState loaded;
    assert(MapMarkersReadState(file, &loaded));
    assert(loaded.count == 1u);
    assert(loaded.targetId == target);
    assert(strcmp(loaded.markers[0].name, "\xe8\x90\xa5\xe5\x9c\xb0") == 0);
    MapMarkersReset();
    assert(MapMarkersInstallState(&loaded));
    MapMarker marker;
    assert(MapMarkersTarget(&marker));
    assert(marker.surface.surfaceId == 42u);
    fclose(file);

    file = tmpfile();
    assert(file != NULL);
    uint32_t badVersion = 99u;
    assert(fwrite(&badVersion, sizeof(badVersion), 1, file) == 1);
    rewind(file);
    assert(!MapMarkersReadState(file, &loaded));
    fclose(file);
}

static void TestGreatCircle(void)
{
    float bearing = 0.0f;
    float distance = 0.0f;
    assert(MapMarkerGreatCircle(0.0f, 0.0f, 0.5f * PI, 0.0f,
                                &bearing, &distance));
    assert(fabsf(bearing - 0.5f * PI) < 0.0001f);
    assert(fabsf(distance - 4096.0f) < 0.01f);

    assert(MapMarkerGreatCircle(PI - 0.01f, 0.0f, -PI + 0.01f, 0.0f,
                                &bearing, &distance));
    assert(distance < 60.0f);
    assert(bearing > 1.5f);

    assert(MapMarkerGreatCircle(0.0f, 1.4f, PI, 1.4f,
                                &bearing, &distance));
    assert(isfinite(bearing));
    assert(distance > 0.0f);
}

static void TestCanonicalMarkerCoordinates(void)
{
    MapMarkersReset();
    uint32_t id = 0u;
    assert(MapMarkersCreate(
        Home(), (float)SURFACE_EQUATOR_BLOCKS + 27.5f, 41.25f,
        "Wrapped", MAP_MARKER_BLUE, &id));
    MapMarker marker;
    assert(MapMarkersFind(id, &marker));
    assert(fabsf(marker.x - 27.5f) < 0.01f);
    assert(fabsf(marker.z - 41.25f) < 0.01f);

    float pole = (float)SURFACE_POLE_TO_POLE_BLOCKS * 0.5f;
    assert(MapMarkersCreate(
        Planet(7u), 100.0f, pole + 12.0f,
        "Polar", MAP_MARKER_CYAN, &id));
    assert(MapMarkersFind(id, &marker));
    assert(fabsf(marker.x -
                 (100.0f - (float)SURFACE_EQUATOR_BLOCKS * 0.5f)) < 0.01f);
    assert(fabsf(marker.z - (pole - 12.0f)) < 0.01f);
}

static void TestLegacyCoordinateMigration(void)
{
    MapMarkerState state;
    MapMarkersEmptyState(&state);
    state.coordinateSchema = 1u;
    state.count = 3u;
    state.nextId = 40u;
    state.targetId = 22u;
    state.markers[0] = (MapMarker){
        .id = 11u,
        .surface = { WORLD_DIMENSION_HOME, 0u },
        .x = (float)SURFACE_EQUATOR_BLOCKS + 19.0f,
        .z = -27.0f,
        .color = MAP_MARKER_GREEN
    };
    strcpy(state.markers[0].name, "Home legacy");
    state.markers[1] = (MapMarker){
        .id = 22u,
        .surface = { WORLD_DIMENSION_PLANET, 77u },
        .x = 31.0f,
        .z = -44.0f,
        .color = MAP_MARKER_CYAN
    };
    strcpy(state.markers[1].name, "Current planet");
    state.markers[2] = (MapMarker){
        .id = 33u,
        .surface = { WORLD_DIMENSION_PLANET, 88u },
        .x = 51.0f,
        .z = 63.0f,
        .color = MAP_MARKER_AMBER
    };
    strcpy(state.markers[2].name, "Historic planet");

    assert(MapMarkersMigrateLegacyState(&state, 77u, 1000, -2000));
    assert(state.coordinateSchema == MAP_MARKER_COORDINATE_SCHEMA);
    assert(state.nextId == 40u && state.targetId == 22u);
    assert(state.markers[0].id == 11u);
    assert(strcmp(state.markers[0].name, "Home legacy") == 0);
    assert(fabsf(state.markers[0].x - 19.0f) < 0.01f);
    assert(fabsf(state.markers[0].z + 27.0f) < 0.01f);
    assert(state.markers[1].id == 22u);
    assert(strcmp(state.markers[1].name, "Current planet") == 0);
    assert(fabsf(state.markers[1].x - 1031.0f) < 0.01f);
    assert(fabsf(state.markers[1].z + 2044.0f) < 0.01f);
    assert(state.markers[2].id == 33u);
    assert(fabsf(state.markers[2].x - 51.0f) < 0.01f);
    assert(fabsf(state.markers[2].z - 63.0f) < 0.01f);
}

static void TestSaveRejectsLegacyCoordinateSchema(void)
{
    MapMarkersReset();
    assert(MapMarkersCreate(Home(), 1.0f, 2.0f, "Current",
                            MAP_MARKER_RED, NULL));
    MapMarkersTestSetCoordinateSchema(1u);
    FILE *file = tmpfile();
    assert(file != NULL);
    assert(!MapMarkersSaveState(file));
    fclose(file);
    MapMarkersReset();
}

int main(void)
{
    TestCrudAndSurfaceLimits();
    TestUtf8Names();
    TestSaveRoundTripAndCorruption();
    TestGreatCircle();
    TestCanonicalMarkerCoordinates();
    TestLegacyCoordinateMigration();
    TestSaveRejectsLegacyCoordinateSchema();
    puts("map marker tests passed");
    return 0;
}
