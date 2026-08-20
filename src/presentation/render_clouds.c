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
    int noiseScaleLoc;
    int stretchLoc;
    int cellularityLoc;
    int verticalDevelopmentLoc;
    int anvilLoc;
    int layerPhaseLoc;
    int windDirectionLoc;
    bool ready;
} CloudRenderResources;

static CloudRenderResources cloudResources = { 0 };
static WeatherCloudMotionState cloudMotion = { 0 };

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
    "uniform float cloudNoiseScale;\n"
    "uniform float cloudStretch;\n"
    "uniform float cloudCellularity;\n"
    "uniform float cloudVerticalDevelopment;\n"
    "uniform float cloudAnvil;\n"
    "uniform float cloudLayerPhase;\n"
    "uniform vec2 windDirection;\n"
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
    "float cloudHash3D(vec3 point) {\n"
    "    point = fract(point*0.1031);\n"
    "    point += dot(point, point.yzx + 33.33);\n"
    "    return fract((point.x + point.y)*point.z);\n"
    "}\n"
    "float cloudValueNoise3D(vec3 point) {\n"
    "    vec3 cell = floor(point);\n"
    "    vec3 blend = fract(point);\n"
    "    blend = blend*blend*(3.0 - 2.0*blend);\n"
    "    float z0 = mix(mix(cloudHash3D(cell + vec3(0.0, 0.0, 0.0)),\n"
    "                       cloudHash3D(cell + vec3(1.0, 0.0, 0.0)), blend.x),\n"
    "                   mix(cloudHash3D(cell + vec3(0.0, 1.0, 0.0)),\n"
    "                       cloudHash3D(cell + vec3(1.0, 1.0, 0.0)), blend.x),\n"
    "                   blend.y);\n"
    "    float z1 = mix(mix(cloudHash3D(cell + vec3(0.0, 0.0, 1.0)),\n"
    "                       cloudHash3D(cell + vec3(1.0, 0.0, 1.0)), blend.x),\n"
    "                   mix(cloudHash3D(cell + vec3(0.0, 1.0, 1.0)),\n"
    "                       cloudHash3D(cell + vec3(1.0, 1.0, 1.0)), blend.x),\n"
    "                   blend.y);\n"
    "    return mix(z0, z1, blend.z);\n"
    "}\n"
    "vec2 cloudCoordinates(vec3 point) {\n"
    "    vec2 warped = point.xz;\n"
    "    vec2 direction = normalize(windDirection + vec2(0.0001));\n"
    "    vec2 perpendicular = vec2(-direction.y, direction.x);\n"
    "    float along = dot(warped, direction)*mix(1.0, 0.20, cloudStretch);\n"
    "    float across = dot(warped, perpendicular)*mix(1.0, 2.35, cloudStretch);\n"
    "    vec2 shaped = direction*along + perpendicular*across;\n"
    "    vec2 phase = vec2(cloudLayerPhase*17.3, cloudLayerPhase*-11.7);\n"
    "    return shaped - windOffset + phase;\n"
    "}\n"
    "float cloudColumnShape(vec3 point) {\n"
    "    vec2 drifted = cloudCoordinates(point);\n"
    "    float scale = max(cloudNoiseScale, 0.10);\n"
    "    vec4 broad = texture(texture0, drifted*(0.0038*scale));\n"
    "    vec4 billow = texture(texture0,\n"
    "        drifted*(0.0105*scale) + vec2(0.17, 0.43));\n"
    "    return broad.r*0.50 + billow.g*0.35 + billow.a*0.15;\n"
    "}\n"
    "vec4 cloudNoiseLayers(vec3 point) {\n"
    "    vec2 drifted = cloudCoordinates(point);\n"
    "    vec2 direction = normalize(windDirection + vec2(0.0001));\n"
    "    vec2 perpendicular = vec2(-direction.y, direction.x);\n"
    "    float scale = max(cloudNoiseScale, 0.10);\n"
    "    vec4 broad = texture(texture0, drifted*(0.0038*scale));\n"
    "    vec4 billow = texture(texture0,\n"
    "        drifted*(0.0105*scale) + vec2(0.17, 0.43));\n"
    "    vec2 finePoint = drifted + vec2(point.y*0.37, point.y*0.21);\n"
    "    vec4 fine = texture(texture0,\n"
    "        finePoint*(0.0290*scale) + vec2(0.61, 0.09));\n"
    "    vec2 worldSide = point.xz - windOffset;\n"
    "    float sideAlong = dot(worldSide, direction);\n"
    "    float sideAcross = dot(worldSide, perpendicular);\n"
    "    vec2 sidePoint = vec2(sideAcross*0.70 + sideAlong*0.20 +\n"
    "                              point.y*0.41,\n"
    "                              sideAlong*0.65 - sideAcross*0.18 -\n"
    "                              point.y*0.33);\n"
    "    vec4 side = texture(texture0,\n"
    "        sidePoint*(0.0135*scale) + vec2(0.29, 0.71));\n"
    "    vec3 volumePoint = vec3(drifted.x*(0.020*scale),\n"
    "                            point.y*(0.043*scale) +\n"
    "                            cloudLayerPhase*5.7,\n"
    "                            drifted.y*(0.020*scale));\n"
    "    float volumeShape = cloudValueNoise3D(volumePoint);\n"
    "    float macroShape = broad.r*0.42 + volumeShape*0.58;\n"
    "    float roundedBillow = billow.g*0.56 + side.g*0.44;\n"
    "    float edgeDetail = fine.b*0.58 + side.b*0.42;\n"
    "    float cells = billow.a*0.56 + side.a*0.44;\n"
    "    return vec4(macroShape, roundedBillow, edgeDetail, cells);\n"
    "}\n"
    "float cloudDensitySample(vec3 point, out vec4 noiseLayers) {\n"
    "    float layerDepth = max(cloudBoxMax.y - cloudBoxMin.y, 0.001);\n"
    "    float height = clamp((point.y - cloudBoxMin.y)/\n"
    "                         layerDepth, 0.0, 1.0);\n"
    "    float bottom = smoothstep(0.0, 0.10, height);\n"
    "    float top = 1.0 - smoothstep(0.70, 0.98, height);\n"
    "    float sheetShape = bottom*top;\n"
    "    vec3 columnPoint = vec3(point.x, cloudBoxMin.y + layerDepth*0.22,\n"
    "                            point.z);\n"
    "    float column = cloudColumnShape(columnPoint);\n"
    "    float columnStrength = smoothstep(0.29, 0.72, column);\n"
    "    float localTop = 0.32 + columnStrength*0.66;\n"
    "    float towerTop = 1.0 - smoothstep(max(localTop - 0.18, 0.12),\n"
    "                                      localTop, height);\n"
    "    float towerShape = smoothstep(0.0, 0.09, height)*towerTop;\n"
    "    float verticalShape = mix(sheetShape, towerShape,\n"
    "                              cloudVerticalDevelopment);\n"
    "    float anvilBand = smoothstep(0.60, 0.75, height)*\n"
    "                      (1.0 - smoothstep(0.91, 1.0, height));\n"
    "    float anvilShape = 0.0;\n"
    "    if (cloudAnvil > 0.01) {\n"
    "        vec3 anvilPoint = vec3(point.x*0.62, point.y, point.z*0.62);\n"
    "        float anvilNoise = cloudColumnShape(anvilPoint);\n"
    "        anvilShape = anvilBand*smoothstep(0.38, 0.59, anvilNoise);\n"
    "    }\n"
    "    verticalShape = max(verticalShape, anvilShape*cloudAnvil*0.88);\n"
    "    noiseLayers = cloudNoiseLayers(point);\n"
    "    float cellular = mix(noiseLayers.y, noiseLayers.w,\n"
    "                         cloudCellularity);\n"
    "    float noiseValue = noiseLayers.x*0.55 + noiseLayers.y*0.31 +\n"
    "                       cellular*0.14;\n"
    "    float threshold = mix(0.70, 0.43, clamp(cloudCoverage, 0.0, 1.0));\n"
    "    threshold -= stormAmount*0.06;\n"
    "    threshold += cloudVerticalDevelopment*mix(-0.025, 0.075, height);\n"
    "    threshold -= cloudAnvil*anvilBand*0.12;\n"
    "    float coarse = smoothstep(threshold - 0.080, threshold + 0.105,\n"
    "                              noiseValue);\n"
    "    float erosion = mix(noiseLayers.z, noiseLayers.w,\n"
    "                        0.20 + cloudCellularity*0.42);\n"
    "    float density = clamp(coarse - (1.0 - erosion)*\n"
    "                          (1.0 - coarse)*0.58, 0.0, 1.0);\n"
    "    density = smoothstep(0.035, 0.72, density);\n"
    "    float upperBillow = mix(1.0, smoothstep(0.20, 0.66,\n"
    "        noiseLayers.y*0.68 + noiseLayers.w*0.32),\n"
    "        cloudVerticalDevelopment*height*0.68);\n"
    "    float baseWeight = mix(0.82, 1.12, 1.0 - height);\n"
    "    density *= upperBillow;\n"
    "    return clamp(density*verticalShape*baseWeight, 0.0, 1.0);\n"
    "}\n"
    "float cloudDensity(vec3 point) {\n"
    "    vec4 noiseLayers;\n"
    "    return cloudDensitySample(point, noiseLayers);\n"
    "}\n"
    "float cloudLight(vec3 point) {\n"
    "    float obstruction = 0.0;\n"
    "    vec3 direction = normalize(sunDirection);\n"
    "    for (int index = 0; index < 4; index++) {\n"
    "        if (index >= lightSteps) break;\n"
    "        float distanceAlongLight = 2.8 + float(index)*4.6;\n"
    "        obstruction += cloudDensity(point + direction*distanceAlongLight)*\n"
    "                       mix(1.0, 0.72, float(index)/3.0);\n"
    "    }\n"
    "    return exp(-obstruction*0.74);\n"
    "}\n"
    "float cloudPhase(float cosineAngle) {\n"
    "    float forwardG = 0.56;\n"
    "    float backG = -0.20;\n"
    "    float forward = (1.0 - forwardG*forwardG)/\n"
    "        pow(1.0 + forwardG*forwardG - 2.0*forwardG*cosineAngle, 1.5);\n"
    "    float backward = (1.0 - backG*backG)/\n"
    "        pow(1.0 + backG*backG - 2.0*backG*cosineAngle, 1.5);\n"
    "    return clamp((forward*0.72 + backward*0.28)*0.18, 0.08, 1.35);\n"
    "}\n"
    "float cloudRayJitter(vec2 pixel) {\n"
    "    return fract(52.9829189*fract(dot(pixel,\n"
    "        vec2(0.06711056, 0.00583715)) + cloudLayerPhase*0.173));\n"
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
    "    float jitter = cloudRayJitter(floor(gl_FragCoord.xy));\n"
    "    float travel = nearDistance + stepLength*(jitter - 0.5)*0.35;\n"
    "    float accumulatedAlpha = 0.0;\n"
    "    vec3 accumulatedColor = vec3(0.0);\n"
    "    float cachedTransmission = -1.0;\n"
    "    float viewSun = dot(rayDirection, normalize(sunDirection));\n"
    "    float phase = cloudPhase(viewSun);\n"
    "    for (int index = 0; index < 24; index++) {\n"
    "        if (index >= raySteps || travel >= farDistance || accumulatedAlpha > 0.985) break;\n"
    "        vec3 point = cameraPosition + rayDirection*\n"
    "                     (travel + stepLength*0.25);\n"
    "        vec3 farPoint = cameraPosition + rayDirection*\n"
    "                        (travel + stepLength*0.75);\n"
    "        float edgeFade = 1.0 - smoothstep(drawDistance*0.72, drawDistance,\n"
    "                                           length(point.xz - cameraPosition.xz));\n"
    "        vec4 nearNoise;\n"
    "        vec4 farNoise;\n"
    "        float nearDensity = cloudDensitySample(point, nearNoise);\n"
    "        float farDensity = cloudDensitySample(farPoint, farNoise);\n"
    "        float density = (nearDensity + farDensity)*0.5*edgeFade;\n"
    "        vec4 localNoise = (nearNoise + farNoise)*0.5;\n"
    "        if (density > 0.015) {\n"
    "            if (cachedTransmission < 0.0 || index%2 == 0) {\n"
    "                cachedTransmission = cloudLight(point);\n"
    "            }\n"
    "            float transmission = cachedTransmission;\n"
    "            float extinction = mix(0.032, 0.096, cloudOpacity);\n"
    "            float sampleAlpha = 1.0 - exp(-density*stepLength*extinction);\n"
    "            float layerHeight = clamp((point.y - cloudBoxMin.y)/\n"
    "                max(cloudBoxMax.y - cloudBoxMin.y, 0.001), 0.0, 1.0);\n"
    "            float powder = 1.0 - exp(-density*2.8);\n"
    "            vec3 shadowColor = mix(vec3(0.40, 0.47, 0.58),\n"
    "                                   colDiffuse.rgb, 0.42);\n"
    "            float ambient = 0.50 + daylight*0.14 + layerHeight*0.12;\n"
    "            vec3 sampleColor = shadowColor*ambient;\n"
    "            float multipleScatter = 0.07 + powder*0.09 +\n"
    "                                    daylight*0.05;\n"
    "            sampleColor += colDiffuse.rgb*multipleScatter;\n"
    "            float lobeExposure = smoothstep(0.24, 0.76,\n"
    "                localNoise.y*0.68 + localNoise.z*0.32);\n"
    "            sampleColor *= mix(0.76, 1.12, lobeExposure);\n"
    "            vec3 directColor = colDiffuse.rgb*lightColor;\n"
    "            float direct = transmission*(0.32 + powder*0.48)*\n"
    "                           (0.52 + phase*0.68)*\n"
    "                           (0.58 + lightStrength*0.30);\n"
    "            sampleColor += directColor*direct;\n"
    "            float silverLining = transmission*(1.0 - powder)*phase*0.42;\n"
    "            sampleColor += lightColor*silverLining;\n"
    "            float remaining = 1.0 - accumulatedAlpha;\n"
    "            accumulatedColor += remaining*sampleColor*sampleAlpha;\n"
    "            accumulatedAlpha += remaining*sampleAlpha;\n"
    "        }\n"
    "        travel += stepLength;\n"
    "    }\n"
    "    float rawAlpha = accumulatedAlpha;\n"
    "    accumulatedAlpha *= colDiffuse.a*fragColor.a;\n"
    "    if (accumulatedAlpha < 0.008) discard;\n"
    "    vec3 color = accumulatedColor/max(rawAlpha, 0.001);\n"
    "    color = mix(color, color*vec3(0.72, 0.76, 0.84), stormAmount*0.34);\n"
    "    color *= 0.78 + daylight*0.22;\n"
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

static float CloudCellularNoise(float x, float y, int period)
{
    int baseX = (int)floorf(x);
    int baseY = (int)floorf(y);
    float nearestSquared = 4.0f;
    for (int offsetY = -1; offsetY <= 1; offsetY++) {
        for (int offsetX = -1; offsetX <= 1; offsetX++) {
            int cellX = baseX + offsetX;
            int cellY = baseY + offsetY;
            int wrappedX = cellX % period;
            int wrappedY = cellY % period;
            if (wrappedX < 0) wrappedX += period;
            if (wrappedY < 0) wrappedY += period;
            float featureX = (float)cellX + 0.14f +
                CloudNoiseLattice(wrappedX, wrappedY, period)*0.72f;
            float featureY = (float)cellY + 0.14f +
                CloudNoiseLattice(wrappedX + 37, wrappedY - 19, period)*0.72f;
            float dx = x - featureX;
            float dy = y - featureY;
            nearestSquared = fminf(nearestSquared, dx*dx + dy*dy);
        }
    }
    return 1.0f - Clamp(sqrtf(nearestSquared)/0.92f, 0.0f, 1.0f);
}

static Texture2D MakeCloudNoiseTexture(void)
{
    enum { CLOUD_NOISE_SIZE = 128 };
    Color *pixels = malloc(sizeof(*pixels)*CLOUD_NOISE_SIZE*CLOUD_NOISE_SIZE);
    if (!pixels) return (Texture2D){ 0 };

    /* RGBA stores macro shape, billows, erosion detail, and cellular lobes. */
    for (int y = 0; y < CLOUD_NOISE_SIZE; y++) {
        for (int x = 0; x < CLOUD_NOISE_SIZE; x++) {
            float nx = (float)x/(float)CLOUD_NOISE_SIZE;
            float ny = (float)y/(float)CLOUD_NOISE_SIZE;
            float macro = CloudValueNoise(nx*4.0f, ny*4.0f, 4)*0.68f +
                          CloudValueNoise(nx*8.0f, ny*8.0f, 8)*0.32f;
            float billow = CloudValueNoise(nx*8.0f, ny*8.0f, 8)*0.56f +
                           CloudValueNoise(nx*16.0f, ny*16.0f, 16)*0.29f +
                           CloudValueNoise(nx*32.0f, ny*32.0f, 32)*0.15f;
            float detail = CloudValueNoise(nx*16.0f, ny*16.0f, 16)*0.50f +
                           CloudValueNoise(nx*32.0f, ny*32.0f, 32)*0.31f +
                           CloudValueNoise(nx*64.0f, ny*64.0f, 64)*0.19f;
            float cellular = CloudCellularNoise(nx*12.0f, ny*12.0f, 12);
            pixels[y*CLOUD_NOISE_SIZE + x] = (Color){
                (unsigned char)Clamp(macro*255.0f, 0.0f, 255.0f),
                (unsigned char)Clamp(billow*255.0f, 0.0f, 255.0f),
                (unsigned char)Clamp(detail*255.0f, 0.0f, 255.0f),
                (unsigned char)Clamp(cellular*255.0f, 0.0f, 255.0f)
            };
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
    CLOUD_LOCATION(noiseScaleLoc, "cloudNoiseScale");
    CLOUD_LOCATION(stretchLoc, "cloudStretch");
    CLOUD_LOCATION(cellularityLoc, "cloudCellularity");
    CLOUD_LOCATION(verticalDevelopmentLoc, "cloudVerticalDevelopment");
    CLOUD_LOCATION(anvilLoc, "cloudAnvil");
    CLOUD_LOCATION(layerPhaseLoc, "cloudLayerPhase");
    CLOUD_LOCATION(windDirectionLoc, "windDirection");
#undef CLOUD_LOCATION
    if (cloudResources.cameraPositionLoc < 0 || cloudResources.boxMinLoc < 0 ||
        cloudResources.boxMaxLoc < 0 || cloudResources.windOffsetLoc < 0 ||
        cloudResources.coverageLoc < 0 || cloudResources.opacityLoc < 0 ||
        cloudResources.stormLoc < 0 || cloudResources.daylightLoc < 0 ||
        cloudResources.drawDistanceLoc < 0 || cloudResources.sunDirectionLoc < 0 ||
        cloudResources.lightColorLoc < 0 || cloudResources.lightStrengthLoc < 0 ||
        cloudResources.rayStepsLoc < 0 || cloudResources.lightStepsLoc < 0 ||
        cloudResources.noiseScaleLoc < 0 || cloudResources.stretchLoc < 0 ||
        cloudResources.cellularityLoc < 0 ||
        cloudResources.verticalDevelopmentLoc < 0 ||
        cloudResources.anvilLoc < 0 || cloudResources.layerPhaseLoc < 0 ||
        cloudResources.windDirectionLoc < 0) {
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
    Color cloud = ColorLerp((Color){ 142, 156, 181, 255 },
                            (Color){ 244, 247, 250, 255 }, daylight);
    cloud = ColorLerp(cloud, (Color){ 72, 80, 96, 255 }, storm * 0.72f);
    cloud = ColorLerp(cloud, (Color){ 224, 232, 240, 255 }, snow * 0.36f);
    return ColorLerp(cloud, tint, 0.07f);
}

static Color WeatherCloudLayerColor(const WeatherVisualState *visual,
                                    WeatherCloudGenus genus, Color tint)
{
    Color color = WeatherCloudColor(visual, tint);
    if (genus >= WEATHER_CLOUD_GENUS_CIRRUS &&
        genus <= WEATHER_CLOUD_GENUS_CIRROSTRATUS) {
        color = ColorLerp(color, (Color){ 244, 247, 250, 255 }, 0.24f);
    } else if (genus == WEATHER_CLOUD_GENUS_NIMBOSTRATUS ||
               genus == WEATHER_CLOUD_GENUS_CUMULONIMBUS) {
        color = ColorLerp(color, (Color){ 76, 84, 101, 255 },
                          visual ? visual->stormDarkening * 0.34f + 0.10f : 0.10f);
    }
    return color;
}

void DrawClouds(const Camera3D *camera, Color tint, double simulationTime,
                const WeatherVisualState *weatherVisual,
                const EnvironmentPresentationState *presentation,
                const WorldLightingState *lighting)
{
    if (!camera || !weatherVisual || !weatherVisual->active ||
        !isfinite(simulationTime) ||
        !cloudResources.ready || cloudModel.meshCount <= 0) {
        return;
    }

    float driftSpeed = 1.2f + weatherVisual->windDrift * 3.6f;
    WeatherCloudMotionAdvance(&cloudMotion, simulationTime,
                              weatherVisual->windAngle, driftSpeed);
    if (weatherVisual->cloudCover <= 0.03f) return;

    int gridRadius = 2;
    int raySteps = 12;
    int lightSteps = 2;
    float presentationOpacity = weatherVisual->cloudOpacity;
    if (presentation) {
        gridRadius = (int)roundf((presentation->cloudDistanceScale - 0.72f) / 0.14f);
        if (gridRadius < 1) gridRadius = 1;
        if (gridRadius > 3) gridRadius = 3;
        raySteps = presentation->cloudRaySteps;
        lightSteps = presentation->cloudLightSteps;
        presentationOpacity = presentation->cloudOpacity;
    }
    raySteps = raySteps < 6 ? 6 : (raySteps > 24 ? 24 : raySteps);
    lightSteps = lightSteps < 1 ? 1 : (lightSteps > 4 ? 4 : lightSteps);
    float drawDistance = 120.0f + (float)gridRadius*60.0f;
    double baseX = floor((double)camera->position.x - 0.5);
    double baseZ = floor((double)camera->position.z - 0.5);
    if (baseX < (double)INT_MIN || baseX >= (double)INT_MAX ||
        baseZ < (double)INT_MIN || baseZ >= (double)INT_MAX) return;
    int x = (int)baseX;
    int z = (int)baseZ;
    float tx = (float)((double)camera->position.x - (baseX + 0.5));
    float tz = (float)((double)camera->position.z - (baseZ + 0.5));
    int seaLevel = PlanetWorldIsActive() ? PlanetTerrainSeaLevel() :
                                           TerrainSeaLevel(WorldTerrainMode());
    float altitudeReference = WeatherCloudAltitudeReference(
        (float)seaLevel, (float)WorldSurfaceHeightAt(x, z),
        (float)WorldSurfaceHeightAt(x + 1, z),
        (float)WorldSurfaceHeightAt(x, z + 1),
        (float)WorldSurfaceHeightAt(x + 1, z + 1), tx, tz);
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
    float storm = Clamp(weatherVisual->stormDarkening, 0.0f, 1.0f);
    float daylight = Clamp(weatherVisual->daylight, 0.0f, 1.0f);
    WeatherCloudVisualLayer fallback = {
        .genus = WEATHER_CLOUD_GENUS_STRATOCUMULUS,
        .coverage = weatherVisual->cloudCover,
        .baseHeight = weatherVisual->cloudBaseHeight,
        .thickness = weatherVisual->cloudThickness,
        .opacity = weatherVisual->cloudOpacity,
        .noiseScale = 1.0f,
        .verticalDevelopment = 0.18f,
        .driftScale = 1.0f
    };
    unsigned layerCount = weatherVisual->cloudLayerCount;
    if (layerCount > WEATHER_VISUAL_CLOUD_LAYER_CAPACITY) {
        layerCount = WEATHER_VISUAL_CLOUD_LAYER_CAPACITY;
    }
    if (layerCount == 0u) layerCount = 1u;
    float opacityRatio = weatherVisual->cloudOpacity > 0.001f ?
        presentationOpacity / weatherVisual->cloudOpacity : 1.0f;
    Vector2 windDirection = {
        cloudMotion.directionX, cloudMotion.directionZ
    };

    BeginBlendMode(BLEND_ALPHA);
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    for (unsigned reverse = layerCount; reverse > 0u; reverse--) {
        unsigned index = reverse - 1u;
        const WeatherCloudVisualLayer *layer =
            weatherVisual->cloudLayerCount > 0u ?
                &weatherVisual->cloudLayers[index] : &fallback;
        if (!isfinite(layer->coverage) || layer->coverage <= 0.025f ||
            !isfinite(layer->baseHeight) || !isfinite(layer->thickness) ||
            !isfinite(layer->opacity)) {
            continue;
        }
        float cloudBottom = altitudeReference + layer->baseHeight;
        float cloudThickness = fmaxf(layer->thickness, 4.0f);
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
        float layerDrift = fmaxf(layer->driftScale, 0.1f);
        Vector2 windOffset = {
            (float)(cloudMotion.offsetX * (double)layerDrift),
            (float)(cloudMotion.offsetZ * (double)layerDrift)
        };
        float coverage = Clamp(layer->coverage, 0.0f, 1.0f);
        float opacity = Clamp(layer->opacity * opacityRatio, 0.0f, 1.0f);
        float layerStorm = storm;
        if (layer->genus >= WEATHER_CLOUD_GENUS_CIRRUS &&
            layer->genus <= WEATHER_CLOUD_GENUS_CIRROSTRATUS) {
            layerStorm *= 0.28f;
        }
        int layerRaySteps = (int)roundf((float)raySteps *
            (0.54f + layer->opacity * 0.18f +
             layer->verticalDevelopment * 0.30f));
        if (layer->verticalDevelopment >= 0.55f) layerRaySteps = raySteps;
        if (layerRaySteps < 4) layerRaySteps = 4;
        if (layerRaySteps > raySteps) layerRaySteps = raySteps;
        int layerLightSteps = layer->opacity < 0.42f ? 1 : lightSteps;
        float noiseScale = fmaxf(layer->noiseScale, 0.10f);
        float stretch = Clamp(layer->stretch, 0.0f, 1.0f);
        float cellularity = Clamp(layer->cellularity, 0.0f, 1.0f);
        float verticalDevelopment = Clamp(layer->verticalDevelopment, 0.0f, 1.0f);
        float anvil = Clamp(layer->anvil, 0.0f, 1.0f);
        float layerPhase = (float)layer->genus * 0.137f + (float)index * 0.271f;

#define SET_CLOUD_UNIFORM(location, value, type) \
        SetShaderValue(cloudResources.shader, location, &(value), type)
        SET_CLOUD_UNIFORM(cloudResources.cameraPositionLoc, camera->position,
                          SHADER_UNIFORM_VEC3);
        SET_CLOUD_UNIFORM(cloudResources.boxMinLoc, boxMin, SHADER_UNIFORM_VEC3);
        SET_CLOUD_UNIFORM(cloudResources.boxMaxLoc, boxMax, SHADER_UNIFORM_VEC3);
        SET_CLOUD_UNIFORM(cloudResources.windOffsetLoc, windOffset,
                          SHADER_UNIFORM_VEC2);
        SET_CLOUD_UNIFORM(cloudResources.coverageLoc, coverage,
                          SHADER_UNIFORM_FLOAT);
        SET_CLOUD_UNIFORM(cloudResources.opacityLoc, opacity,
                          SHADER_UNIFORM_FLOAT);
        SET_CLOUD_UNIFORM(cloudResources.stormLoc, layerStorm,
                          SHADER_UNIFORM_FLOAT);
        SET_CLOUD_UNIFORM(cloudResources.daylightLoc, daylight,
                          SHADER_UNIFORM_FLOAT);
        SET_CLOUD_UNIFORM(cloudResources.drawDistanceLoc, drawDistance,
                          SHADER_UNIFORM_FLOAT);
        SET_CLOUD_UNIFORM(cloudResources.sunDirectionLoc, sunDirection,
                          SHADER_UNIFORM_VEC3);
        SET_CLOUD_UNIFORM(cloudResources.lightColorLoc, lightColor,
                          SHADER_UNIFORM_VEC3);
        SET_CLOUD_UNIFORM(cloudResources.lightStrengthLoc, lightStrength,
                          SHADER_UNIFORM_FLOAT);
        SET_CLOUD_UNIFORM(cloudResources.rayStepsLoc, layerRaySteps,
                          SHADER_UNIFORM_INT);
        SET_CLOUD_UNIFORM(cloudResources.lightStepsLoc, layerLightSteps,
                          SHADER_UNIFORM_INT);
        SET_CLOUD_UNIFORM(cloudResources.noiseScaleLoc, noiseScale,
                          SHADER_UNIFORM_FLOAT);
        SET_CLOUD_UNIFORM(cloudResources.stretchLoc, stretch,
                          SHADER_UNIFORM_FLOAT);
        SET_CLOUD_UNIFORM(cloudResources.cellularityLoc, cellularity,
                          SHADER_UNIFORM_FLOAT);
        SET_CLOUD_UNIFORM(cloudResources.verticalDevelopmentLoc,
                          verticalDevelopment, SHADER_UNIFORM_FLOAT);
        SET_CLOUD_UNIFORM(cloudResources.anvilLoc, anvil, SHADER_UNIFORM_FLOAT);
        SET_CLOUD_UNIFORM(cloudResources.layerPhaseLoc, layerPhase,
                          SHADER_UNIFORM_FLOAT);
        SET_CLOUD_UNIFORM(cloudResources.windDirectionLoc, windDirection,
                          SHADER_UNIFORM_VEC2);
#undef SET_CLOUD_UNIFORM

        Color cloudTint = WeatherCloudLayerColor(
            weatherVisual, layer->genus, tint);
        cloudTint.a = tint.a;
        PerfRecordDrawCall(PERF_DRAW_CLOUD);
        DrawModelEx(cloudModel, center, (Vector3){ 0.0f, 1.0f, 0.0f }, 0.0f,
                    scale, cloudTint);
    }
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
    float dust = weatherVisual->dustDensity;
    float rainbow = weatherVisual->rainbowStrength;
    float aurora = weatherVisual->auroraStrength;
    if (fog <= 0.005f && veil <= 0.005f && dust <= 0.005f &&
        rainbow <= 0.005f && aurora <= 0.005f) return;

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
    fogColor = ColorLerp(fogColor, (Color){ 154, 126, 86, 255 }, dust * 0.72f);
    float topAlpha = Clamp(fog * 0.12f + veil * 0.04f + dust * 0.08f,
                           0.0f, 0.18f);
    float bottomAlpha = Clamp(fog * 0.34f + veil * 0.10f + dust * 0.28f,
                              0.0f, 0.42f);
    if (fogTop < screenHeight) {
        DrawRectangleGradientV(0, fogTop, screenWidth, screenHeight - fogTop,
                               Fade(fogColor, topAlpha),
                               Fade(fogColor, bottomAlpha));
    }
    if (veil > 0.01f) {
        DrawRectangle(0, 0, screenWidth, screenHeight,
                      Fade(fogColor, Clamp(veil * 0.10f, 0.0f, 0.11f)));
    }
    if (aurora > 0.01f) {
        int auroraHeight = (int)((float)screenHeight * 0.46f);
        DrawRectangleGradientV(
            0, 0, screenWidth, auroraHeight,
            Fade((Color){ 38, 210, 146, 255 }, Clamp(aurora * 0.16f, 0.0f, 0.18f)),
            Fade((Color){ 74, 82, 176, 255 }, 0.0f));
        for (int band = 0; band < 5; band++) {
            float x = ((float)band + 0.5f) * (float)screenWidth / 5.0f;
            float bend = sinf((float)band * 1.7f + weatherVisual->windAngle) *
                         (float)screenWidth * 0.06f;
            DrawLineEx((Vector2){ x, 0.0f },
                       (Vector2){ x + bend, (float)auroraHeight * 0.72f },
                       10.0f + aurora * 18.0f,
                       Fade((Color){ 112, 244, 194, 255 },
                            Clamp(aurora * 0.11f, 0.0f, 0.13f)));
        }
    }
    if (rainbow > 0.01f) {
        static const Color colors[] = {
            { 224, 66, 64, 255 }, { 234, 146, 54, 255 },
            { 232, 214, 76, 255 }, { 76, 190, 108, 255 },
            { 66, 132, 218, 255 }, { 142, 82, 190, 255 }
        };
        Vector2 center = {
            (float)screenWidth * 0.5f, (float)screenHeight * 1.04f
        };
        float baseRadius = (float)screenHeight * 0.62f;
        for (unsigned band = 0; band < sizeof(colors) / sizeof(colors[0]);
             band++) {
            float inner = baseRadius + (float)band * 4.0f;
            DrawRing(center, inner, inner + 3.5f, 205.0f, 335.0f, 96,
                     Fade(colors[band], Clamp(rainbow * 0.34f, 0.0f, 0.36f)));
        }
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
