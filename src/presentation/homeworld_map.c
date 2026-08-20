#include "presentation/homeworld_map.h"

#include "ecology/ecology.h"
#include "ecology/entity.h"
#include "presentation/homeworld_map_model.h"
#include "presentation/surface_globe.h"
#include "raylib.h"
#include "raymath.h"
#include "presentation/render.h"
#include "gameplay/map_markers.h"
#include "gameplay/ship.h"
#include "space/space_state.h"
#include "world/terrain.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct HomeWorldMapLayout {
    Rectangle panel;
    Rectangle map;
    Rectangle sidebar;
    Rectangle closeButton;
    Rectangle zoomOutButton;
    Rectangle zoomInButton;
    Rectangle recenterButton;
    Rectangle globe;
    Rectangle globeResetButton;
    bool compact;
} HomeWorldMapLayout;

typedef struct MapSurfaceTerrainSample {
    HomeWorldMapTerrainCell cell;
    BathymetrySample bathymetry;
} MapSurfaceTerrainSample;

typedef struct MapMarkerEditorLayout {
    Rectangle panel;
    Rectangle input;
    Rectangle swatches[MAP_MARKER_COLOR_COUNT];
    Rectangle saveButton;
    Rectangle cancelButton;
    Rectangle deleteButton;
} MapMarkerEditorLayout;

typedef struct HomeWorldMapState {
    bool open;
    bool textureReady;
    bool cacheDirty;
    bool dragging;
    bool showTerrain;
    bool showEcology;
    bool showCreatures;
    bool showLandmarks;
    bool showSubsurfaceLiquids;
    bool planetSurface;
    bool globeDragging;
    bool globePointerMoved;
    bool mapPressPending;
    bool markerEditorOpen;
    bool markerEditorEditing;
    int zoomLevel;
    uint32_t mapPressMarkerId;
    uint32_t markerEditorId;
    MapMarkerColor markerEditorColor;
    char surfaceName[40];
    char markerEditorName[MAP_MARKER_NAME_SIZE];
    char markerEditorError[80];
    HomeWorldMapBounds bounds;
    Vector2 dragOffset;
    Vector2 mapPressStart;
    Vector2 globePressStart;
    Vector2 markerEditorWorld;
    Vector3 playerPosition;
    float playerYaw;
    float daylight;
    float globeLongitude;
    float globeLatitude;
    Texture2D terrainTexture;
    Color pixels[HOMEWORLD_MAP_RASTER_SIZE * HOMEWORLD_MAP_RASTER_SIZE];
    HomeWorldMapTerrainCell cells[
        HOMEWORLD_MAP_RASTER_SIZE * HOMEWORLD_MAP_RASTER_SIZE];
    float heat[HOMEWORLD_MAP_HEAT_SIZE * HOMEWORLD_MAP_HEAT_SIZE];
    TerrainSubsurfaceLiquidSummary subsurfaceLiquids[
        HOMEWORLD_MAP_HEAT_SIZE * HOMEWORLD_MAP_HEAT_SIZE];
    HomeWorldMapLandmark landmarks[HOMEWORLD_MAP_MAX_LANDMARKS];
    int landmarkCount;
} HomeWorldMapState;

static HomeWorldMapState homeMap = { 0 };

static void MapCloseMarkerEditor(void);

static void MapPlayerLatLon(float *outLongitude, float *outLatitude)
{
    if (homeMap.planetSurface) {
        PlanetSurfaceLatLonAt((int)floorf(homeMap.playerPosition.x),
                              (int)floorf(homeMap.playerPosition.z),
                              outLongitude, outLatitude);
    } else {
        HomeSurfaceLatLonAt((int)floorf(homeMap.playerPosition.x),
                            (int)floorf(homeMap.playerPosition.z),
                            outLongitude, outLatitude);
    }
}

static float MapClamp(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static HomeWorldMapLayout MapLayout(void)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float margin = sw < 900 ? 10.0f : 16.0f;
    Rectangle panel = {
        margin, margin, (float)sw - margin * 2.0f,
        (float)sh - margin * 2.0f
    };
    bool compact = sw < 900 || sh < 700;
    Rectangle map = { 0 };
    Rectangle sidebar = { 0 };
    if (!compact) {
        float sidebarWidth = 244.0f;
        float availableWidth = panel.width - sidebarWidth - 54.0f;
        float availableHeight = panel.height - 92.0f;
        float mapSize = fminf(availableWidth, availableHeight);
        map = (Rectangle){
            panel.x + 24.0f,
            panel.y + 68.0f + (availableHeight - mapSize) * 0.5f,
            mapSize, mapSize
        };
        sidebar = (Rectangle){
            map.x + map.width + 24.0f, panel.y + 68.0f,
            panel.x + panel.width - map.x - map.width - 40.0f,
            panel.height - 88.0f
        };
    } else {
        float sidebarHeight = fminf(178.0f, panel.height * 0.42f);
        float availableHeight = panel.height - sidebarHeight - 92.0f;
        float mapSize = fminf(panel.width - 32.0f, availableHeight);
        map = (Rectangle){
            panel.x + (panel.width - mapSize) * 0.5f,
            panel.y + 62.0f, mapSize, mapSize
        };
        sidebar = (Rectangle){
            panel.x + 16.0f, map.y + map.height + 10.0f,
            panel.width - 32.0f,
            panel.y + panel.height - map.y - map.height - 20.0f
        };
    }
    float globeSize = compact
        ? sidebar.width - 206.0f
        : fminf(sidebar.width * 0.48f, sidebar.height * 0.31f);
    float maximumGlobeSize = compact
        ? fmaxf(1.0f, sidebar.height - 24.0f) : 320.0f;
    float minimumGlobeSize = compact ? 64.0f : 128.0f;
    globeSize = fminf(fmaxf(globeSize, minimumGlobeSize), maximumGlobeSize);
    Rectangle globe = compact
        ? (Rectangle){ sidebar.x, sidebar.y + 18.0f, globeSize, globeSize }
        : (Rectangle){ sidebar.x + (sidebar.width - globeSize) * 0.5f,
                       sidebar.y + 18.0f, globeSize, globeSize };
    return (HomeWorldMapLayout){
        .panel = panel,
        .map = map,
        .sidebar = sidebar,
        .closeButton = {
            panel.x + panel.width - 42.0f, panel.y + 13.0f, 28.0f, 28.0f
        },
        .zoomOutButton = { map.x + 10.0f, map.y + 10.0f, 30.0f, 30.0f },
        .zoomInButton = { map.x + 44.0f, map.y + 10.0f, 30.0f, 30.0f },
        .recenterButton = {
            map.x + map.width - 40.0f, map.y + 10.0f, 30.0f, 30.0f
        },
        .globe = globe,
        .globeResetButton = {
            globe.x + globe.width - 25.0f, globe.y + 5.0f, 21.0f, 21.0f
        },
        .compact = compact
    };
}

static HomeWorldMapBounds MapViewBounds(const HomeWorldMapLayout *layout)
{
    HomeWorldMapBounds bounds = homeMap.bounds;
    if (!layout || layout->map.width <= 0.0f || layout->map.height <= 0.0f) {
        return bounds;
    }
    bounds.centerX -= homeMap.dragOffset.x * bounds.span / layout->map.width;
    bounds.centerZ -= homeMap.dragOffset.y * bounds.span / layout->map.height;
    return bounds;
}

static MapSurfaceTerrainSample MapTerrainAt(int x, int z,
                                            TerrainMode terrainMode)
{
    if (!homeMap.planetSurface) {
        SurfaceTerrainSample sample = SurfaceTerrainAt(x, z, terrainMode);
        return (MapSurfaceTerrainSample){
            .cell = {
                .planetSurface = false,
                .biome = sample.biome,
                .elevation = sample.elevation,
                .seaLevel = sample.seaLevel,
                .slope = sample.slope,
                .waterDepth = sample.bathymetry.waterDepth
            },
            .bathymetry = sample.bathymetry
        };
    }

    BathymetrySample bathymetry = PlanetBathymetryAt(x, z);
    float east = (float)PlanetTerrainHeight(x + 1, z);
    float west = (float)PlanetTerrainHeight(x - 1, z);
    float north = (float)PlanetTerrainHeight(x, z - 1);
    float south = (float)PlanetTerrainHeight(x, z + 1);
    return (MapSurfaceTerrainSample){
        .cell = {
            .planetSurface = true,
            .planetBiome = PlanetBiomeAt(x, z),
            .elevation = (float)bathymetry.seabedY,
            .seaLevel = (float)bathymetry.seaLevel,
            .slope = fmaxf(fabsf(east - west), fabsf(north - south)) * 0.5f,
            .waterDepth = bathymetry.waterDepth
        },
        .bathymetry = bathymetry
    };
}

static void MapRefresh(float daylight)
{
    homeMap.daylight = daylight;
    TerrainMode terrainMode = WorldTerrainMode();
    float half = homeMap.bounds.span * 0.5f;
    for (int z = 0; z < HOMEWORLD_MAP_RASTER_SIZE; z++) {
        for (int x = 0; x < HOMEWORLD_MAP_RASTER_SIZE; x++) {
            float u = ((float)x + 0.5f) /
                      (float)HOMEWORLD_MAP_RASTER_SIZE;
            float v = ((float)z + 0.5f) /
                      (float)HOMEWORLD_MAP_RASTER_SIZE;
            int worldX = (int)floorf(homeMap.bounds.centerX - half +
                                     u * homeMap.bounds.span);
            int worldZ = (int)floorf(homeMap.bounds.centerZ - half +
                                     v * homeMap.bounds.span);
            MapSurfaceTerrainSample sample =
                MapTerrainAt(worldX, worldZ, terrainMode);
            HomeWorldMapTerrainCell *cell =
                &homeMap.cells[z * HOMEWORLD_MAP_RASTER_SIZE + x];
            *cell = sample.cell;
        }
    }

    for (int z = 0; z < HOMEWORLD_MAP_HEAT_SIZE; z++) {
        for (int x = 0; x < HOMEWORLD_MAP_HEAT_SIZE; x++) {
            float u = ((float)x + 0.5f) /
                      (float)HOMEWORLD_MAP_HEAT_SIZE;
            float v = ((float)z + 0.5f) /
                      (float)HOMEWORLD_MAP_HEAT_SIZE;
            int worldX = (int)floorf(homeMap.bounds.centerX - half +
                                     u * homeMap.bounds.span);
            int worldZ = (int)floorf(homeMap.bounds.centerZ - half +
                                     v * homeMap.bounds.span);
            PlanetLocalEcology ecology =
                PlanetEcologyLocalAt(worldX, worldZ, daylight);
            int index = z * HOMEWORLD_MAP_HEAT_SIZE + x;
            homeMap.heat[index] =
                MapClamp(ecology.suitability.faunaActivity, 0.0f, 1.0f);
            int surfaceHeight = homeMap.planetSurface
                ? PlanetTerrainHeight(worldX, worldZ)
                : TerrainHeight(worldX, worldZ, terrainMode);
            homeMap.subsurfaceLiquids[index] =
                TerrainSubsurfaceLiquidSummaryAt(
                    worldX, worldZ, surfaceHeight);
        }
    }

    for (int z = 0; z < HOMEWORLD_MAP_RASTER_SIZE; z++) {
        for (int x = 0; x < HOMEWORLD_MAP_RASTER_SIZE; x++) {
            float u = ((float)x + 0.5f) /
                      (float)HOMEWORLD_MAP_RASTER_SIZE;
            float v = ((float)z + 0.5f) /
                      (float)HOMEWORLD_MAP_RASTER_SIZE;
            int index = z * HOMEWORLD_MAP_RASTER_SIZE + x;
            homeMap.cells[index].faunaActivity =
                HomeWorldMapHeatSample(homeMap.heat, u, v);
            homeMap.pixels[index] =
                HomeWorldMapTerrainColor(homeMap.cells[index]);
        }
    }

    if (!homeMap.textureReady) {
        Image image = {
            .data = homeMap.pixels,
            .width = HOMEWORLD_MAP_RASTER_SIZE,
            .height = HOMEWORLD_MAP_RASTER_SIZE,
            .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
        };
        homeMap.terrainTexture = LoadTextureFromImage(image);
        homeMap.textureReady = homeMap.terrainTexture.id != 0u;
        if (homeMap.textureReady) {
            SetTextureFilter(homeMap.terrainTexture, TEXTURE_FILTER_POINT);
        }
    } else {
        UpdateTexture(homeMap.terrainTexture, homeMap.pixels);
    }
    homeMap.landmarkCount = HomeWorldMapSelectLandmarks(
        homeMap.cells, homeMap.bounds, homeMap.landmarks,
        HOMEWORLD_MAP_MAX_LANDMARKS);
    homeMap.cacheDirty = false;
}

void HomeWorldMapOpen(Vector3 playerPosition, float daylight)
{
    homeMap.open = true;
    homeMap.cacheDirty = true;
    homeMap.dragging = false;
    homeMap.globeDragging = false;
    homeMap.mapPressPending = false;
    MapCloseMarkerEditor();
    homeMap.dragOffset = Vector2Zero();
    homeMap.zoomLevel = 1;
    homeMap.planetSurface = PlanetWorldIsActive() &&
                            WorldCurrentDimension() == WORLD_DIMENSION_PLANET;
    snprintf(homeMap.surfaceName, sizeof(homeMap.surfaceName), "%s",
             homeMap.planetSurface ? PlanetWorldName() : "Homeworld");
    homeMap.bounds = (HomeWorldMapBounds){
        floorf(playerPosition.x), floorf(playerPosition.z),
        HomeWorldMapSpanForLevel(homeMap.zoomLevel)
    };
    homeMap.playerPosition = playerPosition;
    MapPlayerLatLon(&homeMap.globeLongitude, &homeMap.globeLatitude);
    homeMap.showTerrain = true;
    homeMap.showEcology = true;
    homeMap.showCreatures = true;
    homeMap.showLandmarks = true;
    homeMap.showSubsurfaceLiquids = true;
    SurfaceGlobeInvalidate();
    MapRefresh(daylight);
}

void HomeWorldMapClose(void)
{
    homeMap.open = false;
    homeMap.dragging = false;
    homeMap.mapPressPending = false;
    MapCloseMarkerEditor();
    homeMap.dragOffset = Vector2Zero();
}

bool HomeWorldMapIsOpen(void)
{
    return homeMap.open;
}

void HomeWorldMapSetSubsurfaceLiquidsVisible(bool visible)
{
    homeMap.showSubsurfaceLiquids = visible;
}

bool HomeWorldMapSubsurfaceLiquidsVisible(void)
{
    return homeMap.showSubsurfaceLiquids;
}

static void MapZoom(int direction)
{
    int level = homeMap.zoomLevel + direction;
    if (level < 0) level = 0;
    if (level >= HOMEWORLD_MAP_ZOOM_LEVELS) {
        level = HOMEWORLD_MAP_ZOOM_LEVELS - 1;
    }
    if (level == homeMap.zoomLevel) return;
    homeMap.zoomLevel = level;
    homeMap.bounds.span = HomeWorldMapSpanForLevel(level);
    MapRefresh(homeMap.daylight);
}

static void MapPan(float dx, float dz)
{
    homeMap.bounds.centerX += dx;
    homeMap.bounds.centerZ += dz;
    MapRefresh(homeMap.daylight);
}

static void MapRecenter(void)
{
    homeMap.bounds.centerX = floorf(homeMap.playerPosition.x);
    homeMap.bounds.centerZ = floorf(homeMap.playerPosition.z);
    homeMap.dragOffset = Vector2Zero();
    MapRefresh(homeMap.daylight);
}

static Rectangle MapLayerButton(const HomeWorldMapLayout *layout, int index)
{
    if (layout->compact) {
        float startX = layout->globe.x + layout->globe.width + 12.0f;
        float available = layout->sidebar.x + layout->sidebar.width - startX;
        float width = fmaxf(78.0f, (available - 6.0f) * 0.5f);
        if (index == 4) {
            return (Rectangle){
                startX, layout->sidebar.y + 80.0f,
                available, 24.0f
            };
        }
        return (Rectangle){
            startX + (float)(index % 2) * (width + 6.0f),
            layout->sidebar.y + 24.0f + (float)(index / 2) * 28.0f,
            width, 24.0f
        };
    }
    float width = (layout->sidebar.width - 8.0f) * 0.5f;
    float startY = layout->globe.y + layout->globe.height + 34.0f;
    if (index == 4) {
        return (Rectangle){
            layout->sidebar.x, startY + 56.0f,
            layout->sidebar.width, 24.0f
        };
    }
    return (Rectangle){
        layout->sidebar.x + (float)(index % 2) * (width + 8.0f),
        startY + (float)(index / 2) * 28.0f, width, 24.0f
    };
}

static bool MapPointInButton(Vector2 mouse, Rectangle button)
{
    return CheckCollisionPointRec(mouse, button);
}

static MapMarkerSurface MapCurrentSurface(void)
{
    return (MapMarkerSurface){
        .dimension = WorldCurrentDimension(),
        .surfaceId = WorldCurrentSurfaceId()
    };
}

static void MapMarkerLatLon(const MapMarker *marker, float *outLongitude,
                            float *outLatitude)
{
    if (!marker || !outLongitude || !outLatitude) return;
    if (marker->surface.dimension == WORLD_DIMENSION_PLANET) {
        PlanetSurfaceLatLonAt((int)floorf(marker->x), (int)floorf(marker->z),
                              outLongitude, outLatitude);
    } else {
        HomeSurfaceLatLonAt((int)floorf(marker->x), (int)floorf(marker->z),
                            outLongitude, outLatitude);
    }
}

static bool MapWorldFromGlobe(float longitude, float latitude,
                              Vector2 *outWorld)
{
    if (!outWorld) return false;
    SurfaceAddress address = SurfaceAddressFromLatLon(
        WorldCurrentSurfaceId(), longitude, latitude, 0);
    SurfaceMapCell cell = { 0 };
    if (!SurfaceAddressCanonicalMapCell(address, &cell)) return false;
    *outWorld = (Vector2){ (float)cell.x, (float)cell.z };
    return true;
}

static uint32_t MapGlobeMarkerAt(float longitude, float latitude)
{
    MapMarker markers[MAP_MARKERS_PER_SURFACE];
    int count = MapMarkersCollect(MapCurrentSurface(), markers,
                                  MAP_MARKERS_PER_SURFACE);
    uint32_t closestId = 0u;
    float closestDistance = 0.060f;
    for (int i = 0; i < count; i++) {
        float markerLongitude = 0.0f;
        float markerLatitude = 0.0f;
        float bearing = 0.0f;
        float distance = 0.0f;
        MapMarkerLatLon(&markers[i], &markerLongitude, &markerLatitude);
        if (!MapMarkerGreatCircle(longitude, latitude,
                                  markerLongitude, markerLatitude,
                                  &bearing, &distance)) continue;
        if (distance < closestDistance) {
            closestDistance = distance;
            closestId = markers[i].id;
        }
    }
    return closestId;
}

static bool MapFocusGlobe(float longitude, float latitude)
{
    Vector2 world = { 0 };
    if (!MapWorldFromGlobe(longitude, latitude, &world)) return false;
    homeMap.globeLongitude = atan2f(sinf(longitude), cosf(longitude));
    homeMap.globeLatitude = MapClamp(latitude, -PI * 0.48f, PI * 0.48f);
    homeMap.bounds.centerX = floorf(world.x);
    homeMap.bounds.centerZ = floorf(world.y);
    homeMap.dragOffset = Vector2Zero();
    MapRefresh(homeMap.daylight);
    return true;
}

static uint32_t MapMarkerAt(const HomeWorldMapLayout *layout,
                            HomeWorldMapBounds viewBounds, Vector2 screen)
{
    MapMarker markers[MAP_MARKERS_PER_SURFACE];
    int count = MapMarkersCollect(MapCurrentSurface(), markers,
                                  MAP_MARKERS_PER_SURFACE);
    uint32_t closestId = 0u;
    float closestDistance = 13.0f;
    for (int i = 0; i < count; i++) {
        if (!HomeWorldMapWorldVisible(viewBounds, markers[i].x,
                                      markers[i].z)) continue;
        Vector2 point = HomeWorldMapWorldToScreen(
            viewBounds, layout->map, markers[i].x, markers[i].z);
        float distance = Vector2Distance(screen, point);
        if (distance < closestDistance) {
            closestDistance = distance;
            closestId = markers[i].id;
        }
    }
    return closestId;
}

static MapMarkerEditorLayout MapEditorLayout(
    const HomeWorldMapLayout *layout)
{
    float width = fminf(430.0f, layout->panel.width - 28.0f);
    float height = 236.0f;
    Rectangle panel = {
        layout->panel.x + (layout->panel.width - width) * 0.5f,
        layout->panel.y + (layout->panel.height - height) * 0.5f,
        width, height
    };
    MapMarkerEditorLayout editor = {
        .panel = panel,
        .input = { panel.x + 24.0f, panel.y + 66.0f,
                   panel.width - 48.0f, 42.0f },
        .deleteButton = { panel.x + 24.0f, panel.y + 182.0f,
                          82.0f, 34.0f },
        .cancelButton = { panel.x + panel.width - 202.0f,
                          panel.y + 182.0f, 82.0f, 34.0f },
        .saveButton = { panel.x + panel.width - 106.0f,
                        panel.y + 182.0f, 82.0f, 34.0f }
    };
    float swatchSize = 28.0f;
    float swatchGap = 10.0f;
    float total = (float)MAP_MARKER_COLOR_COUNT * swatchSize +
                  (float)(MAP_MARKER_COLOR_COUNT - 1) * swatchGap;
    float startX = panel.x + (panel.width - total) * 0.5f;
    for (int i = 0; i < MAP_MARKER_COLOR_COUNT; i++) {
        editor.swatches[i] = (Rectangle){
            startX + (float)i * (swatchSize + swatchGap), panel.y + 130.0f,
            swatchSize, swatchSize
        };
    }
    return editor;
}

static void MapCloseMarkerEditor(void)
{
    homeMap.markerEditorOpen = false;
    homeMap.markerEditorEditing = false;
    homeMap.markerEditorId = 0u;
    homeMap.markerEditorError[0] = '\0';
}

static void MapOpenMarkerEditor(uint32_t markerId, Vector2 world)
{
    MapMarker marker;
    memset(homeMap.markerEditorName, 0, sizeof(homeMap.markerEditorName));
    homeMap.markerEditorError[0] = '\0';
    homeMap.markerEditorOpen = true;
    homeMap.markerEditorEditing = markerId != 0u &&
                                  MapMarkersFind(markerId, &marker);
    homeMap.markerEditorId = homeMap.markerEditorEditing ? markerId : 0u;
    homeMap.markerEditorColor = homeMap.markerEditorEditing
        ? marker.color : MAP_MARKER_RED;
    homeMap.markerEditorWorld = homeMap.markerEditorEditing
        ? (Vector2){ marker.x, marker.z } : world;
    if (homeMap.markerEditorEditing) {
        snprintf(homeMap.markerEditorName,
                 sizeof(homeMap.markerEditorName), "%s", marker.name);
    }
}

static bool MapSaveMarkerEditor(void)
{
    if (!MapMarkerNameIsValid(homeMap.markerEditorName)) {
        snprintf(homeMap.markerEditorName,
                 sizeof(homeMap.markerEditorName), "Marker %d",
                 MapMarkersCount(MapCurrentSurface()) + 1);
    }
    bool saved = homeMap.markerEditorEditing
        ? MapMarkersUpdate(homeMap.markerEditorId,
                           homeMap.markerEditorName,
                           homeMap.markerEditorColor)
        : MapMarkersCreate(MapCurrentSurface(), homeMap.markerEditorWorld.x,
                           homeMap.markerEditorWorld.y,
                           homeMap.markerEditorName,
                           homeMap.markerEditorColor,
                           &homeMap.markerEditorId);
    if (!saved) {
        snprintf(homeMap.markerEditorError,
                 sizeof(homeMap.markerEditorError),
                 "This surface already has 64 markers");
        return false;
    }
    MapCloseMarkerEditor();
    return true;
}

static void MapUpdateMarkerEditor(const HomeWorldMapLayout *layout)
{
    MapMarkerEditorLayout editor = MapEditorLayout(layout);
    Vector2 mouse = GetMousePosition();
    if (IsKeyPressed(KEY_ESCAPE)) {
        MapCloseMarkerEditor();
        return;
    }

    bool controlDown = IsKeyDown(KEY_LEFT_CONTROL) ||
                       IsKeyDown(KEY_RIGHT_CONTROL);
    if (controlDown && IsKeyPressed(KEY_V)) {
        const char *clipboard = GetClipboardText();
        if (clipboard && clipboard[0] != '\0' &&
            !MapMarkerNameAppendUtf8(homeMap.markerEditorName,
                                     sizeof(homeMap.markerEditorName),
                                     clipboard)) {
            snprintf(homeMap.markerEditorError,
                     sizeof(homeMap.markerEditorError),
                     "Name is invalid or too long");
        }
    }
    if (!controlDown) {
        for (int codepoint = GetCharPressed(); codepoint > 0;
             codepoint = GetCharPressed()) {
            if (!MapMarkerNameAppendCodepoint(
                    homeMap.markerEditorName,
                    sizeof(homeMap.markerEditorName), codepoint)) {
                snprintf(homeMap.markerEditorError,
                         sizeof(homeMap.markerEditorError),
                         "Name is limited to 63 UTF-8 bytes");
            } else {
                homeMap.markerEditorError[0] = '\0';
            }
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE)) {
        MapMarkerNameBackspace(homeMap.markerEditorName);
        homeMap.markerEditorError[0] = '\0';
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        for (int i = 0; i < MAP_MARKER_COLOR_COUNT; i++) {
            if (CheckCollisionPointRec(mouse, editor.swatches[i])) {
                homeMap.markerEditorColor = (MapMarkerColor)i;
            }
        }
        if (CheckCollisionPointRec(mouse, editor.cancelButton)) {
            MapCloseMarkerEditor();
            return;
        }
        if (homeMap.markerEditorEditing &&
            CheckCollisionPointRec(mouse, editor.deleteButton)) {
            MapMarkersRemove(homeMap.markerEditorId);
            MapCloseMarkerEditor();
            return;
        }
        if (CheckCollisionPointRec(mouse, editor.saveButton)) {
            MapSaveMarkerEditor();
            return;
        }
    }
    if (IsKeyPressed(KEY_ENTER)) MapSaveMarkerEditor();
}

void HomeWorldMapUpdate(Vector3 playerPosition, float playerYaw,
                        float daylight)
{
    if (!homeMap.open) return;
    homeMap.playerPosition = playerPosition;
    homeMap.playerYaw = playerYaw;
    homeMap.daylight = daylight;
    float playerLongitude = 0.0f;
    float playerLatitude = 0.0f;
    MapPlayerLatLon(&playerLongitude, &playerLatitude);
    if (!homeMap.textureReady || homeMap.cacheDirty) MapRefresh(daylight);

    HomeWorldMapLayout layout = MapLayout();
    Vector2 mouse = GetMousePosition();
    if (homeMap.markerEditorOpen) {
        MapUpdateMarkerEditor(&layout);
        return;
    }
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_M) ||
        (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
         MapPointInButton(mouse, layout.closeButton))) {
        HomeWorldMapClose();
        return;
    }

    HomeWorldMapBounds viewBounds = MapViewBounds(&layout);
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) &&
        CheckCollisionPointRec(mouse, layout.map)) {
        uint32_t markerId = MapMarkerAt(&layout, viewBounds, mouse);
        Vector2 world = HomeWorldMapScreenToWorld(
            viewBounds, layout.map, mouse);
        MapOpenMarkerEditor(markerId, world);
        homeMap.mapPressPending = false;
        homeMap.dragging = false;
        return;
    }

    bool pointerHandled = false;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (MapPointInButton(mouse, layout.zoomOutButton)) {
            MapZoom(1);
            pointerHandled = true;
        } else if (MapPointInButton(mouse, layout.zoomInButton)) {
            MapZoom(-1);
            pointerHandled = true;
        } else if (MapPointInButton(mouse, layout.recenterButton)) {
            MapRecenter();
            pointerHandled = true;
        } else if (MapPointInButton(mouse, layout.globeResetButton)) {
            homeMap.globeLongitude = playerLongitude;
            homeMap.globeLatitude = playerLatitude;
            pointerHandled = true;
        } else if (MapPointInButton(mouse, layout.globe)) {
            homeMap.globeDragging = true;
            homeMap.globePointerMoved = false;
            homeMap.globePressStart = mouse;
            pointerHandled = true;
        }
        if (!pointerHandled) {
            bool *layers[] = {
                &homeMap.showTerrain, &homeMap.showEcology,
                &homeMap.showCreatures, &homeMap.showLandmarks,
                &homeMap.showSubsurfaceLiquids
            };
            for (int i = 0; i < 5; i++) {
                if (MapPointInButton(mouse, MapLayerButton(&layout, i))) {
                    *layers[i] = !*layers[i];
                    pointerHandled = true;
                }
            }
        }
    }

    if (homeMap.globeDragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 delta = GetMouseDelta();
        if (Vector2Distance(mouse, homeMap.globePressStart) > 3.0f) {
            homeMap.globePointerMoved = true;
        }
        homeMap.globeLongitude -= delta.x * 0.012f;
        homeMap.globeLongitude = atan2f(sinf(homeMap.globeLongitude),
                                        cosf(homeMap.globeLongitude));
        homeMap.globeLatitude = MapClamp(
            homeMap.globeLatitude + delta.y * 0.010f,
            -PI * 0.48f, PI * 0.48f);
    }
    if (homeMap.globeDragging &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (!homeMap.globePointerMoved) {
            float longitude = 0.0f;
            float latitude = 0.0f;
            if (SurfaceGlobeHitTest(layout.globe, homeMap.globeLongitude,
                                    homeMap.globeLatitude, mouse,
                                    &longitude, &latitude)) {
                uint32_t markerId = MapGlobeMarkerAt(longitude, latitude);
                if (markerId != 0u) MapMarkersToggleTarget(markerId);
                MapFocusGlobe(longitude, latitude);
            }
        }
        homeMap.globeDragging = false;
        homeMap.globePointerMoved = false;
    }

    if (!homeMap.globeDragging && CheckCollisionPointRec(mouse, layout.map)) {
        float wheel = GetMouseWheelMove();
        if (wheel > 0.0f) MapZoom(-1);
        if (wheel < 0.0f) MapZoom(1);
        if (!pointerHandled && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            homeMap.mapPressPending = true;
            homeMap.mapPressStart = mouse;
            homeMap.mapPressMarkerId = MapMarkerAt(
                &layout, viewBounds, mouse);
        }
    }
    if (homeMap.mapPressPending &&
        IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
        Vector2Distance(mouse, homeMap.mapPressStart) > 6.0f) {
        homeMap.mapPressPending = false;
        homeMap.dragging = true;
        homeMap.dragOffset = Vector2Subtract(mouse, homeMap.mapPressStart);
    } else if (homeMap.dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 delta = GetMouseDelta();
        homeMap.dragOffset.x += delta.x;
        homeMap.dragOffset.y += delta.y;
    }
    if (homeMap.mapPressPending &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (homeMap.mapPressMarkerId != 0u) {
            MapMarkersToggleTarget(homeMap.mapPressMarkerId);
        }
        homeMap.mapPressPending = false;
        homeMap.mapPressMarkerId = 0u;
    }
    if (homeMap.dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        homeMap.dragging = false;
        homeMap.bounds.centerX -=
            homeMap.dragOffset.x * homeMap.bounds.span / layout.map.width;
        homeMap.bounds.centerZ -=
            homeMap.dragOffset.y * homeMap.bounds.span / layout.map.height;
        homeMap.dragOffset = Vector2Zero();
        MapRefresh(daylight);
    }

    float step = homeMap.bounds.span * 0.25f;
    if (IsKeyPressed(KEY_A)) MapPan(-step, 0.0f);
    if (IsKeyPressed(KEY_D)) MapPan(step, 0.0f);
    if (IsKeyPressed(KEY_W)) MapPan(0.0f, -step);
    if (IsKeyPressed(KEY_S)) MapPan(0.0f, step);
    if (IsKeyPressed(KEY_R)) MapRecenter();
    if (IsKeyPressed(KEY_PAGE_UP)) MapZoom(-1);
    if (IsKeyPressed(KEY_PAGE_DOWN)) MapZoom(1);
}

static void MapDrawIconButton(Rectangle button, int icon)
{
    Vector2 mouse = GetMousePosition();
    Color fill = CheckCollisionPointRec(mouse, button)
        ? (Color){ 62, 72, 76, 245 } : (Color){ 35, 43, 46, 235 };
    DrawRectangleRec(button, fill);
    DrawRectangleLinesEx(button, 1.0f, Fade(WHITE, 0.36f));
    float cx = button.x + button.width * 0.5f;
    float cy = button.y + button.height * 0.5f;
    if (icon == 0 || icon == 1) {
        DrawLineEx((Vector2){ cx - 6.0f, cy },
                   (Vector2){ cx + 6.0f, cy }, 2.0f, WHITE);
        if (icon == 1) {
            DrawLineEx((Vector2){ cx, cy - 6.0f },
                       (Vector2){ cx, cy + 6.0f }, 2.0f, WHITE);
        }
    } else if (icon == 2) {
        DrawCircleLines((int)cx, (int)cy, 6.0f, WHITE);
        DrawCircle((int)cx, (int)cy, 2.0f, WHITE);
    } else if (icon == 3) {
        DrawLineEx((Vector2){ cx - 5.0f, cy - 5.0f },
                   (Vector2){ cx + 5.0f, cy + 5.0f }, 2.0f, WHITE);
        DrawLineEx((Vector2){ cx + 5.0f, cy - 5.0f },
                   (Vector2){ cx - 5.0f, cy + 5.0f }, 2.0f, WHITE);
    } else {
        DrawCircleSectorLines((Vector2){ cx, cy }, 6.0f, -48.0f, 250.0f,
                               16, WHITE);
        DrawTriangle((Vector2){ cx + 5.0f, cy - 6.0f },
                     (Vector2){ cx + 7.0f, cy + 1.0f },
                     (Vector2){ cx, cy - 1.0f }, WHITE);
    }
}

static void MapDrawHeat(const HomeWorldMapLayout *layout)
{
    float cellWidth = layout->map.width / (float)HOMEWORLD_MAP_HEAT_SIZE;
    float cellHeight = layout->map.height / (float)HOMEWORLD_MAP_HEAT_SIZE;
    for (int z = 0; z < HOMEWORLD_MAP_HEAT_SIZE; z++) {
        for (int x = 0; x < HOMEWORLD_MAP_HEAT_SIZE; x++) {
            float activity = homeMap.heat[z * HOMEWORLD_MAP_HEAT_SIZE + x];
            if (activity < 0.08f) continue;
            Rectangle cell = {
                layout->map.x + (float)x * cellWidth + homeMap.dragOffset.x,
                layout->map.y + (float)z * cellHeight + homeMap.dragOffset.y,
                cellWidth + 1.0f, cellHeight + 1.0f
            };
            Color heatColor = activity > 0.62f
                ? (Color){ 232, 90, 62, (unsigned char)(activity * 82.0f) }
                : (Color){ 232, 185, 66, (unsigned char)(activity * 65.0f) };
            DrawRectangleRec(cell, heatColor);
        }
    }
}

static Color MapSubsurfaceLiquidColor(TerrainSubsurfaceLiquidKind kind,
                                      unsigned char alpha)
{
    if (kind == TERRAIN_SUBSURFACE_LIQUID_LAVA) {
        return (Color){ 246, 92, 40, alpha };
    }
    return (Color){ 44, 177, 226, alpha };
}

static void MapDrawSubsurfaceLiquids(const HomeWorldMapLayout *layout)
{
    float cellWidth = layout->map.width / (float)HOMEWORLD_MAP_HEAT_SIZE;
    float cellHeight = layout->map.height / (float)HOMEWORLD_MAP_HEAT_SIZE;
    for (int z = 0; z < HOMEWORLD_MAP_HEAT_SIZE; z++) {
        for (int x = 0; x < HOMEWORLD_MAP_HEAT_SIZE; x++) {
            TerrainSubsurfaceLiquidSummary summary =
                homeMap.subsurfaceLiquids[
                    z * HOMEWORLD_MAP_HEAT_SIZE + x];
            if (summary.kind == TERRAIN_SUBSURFACE_LIQUID_NONE) continue;
            unsigned char alpha = (unsigned char)MapClamp(
                18.0f + summary.floodedFraction * 70.0f, 18.0f, 58.0f);
            Rectangle cell = {
                layout->map.x + (float)x * cellWidth + homeMap.dragOffset.x,
                layout->map.y + (float)z * cellHeight + homeMap.dragOffset.y,
                cellWidth + 1.0f, cellHeight + 1.0f
            };
            Color fill = MapSubsurfaceLiquidColor(summary.kind, alpha);
            Color hatch = MapSubsurfaceLiquidColor(
                summary.kind, (unsigned char)MapClamp(
                    (float)alpha + 60.0f, 0.0f, 118.0f));
            DrawRectangleRec(cell, fill);
            DrawLineEx(
                (Vector2){ cell.x, cell.y + cell.height * 0.72f },
                (Vector2){ cell.x + cell.width * 0.72f, cell.y },
                1.0f, hatch);
            DrawLineEx(
                (Vector2){ cell.x + cell.width * 0.28f,
                           cell.y + cell.height },
                (Vector2){ cell.x + cell.width,
                           cell.y + cell.height * 0.28f },
                1.0f, hatch);
        }
    }
}

static const char *MapLandmarkName(HomeWorldMapLandmarkKind kind)
{
    switch (kind) {
    case HOMEWORLD_MAP_LANDMARK_PEAK: return "Peak";
    case HOMEWORLD_MAP_LANDMARK_SHORE: return "Shore";
    case HOMEWORLD_MAP_LANDMARK_FOREST: return "Forest Core";
    case HOMEWORLD_MAP_LANDMARK_FAUNA: return "Fauna Hotspot";
    default: return "Landmark";
    }
}

static Color MapLandmarkColor(HomeWorldMapLandmarkKind kind)
{
    switch (kind) {
    case HOMEWORLD_MAP_LANDMARK_PEAK: return (Color){ 238, 238, 230, 255 };
    case HOMEWORLD_MAP_LANDMARK_SHORE: return (Color){ 76, 190, 218, 255 };
    case HOMEWORLD_MAP_LANDMARK_FOREST: return (Color){ 112, 214, 115, 255 };
    case HOMEWORLD_MAP_LANDMARK_FAUNA: return (Color){ 244, 169, 58, 255 };
    default: return WHITE;
    }
}

static void MapDrawLandmark(Vector2 point, HomeWorldMapLandmarkKind kind,
                            Color color)
{
    if (kind == HOMEWORLD_MAP_LANDMARK_PEAK) {
        DrawTriangle((Vector2){ point.x, point.y - 6.0f },
                     (Vector2){ point.x - 6.0f, point.y + 5.0f },
                     (Vector2){ point.x + 6.0f, point.y + 5.0f }, color);
    } else if (kind == HOMEWORLD_MAP_LANDMARK_SHORE) {
        DrawLineEx((Vector2){ point.x - 6.0f, point.y - 2.0f },
                   (Vector2){ point.x, point.y + 1.0f }, 2.0f, color);
        DrawLineEx((Vector2){ point.x, point.y + 1.0f },
                   (Vector2){ point.x + 6.0f, point.y - 2.0f }, 2.0f, color);
        DrawLineEx((Vector2){ point.x - 6.0f, point.y + 3.0f },
                   (Vector2){ point.x + 6.0f, point.y + 3.0f }, 2.0f, color);
    } else if (kind == HOMEWORLD_MAP_LANDMARK_FOREST) {
        DrawCircle(point.x, point.y - 2.0f, 5.0f, color);
        DrawRectangle((int)point.x - 1, (int)point.y + 2, 3, 5, color);
    } else {
        DrawCircleLines((int)point.x, (int)point.y, 6.0f, color);
        DrawCircle((int)point.x, (int)point.y, 2.0f, color);
    }
}

static const char *MapEntityKindName(const EntityMapMarker *marker)
{
    if (!marker) return "Creature";
    switch (marker->kind) {
    case ENTITY_MAP_MARKER_AQUATIC: return "Aquatic creature";
    case ENTITY_MAP_MARKER_AERIAL: return "Aerial creature";
    case ENTITY_MAP_MARKER_HOSTILE: return "Hostile creature";
    case ENTITY_MAP_MARKER_LAND:
    default: return "Land creature";
    }
}

static Color MapEntityColor(EntityMapMarkerKind kind)
{
    switch (kind) {
    case ENTITY_MAP_MARKER_AQUATIC: return (Color){ 76, 205, 226, 255 };
    case ENTITY_MAP_MARKER_AERIAL: return (Color){ 246, 211, 92, 255 };
    case ENTITY_MAP_MARKER_HOSTILE: return (Color){ 235, 78, 74, 255 };
    case ENTITY_MAP_MARKER_LAND:
    default: return (Color){ 239, 238, 220, 255 };
    }
}

static void MapDrawEntityMarker(Vector2 point, EntityMapMarkerKind kind)
{
    Color color = MapEntityColor(kind);
    if (kind == ENTITY_MAP_MARKER_AQUATIC) {
        DrawTriangle((Vector2){ point.x, point.y - 5.0f },
                     (Vector2){ point.x - 5.0f, point.y },
                     (Vector2){ point.x, point.y + 5.0f }, color);
        DrawTriangle((Vector2){ point.x, point.y - 5.0f },
                     (Vector2){ point.x, point.y + 5.0f },
                     (Vector2){ point.x + 5.0f, point.y }, color);
    } else if (kind == ENTITY_MAP_MARKER_AERIAL) {
        DrawTriangle((Vector2){ point.x, point.y - 6.0f },
                     (Vector2){ point.x - 5.0f, point.y + 4.0f },
                     (Vector2){ point.x + 5.0f, point.y + 4.0f }, color);
    } else if (kind == ENTITY_MAP_MARKER_HOSTILE) {
        DrawLineEx((Vector2){ point.x - 4.0f, point.y - 4.0f },
                   (Vector2){ point.x + 4.0f, point.y + 4.0f }, 2.5f, color);
        DrawLineEx((Vector2){ point.x + 4.0f, point.y - 4.0f },
                   (Vector2){ point.x - 4.0f, point.y + 4.0f }, 2.5f, color);
    } else {
        DrawCircle((int)point.x, (int)point.y, 4.2f, color);
        DrawCircleLines((int)point.x, (int)point.y, 6.0f, Fade(BLACK, 0.65f));
    }
}

static void MapDrawCustomMarker(Vector2 point, const MapMarker *marker,
                                bool selected)
{
    if (!marker) return;
    Color color = MapMarkerColorValue(marker->color);
    if (selected) {
        DrawCircleLines((int)point.x, (int)point.y - 4, 10.0f,
                        Fade(WHITE, 0.94f));
        DrawCircleLines((int)point.x, (int)point.y - 4, 12.0f,
                        Fade(color, 0.62f));
    }
    DrawTriangle((Vector2){ point.x, point.y + 7.0f },
                 (Vector2){ point.x - 5.0f, point.y - 2.0f },
                 (Vector2){ point.x + 5.0f, point.y - 2.0f }, color);
    DrawCircleV((Vector2){ point.x, point.y - 4.0f }, 6.0f,
                Fade(BLACK, 0.78f));
    DrawCircleV((Vector2){ point.x, point.y - 4.0f }, 4.2f, color);
    UiDrawText(marker->name, (int)point.x + 10, (int)point.y - 13, 13,
               selected ? WHITE : Fade(WHITE, 0.88f));
}

static void MapDrawPlayer(const HomeWorldMapLayout *layout,
                          HomeWorldMapBounds viewBounds)
{
    Vector2 point = HomeWorldMapWorldToScreen(
        viewBounds, layout->map, homeMap.playerPosition.x,
        homeMap.playerPosition.z);
    float forwardX = sinf(homeMap.playerYaw);
    float forwardY = cosf(homeMap.playerYaw);
    Vector2 tip = { point.x + forwardX * 10.0f,
                    point.y + forwardY * 10.0f };
    Vector2 side = { -forwardY * 6.0f, forwardX * 6.0f };
    Vector2 back = { point.x - forwardX * 6.0f,
                     point.y - forwardY * 6.0f };
    DrawTriangle(tip, (Vector2){ back.x + side.x, back.y + side.y },
                 (Vector2){ back.x - side.x, back.y - side.y }, WHITE);
    DrawCircleLines((int)point.x, (int)point.y, 12.0f,
                    Fade(BLACK, 0.72f));
}

static bool MapShipPosition(Vector3 *position)
{
    if (!position) return false;
    ShipLocatorRecord record = ShipLocatorGetRecord();
    if (!record.deployed || record.dimension != WorldCurrentDimension() ||
        record.surfaceId != WorldCurrentSurfaceId()) return false;
    *position = (Vector3){ (float)record.x + 0.5f, (float)record.y + 0.5f,
                           (float)record.z + 0.5f };
    return true;
}

static Vector2 MapClampToEdge(Rectangle map, Vector2 point)
{
    float margin = 10.0f;
    point.x = MapClamp(point.x, map.x + margin,
                       map.x + map.width - margin);
    point.y = MapClamp(point.y, map.y + margin,
                       map.y + map.height - margin);
    return point;
}

static void MapDrawShipMarker(const HomeWorldMapLayout *layout,
                              HomeWorldMapBounds viewBounds,
                              Vector3 shipPosition)
{
    Vector2 point = HomeWorldMapWorldToScreen(
        viewBounds, layout->map, shipPosition.x, shipPosition.z);
    point = MapClampToEdge(layout->map, point);
    Color color = (Color){ 244, 174, 62, 255 };
    DrawRectangle((int)point.x - 5, (int)point.y - 5, 10, 10, color);
    DrawRectangleLines((int)point.x - 7, (int)point.y - 7, 14, 14,
                       Fade(WHITE, 0.82f));
}

static void MapDrawLayerToggle(Rectangle row, bool enabled,
                               const char *label)
{
    Rectangle box = { row.x + 2.0f, row.y + 3.0f, 16.0f, 16.0f };
    DrawRectangleRec(box, enabled ? (Color){ 68, 161, 128, 255 }
                                   : (Color){ 30, 37, 40, 255 });
    DrawRectangleLinesEx(box, 1.0f, Fade(WHITE, 0.46f));
    if (enabled) {
        DrawLineEx((Vector2){ box.x + 3.0f, box.y + 8.0f },
                   (Vector2){ box.x + 7.0f, box.y + 12.0f }, 2.0f, WHITE);
        DrawLineEx((Vector2){ box.x + 7.0f, box.y + 12.0f },
                   (Vector2){ box.x + 14.0f, box.y + 4.0f }, 2.0f, WHITE);
    }
    UiDrawText(label, (int)row.x + 25, (int)row.y + 2, 15,
               enabled ? WHITE : Fade(WHITE, 0.52f));
}

static const char *MapTerrainName(HomeWorldMapTerrainCell cell)
{
    return cell.planetSurface
        ? PlanetBiomeName(cell.planetBiome)
        : HomeWorldMapBiomeName(cell.biome, cell.waterDepth > 0);
}

static void MapDrawTerrainLegend(const HomeWorldMapLayout *layout,
                                 float legendY)
{
    if (!homeMap.planetSurface) {
        const Color terrainColors[] = {
            { 89, 143, 76, 255 }, { 44, 102, 60, 255 },
            { 184, 156, 88, 255 }, { 202, 216, 218, 255 },
            { 52, 96, 69, 255 }, { 42, 112, 153, 255 }
        };
        const char *terrainNames[] = {
            "Plains", "Forest", "Desert", "Snow / highland", "Swamp",
            "Water"
        };
        for (int i = 0; i < 6; i++) {
            DrawRectangle((int)layout->sidebar.x,
                          (int)legendY + 26 + i * 22, 14, 14,
                          terrainColors[i]);
            UiDrawText(terrainNames[i], (int)layout->sidebar.x + 22,
                       (int)legendY + 24 + i * 22, 14,
                       Fade(WHITE, 0.76f));
        }
        return;
    }

    int counts[PLANET_BIOME_COUNT] = { 0 };
    bool drawn[PLANET_BIOME_COUNT] = { false };
    for (int i = 0;
         i < HOMEWORLD_MAP_RASTER_SIZE * HOMEWORLD_MAP_RASTER_SIZE; i++) {
        PlanetBiome biome = homeMap.cells[i].planetBiome;
        if (biome >= 0 && biome < PLANET_BIOME_COUNT) counts[biome]++;
    }
    for (int row = 0; row < 5; row++) {
        int selected = -1;
        for (int biome = 0; biome < PLANET_BIOME_COUNT; biome++) {
            if (!drawn[biome] && counts[biome] > 0 &&
                (selected < 0 || counts[biome] > counts[selected])) {
                selected = biome;
            }
        }
        if (selected < 0) break;
        drawn[selected] = true;
        HomeWorldMapTerrainCell cell = {
            .planetSurface = true,
            .planetBiome = (PlanetBiome)selected,
            .elevation = 84.0f,
            .seaLevel = 80.0f
        };
        if (selected == PLANET_BIOME_OCEAN ||
            selected == PLANET_BIOME_LAVA_SEA ||
            selected == PLANET_BIOME_GLACIER) {
            cell.elevation = 56.0f;
            cell.waterDepth = 24;
        }
        DrawRectangle((int)layout->sidebar.x,
                      (int)legendY + 26 + row * 22, 14, 14,
                      HomeWorldMapTerrainColor(cell));
        UiDrawText(MapTerrainName(cell),
                   (int)layout->sidebar.x + 22,
                   (int)legendY + 24 + row * 22, 14,
                   Fade(WHITE, 0.76f));
    }
}

static void MapDrawGlobe(const HomeWorldMapLayout *layout)
{
    float longitude = 0.0f;
    float latitude = 0.0f;
    MapPlayerLatLon(&longitude, &latitude);
    MapMarker mapMarkers[MAP_MARKERS_PER_SURFACE];
    SurfaceGlobeMarker globeMarkers[MAP_MARKERS_PER_SURFACE];
    int markerCount = MapMarkersCollect(
        MapCurrentSurface(), mapMarkers, MAP_MARKERS_PER_SURFACE);
    uint32_t targetId = MapMarkersTargetId();
    for (int i = 0; i < markerCount; i++) {
        float markerLongitude = 0.0f;
        float markerLatitude = 0.0f;
        MapMarkerLatLon(&mapMarkers[i], &markerLongitude, &markerLatitude);
        globeMarkers[i] = (SurfaceGlobeMarker){
            .longitude = markerLongitude,
            .latitude = markerLatitude,
            .color = MapMarkerColorValue(mapMarkers[i].color),
            .selected = mapMarkers[i].id == targetId
        };
    }
    SurfaceGlobeDraw(&(SurfaceGlobeDrawParams){
        .destination = layout->globe,
        .planetSurface = homeMap.planetSurface,
        .cameraLongitude = homeMap.globeLongitude,
        .cameraLatitude = homeMap.globeLatitude,
        .markerLongitude = longitude,
        .markerLatitude = latitude,
        .markers = globeMarkers,
        .markerCount = markerCount
    });
    UiDrawText("GLOBAL SURFACE", (int)layout->globe.x,
               (int)layout->globe.y - 16, 12, Fade(WHITE, 0.58f));
    DrawCircleLines((int)(layout->globe.x + layout->globe.width * 0.5f),
                    (int)(layout->globe.y + layout->globe.height * 0.5f),
                    layout->globe.width * 0.49f, Fade(WHITE, 0.42f));
    MapDrawIconButton(layout->globeResetButton, 4);
}

static void MapDrawSidebar(const HomeWorldMapLayout *layout,
                           const char *hoverTitle, const char *hoverDetail)
{
    MapDrawGlobe(layout);
    float layerX = layout->compact
        ? layout->globe.x + layout->globe.width + 12.0f
        : layout->sidebar.x;
    float layerY = layout->compact
        ? layout->sidebar.y
        : layout->globe.y + layout->globe.height + 8.0f;
    UiDrawText("LAYERS", (int)layerX, (int)layerY, 14, Fade(WHITE, 0.58f));
    const char *labels[] = {
        "Terrain", "Ecology", "Creatures", "Landmarks",
        "Subsurface liquids"
    };
    bool enabled[] = {
        homeMap.showTerrain, homeMap.showEcology,
        homeMap.showCreatures, homeMap.showLandmarks,
        homeMap.showSubsurfaceLiquids
    };
    for (int i = 0; i < 5; i++) {
        MapDrawLayerToggle(MapLayerButton(layout, i), enabled[i], labels[i]);
    }

    if (layout->compact) {
        int infoY = (int)layout->sidebar.y + 116;
        UiDrawText(hoverTitle, (int)layerX, infoY, 15, WHITE);
        UiDrawText(hoverDetail, (int)layerX, infoY + 21, 12,
                   Fade(WHITE, 0.66f));
        return;
    }

    float legendY = layout->globe.y + layout->globe.height + 122.0f;
    UiDrawText("TERRAIN", (int)layout->sidebar.x, (int)legendY, 14,
               Fade(WHITE, 0.58f));
    MapDrawTerrainLegend(layout, legendY);

    float infoY = layout->sidebar.y + layout->sidebar.height - 72.0f;
    bool tightSidebar = layout->sidebar.height < 620.0f;
    float markerY = tightSidebar ? infoY - 34.0f : legendY + 154.0f;
    UiDrawText("MARKERS", (int)layout->sidebar.x, (int)markerY, 14,
               Fade(WHITE, 0.58f));
    const char *markerNames[] = {
        "Land", "Aquatic", "Aerial", "Hostile"
    };
    if (tightSidebar) {
        float markerWidth = layout->sidebar.width / 4.0f;
        for (int i = 0; i < 4; i++) {
            float markerX = layout->sidebar.x + (float)i * markerWidth;
            DrawCircle((int)markerX + 4, (int)markerY + 24, 3.0f,
                       MapEntityColor((EntityMapMarkerKind)i));
            UiDrawText(markerNames[i], (int)markerX + 11,
                       (int)markerY + 17, 11, Fade(WHITE, 0.76f));
        }
    } else {
        float markerWidth = (layout->sidebar.width - 8.0f) * 0.5f;
        for (int i = 0; i < 4; i++) {
            int column = i % 2;
            int row = i / 2;
            float markerX = layout->sidebar.x +
                            (float)column * (markerWidth + 8.0f);
            DrawCircle((int)markerX + 7,
                       (int)markerY + 30 + row * 22, 4.0f,
                       MapEntityColor((EntityMapMarkerKind)i));
            UiDrawText(markerNames[i], (int)markerX + 22,
                       (int)markerY + 22 + row * 22, 14,
                       Fade(WHITE, 0.76f));
        }
    }

    DrawLine((int)layout->sidebar.x, (int)infoY - 12,
             (int)(layout->sidebar.x + layout->sidebar.width),
             (int)infoY - 12, Fade(WHITE, 0.16f));
    UiDrawText(hoverTitle, (int)layout->sidebar.x, (int)infoY, 17, WHITE);
    UiDrawText(hoverDetail, (int)layout->sidebar.x, (int)infoY + 25, 14,
               Fade(WHITE, 0.66f));
}

static void MapDrawEditorButton(Rectangle button, const char *label,
                                Color accent)
{
    bool hovered = CheckCollisionPointRec(GetMousePosition(), button);
    DrawRectangleRec(button, hovered ? Fade(accent, 0.42f)
                                     : Fade(accent, 0.24f));
    DrawRectangleLinesEx(button, 1.0f,
                         hovered ? Fade(accent, 0.92f)
                                 : Fade(WHITE, 0.34f));
    int width = UiMeasureText(label, 15);
    UiDrawText(label, (int)(button.x + (button.width - width) * 0.5f),
               (int)button.y + 8, 15, WHITE);
}

static void MapDrawMarkerEditor(const HomeWorldMapLayout *layout)
{
    if (!homeMap.markerEditorOpen) return;
    MapMarkerEditorLayout editor = MapEditorLayout(layout);
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  Fade(BLACK, 0.48f));
    DrawRectangleRec(editor.panel, (Color){ 24, 31, 33, 255 });
    DrawRectangleLinesEx(editor.panel, 1.5f, Fade(WHITE, 0.48f));
    UiDrawText(homeMap.markerEditorEditing ? "Edit map marker"
                                           : "Add map marker",
               (int)editor.panel.x + 24, (int)editor.panel.y + 16,
               21, WHITE);
    UiDrawText(TextFormat("XZ %.0f, %.0f", homeMap.markerEditorWorld.x,
                          homeMap.markerEditorWorld.y),
               (int)editor.panel.x + 24, (int)editor.panel.y + 42,
               13, Fade(WHITE, 0.58f));

    DrawRectangleRec(editor.input, (Color){ 11, 17, 19, 255 });
    DrawRectangleLinesEx(editor.input, 1.0f, Fade(WHITE, 0.48f));
    int nameFont = 19;
    while (nameFont > 13 &&
           UiMeasureText(homeMap.markerEditorName, nameFont) >
               (int)editor.input.width - 18) {
        nameFont--;
    }
    UiDrawText(homeMap.markerEditorName, (int)editor.input.x + 9,
               (int)editor.input.y + 11, nameFont, WHITE);
    if (((int)(GetTime() * 2.0) & 1) == 0) {
        int cursorX = (int)editor.input.x + 9 +
                      UiMeasureText(homeMap.markerEditorName, nameFont) + 2;
        cursorX = (int)fminf((float)cursorX,
                             editor.input.x + editor.input.width - 7.0f);
        DrawLine(cursorX, (int)editor.input.y + 9,
                 cursorX, (int)(editor.input.y + editor.input.height) - 8,
                 WHITE);
    }

    for (int i = 0; i < MAP_MARKER_COLOR_COUNT; i++) {
        Rectangle swatch = editor.swatches[i];
        Color color = MapMarkerColorValue((MapMarkerColor)i);
        DrawCircleV((Vector2){ swatch.x + swatch.width * 0.5f,
                               swatch.y + swatch.height * 0.5f },
                    10.0f, color);
        if (homeMap.markerEditorColor == (MapMarkerColor)i) {
            DrawCircleLines((int)(swatch.x + swatch.width * 0.5f),
                            (int)(swatch.y + swatch.height * 0.5f),
                            13.0f, WHITE);
        }
    }

    if (homeMap.markerEditorEditing) {
        MapDrawEditorButton(editor.deleteButton, "Delete",
                            (Color){ 216, 72, 68, 255 });
    }
    MapDrawEditorButton(editor.cancelButton, "Cancel",
                        (Color){ 95, 108, 112, 255 });
    MapDrawEditorButton(editor.saveButton, "Save",
                        (Color){ 68, 161, 128, 255 });
    if (homeMap.markerEditorError[0] != '\0') {
        UiDrawText(homeMap.markerEditorError,
                   (int)editor.panel.x + 24,
                   (int)editor.panel.y + 164, 12,
                   (Color){ 242, 112, 96, 255 });
    }
}

void HomeWorldMapDraw(void)
{
    if (!homeMap.open) return;
    HomeWorldMapLayout layout = MapLayout();
    HomeWorldMapBounds viewBounds = MapViewBounds(&layout);
    Vector2 mouse = GetMousePosition();
    char mapTitle[64];
    char surveyTitle[64];
    snprintf(mapTitle, sizeof(mapTitle), "%s Map", homeMap.surfaceName);
    snprintf(surveyTitle, sizeof(surveyTitle), "%s survey",
             homeMap.surfaceName);
    const char *hoverTitle = surveyTitle;
    char customHoverTitle[MAP_MARKER_NAME_SIZE] = { 0 };
    char hoverDetail[128];
    snprintf(hoverDetail, sizeof(hoverDetail),
             "Center %.0f, %.0f   |   %.0f block view",
             viewBounds.centerX, viewBounds.centerZ, viewBounds.span);

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  Fade(BLACK, 0.72f));
    DrawRectangleRounded(layout.panel, 0.018f, 6,
                         (Color){ 22, 28, 29, 250 });
    DrawRectangleRoundedLinesEx(layout.panel, 0.018f, 6, 1.5f,
                                Fade(WHITE, 0.34f));
    UiDrawText(mapTitle, (int)layout.panel.x + 22,
               (int)layout.panel.y + 13, 25, WHITE);
    UiDrawText(TextFormat("%.0f block regional survey", viewBounds.span),
               (int)layout.panel.x + 22, (int)layout.panel.y + 42, 14,
               Fade(WHITE, 0.58f));

    DrawRectangleRec(layout.map, (Color){ 11, 17, 19, 255 });
    BeginScissorMode((int)layout.map.x, (int)layout.map.y,
                     (int)layout.map.width, (int)layout.map.height);
    if (homeMap.showTerrain && homeMap.textureReady) {
        Rectangle source = {
            0.0f, 0.0f, (float)homeMap.terrainTexture.width,
            (float)homeMap.terrainTexture.height
        };
        Rectangle destination = {
            layout.map.x + homeMap.dragOffset.x,
            layout.map.y + homeMap.dragOffset.y,
            layout.map.width, layout.map.height
        };
        DrawTexturePro(homeMap.terrainTexture, source, destination,
                       Vector2Zero(), 0.0f, WHITE);
    }
    if (homeMap.showEcology) MapDrawHeat(&layout);
    if (homeMap.showSubsurfaceLiquids) {
        MapDrawSubsurfaceLiquids(&layout);
    }

    if (homeMap.showLandmarks) {
        for (int i = 0; i < homeMap.landmarkCount; i++) {
            HomeWorldMapLandmark *landmark = &homeMap.landmarks[i];
            if (!HomeWorldMapWorldVisible(viewBounds,
                                          (float)landmark->x,
                                          (float)landmark->z)) continue;
            Vector2 point = HomeWorldMapWorldToScreen(
                viewBounds, layout.map, (float)landmark->x,
                (float)landmark->z);
            MapDrawLandmark(point, landmark->kind,
                            MapLandmarkColor(landmark->kind));
            if (Vector2Distance(mouse, point) <= 9.0f) {
                hoverTitle = MapLandmarkName(landmark->kind);
                snprintf(hoverDetail, sizeof(hoverDetail),
                         "XZ %d, %d   |   elevation %d",
                         landmark->x, landmark->z, landmark->elevation);
            }
        }
    }

    EntityMapMarker markers[MAX_ENTITIES];
    int markerCount = homeMap.showCreatures
        ? EntitiesCollectMapMarkers(markers, MAX_ENTITIES) : 0;
    for (int i = 0; i < markerCount; i++) {
        EntityMapMarker *marker = &markers[i];
        if (!HomeWorldMapWorldVisible(viewBounds, marker->position.x,
                                      marker->position.z)) continue;
        Vector2 point = HomeWorldMapWorldToScreen(
            viewBounds, layout.map, marker->position.x, marker->position.z);
        MapDrawEntityMarker(point, marker->kind);
        if (Vector2Distance(mouse, point) <= 9.0f) {
            hoverTitle = MapEntityKindName(marker);
            if (marker->evolvable) {
                snprintf(hoverDetail, sizeof(hoverDetail),
                         "Species %08X   |   XZ %.0f, %.0f",
                         marker->speciesId, marker->position.x,
                         marker->position.z);
            } else {
                snprintf(hoverDetail, sizeof(hoverDetail),
                         "XZ %.0f, %.0f", marker->position.x,
                         marker->position.z);
            }
        }
    }

    MapMarker customMarkers[MAP_MARKERS_PER_SURFACE];
    int customMarkerCount = MapMarkersCollect(
        MapCurrentSurface(), customMarkers, MAP_MARKERS_PER_SURFACE);
    uint32_t targetId = MapMarkersTargetId();
    for (int i = 0; i < customMarkerCount; i++) {
        MapMarker *marker = &customMarkers[i];
        if (!HomeWorldMapWorldVisible(viewBounds, marker->x, marker->z)) {
            continue;
        }
        Vector2 point = HomeWorldMapWorldToScreen(
            viewBounds, layout.map, marker->x, marker->z);
        MapDrawCustomMarker(point, marker, marker->id == targetId);
        if (Vector2Distance(mouse, point) <= 13.0f) {
            snprintf(customHoverTitle, sizeof(customHoverTitle), "%s",
                     marker->name);
            hoverTitle = customHoverTitle;
            snprintf(hoverDetail, sizeof(hoverDetail),
                     "XZ %.0f, %.0f%s", marker->x, marker->z,
                     marker->id == targetId ? "   |   navigation target" : "");
        }
    }

    Vector3 shipPosition = { 0 };
    if (MapShipPosition(&shipPosition)) {
        MapDrawShipMarker(&layout, viewBounds, shipPosition);
        Vector2 shipPoint = MapClampToEdge(
            layout.map, HomeWorldMapWorldToScreen(
                viewBounds, layout.map, shipPosition.x, shipPosition.z));
        if (Vector2Distance(mouse, shipPoint) <= 11.0f) {
            hoverTitle = "Parked ship";
            snprintf(hoverDetail, sizeof(hoverDetail),
                     "XZ %.0f, %.0f   |   %.0f blocks",
                     shipPosition.x, shipPosition.z,
                     Vector2Distance(
                         (Vector2){ homeMap.playerPosition.x,
                                    homeMap.playerPosition.z },
                         (Vector2){ shipPosition.x, shipPosition.z }));
        }
    }
    MapDrawPlayer(&layout, viewBounds);
    EndScissorMode();

    DrawRectangleLinesEx(layout.map, 1.5f, Fade(WHITE, 0.45f));
    UiDrawText("N", (int)(layout.map.x + layout.map.width * 0.5f - 5.0f),
               (int)layout.map.y + 8, 14, Fade(WHITE, 0.8f));
    MapDrawIconButton(layout.zoomOutButton, 0);
    MapDrawIconButton(layout.zoomInButton, 1);
    MapDrawIconButton(layout.recenterButton, 2);
    MapDrawIconButton(layout.closeButton, 3);

    if (CheckCollisionPointRec(mouse, layout.zoomOutButton)) {
        hoverTitle = "Zoom out";
        snprintf(hoverDetail, sizeof(hoverDetail), "Show a wider region");
    } else if (CheckCollisionPointRec(mouse, layout.zoomInButton)) {
        hoverTitle = "Zoom in";
        snprintf(hoverDetail, sizeof(hoverDetail), "Show more terrain detail");
    } else if (CheckCollisionPointRec(mouse, layout.recenterButton)) {
        hoverTitle = "Center on player";
        snprintf(hoverDetail, sizeof(hoverDetail), "Return to your position");
    } else if (CheckCollisionPointRec(mouse, layout.closeButton)) {
        hoverTitle = "Close map";
        snprintf(hoverDetail, sizeof(hoverDetail), "Return to %s",
                 homeMap.surfaceName);
    } else if (CheckCollisionPointRec(mouse, layout.globeResetButton)) {
        hoverTitle = "Center globe";
        snprintf(hoverDetail, sizeof(hoverDetail), "Show your current surface");
    } else if (CheckCollisionPointRec(mouse, layout.globe)) {
        hoverTitle = "Global surface";
        float longitudeDegrees = homeMap.globeLongitude * RAD2DEG;
        float latitudeDegrees = homeMap.globeLatitude * RAD2DEG;
        snprintf(hoverDetail, sizeof(hoverDetail),
                 "Center %.1f %c   |   %.1f %c",
                 fabsf(longitudeDegrees), longitudeDegrees < 0.0f ? 'W' : 'E',
                 fabsf(latitudeDegrees), latitudeDegrees < 0.0f ? 'S' : 'N');
    }

    if (CheckCollisionPointRec(mouse, layout.map)) {
        Vector2 world = HomeWorldMapScreenToWorld(
            viewBounds, layout.map, mouse);
        MapSurfaceTerrainSample sample = MapTerrainAt(
            (int)floorf(world.x), (int)floorf(world.y), WorldTerrainMode());
        float u = (world.x - (homeMap.bounds.centerX - homeMap.bounds.span * 0.5f)) /
                  homeMap.bounds.span;
        float v = (world.y - (homeMap.bounds.centerZ - homeMap.bounds.span * 0.5f)) /
                  homeMap.bounds.span;
        float fauna = HomeWorldMapHeatSample(homeMap.heat, u, v);
        if (hoverTitle == surveyTitle) {
            TerrainSubsurfaceLiquidSummary liquid =
                TerrainSubsurfaceLiquidSummaryAt(
                    (int)floorf(world.x), (int)floorf(world.y),
                    (int)lroundf(sample.cell.elevation));
            if (homeMap.showSubsurfaceLiquids &&
                liquid.kind != TERRAIN_SUBSURFACE_LIQUID_NONE) {
                hoverTitle = liquid.kind == TERRAIN_SUBSURFACE_LIQUID_LAVA
                    ? "Lava cavity" : "Water cave";
                snprintf(
                    hoverDetail, sizeof(hoverDetail),
                    "XZ %.0f, %.0f   |   Y %d-%d   |   flooded %.0f%%",
                    world.x, world.y, liquid.minY, liquid.maxY,
                    liquid.floodedFraction * 100.0f);
            } else if (sample.cell.waterDepth > 0) {
                hoverTitle = MapTerrainName(sample.cell);
                snprintf(
                    hoverDetail, sizeof(hoverDetail),
                    "XZ %.0f,%.0f | fauna %.0f%%\nY %d | %d m | %s",
                    world.x, world.y, fauna * 100.0f,
                    sample.bathymetry.seabedY, sample.cell.waterDepth,
                    BathymetryZoneName(sample.bathymetry.zone));
            } else {
                hoverTitle = MapTerrainName(sample.cell);
                snprintf(
                    hoverDetail, sizeof(hoverDetail),
                    "XZ %.0f, %.0f   |   elevation %.0f   |   fauna %.0f%%",
                    world.x, world.y, sample.cell.elevation,
                    fauna * 100.0f);
            }
        }
    }
    MapDrawSidebar(&layout, hoverTitle, hoverDetail);
    MapDrawMarkerEditor(&layout);
}

void HomeWorldMapUnload(void)
{
    if (homeMap.textureReady) {
        UnloadTexture(homeMap.terrainTexture);
    }
    SurfaceGlobeUnload();
    memset(&homeMap, 0, sizeof(homeMap));
}
