#include "homeworld_map.h"

#include "ecology.h"
#include "entity.h"
#include "homeworld_map_model.h"
#include "raylib.h"
#include "raymath.h"
#include "render.h"
#include "ship.h"
#include "space.h"
#include "terrain.h"
#include "world.h"
#include "world_environment.h"

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
    bool compact;
} HomeWorldMapLayout;

typedef struct HomeWorldMapState {
    bool open;
    bool textureReady;
    bool cacheDirty;
    bool dragging;
    bool showTerrain;
    bool showEcology;
    bool showCreatures;
    bool showLandmarks;
    int zoomLevel;
    HomeWorldMapBounds bounds;
    Vector2 dragOffset;
    Vector3 playerPosition;
    float playerYaw;
    float daylight;
    Texture2D terrainTexture;
    Color pixels[HOMEWORLD_MAP_RASTER_SIZE * HOMEWORLD_MAP_RASTER_SIZE];
    HomeWorldMapTerrainCell cells[
        HOMEWORLD_MAP_RASTER_SIZE * HOMEWORLD_MAP_RASTER_SIZE];
    float heat[HOMEWORLD_MAP_HEAT_SIZE * HOMEWORLD_MAP_HEAT_SIZE];
    HomeWorldMapLandmark landmarks[HOMEWORLD_MAP_MAX_LANDMARKS];
    int landmarkCount;
} HomeWorldMapState;

static HomeWorldMapState homeMap = { 0 };

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
    bool compact = sw < 900 || sh < 580;
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
        float sidebarHeight = fminf(142.0f, panel.height * 0.32f);
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
            SurfaceTerrainSample sample =
                SurfaceTerrainAt(worldX, worldZ, terrainMode);
            HomeWorldMapTerrainCell *cell =
                &homeMap.cells[z * HOMEWORLD_MAP_RASTER_SIZE + x];
            *cell = (HomeWorldMapTerrainCell){
                .biome = sample.biome,
                .elevation = sample.elevation,
                .seaLevel = sample.seaLevel,
                .slope = sample.slope,
                .waterDepth = sample.bathymetry.waterDepth,
                .faunaActivity = 0.0f
            };
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
            homeMap.heat[z * HOMEWORLD_MAP_HEAT_SIZE + x] =
                MapClamp(ecology.suitability.faunaActivity, 0.0f, 1.0f);
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

void HomeWorldMapOpen(Vector3 playerPosition)
{
    homeMap.open = true;
    homeMap.cacheDirty = true;
    homeMap.dragging = false;
    homeMap.dragOffset = Vector2Zero();
    homeMap.zoomLevel = 1;
    homeMap.bounds = (HomeWorldMapBounds){
        floorf(playerPosition.x), floorf(playerPosition.z),
        HomeWorldMapSpanForLevel(homeMap.zoomLevel)
    };
    homeMap.playerPosition = playerPosition;
    homeMap.showTerrain = true;
    homeMap.showEcology = true;
    homeMap.showCreatures = true;
    homeMap.showLandmarks = true;
}

void HomeWorldMapClose(void)
{
    homeMap.open = false;
    homeMap.dragging = false;
    homeMap.dragOffset = Vector2Zero();
}

bool HomeWorldMapIsOpen(void)
{
    return homeMap.open;
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
        float width = fminf(130.0f, layout->sidebar.width * 0.24f);
        return (Rectangle){
            layout->sidebar.x + (float)index * width,
            layout->sidebar.y + 24.0f, width, 24.0f
        };
    }
    return (Rectangle){
        layout->sidebar.x, layout->sidebar.y + 34.0f + (float)index * 28.0f,
        layout->sidebar.width, 24.0f
    };
}

static bool MapPointInButton(Vector2 mouse, Rectangle button)
{
    return CheckCollisionPointRec(mouse, button);
}

void HomeWorldMapUpdate(Vector3 playerPosition, float playerYaw,
                        float daylight)
{
    if (!homeMap.open) return;
    homeMap.playerPosition = playerPosition;
    homeMap.playerYaw = playerYaw;
    homeMap.daylight = daylight;
    if (!homeMap.textureReady || homeMap.cacheDirty) MapRefresh(daylight);

    HomeWorldMapLayout layout = MapLayout();
    Vector2 mouse = GetMousePosition();
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_M) ||
        (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
         MapPointInButton(mouse, layout.closeButton))) {
        HomeWorldMapClose();
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
        }
        bool *layers[] = {
            &homeMap.showTerrain, &homeMap.showEcology,
            &homeMap.showCreatures, &homeMap.showLandmarks
        };
        for (int i = 0; i < 4; i++) {
            if (MapPointInButton(mouse, MapLayerButton(&layout, i))) {
                *layers[i] = !*layers[i];
                pointerHandled = true;
            }
        }
    }

    if (CheckCollisionPointRec(mouse, layout.map)) {
        float wheel = GetMouseWheelMove();
        if (wheel > 0.0f) MapZoom(-1);
        if (wheel < 0.0f) MapZoom(1);
        if (!pointerHandled && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            homeMap.dragging = true;
        }
    }
    if (homeMap.dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 delta = GetMouseDelta();
        homeMap.dragOffset.x += delta.x;
        homeMap.dragOffset.y += delta.y;
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
    } else {
        DrawLineEx((Vector2){ cx - 5.0f, cy - 5.0f },
                   (Vector2){ cx + 5.0f, cy + 5.0f }, 2.0f, WHITE);
        DrawLineEx((Vector2){ cx + 5.0f, cy - 5.0f },
                   (Vector2){ cx - 5.0f, cy + 5.0f }, 2.0f, WHITE);
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
    if (!record.deployed || record.dimension != WORLD_DIMENSION_HOME ||
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

static void MapDrawSidebar(const HomeWorldMapLayout *layout,
                           const char *hoverTitle, const char *hoverDetail)
{
    UiDrawText("LAYERS", (int)layout->sidebar.x,
               (int)layout->sidebar.y, 14, Fade(WHITE, 0.58f));
    const char *labels[] = { "Terrain", "Ecology", "Creatures", "Landmarks" };
    bool enabled[] = {
        homeMap.showTerrain, homeMap.showEcology,
        homeMap.showCreatures, homeMap.showLandmarks
    };
    for (int i = 0; i < 4; i++) {
        MapDrawLayerToggle(MapLayerButton(layout, i), enabled[i], labels[i]);
    }

    if (layout->compact) {
        int infoY = (int)layout->sidebar.y + 58;
        UiDrawText(hoverTitle, (int)layout->sidebar.x, infoY, 16, WHITE);
        UiDrawText(hoverDetail, (int)layout->sidebar.x, infoY + 22, 14,
                   Fade(WHITE, 0.66f));
        return;
    }

    float legendY = layout->sidebar.y + 164.0f;
    UiDrawText("TERRAIN", (int)layout->sidebar.x, (int)legendY, 14,
               Fade(WHITE, 0.58f));
    const Color terrainColors[] = {
        { 89, 143, 76, 255 }, { 44, 102, 60, 255 },
        { 184, 156, 88, 255 }, { 202, 216, 218, 255 },
        { 42, 112, 153, 255 }
    };
    const char *terrainNames[] = {
        "Plains", "Forest", "Desert", "Snow / highland", "Water"
    };
    for (int i = 0; i < 5; i++) {
        DrawRectangle((int)layout->sidebar.x,
                      (int)legendY + 26 + i * 22, 14, 14, terrainColors[i]);
        UiDrawText(terrainNames[i], (int)layout->sidebar.x + 22,
                   (int)legendY + 24 + i * 22, 14, Fade(WHITE, 0.76f));
    }

    float markerY = legendY + 150.0f;
    UiDrawText("MARKERS", (int)layout->sidebar.x, (int)markerY, 14,
               Fade(WHITE, 0.58f));
    const char *markerNames[] = {
        "Land", "Aquatic", "Aerial", "Hostile"
    };
    for (int i = 0; i < 4; i++) {
        DrawCircle((int)layout->sidebar.x + 7,
                   (int)markerY + 30 + i * 22, 4.0f,
                   MapEntityColor((EntityMapMarkerKind)i));
        UiDrawText(markerNames[i], (int)layout->sidebar.x + 22,
                   (int)markerY + 22 + i * 22, 14, Fade(WHITE, 0.76f));
    }

    float infoY = layout->sidebar.y + layout->sidebar.height - 72.0f;
    DrawLine((int)layout->sidebar.x, (int)infoY - 12,
             (int)(layout->sidebar.x + layout->sidebar.width),
             (int)infoY - 12, Fade(WHITE, 0.16f));
    UiDrawText(hoverTitle, (int)layout->sidebar.x, (int)infoY, 17, WHITE);
    UiDrawText(hoverDetail, (int)layout->sidebar.x, (int)infoY + 25, 14,
               Fade(WHITE, 0.66f));
}

void HomeWorldMapDraw(void)
{
    if (!homeMap.open) return;
    HomeWorldMapLayout layout = MapLayout();
    HomeWorldMapBounds viewBounds = MapViewBounds(&layout);
    Vector2 mouse = GetMousePosition();
    const char *hoverTitle = "Homeworld survey";
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
    UiDrawText("Homeworld Map", (int)layout.panel.x + 22,
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
        snprintf(hoverDetail, sizeof(hoverDetail), "Return to Homeworld");
    }

    if (CheckCollisionPointRec(mouse, layout.map)) {
        Vector2 world = HomeWorldMapScreenToWorld(
            viewBounds, layout.map, mouse);
        SurfaceTerrainSample sample = SurfaceTerrainAt(
            (int)floorf(world.x), (int)floorf(world.y), WorldTerrainMode());
        float u = (world.x - (homeMap.bounds.centerX - homeMap.bounds.span * 0.5f)) /
                  homeMap.bounds.span;
        float v = (world.y - (homeMap.bounds.centerZ - homeMap.bounds.span * 0.5f)) /
                  homeMap.bounds.span;
        float fauna = HomeWorldMapHeatSample(homeMap.heat, u, v);
        if (strcmp(hoverTitle, "Homeworld survey") == 0) {
            hoverTitle = HomeWorldMapBiomeName(
                sample.biome, sample.bathymetry.waterDepth > 0);
            if (sample.bathymetry.waterDepth > 0) {
                snprintf(
                    hoverDetail, sizeof(hoverDetail),
                    "XZ %.0f,%.0f | fauna %.0f%%\nY %d | %d m | %s",
                    world.x, world.y, fauna * 100.0f,
                    sample.bathymetry.seabedY, sample.bathymetry.waterDepth,
                    BathymetryZoneName(sample.bathymetry.zone));
            } else {
                snprintf(
                    hoverDetail, sizeof(hoverDetail),
                    "XZ %.0f, %.0f   |   elevation %.0f   |   fauna %.0f%%",
                    world.x, world.y, sample.elevation, fauna * 100.0f);
            }
        }
    }
    MapDrawSidebar(&layout, hoverTitle, hoverDetail);
}

void HomeWorldMapUnload(void)
{
    if (homeMap.textureReady) {
        UnloadTexture(homeMap.terrainTexture);
    }
    memset(&homeMap, 0, sizeof(homeMap));
}
