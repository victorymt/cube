#include "presentation/surface_globe.h"

#include "presentation/planet_renderer.h"
#include "presentation/render.h"
#include "raymath.h"

#include <math.h>

#define SURFACE_GLOBE_TEXTURE_SIZE 512

typedef struct SurfaceGlobeResources {
    bool initialized;
    RenderTexture2D target;
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

    Vector3 cameraDirection = SurfaceGlobeDirection(
        params->cameraLongitude, params->cameraLatitude);
    Camera3D camera = {
        .position = Vector3Scale(cameraDirection, 3.35f),
        .target = Vector3Zero(),
        .up = { 0.0f, 1.0f, 0.0f },
        .fovy = 34.0f,
        .projection = CAMERA_PERSPECTIVE
    };
    if (fabsf(Vector3DotProduct(cameraDirection, camera.up)) > 0.96f) {
        camera.up = (Vector3){ 0.0f, 0.0f, 1.0f };
    }

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

    if (params->markers && params->markerCount > 0) {
        for (int i = 0; i < params->markerCount; i++) {
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

    Rectangle source = {
        0.0f, 0.0f, (float)globe.target.texture.width,
        -(float)globe.target.texture.height
    };
    DrawTexturePro(globe.target.texture, source, params->destination,
                   Vector2Zero(), 0.0f, WHITE);
    return true;
}

void SurfaceGlobeUnload(void)
{
    if (globe.target.id != 0) UnloadRenderTexture(globe.target);
    globe = (SurfaceGlobeResources){ 0 };
}
