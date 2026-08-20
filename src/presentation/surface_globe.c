#include "presentation/surface_globe.h"

#include "presentation/planet_renderer.h"
#include "presentation/render.h"
#include "raymath.h"

#include <math.h>

#define SURFACE_GLOBE_TEXTURE_SIZE 512
#define SURFACE_GLOBE_MAX_MARKERS 64
#define SURFACE_GLOBE_FOVY 34.0f

typedef struct SurfaceGlobeResources {
    bool initialized;
    RenderTexture2D target;
    bool cacheValid;
    bool cachePlanetSurface;
    float cacheCameraLongitude;
    float cacheCameraLatitude;
    float cacheMarkerLongitude;
    float cacheMarkerLatitude;
    int cacheMarkerCount;
    SurfaceGlobeMarker cacheMarkers[SURFACE_GLOBE_MAX_MARKERS];
} SurfaceGlobeResources;

static SurfaceGlobeResources globe = { 0 };

static Vector3 SurfaceGlobeDirection(float longitude, float latitude)
{
    float cosLatitude = cosf(latitude);
    return Vector3Normalize((Vector3){
        cosLatitude * cosf(longitude), sinf(latitude),
        cosLatitude * sinf(longitude)
    });
}

static void SurfaceGlobeCameraBasis(float longitude, float latitude,
                                    Vector3 *outPosition,
                                    Vector3 *outForward, Vector3 *outUp,
                                    Vector3 *outRight)
{
    Vector3 direction = SurfaceGlobeDirection(longitude, latitude);
    Vector3 position = Vector3Scale(direction, 3.35f);
    Vector3 forward = Vector3Normalize(Vector3Negate(direction));
    Vector3 up = Vector3Subtract(
        (Vector3){ 0.0f, 1.0f, 0.0f },
        Vector3Scale(forward, Vector3DotProduct(
            (Vector3){ 0.0f, 1.0f, 0.0f }, forward)));
    if (Vector3LengthSqr(up) < 0.0025f) {
        up = Vector3Subtract(
            (Vector3){ 0.0f, 0.0f, 1.0f },
            Vector3Scale(forward, Vector3DotProduct(
                (Vector3){ 0.0f, 0.0f, 1.0f }, forward)));
    }
    up = Vector3Normalize(up);
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, up));
    up = Vector3Normalize(Vector3CrossProduct(right, forward));
    if (outPosition) *outPosition = position;
    if (outForward) *outForward = forward;
    if (outUp) *outUp = up;
    if (outRight) *outRight = right;
}

static bool SurfaceGlobeMarkersEqual(const SurfaceGlobeMarker *a,
                                     const SurfaceGlobeMarker *b)
{
    return a->longitude == b->longitude && a->latitude == b->latitude &&
           a->color.r == b->color.r && a->color.g == b->color.g &&
           a->color.b == b->color.b && a->color.a == b->color.a &&
           a->selected == b->selected;
}

static bool SurfaceGlobeCacheMatches(const SurfaceGlobeDrawParams *params)
{
    if (!globe.cacheValid || globe.cachePlanetSurface != params->planetSurface ||
        globe.cacheCameraLongitude != params->cameraLongitude ||
        globe.cacheCameraLatitude != params->cameraLatitude ||
        globe.cacheMarkerLongitude != params->markerLongitude ||
        globe.cacheMarkerLatitude != params->markerLatitude) {
        return false;
    }
    int markerCount = params->markerCount < 0 ? 0 : params->markerCount;
    if (markerCount > SURFACE_GLOBE_MAX_MARKERS) {
        markerCount = SURFACE_GLOBE_MAX_MARKERS;
    }
    if (markerCount > 0 && !params->markers) return false;
    if (markerCount != globe.cacheMarkerCount) return false;
    for (int i = 0; i < markerCount; i++) {
        if (!SurfaceGlobeMarkersEqual(&globe.cacheMarkers[i],
                                      &params->markers[i])) {
            return false;
        }
    }
    return true;
}

static void SurfaceGlobeCacheParams(const SurfaceGlobeDrawParams *params)
{
    globe.cacheValid = true;
    globe.cachePlanetSurface = params->planetSurface;
    globe.cacheCameraLongitude = params->cameraLongitude;
    globe.cacheCameraLatitude = params->cameraLatitude;
    globe.cacheMarkerLongitude = params->markerLongitude;
    globe.cacheMarkerLatitude = params->markerLatitude;
    int markerCount = params->markerCount < 0 ? 0 : params->markerCount;
    if (markerCount > SURFACE_GLOBE_MAX_MARKERS) {
        markerCount = SURFACE_GLOBE_MAX_MARKERS;
    }
    globe.cacheMarkerCount = markerCount;
    if (markerCount > 0 && !params->markers) {
        globe.cacheMarkerCount = 0;
        return;
    }
    for (int i = 0; i < markerCount; i++) {
        globe.cacheMarkers[i] = params->markers[i];
    }
}

static bool SurfaceGlobeEnsureTarget(void)
{
    if (globe.initialized) {
        return globe.target.id != 0 && globe.target.texture.id != 0;
    }
    globe.initialized = true;
    globe.target = LoadRenderTexture(SURFACE_GLOBE_TEXTURE_SIZE,
                                     SURFACE_GLOBE_TEXTURE_SIZE);
    if (globe.target.texture.id != 0) {
        SetTextureFilter(globe.target.texture, TEXTURE_FILTER_BILINEAR);
    }
    return globe.target.id != 0 && globe.target.texture.id != 0;
}

bool SurfaceGlobeDraw(const SurfaceGlobeDrawParams *params)
{
    if (!params || params->destination.width <= 0.0f ||
        params->destination.height <= 0.0f || !SurfaceGlobeEnsureTarget()) {
        return false;
    }

    if (SurfaceGlobeCacheMatches(params)) {
        Rectangle source = {
            0.0f, 0.0f, (float)globe.target.texture.width,
            -(float)globe.target.texture.height
        };
        DrawTexturePro(globe.target.texture, source, params->destination,
                       Vector2Zero(), 0.0f, WHITE);
        return true;
    }

    Vector3 cameraPosition = { 0 };
    Vector3 cameraForward = { 0 };
    Vector3 cameraUp = { 0 };
    SurfaceGlobeCameraBasis(params->cameraLongitude,
                            params->cameraLatitude, &cameraPosition,
                            &cameraForward, &cameraUp, NULL);
    Camera3D camera = {
        .position = cameraPosition,
        .target = Vector3Zero(),
        .up = cameraUp,
        .fovy = SURFACE_GLOBE_FOVY,
        .projection = CAMERA_PERSPECTIVE
    };

    PlanetTextureSet textures = { 0 };
    Color fallback = (Color){ 80, 126, 112, 255 };
    bool textured = PlanetRenderSurfaceVisual(params->planetSurface,
                                               &textures, &fallback);
    PlanetSpaceLighting lighting = {
        .count = 1,
        .positions = { camera.position },
        .colors = { { 1.0f, 0.97f, 0.91f } },
        .intensities = { 1.35f },
        .exposure = 1.04f
    };
    PlanetMaterialResponse material = {
        .roughness = 0.82f,
        .specular = 0.18f,
        .metallic = 0.0f,
        .model = 0
    };

    BeginTextureMode(globe.target);
    ClearBackground(BLANK);
    BeginMode3D(camera);
    if (textured) {
        PlanetRendererDrawSurface(&(PlanetSurfaceDrawParams){
            .center = { 0.0f, 0.0f, 0.0f },
            .radius = 1.0f,
            .textures = textures,
            .fallback = fallback,
            .cameraPosition = camera.position,
            .lighting = &lighting,
            .material = &material,
            .ambientLight = 0.16f,
            .sceneExposure = 1.04f
        });
    } else {
        DrawSphere(Vector3Zero(), 1.0f, fallback);
    }

    const Color gridColor = (Color){ 210, 228, 220, 46 };
    const int gridSegments = 24;
    for (int latitudeIndex = -2; latitudeIndex <= 2; latitudeIndex++) {
        float latitude = (float)latitudeIndex * PI / 6.0f;
        for (int segment = 0; segment < gridSegments; segment++) {
            float lonA = -PI + (float)segment * 2.0f * PI /
                         (float)gridSegments;
            float lonB = -PI + (float)(segment + 1) * 2.0f * PI /
                         (float)gridSegments;
            DrawLine3D(Vector3Scale(SurfaceGlobeDirection(lonA, latitude), 1.006f),
                       Vector3Scale(SurfaceGlobeDirection(lonB, latitude), 1.006f),
                       gridColor);
        }
    }
    for (int longitudeIndex = 0; longitudeIndex < 8; longitudeIndex++) {
        float longitude = -PI + (float)longitudeIndex * PI / 4.0f;
        for (int segment = 0; segment < gridSegments; segment++) {
            float latA = -PI * 0.5f + (float)segment * PI /
                         (float)gridSegments;
            float latB = -PI * 0.5f + (float)(segment + 1) * PI /
                         (float)gridSegments;
            DrawLine3D(Vector3Scale(SurfaceGlobeDirection(longitude, latA), 1.006f),
                       Vector3Scale(SurfaceGlobeDirection(longitude, latB), 1.006f),
                       gridColor);
        }
    }

    int markerCount = params->markerCount < 0 ? 0 : params->markerCount;
    if (markerCount > SURFACE_GLOBE_MAX_MARKERS) {
        markerCount = SURFACE_GLOBE_MAX_MARKERS;
    }
    if (params->markers && markerCount > 0) {
        for (int i = 0; i < markerCount; i++) {
            const SurfaceGlobeMarker *marker = &params->markers[i];
            if (!isfinite(marker->longitude) || !isfinite(marker->latitude)) {
                continue;
            }
            Vector3 direction = SurfaceGlobeDirection(
                marker->longitude, marker->latitude);
            float baseRadius = marker->selected ? 0.047f : 0.032f;
            if (marker->selected) {
                DrawSphereEx(Vector3Scale(direction, 1.052f), 0.060f,
                             10, 10, Fade(WHITE, 0.92f));
            }
            DrawSphereEx(Vector3Scale(direction, 1.075f), baseRadius,
                         10, 10, marker->color);
        }
    }

    Vector3 markerDirection = SurfaceGlobeDirection(
        params->markerLongitude, params->markerLatitude);
    DrawSphereEx(Vector3Scale(markerDirection, 1.045f), 0.058f, 10, 10,
                 Fade(WHITE, 0.96f));
    DrawSphereEx(Vector3Scale(markerDirection, 1.084f), 0.034f, 10, 10,
                 (Color){ 240, 72, 61, 255 });
    EndMode3D();
    EndTextureMode();
    SurfaceGlobeCacheParams(params);

    Rectangle source = {
        0.0f, 0.0f, (float)globe.target.texture.width,
        -(float)globe.target.texture.height
    };
    DrawTexturePro(globe.target.texture, source, params->destination,
                   Vector2Zero(), 0.0f, WHITE);
    return true;
}

bool SurfaceGlobeHitTest(Rectangle destination, float cameraLongitude,
                         float cameraLatitude, Vector2 screen,
                         float *outLongitude, float *outLatitude)
{
    if (destination.width <= 0.0f || destination.height <= 0.0f ||
        !CheckCollisionPointRec(screen, destination)) return false;
    float nx = (screen.x - (destination.x + destination.width * 0.5f)) /
               (destination.width * 0.5f);
    float ny = (screen.y - (destination.y + destination.height * 0.5f)) /
               (destination.height * 0.5f);
    if (nx * nx + ny * ny > 1.0f) return false;

    Vector3 position = { 0 };
    Vector3 forward = { 0 };
    Vector3 up = { 0 };
    Vector3 right = { 0 };
    SurfaceGlobeCameraBasis(cameraLongitude, cameraLatitude, &position,
                            &forward, &up, &right);
    float aspect = destination.width / destination.height;
    float scale = tanf(SURFACE_GLOBE_FOVY * DEG2RAD * 0.5f);
    Vector3 ray = Vector3Add(
        Vector3Add(Vector3Scale(right, nx * aspect * scale),
                   Vector3Scale(up, -ny * scale)),
        forward);
    ray = Vector3Normalize(ray);
    float b = Vector3DotProduct(position, ray);
    float discriminant = b * b - (Vector3LengthSqr(position) - 1.0f);
    if (discriminant < 0.0f) return false;
    float distance = -b - sqrtf(discriminant);
    if (distance <= 0.0f) return false;
    Vector3 point = Vector3Add(position, Vector3Scale(ray, distance));
    point = Vector3Normalize(point);
    if (outLongitude) *outLongitude = atan2f(point.z, point.x);
    if (outLatitude) *outLatitude = asinf(fmaxf(-1.0f, fminf(1.0f, point.y)));
    return true;
}

void SurfaceGlobeInvalidate(void)
{
    globe.cacheValid = false;
}

void SurfaceGlobeUnload(void)
{
    if (globe.target.id != 0) UnloadRenderTexture(globe.target);
    globe = (SurfaceGlobeResources){ 0 };
}
