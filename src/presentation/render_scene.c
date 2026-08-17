#include "presentation/render.h"
#include "presentation/render_dependencies.h"
#include "presentation/render_internal.h"

static TransparentRenderItem *surfaceTransparentItems;
static int surfaceTransparentCount;
static int surfaceTransparentCapacity;
static int worldFrameCameraCx;
static int worldFrameCameraCz;
static int worldFrameShadowChunkRadius;
static int worldFrameEffectiveRenderDistance;
static bool worldFrameDrawSurfaceChunks;

#define MAX_OTHER_TRANSPARENT_ITEMS (MAX_SPACE_CHUNKS + MAX_NETHER_CHUNKS)

static bool EnsureSurfaceTransparentCapacity(int required)
{
    if (required <= surfaceTransparentCapacity) return true;
    int capacity = surfaceTransparentCapacity > 0
        ? surfaceTransparentCapacity : 256;
    while (capacity < required) {
        if (capacity > INT_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    if ((size_t)capacity > SIZE_MAX / sizeof(*surfaceTransparentItems)) {
        return false;
    }
    TransparentRenderItem *items = realloc(
        surfaceTransparentItems, (size_t)capacity * sizeof(*items));
    if (!items) return false;
    surfaceTransparentItems = items;
    surfaceTransparentCapacity = capacity;
    return true;
}

static Vector3 ChunkRenderCenter(int cx, int cz, float centerY)
{
    return (Vector3){
        (float)(cx * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f,
        centerY,
        (float)(cz * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f
    };
}

void WorldRenderFramePrepare(const Camera3D *camera,
                             int effectiveRenderDistance,
                             bool drawSurfaceChunks)
{
    surfaceTransparentCount = 0;
    worldFrameCameraCx = 0;
    worldFrameCameraCz = 0;
    worldFrameEffectiveRenderDistance = effectiveRenderDistance;
    worldFrameDrawSurfaceChunks = drawSurfaceChunks;
    int localX = 0;
    int localZ = 0;
    WorldToChunkLocal((int)floorf(camera->position.x),
                      (int)floorf(camera->position.z),
                      &worldFrameCameraCx, &worldFrameCameraCz,
                      &localX, &localZ);
    worldFrameShadowChunkRadius = WorldRendererShadowChunkRadius();
    if (worldFrameShadowChunkRadius > effectiveRenderDistance) {
        worldFrameShadowChunkRadius = effectiveRenderDistance;
    }
}

static void CollectSurfaceRenderItems(const Camera3D *camera, Color tint)
{
    if (!worldFrameDrawSurfaceChunks) return;

    const Chunk *surfaceChunks = ChunksView();
    float referenceX = camera->position.x;
    float referenceZ = camera->position.z;
    int mapOriginX = WorldSurfaceMapOriginX();
    int mapOriginZ = WorldSurfaceMapOriginZ();
    Vector2 referenceMap = {
        (float)mapOriginX + referenceX,
        (float)mapOriginZ + referenceZ
    };
    Vector3 referenceOrigin = {
        referenceX, 0.0f, referenceZ
    };
    float half = (float)SURFACE_SECTION_HEIGHT * 0.5f;
    float radius = sqrtf(half * half * 3.0f);
    int stableOrder = 0;
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        const Chunk *chunk = &surfaceChunks[i];
        if (!chunk->loaded) continue;
        bool distanceVisible =
            abs(chunk->cx - worldFrameCameraCx) <=
                worldFrameEffectiveRenderDistance &&
            abs(chunk->cz - worldFrameCameraCz) <=
                worldFrameEffectiveRenderDistance;
        if (!distanceVisible) continue;
        Vector2 patchMap = {
            (float)(mapOriginX + chunk->cx * CHUNK_SIZE),
            (float)(mapOriginZ + chunk->cz * CHUNK_SIZE)
        };
        for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
             sectionIndex++) {
            const ChunkSection *section = chunk->sections[sectionIndex];
            int sectionY = section->sectionY;
            int radialBase = sectionY * SURFACE_SECTION_HEIGHT;
            Matrix transform = chunk->spherical
                ? SurfacePatchTransformAtMap(
                    chunk->surfaceAddress.bodyId, referenceMap,
                    referenceOrigin, patchMap, radialBase)
                : MatrixTranslate((float)(chunk->cx * CHUNK_SIZE),
                                  (float)radialBase,
                                  (float)(chunk->cz * CHUNK_SIZE));
            Vector3 center = Vector3Transform(
                (Vector3){ (float)CHUNK_SIZE * 0.5f,
                           (float)SURFACE_SECTION_HEIGHT * 0.5f,
                           (float)CHUNK_SIZE * 0.5f }, transform);
            bool frustumVisible = SphereInFrustum(camera, center, radius);
            PerfRecordWorldCandidate(true, frustumVisible);
            if (frustumVisible && section->hasWaterModel &&
                EnsureSurfaceTransparentCapacity(surfaceTransparentCount + 1)) {
                TransparentRenderItemAppendTransformed(
                    surfaceTransparentItems, surfaceTransparentCapacity,
                    &surfaceTransparentCount,
                    &section->waterModel, transform, center,
                    camera->position, TRANSPARENT_RENDER_SURFACE,
                    chunk->cx, chunk->cz, stableOrder);
            }
            stableOrder++;
            if (!frustumVisible) continue;
            if (section->hasModel) {
                PerfRecordDrawCall(PERF_DRAW_SOLID);
                WorldRendererDrawModelTransformed(
                    &section->model, transform, tint, false);
            }
            if (section->hasFloraModel) {
                PerfRecordDrawCall(PERF_DRAW_FLORA);
                WorldRendererDrawModelTransformed(
                    &section->floraModel, transform, tint, false);
            }
        }
    }
}

void WorldRenderFrameShutdown(void)
{
    free(surfaceTransparentItems);
    surfaceTransparentItems = NULL;
    surfaceTransparentCount = 0;
    surfaceTransparentCapacity = 0;
}

static void CollectSpaceRenderItems(
    const Camera3D *camera, int cameraCx, int cameraCz, Color tint,
    TransparentRenderItem *transparent, int *transparentCount)
{
    const SpaceChunk *spaceChunks = SpaceChunksView();
    Vector3 translation = { 0.0f, (float)SPACE_LAYER_Y, 0.0f };
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        const SpaceChunk *chunk = &spaceChunks[i];
        if (!chunk->loaded) continue;
        bool distanceVisible = abs(chunk->cx - cameraCx) <= SPACE_RENDER_DISTANCE_CHUNKS &&
                               abs(chunk->cz - cameraCz) <= SPACE_RENDER_DISTANCE_CHUNKS;
        Vector3 center = ChunkRenderCenter(
            chunk->cx, chunk->cz,
            (float)SPACE_LAYER_Y + (float)SPACE_LAYER_HEIGHT * 0.5f);
        bool frustumVisible = distanceVisible && SphereInFrustum(camera, center, 66.0f);
        PerfRecordWorldCandidate(distanceVisible, frustumVisible);
        if (!distanceVisible || !frustumVisible) continue;
        if (chunk->hasModel) {
            PerfRecordDrawCall(PERF_DRAW_SPACE);
            WorldRendererDrawModel(&chunk->model, translation, tint, false);
        }
        if (chunk->hasWaterModel) {
            TransparentRenderItemAppend(
                transparent, MAX_OTHER_TRANSPARENT_ITEMS, transparentCount,
                &chunk->waterModel, translation, center, camera->position,
                TRANSPARENT_RENDER_SPACE, chunk->cx, chunk->cz, i);
        }
    }
}

static void CollectNetherRenderItems(
    const Camera3D *camera, int cameraCx, int cameraCz, Color tint,
    TransparentRenderItem *transparent, int *transparentCount)
{
    const NetherChunk *netherChunks = NetherChunksView();
    Vector3 translation = { 0.0f, (float)NETHER_LAYER_Y, 0.0f };
    for (int i = 0; i < MAX_NETHER_CHUNKS; i++) {
        const NetherChunk *chunk = &netherChunks[i];
        if (!chunk->loaded) continue;
        bool distanceVisible = abs(chunk->cx - cameraCx) <= NETHER_RENDER_DISTANCE_CHUNKS &&
                               abs(chunk->cz - cameraCz) <= NETHER_RENDER_DISTANCE_CHUNKS;
        Vector3 center = ChunkRenderCenter(
            chunk->cx, chunk->cz, (float)NETHER_LAYER_Y + 16.0f);
        bool frustumVisible = distanceVisible && SphereInFrustum(camera, center, 34.0f);
        PerfRecordWorldCandidate(distanceVisible, frustumVisible);
        if (!distanceVisible || !frustumVisible) continue;
        if (chunk->hasModel) {
            PerfRecordDrawCall(PERF_DRAW_NETHER);
            WorldRendererDrawModel(&chunk->model, translation, tint, false);
        }
        if (chunk->hasWaterModel) {
            TransparentRenderItemAppend(
                transparent, MAX_OTHER_TRANSPARENT_ITEMS, transparentCount,
                &chunk->waterModel, translation, center, camera->position,
                TRANSPARENT_RENDER_NETHER, chunk->cx, chunk->cz, i);
        }
    }
}

void DrawWorld(const Camera3D *camera, Color tint, bool drawNetherChunks,
               const WorldLightingState *lighting)
{
    if (lighting) WorldRendererPrepare(lighting);
    TransparentRenderItem transparent[MAX_OTHER_TRANSPARENT_ITEMS];
    int transparentCount = 0;
    CollectSurfaceRenderItems(camera, tint);

    CollectSpaceRenderItems(camera, worldFrameCameraCx, worldFrameCameraCz, tint,
                            transparent, &transparentCount);
    if (drawNetherChunks) {
        CollectNetherRenderItems(camera, worldFrameCameraCx, worldFrameCameraCz,
                                 tint,
                                 transparent, &transparentCount);
    }

    TransparentRenderItem *water = surfaceTransparentItems;
    int waterCount = surfaceTransparentCount;
    SortTransparentRenderItems(water, waterCount);
    BeginBlendMode(BLEND_ALPHA);
    WorldRendererBeginWaterPass();
    for (int i = 0; i < waterCount; i++) {
        PerfRecordDrawCall(PERF_DRAW_WATER);
        if (water[i].transformed) {
            WorldRendererDrawModelTransformed(
                water[i].model, water[i].transform, tint, true);
        } else {
            WorldRendererDrawModel(water[i].model, water[i].translation,
                                   tint, true);
        }
    }
    WorldRendererEndWaterPass();
    EndBlendMode();

    SortTransparentRenderItems(transparent, transparentCount);
    BeginBlendMode(BLEND_ALPHA);
    for (int i = 0; i < transparentCount; i++) {
        PerfDrawKind kind = transparent[i].dimension == TRANSPARENT_RENDER_SPACE
            ? PERF_DRAW_SPACE : PERF_DRAW_NETHER;
        PerfRecordDrawCall(kind);
        WorldRendererDrawModel(transparent[i].model, transparent[i].translation,
                               tint, true);
    }
    EndBlendMode();
}

void DrawWorldShadowMap(const Camera3D *camera, bool drawNetherChunks,
                        const WorldLightingState *lighting)
{
    if (!WorldRendererBeginShadow(camera, lighting)) return;
    if (worldFrameDrawSurfaceChunks) {
        const Chunk *surfaceChunks = ChunksView();
        float referenceX = camera->position.x;
        float referenceZ = camera->position.z;
        int mapOriginX = WorldSurfaceMapOriginX();
        int mapOriginZ = WorldSurfaceMapOriginZ();
        Vector2 referenceMap = {
            (float)mapOriginX + referenceX,
            (float)mapOriginZ + referenceZ
        };
        Vector3 referenceOrigin = { referenceX, 0.0f, referenceZ };
        float half = (float)SURFACE_SECTION_HEIGHT * 0.5f;
        float radius = sqrtf(half * half * 3.0f);
        for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
            const Chunk *chunk = &surfaceChunks[i];
            if (!chunk->loaded ||
                abs(chunk->cx - worldFrameCameraCx) >
                    worldFrameShadowChunkRadius ||
                abs(chunk->cz - worldFrameCameraCz) >
                    worldFrameShadowChunkRadius) {
                continue;
            }
            Vector2 patchMap = {
                (float)(mapOriginX + chunk->cx * CHUNK_SIZE),
                (float)(mapOriginZ + chunk->cz * CHUNK_SIZE)
            };
            for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
                 sectionIndex++) {
                const ChunkSection *section = chunk->sections[sectionIndex];
                int radialBase = section->sectionY * SURFACE_SECTION_HEIGHT;
                Matrix transform = chunk->spherical
                    ? SurfacePatchTransformAtMap(
                          chunk->surfaceAddress.bodyId, referenceMap,
                          referenceOrigin, patchMap, radialBase)
                    : MatrixTranslate((float)(chunk->cx * CHUNK_SIZE),
                                      (float)radialBase,
                                      (float)(chunk->cz * CHUNK_SIZE));
                Vector3 center = Vector3Transform(
                    (Vector3){ (float)CHUNK_SIZE * 0.5f,
                               (float)SURFACE_SECTION_HEIGHT * 0.5f,
                               (float)CHUNK_SIZE * 0.5f }, transform);
                if (!WorldRendererShadowSphereVisible(center, radius)) continue;
                if (section->hasModel) {
                    WorldRendererDrawShadowModelTransformed(
                        &section->model, transform);
                }
                if (section->hasFloraModel) {
                    WorldRendererDrawShadowModelTransformed(
                        &section->floraModel, transform);
                }
            }
        }
    }
    const NetherChunk *netherChunks = NetherChunksView();
    if (drawNetherChunks) {
        Vector3 translation = { 0.0f, (float)NETHER_LAYER_Y, 0.0f };
        for (int i = 0; i < MAX_NETHER_CHUNKS; i++) {
            const NetherChunk *chunk = &netherChunks[i];
            if (!chunk->loaded || !chunk->hasModel ||
                abs(chunk->cx - worldFrameCameraCx) > worldFrameShadowChunkRadius ||
                abs(chunk->cz - worldFrameCameraCz) > worldFrameShadowChunkRadius) {
                continue;
            }
            WorldRendererDrawShadowModel(&chunk->model, translation);
        }
    }
    WorldRendererEndShadow();
}

Color SolarStyleColor(SolarBodyStyle style)
{
    switch (style) {
    case SOLAR_STYLE_LAVA:   return (Color){ 235, 120, 70, 255 };
    case SOLAR_STYLE_ICE:    return (Color){ 170, 210, 240, 255 };
    case SOLAR_STYLE_DESERT: return (Color){ 226, 196, 132, 255 };
    case SOLAR_STYLE_GAS:    return (Color){ 190, 170, 230, 255 };
    case SOLAR_STYLE_CRATER: return (Color){ 150, 152, 158, 255 };
    case SOLAR_STYLE_TEMPERATE: return (Color){ 74, 152, 104, 255 };
    default:                 return (Color){ 200, 200, 200, 255 };
    }
}

static void FormatCelestialDistance(char *out, size_t outSize,
                                    float gameDistance)
{
    if (!out || outSize == 0) return;
    double kilometers = SpaceUnitsGameDistanceToKilometers(gameDistance);
    double au = kilometers / SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
    if (au >= 0.1) {
        snprintf(out, outSize, "%.3g AU", au);
    } else {
        snprintf(out, outSize, "%.3g km", kilometers);
    }
}

static void DrawCelestialLabel(Vector2 center, float radiusPixels,
                               const char *text, int fontSize, Color color)
{
    if (!text) return;
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    int textWidth = UiMeasureText(text, fontSize);
    int clearance = (int)ceilf(fmaxf(radiusPixels, 4.0f)) + 8;
    int textX = (int)center.x + clearance;
    if (textX + textWidth > screenWidth - 8) {
        textX = (int)center.x - clearance - textWidth;
    }
    int textY = (int)center.y - fontSize / 2;
    textX = (int)Clamp((float)textX, 8.0f,
                       fmaxf(8.0f, (float)screenWidth - textWidth - 8.0f));
    textY = (int)Clamp((float)textY, 8.0f,
                       fmaxf(8.0f, (float)screenHeight - 96.0f));
    UiDrawText(text, textX, textY, fontSize, color);
}

static void DrawEdgeIndicator(float px, float py, bool behind, Vector3 origin, Vector3 center,
                              Color color, float spaceFade, const char *label)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float cx = (float)sw * 0.5f;
    float cy = (float)sh * 0.5f;
    float dx = px - cx;
    float dy = py - cy;
    if (behind) {
        dx = -dx;
        dy = -dy;
    }
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) {
        dx = 0.0f;
        dy = -1.0f;
        len = 1.0f;
    }
    dx /= len;
    dy /= len;
    float margin = 30.0f;
    float tx = ((float)sw * 0.5f - margin) / fmaxf(fabsf(dx), 1e-5f);
    float ty = ((float)sh * 0.5f - margin) / fmaxf(fabsf(dy), 1e-5f);
    float t = fminf(tx, ty);
    float ex = cx + dx * t;
    float ey = cy + dy * t;

    DrawTriangle((Vector2){ ex - dy * 5.0f, ey + dx * 5.0f },
                 (Vector2){ ex + dy * 5.0f, ey - dx * 5.0f },
                 (Vector2){ ex + dx * 12.0f, ey + dy * 12.0f },
                 Fade(color, 0.9f * spaceFade));

    if (label) {
        float dist = Vector3Distance(origin, center);
        char distance[32];
        FormatCelestialDistance(distance, sizeof(distance), dist);
        const char *indicator = TextFormat("%s - %s", label, distance);
        int textWidth = UiMeasureText(indicator, 15);
        int textX = dx > 0.0f ? (int)ex - textWidth - 16 : (int)ex + 16;
        int textY = (int)ey - 10;
        textX = (int)Clamp((float)textX, 8.0f,
                           fmaxf(8.0f, (float)sw - textWidth - 8.0f));
        textY = (int)Clamp((float)textY, 8.0f,
                           fmaxf(8.0f, (float)sh - 96.0f));
        UiDrawText(indicator, textX, textY, 15,
                 Fade(WHITE, 0.9f * spaceFade));
    }
}

bool FindSystemForGuide(Vector3 pos, SolarSystemDef *sys, float *dist)
{
    static const float probes[3] = {
        STAR_SYSTEM_SPACING * 2.15f,
        STAR_SYSTEM_SPACING * 5.0f,
        STAR_SYSTEM_SPACING * 10.0f
    };
    for (int i = 0; i < 3; i++) {
        if (FindNearestSystem(pos, probes[i], sys, dist)) return true;
    }
    return false;
}

void DrawSolarOrbitTrajectories(const Camera3D *camera, float spaceFade)
{
    if (!camera || spaceFade <= 0.05f) return;

    SolarSystemDef system = { 0 };
    float systemDistance = 0.0f;
    if (!FindNearestSystem(camera->position, STAR_SYSTEM_SPACING * 1.9f,
                           &system, &systemDistance)) return;

    const int samples = 64;
    double now = SpacePeriodicSimulationTime(SpaceElapsedSimulationTime());
    for (int i = 0; i < system.planetCount; i++) {
        PlanetProfile profile = SolarPlanetProfile(&system, i);
        double period = SolarSystemPlanetOrbitPeriodGameTime(&system, i);
        if (period <= 0.0) continue;

        Color color = ColorLerp(SpectrumColor(system.spectrum),
                                SolarStyleColor(profile.style), 0.45f);
        color.a = (unsigned char)(Clamp(spaceFade, 0.0f, 1.0f) * 88.0f);

        Vector3 previous = SolarSystemPlanetPositionAtTime(&system, i, now);
        for (int sample = 1; sample <= samples; sample++) {
            double sampleTime = now + (double)period * (double)sample / (double)samples;
            Vector3 current = SolarSystemPlanetPositionAtTime(&system, i, sampleTime);
            DrawLine3D(previous, current, color);
            previous = current;
        }
    }
}

void DrawSolarGuide(const Camera3D *camera, float spaceFade)
{
    if (spaceFade <= 0.05f) return;

    SpaceBodyInfo bodies[48];
    int count = SpaceBodiesNear(camera->position, SOLAR_SYSTEM_QUERY_RADIUS,
                                bodies, 48);
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    for (int i = 0; i < count; i++) {
        if (!bodies[i].isStar && bodies[i].systemAnchorX == 0 &&
            bodies[i].systemAnchorZ == 0 && bodies[i].bodyId == 3u) {
            continue;
        }
        Vector3 toBody = Vector3Subtract(bodies[i].center, camera->position);
        bool behind = Vector3DotProduct(toBody, forward) < 0.0f;
        Vector2 screen = GetWorldToScreen(bodies[i].center, *camera);
        float px = screen.x;
        float py = screen.y;
        bool onScreen = !behind && px > -10.0f && px < (float)sw + 10.0f &&
                        py > -10.0f && py < (float)sh + 10.0f;

        if (bodies[i].isStar) {
            Color color = SpectrumColor(bodies[i].spectrum);
            if (onScreen) {
                float scale = Clamp(1400.0f / bodies[i].dist, 5.0f, 28.0f);
                DrawCircleGradient((int)px, (int)py, (int)(scale * 1.7f),
                                   Fade(color, 0.30f * spaceFade), BLANK);
                DrawCircle((int)px, (int)py, (int)scale,
                           Fade((Color){ 255, 244, 200, 255 }, spaceFade));
                DrawLine((int)px - (int)(scale * 2.0f), (int)py, (int)px + (int)(scale * 2.0f), (int)py,
                         Fade(color, 0.30f * spaceFade));
                DrawLine((int)px, (int)py - (int)(scale * 2.0f), (int)px, (int)py + (int)(scale * 2.0f),
                         Fade(color, 0.30f * spaceFade));
                if (bodies[i].dist < 350.0f) {
                    UiDrawText(bodies[i].name, (int)px + (int)scale + 6,
                             (int)py - 8, 15,
                             Fade(WHITE, 0.85f * spaceFade));
                }
            } else {
                DrawEdgeIndicator(px, py, behind, camera->position, bodies[i].center, color, spaceFade,
                                  bodies[i].name);
            }
            continue;
        }

        Color color = SolarStyleColor(bodies[i].style);
        if (onScreen) {
            float radiusPixels = CelestialRadiusPixels(
                camera, bodies[i].center, bodies[i].physicalRadiusGame);
            if (radiusPixels < 0.75f) {
                DrawCircle((int)px, (int)py, 4.0f,
                           Fade(color, spaceFade));
            }
            if (bodies[i].dist < 350.0f) {
                DrawCelestialLabel((Vector2){ px, py }, radiusPixels,
                                   bodies[i].name, 15,
                                   Fade(WHITE, 0.85f * spaceFade));
            }
        }
    }

    Vector3 homeCenter = HomeWorldCenter();
    float homeDist = Vector3Distance(camera->position, homeCenter);
    if ((!HomeWorldSurfaceIsActive() && !PlanetWorldIsActive()) ||
        homeDist > 90.0f) {
        Vector3 toHome = Vector3Subtract(homeCenter, camera->position);
        bool behind = Vector3DotProduct(toHome, forward) < 0.0f;
        Vector2 homeScreen = GetWorldToScreen(homeCenter, *camera);
        bool onScreen = !behind && homeScreen.x > -10.0f && homeScreen.x < (float)sw + 10.0f &&
                        homeScreen.y > -10.0f && homeScreen.y < (float)sh + 10.0f;
        Color homeColor = (Color){ 130, 202, 255, 255 };
        if (onScreen) {
            char distance[32];
            FormatCelestialDistance(distance, sizeof(distance), homeDist);
            float earthRadius = (float)SpaceUnitsKilometersToGameDistance(
                SPACE_UNITS_EARTH_RADIUS_KM);
            float radiusPixels = CelestialRadiusPixels(
                camera, homeCenter, earthRadius);
            if (radiusPixels < 0.75f) {
                DrawCircle((int)homeScreen.x, (int)homeScreen.y, 6.0f,
                           Fade(homeColor, 0.9f * spaceFade));
            }
            DrawCelestialLabel(homeScreen, radiusPixels,
                               TextFormat("Earth - %s", distance), 15,
                               Fade(WHITE, 0.9f * spaceFade));
        } else {
            DrawEdgeIndicator(homeScreen.x, homeScreen.y, behind, camera->position,
                              homeCenter, homeColor, spaceFade, "Earth");
        }
    }

    if (count == 0) {
        SolarSystemDef sys;
        float sysDist = 0.0f;
        if (FindSystemForGuide(camera->position, &sys, &sysDist)) {
            Vector3 toSys = Vector3Subtract(sys.center, camera->position);
            bool behind = Vector3DotProduct(toSys, forward) < 0.0f;
            Vector2 screen = GetWorldToScreen(sys.center, *camera);
            Color color = SpectrumColor(sys.spectrum);
            DrawEdgeIndicator(screen.x, screen.y, behind, camera->position, sys.center, color, spaceFade,
                              sys.name);
        }
    }
}
