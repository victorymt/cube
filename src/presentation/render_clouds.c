#include "presentation/render.h"
#include "presentation/render_dependencies.h"
#include "presentation/render_internal.h"

typedef struct CloudRenderResources {
    Shader shader;
    Texture2D noiseTexture;
    int cameraPositionLoc;
    int boxMinLoc;
    int boxMaxLoc;
    int windOffsetLoc;
    int coverageLoc;
    int opacityLoc;
    int stormLoc;
    int daylightLoc;
    int drawDistanceLoc;
    int sunDirectionLoc;
    int lightColorLoc;
    int lightStrengthLoc;
    int rayStepsLoc;
    int lightStepsLoc;
    bool ready;
} CloudRenderResources;

static CloudRenderResources cloudResources = { 0 };

void SetCloudModel(Model model)
{
    cloudModel = model;
}

static const char *cloudVertexShader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "out vec3 fragPosition;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 world = matModel*vec4(vertexPosition, 1.0);\n"
    "    fragPosition = world.xyz;\n"
    "    fragColor = vertexColor;\n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *cloudFragmentShader =
    "#version 330\n"
    "in vec3 fragPosition;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 cameraPosition;\n"
    "uniform vec3 cloudBoxMin;\n"
    "uniform vec3 cloudBoxMax;\n"
    "uniform vec2 windOffset;\n"
    "uniform float cloudCoverage;\n"
    "uniform float cloudOpacity;\n"
    "uniform float stormAmount;\n"
    "uniform float daylight;\n"
    "uniform float drawDistance;\n"
    "uniform vec3 sunDirection;\n"
    "uniform vec3 lightColor;\n"
    "uniform float lightStrength;\n"
    "uniform int raySteps;\n"
    "uniform int lightSteps;\n"
    "out vec4 finalColor;\n"
    "vec3 safeDirection(vec3 value) {\n"
    "    return vec3(value.x < 0.0 ? min(value.x, -0.0001) : max(value.x, 0.0001),\n"
    "                value.y < 0.0 ? min(value.y, -0.0001) : max(value.y, 0.0001),\n"
    "                value.z < 0.0 ? min(value.z, -0.0001) : max(value.z, 0.0001));\n"
    "}\n"
    "vec2 boxInterval(vec3 origin, vec3 direction) {\n"
    "    vec3 inverseDirection = 1.0/safeDirection(direction);\n"
    "    vec3 first = (cloudBoxMin - origin)*inverseDirection;\n"
    "    vec3 second = (cloudBoxMax - origin)*inverseDirection;\n"
    "    vec3 nearValues = min(first, second);\n"
    "    vec3 farValues = max(first, second);\n"
    "    return vec2(max(max(nearValues.x, nearValues.y), nearValues.z),\n"
    "                min(min(farValues.x, farValues.y), farValues.z));\n"
    "}\n"
    "float cloudNoise(vec3 point) {\n"
    "    vec2 warped = vec2(point.x + point.y*0.63, point.z - point.y*0.37);\n"
    "    vec2 drifted = warped - windOffset;\n"
    "    float broad = texture(texture0, drifted*0.0046).r;\n"
    "    float billow = texture(texture0, drifted*0.0127 + vec2(0.17, 0.43)).r;\n"
    "    float detail = texture(texture0, drifted*0.0340 + vec2(0.61, 0.09)).r;\n"
    "    return broad*0.58 + billow*0.29 + detail*0.13;\n"
    "}\n"
    "float cloudDensity(vec3 point) {\n"
    "    float height = clamp((point.y - cloudBoxMin.y)/\n"
    "                         max(cloudBoxMax.y - cloudBoxMin.y, 0.001), 0.0, 1.0);\n"
    "    float bottom = smoothstep(0.0, 0.16, height);\n"
    "    float top = 1.0 - smoothstep(0.64, 1.0, height);\n"
    "    float verticalShape = bottom*top;\n"
    "    float noiseValue = cloudNoise(point);\n"
    "    float threshold = mix(0.73, 0.34, clamp(cloudCoverage, 0.0, 1.0));\n"
    "    threshold -= stormAmount*0.06;\n"
    "    float density = smoothstep(threshold - 0.08, threshold + 0.11, noiseValue);\n"
    "    float baseWeight = mix(0.72, 1.08, 1.0 - height);\n"
    "    return clamp(density*verticalShape*baseWeight, 0.0, 1.0);\n"
    "}\n"
    "float cloudLight(vec3 point) {\n"
    "    float obstruction = 0.0;\n"
    "    vec3 direction = normalize(sunDirection);\n"
    "    for (int index = 0; index < 4; index++) {\n"
    "        if (index >= lightSteps) break;\n"
    "        float distanceAlongLight = 2.4 + float(index)*3.8;\n"
    "        obstruction += cloudDensity(point + direction*distanceAlongLight);\n"
    "    }\n"
    "    return exp(-obstruction*0.62);\n"
    "}\n"
    "void main() {\n"
    "    vec3 rayDirection = normalize(fragPosition - cameraPosition);\n"
    "    vec2 interval = boxInterval(cameraPosition, rayDirection);\n"
    "    float nearDistance = max(interval.x, 0.0);\n"
    "    float farDistance = interval.y;\n"
    "    if (farDistance <= nearDistance) discard;\n"
    "    bool cameraInside = all(greaterThanEqual(cameraPosition, cloudBoxMin)) &&\n"
    "                        all(lessThanEqual(cameraPosition, cloudBoxMax));\n"
    "    float surfaceDistance = length(fragPosition - cameraPosition);\n"
    "    float segmentLength = farDistance - nearDistance;\n"
    "    float stepLength = segmentLength/float(max(raySteps, 1));\n"
    "    if (!cameraInside && abs(surfaceDistance - nearDistance) > max(2.0, stepLength*1.5)) discard;\n"
    "    float jitter = texture(texture0, fragPosition.xz*0.021).r;\n"
    "    float travel = nearDistance + stepLength*(0.18 + jitter*0.64);\n"
    "    float accumulatedAlpha = 0.0;\n"
    "    vec3 accumulatedColor = vec3(0.0);\n"
    "    float forwardScatter = pow(max(dot(rayDirection, normalize(sunDirection)), 0.0), 8.0);\n"
    "    for (int index = 0; index < 24; index++) {\n"
    "        if (index >= raySteps || travel >= farDistance || accumulatedAlpha > 0.985) break;\n"
    "        vec3 point = cameraPosition + rayDirection*travel;\n"
    "        float edgeFade = 1.0 - smoothstep(drawDistance*0.72, drawDistance,\n"
    "                                           length(point.xz - cameraPosition.xz));\n"
    "        float density = cloudDensity(point)*edgeFade;\n"
    "        if (density > 0.015) {\n"
    "            float transmission = cloudLight(point);\n"
    "            float sampleAlpha = 1.0 - exp(-density*stepLength*0.105);\n"
    "            vec3 shadowColor = mix(vec3(0.32, 0.37, 0.45), colDiffuse.rgb, 0.26);\n"
    "            vec3 litColor = colDiffuse.rgb*lightColor*(0.62 + lightStrength*0.34);\n"
    "            vec3 sampleColor = mix(shadowColor, litColor, 0.20 + transmission*0.80);\n"
    "            sampleColor += lightColor*forwardScatter*transmission*0.22;\n"
    "            float remaining = 1.0 - accumulatedAlpha;\n"
    "            accumulatedColor += remaining*sampleColor*sampleAlpha;\n"
    "            accumulatedAlpha += remaining*sampleAlpha;\n"
    "        }\n"
    "        travel += stepLength;\n"
    "    }\n"
    "    float rawAlpha = accumulatedAlpha;\n"
    "    accumulatedAlpha *= cloudOpacity*colDiffuse.a*fragColor.a;\n"
    "    if (accumulatedAlpha < 0.008) discard;\n"
    "    vec3 color = accumulatedColor/max(rawAlpha, 0.001);\n"
    "    color = mix(color, color*vec3(0.72, 0.76, 0.84), stormAmount*0.34);\n"
    "    color *= 0.72 + daylight*0.28;\n"
    "    finalColor = vec4(color, clamp(accumulatedAlpha, 0.0, 0.96));\n"
    "}\n";

static uint32_t CloudNoiseHash(int x, int y)
{
    uint32_t hash = (uint32_t)x*0x8da6b343u ^ (uint32_t)y*0xd8163841u;
    hash ^= hash >> 13;
    hash *= 0x85ebca6bu;
    hash ^= hash >> 16;
    return hash;
}

static float CloudNoiseLattice(int x, int y, int period)
{
    x %= period;
    y %= period;
    if (x < 0) x += period;
    if (y < 0) y += period;
    return (float)(CloudNoiseHash(x, y) & 0x00ffffffu)/16777215.0f;
}

static float CloudValueNoise(float x, float y, int period)
{
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    float tx = x - (float)x0;
    float ty = y - (float)y0;
    tx = tx*tx*(3.0f - 2.0f*tx);
    ty = ty*ty*(3.0f - 2.0f*ty);
    float a = Lerp(CloudNoiseLattice(x0, y0, period),
                   CloudNoiseLattice(x0 + 1, y0, period), tx);
    float b = Lerp(CloudNoiseLattice(x0, y0 + 1, period),
                   CloudNoiseLattice(x0 + 1, y0 + 1, period), tx);
    return Lerp(a, b, ty);
}

static Texture2D MakeCloudNoiseTexture(void)
{
    enum { CLOUD_NOISE_SIZE = 128 };
    Color *pixels = malloc(sizeof(*pixels)*CLOUD_NOISE_SIZE*CLOUD_NOISE_SIZE);
    if (!pixels) return (Texture2D){ 0 };

    for (int y = 0; y < CLOUD_NOISE_SIZE; y++) {
        for (int x = 0; x < CLOUD_NOISE_SIZE; x++) {
            float nx = (float)x/(float)CLOUD_NOISE_SIZE;
            float ny = (float)y/(float)CLOUD_NOISE_SIZE;
            float value = 0.0f;
            float weight = 0.0f;
            for (int octave = 0; octave < 5; octave++) {
                int period = 4 << octave;
                float amplitude = powf(0.55f, (float)octave);
                value += CloudValueNoise(nx*(float)period,
                                         ny*(float)period, period)*amplitude;
                weight += amplitude;
            }
            unsigned char gray = (unsigned char)Clamp(value/weight*255.0f,
                                                       0.0f, 255.0f);
            pixels[y*CLOUD_NOISE_SIZE + x] = (Color){ gray, gray, gray, 255 };
        }
    }

    Image image = {
        .data = pixels,
        .width = CLOUD_NOISE_SIZE,
        .height = CLOUD_NOISE_SIZE,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };
    Texture2D texture = LoadTextureFromImage(image);
    free(pixels);
    if (texture.id != 0) {
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
    }
    return texture;
}

void UnloadCloudRenderResources(void)
{
    if (cloudModel.meshCount > 0) UnloadModel(cloudModel);
    if (cloudResources.noiseTexture.id != 0) {
        UnloadTexture(cloudResources.noiseTexture);
    }
    if (cloudResources.shader.id != 0) UnloadShader(cloudResources.shader);
    cloudModel = (Model){ 0 };
    cloudResources = (CloudRenderResources){ 0 };
}

Model LoadCloudModel(void)
{
    UnloadCloudRenderResources();
    cloudResources.shader = LoadShaderFromMemory(cloudVertexShader,
                                                  cloudFragmentShader);
    if (cloudResources.shader.id == 0) return (Model){ 0 };

#define CLOUD_LOCATION(field, name) \
    cloudResources.field = GetShaderLocation(cloudResources.shader, name)
    CLOUD_LOCATION(cameraPositionLoc, "cameraPosition");
    CLOUD_LOCATION(boxMinLoc, "cloudBoxMin");
    CLOUD_LOCATION(boxMaxLoc, "cloudBoxMax");
    CLOUD_LOCATION(windOffsetLoc, "windOffset");
    CLOUD_LOCATION(coverageLoc, "cloudCoverage");
    CLOUD_LOCATION(opacityLoc, "cloudOpacity");
    CLOUD_LOCATION(stormLoc, "stormAmount");
    CLOUD_LOCATION(daylightLoc, "daylight");
    CLOUD_LOCATION(drawDistanceLoc, "drawDistance");
    CLOUD_LOCATION(sunDirectionLoc, "sunDirection");
    CLOUD_LOCATION(lightColorLoc, "lightColor");
    CLOUD_LOCATION(lightStrengthLoc, "lightStrength");
    CLOUD_LOCATION(rayStepsLoc, "raySteps");
    CLOUD_LOCATION(lightStepsLoc, "lightSteps");
#undef CLOUD_LOCATION
    if (cloudResources.cameraPositionLoc < 0 || cloudResources.boxMinLoc < 0 ||
        cloudResources.boxMaxLoc < 0 || cloudResources.windOffsetLoc < 0 ||
        cloudResources.coverageLoc < 0 || cloudResources.opacityLoc < 0 ||
        cloudResources.stormLoc < 0 || cloudResources.daylightLoc < 0 ||
        cloudResources.drawDistanceLoc < 0 || cloudResources.sunDirectionLoc < 0 ||
        cloudResources.lightColorLoc < 0 || cloudResources.lightStrengthLoc < 0 ||
        cloudResources.rayStepsLoc < 0 || cloudResources.lightStepsLoc < 0) {
        UnloadCloudRenderResources();
        return (Model){ 0 };
    }

    cloudResources.noiseTexture = MakeCloudNoiseTexture();
    if (cloudResources.noiseTexture.id == 0) {
        UnloadCloudRenderResources();
        return (Model){ 0 };
    }

    Model model = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    if (model.meshCount <= 0 || model.materialCount <= 0) {
        if (model.meshCount > 0) UnloadModel(model);
        UnloadCloudRenderResources();
        return (Model){ 0 };
    }
    model.materials[0].shader = cloudResources.shader;
    SetMaterialTexture(&model.materials[0], MATERIAL_MAP_DIFFUSE,
                       cloudResources.noiseTexture);
    cloudResources.ready = true;
    return model;
}

static Color WeatherCloudColor(const WeatherVisualState *visual, Color tint)
{
    float daylight = visual ? visual->daylight : 0.5f;
    float storm = visual ? visual->stormDarkening : 0.0f;
    float snow = visual ? visual->snowFraction : 0.0f;
    Color cloud = ColorLerp((Color){ 96, 105, 122, 255 },
                            (Color){ 238, 242, 246, 255 }, daylight);
    cloud = ColorLerp(cloud, (Color){ 72, 80, 96, 255 }, storm * 0.72f);
    cloud = ColorLerp(cloud, (Color){ 224, 232, 240, 255 }, snow * 0.36f);
    return ColorLerp(cloud, tint, 0.18f);
}

void DrawClouds(const Camera3D *camera, Color tint, double simulationTime,
                const WeatherVisualState *weatherVisual,
                const EnvironmentPresentationState *presentation,
                const WorldLightingState *lighting)
{
    if (!camera || !weatherVisual || !weatherVisual->active ||
        weatherVisual->cloudCover <= 0.03f || !isfinite(simulationTime) ||
        !cloudResources.ready || cloudModel.meshCount <= 0) {
        return;
    }

    double phaseTime = fmod(simulationTime, 1000000.0);
    double driftSpeed = 1.2 + (double)weatherVisual->windDrift * 3.6;
    double driftDistance = fmod(phaseTime*driftSpeed, 8192.0);
    Vector2 windOffset = {
        (float)(cos((double)weatherVisual->windAngle)*driftDistance),
        (float)(sin((double)weatherVisual->windAngle)*driftDistance)
    };
    double cameraX = floor((double)camera->position.x);
    double cameraZ = floor((double)camera->position.z);
    if (cameraX < (double)INT_MIN || cameraX > (double)INT_MAX ||
        cameraZ < (double)INT_MIN || cameraZ > (double)INT_MAX) {
        return;
    }

    int gridRadius = 2;
    int raySteps = 12;
    int lightSteps = 2;
    float opacity = weatherVisual->cloudOpacity;
    if (presentation) {
        gridRadius = (int)roundf((presentation->cloudDistanceScale - 0.72f) / 0.14f);
        if (gridRadius < 1) gridRadius = 1;
        if (gridRadius > 3) gridRadius = 3;
        raySteps = presentation->cloudRaySteps;
        lightSteps = presentation->cloudLightSteps;
        opacity = presentation->cloudOpacity;
    }
    raySteps = raySteps < 6 ? 6 : (raySteps > 24 ? 24 : raySteps);
    lightSteps = lightSteps < 1 ? 1 : (lightSteps > 4 ? 4 : lightSteps);
    float drawDistance = 120.0f + (float)gridRadius*60.0f;
    int sampleX = (int)cameraX;
    int sampleZ = (int)cameraZ;
    int seaLevel = PlanetWorldIsActive() ? PlanetTerrainSeaLevel() :
                                           TerrainSeaLevel(WorldTerrainMode());
    float altitudeReference = seaLevel >= 0 ? (float)seaLevel :
        (PlanetWorldIsActive() ? (float)PlanetTerrainHeight(sampleX, sampleZ) :
                                 (float)WorldSurfaceHeightAt(sampleX, sampleZ));
    float cloudBottom = altitudeReference + weatherVisual->cloudBaseHeight;
    float cloudThickness = fmaxf(weatherVisual->cloudThickness, 4.0f);
    Vector3 boxMin = {
        camera->position.x - drawDistance,
        cloudBottom,
        camera->position.z - drawDistance
    };
    Vector3 boxMax = {
        camera->position.x + drawDistance,
        cloudBottom + cloudThickness,
        camera->position.z + drawDistance
    };
    Vector3 center = Vector3Scale(Vector3Add(boxMin, boxMax), 0.5f);
    Vector3 scale = Vector3Subtract(boxMax, boxMin);
    Vector3 sunDirection = lighting ? lighting->sunDirection :
                           (Vector3){ 0.32f, 0.88f, 0.18f };
    Color sun = lighting ? lighting->sunColor : WHITE;
    Vector3 lightColor = {
        (float)sun.r/255.0f,
        (float)sun.g/255.0f,
        (float)sun.b/255.0f
    };
    float lightStrength = lighting ? Clamp(lighting->directStrength, 0.0f, 2.0f) :
                                     weatherVisual->daylight;
    float coverage = Clamp(weatherVisual->cloudCover, 0.0f, 1.0f);
    opacity = Clamp(opacity, 0.0f, 1.0f);
    float storm = Clamp(weatherVisual->stormDarkening, 0.0f, 1.0f);
    float daylight = Clamp(weatherVisual->daylight, 0.0f, 1.0f);

#define SET_CLOUD_UNIFORM(location, value, type) \
    SetShaderValue(cloudResources.shader, location, &(value), type)
    SET_CLOUD_UNIFORM(cloudResources.cameraPositionLoc, camera->position,
                      SHADER_UNIFORM_VEC3);
    SET_CLOUD_UNIFORM(cloudResources.boxMinLoc, boxMin, SHADER_UNIFORM_VEC3);
    SET_CLOUD_UNIFORM(cloudResources.boxMaxLoc, boxMax, SHADER_UNIFORM_VEC3);
    SET_CLOUD_UNIFORM(cloudResources.windOffsetLoc, windOffset, SHADER_UNIFORM_VEC2);
    SET_CLOUD_UNIFORM(cloudResources.coverageLoc, coverage, SHADER_UNIFORM_FLOAT);
    SET_CLOUD_UNIFORM(cloudResources.opacityLoc, opacity, SHADER_UNIFORM_FLOAT);
    SET_CLOUD_UNIFORM(cloudResources.stormLoc, storm, SHADER_UNIFORM_FLOAT);
    SET_CLOUD_UNIFORM(cloudResources.daylightLoc, daylight, SHADER_UNIFORM_FLOAT);
    SET_CLOUD_UNIFORM(cloudResources.drawDistanceLoc, drawDistance,
                      SHADER_UNIFORM_FLOAT);
    SET_CLOUD_UNIFORM(cloudResources.sunDirectionLoc, sunDirection,
                      SHADER_UNIFORM_VEC3);
    SET_CLOUD_UNIFORM(cloudResources.lightColorLoc, lightColor,
                      SHADER_UNIFORM_VEC3);
    SET_CLOUD_UNIFORM(cloudResources.lightStrengthLoc, lightStrength,
                      SHADER_UNIFORM_FLOAT);
    SET_CLOUD_UNIFORM(cloudResources.rayStepsLoc, raySteps, SHADER_UNIFORM_INT);
    SET_CLOUD_UNIFORM(cloudResources.lightStepsLoc, lightSteps, SHADER_UNIFORM_INT);
#undef SET_CLOUD_UNIFORM

    Color cloudTint = WeatherCloudColor(weatherVisual, tint);
    cloudTint.a = tint.a;
    BeginBlendMode(BLEND_ALPHA);
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    PerfRecordDrawCall(PERF_DRAW_CLOUD);
    DrawModelEx(cloudModel, center, (Vector3){ 0.0f, 1.0f, 0.0f }, 0.0f,
                scale, cloudTint);
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    EndBlendMode();
}

void DrawEnvironmentPostProcess(
    const EnvironmentPresentationState *presentation)
{
    if (!presentation) return;
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    if (presentation->skyDarkening > 0.01f) {
        DrawRectangle(0, 0, width, height,
                      Fade((Color){ 16, 22, 32, 255 },
                           Clamp(presentation->skyDarkening * 0.10f, 0.0f, 0.12f)));
    }
    if (presentation->warmth > 0.01f) {
        DrawRectangle(0, 0, width, height,
                      Fade((Color){ 255, 112, 42, 255 },
                           Clamp(presentation->warmth * 0.035f, 0.0f, 0.04f)));
    }
    if (presentation->lightningFlash > 0.01f) {
        DrawRectangle(0, 0, width, height,
                      Fade((Color){ 218, 230, 255, 255 },
                           Clamp(presentation->lightningFlash * 0.32f,
                                 0.0f, 0.34f)));
    }
}

void DrawWeatherOverlay(const Camera3D *camera,
                        const WeatherVisualState *weatherVisual)
{
    if (!camera || !weatherVisual || !weatherVisual->active) return;
    float fog = weatherVisual->fogDensity;
    float veil = weatherVisual->precipitationVeil;
    if (fog <= 0.005f && veil <= 0.005f) return;

    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    if (screenWidth <= 0 || screenHeight <= 0) return;

    Vector3 forward = Vector3Normalize(
        Vector3Subtract(camera->target, camera->position));
    Vector3 flatForward = { forward.x, 0.0f, forward.z };
    float horizonY = (float)screenHeight * 0.52f;
    if (Vector3LengthSqr(flatForward) > 0.0001f) {
        flatForward = Vector3Normalize(flatForward);
        Vector3 horizonPoint = Vector3Add(
            camera->position, Vector3Scale(flatForward, SUN_DISTANCE));
        horizonY = GetWorldToScreen(horizonPoint, *camera).y;
    }
    int fogTop = (int)Clamp(horizonY - (float)screenHeight * 0.16f,
                            0.0f, (float)screenHeight);
    Color rainFog = ColorLerp((Color){ 64, 76, 92, 255 },
                              (Color){ 142, 154, 166, 255 },
                              weatherVisual->daylight);
    Color snowFog = ColorLerp((Color){ 112, 126, 148, 255 },
                              (Color){ 216, 226, 235, 255 },
                              weatherVisual->daylight);
    Color fogColor = ColorLerp(rainFog, snowFog,
                               weatherVisual->snowFraction);
    float topAlpha = Clamp(fog * 0.12f + veil * 0.04f, 0.0f, 0.14f);
    float bottomAlpha = Clamp(fog * 0.34f + veil * 0.10f, 0.0f, 0.38f);
    if (fogTop < screenHeight) {
        DrawRectangleGradientV(0, fogTop, screenWidth, screenHeight - fogTop,
                               Fade(fogColor, topAlpha),
                               Fade(fogColor, bottomAlpha));
    }
    if (veil > 0.01f) {
        DrawRectangle(0, 0, screenWidth, screenHeight,
                      Fade(fogColor, Clamp(veil * 0.10f, 0.0f, 0.11f)));
    }
}

WorldLightingState WorldLightingForScene(
    const Camera3D *camera, float currentDayTime, float daylight, float sunset,
    const PlanetLightState *planetLight,
    const WeatherVisualState *weatherVisual, Color skyHorizon, bool inNether,
    const EnvironmentPresentationState *presentation)
{
    float theta = (currentDayTime - 0.25f) * (2.0f * PI);
    Vector3 sunDirection = Vector3Normalize(
        (Vector3){ cosf(theta), sinf(theta), 0.18f });
    Color sunColor = ColorLerp((Color){ 255, 150, 94, 255 },
                               (Color){ 255, 236, 208, 255 },
                               Clamp(daylight * 1.35f, 0.0f, 1.0f));
    float sourceStrength = daylight;
    if (PlanetWorldIsActive() && planetLight &&
        planetLight->sourceCount > 0) {
        sunDirection = planetLight->sourceDirections[0];
        sunColor = planetLight->sourceColors[0];
        float intensity = planetLight->sourceIntensities[0];
        sourceStrength = Clamp((1.0f - expf(-fmaxf(intensity, 0.0f))) *
                                   planetLight->sourceVisibility[0],
                               0.0f, 1.6f);
    }
    float night = 1.0f - Clamp(daylight, 0.0f, 1.0f);
    WorldLightingState state = {
        .sunDirection = sunDirection,
        .sunColor = sunColor,
        .ambientColor = ColorLerp((Color){ 76, 94, 146, 255 },
                                  (Color){ 194, 214, 232, 255 }, daylight),
        .fogColor = skyHorizon,
        .cameraPosition = camera ? camera->position : Vector3Zero(),
        .directStrength = sourceStrength * 2.1f,
        .ambientStrength = 0.20f + daylight * 0.42f +
                           night * 0.08f,
        .shadowStrength = 0.42f + daylight * 0.34f,
        .fogDensity = 0.0f,
        .fogStart = 38.0f,
        .wetness = 0.0f,
        .exposure = 1.0f,
        .saturation = 1.0f,
        .warmth = 0.0f,
        .waveStrength = 0.18f,
        .time = (float)fmod(SpaceElapsedSimulationTime(), 1000000.0),
        .shadowsEnabled = daylight > 0.05f
    };
    state.ambientColor = ColorLerp(state.ambientColor,
                                   (Color){ 255, 146, 94, 255 },
                                   sunset * 0.18f);
    if (inNether) {
        state.sunDirection = (Vector3){ 0.25f, 0.88f, 0.18f };
        state.sunColor = (Color){ 192, 62, 34, 255 };
        state.ambientColor = (Color){ 128, 34, 28, 255 };
        state.fogColor = (Color){ 40, 10, 8, 255 };
        state.directStrength = 1.0f;
        state.ambientStrength = 1.0f;
    }
    EnvironmentPresentationState fallback;
    if (!presentation) {
        fallback = WorldLightingFallbackPresentation(
            daylight, sunset, weatherVisual, inNether);
        presentation = &fallback;
    }
    return WorldLightingCompose(state, presentation);
}
