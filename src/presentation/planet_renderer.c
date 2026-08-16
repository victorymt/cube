#include "presentation/planet_renderer.h"

#include "space/planet_material.h"

#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stdlib.h>

#define PLANET_RENDER_STRINGIFY_IMPL(value) #value
#define PLANET_RENDER_STRINGIFY(value) PLANET_RENDER_STRINGIFY_IMPL(value)

typedef struct PlanetRendererResources {
    bool initialized;
    bool lightingReady;
    bool atmosphereReady;
    Model sphere;
    Shader lightingShader;
    Shader atmosphereShader;
    int lightCountLoc;
    int lightPositionLoc;
    int lightColorLoc;
    int lightIntensityLoc;
    int cameraPositionLoc;
    int ambientLightLoc;
    int emissiveStrengthLoc;
    int exposureLoc;
    int materialRoughnessLoc;
    int materialSpecularLoc;
    int materialMetallicLoc;
    int materialModelLoc;
    int materialMapLoc;
    int materialMapEnabledLoc;
    int cloudShadowMapLoc;
    int cloudShadowEnabledLoc;
    int cloudShadowRotationLoc;
    int cloudShadowStrengthLoc;
    int ringShadowEnabledLoc;
    int ringShadowCenterLoc;
    int ringShadowNormalLoc;
    int ringShadowRadiiLoc;
    int ringShadowParamsLoc;
    int atmosphereLightCountLoc;
    int atmosphereLightPositionLoc;
    int atmosphereLightColorLoc;
    int atmosphereLightIntensityLoc;
    int atmosphereCameraPositionLoc;
    int atmosphereRayleighColorLoc;
    int atmosphereHorizonColorLoc;
    int atmosphereOpticalDepthLoc;
    int atmosphereMieStrengthLoc;
    int atmosphereAlphaLoc;
    int atmosphereExposureLoc;
} PlanetRendererResources;

static PlanetRendererResources renderer = { 0 };

static const char *planetLightingVertexShader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "out vec3 fragPosition;\n"
    "out vec3 fragNormal;\n"
    "void main()\n"
    "{\n"
    "    vec4 worldPosition = matModel*vec4(vertexPosition, 1.0);\n"
    "    fragPosition = worldPosition.xyz;\n"
    "    fragNormal = normalize((matModel*vec4(vertexNormal, 0.0)).xyz);\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragColor = vertexColor;\n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *planetLightingFragmentShader =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "in vec3 fragPosition;\n"
    "in vec3 fragNormal;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform int lightCount;\n"
    "uniform vec3 lightPosition[3];\n"
    "uniform vec3 lightColor[3];\n"
    "uniform float lightIntensity[3];\n"
    "uniform vec3 cameraPosition;\n"
    "uniform float ambientLight;\n"
    "uniform float emissiveStrength;\n"
    "uniform float sceneExposure;\n"
    "uniform float materialRoughness;\n"
    "uniform float materialSpecular;\n"
    "uniform float materialMetallic;\n"
    "uniform int materialModel;\n"
    "uniform sampler2D materialMap;\n"
    "uniform int materialMapEnabled;\n"
    "uniform sampler2D cloudShadowMap;\n"
    "uniform int cloudShadowEnabled;\n"
    "uniform float cloudShadowRotation;\n"
    "uniform float cloudShadowStrength;\n"
    "uniform int ringShadowEnabled;\n"
    "uniform vec3 ringShadowCenter;\n"
    "uniform vec3 ringShadowNormal;\n"
    "uniform vec2 ringShadowRadii;\n"
    "uniform vec2 ringShadowParams;\n"
    "out vec4 finalColor;\n"
    "vec2 cloudUvForNormal(vec3 normal)\n"
    "{\n"
    "    float c = cos(cloudShadowRotation);\n"
    "    float s = sin(cloudShadowRotation);\n"
    "    vec3 localNormal = vec3(c*normal.x - s*normal.z, normal.y,\n"
    "                             s*normal.x + c*normal.z);\n"
    "    float longitude = atan(localNormal.z, localNormal.x);\n"
    "    float latitude = asin(clamp(localNormal.y, -1.0, 1.0));\n"
    "    float u = fract(longitude/6.28318530718);\n"
    "    if (u < 0.0) u += 1.0;\n"
    "    return vec2(u, 0.5 - latitude/3.14159265359);\n"
    "}\n"
    "float ringDensityAt(float radialFraction)\n"
    "{\n"
    "    float phase = ringShadowParams.x;\n"
    "    float broad = 0.5 + 0.5*sin(radialFraction*29.0 + phase);\n"
    "    float fine = 0.5 + 0.5*sin(radialFraction*73.0 + phase*1.73);\n"
    "    float gapCenterA = 0.22 + 0.18*(0.5 + 0.5*sin(phase*0.71));\n"
    "    float gapCenterB = 0.62 + 0.18*(0.5 + 0.5*sin(phase*1.13 + 1.7));\n"
    "    float gapACoord = (radialFraction - gapCenterA)/0.028;\n"
    "    float gapBCoord = (radialFraction - gapCenterB)/0.045;\n"
    "    float gapA = exp(-gapACoord*gapACoord);\n"
    "    float gapB = exp(-gapBCoord*gapBCoord);\n"
    "    return clamp(0.10 + broad*0.54 + fine*0.24 -\n"
    "                 max(gapA, gapB)*0.72, 0.008, 0.94);\n"
    "}\n"
    "float ringShadowOpacity(vec3 surfacePoint, vec3 lightDirection)\n"
    "{\n"
    "    if (ringShadowEnabled == 0) return 0.0;\n"
    "    vec3 ringNormal = normalize(ringShadowNormal);\n"
    "    float denominator = dot(lightDirection, ringNormal);\n"
    "    if (abs(denominator) < 0.0005) return 0.0;\n"
    "    float distanceToPlane = -dot(surfacePoint - ringShadowCenter, ringNormal)/\n"
    "                            denominator;\n"
    "    if (distanceToPlane <= 0.0) return 0.0;\n"
    "    vec3 hit = surfacePoint + lightDirection*distanceToPlane;\n"
    "    vec3 offset = hit - ringShadowCenter;\n"
    "    vec3 planar = offset - ringNormal*dot(offset, ringNormal);\n"
    "    float radialDistance = length(planar);\n"
    "    float ringWidth = max(ringShadowRadii.y - ringShadowRadii.x, 0.001);\n"
    "    float radialFraction = (radialDistance - ringShadowRadii.x)/ringWidth;\n"
    "    if (radialFraction < 0.0 || radialFraction > 1.0) return 0.0;\n"
    "    float sampledFraction = (floor(clamp(radialFraction, 0.0, 0.9999)*24.0) +\n"
    "                             0.5)/24.0;\n"
    "    return ringDensityAt(sampledFraction)*ringShadowParams.y;\n"
    "}\n"
    "float distributionGgx(float nDotH, float roughness)\n"
    "{\n"
    "    float alpha = roughness*roughness;\n"
    "    float alphaSquared = alpha*alpha;\n"
    "    float denominator = nDotH*nDotH*(alphaSquared - 1.0) + 1.0;\n"
    "    return alphaSquared/max(3.14159265359*denominator*denominator, 0.0001);\n"
    "}\n"
    "float geometrySchlickGgx(float nDot, float roughness)\n"
    "{\n"
    "    float k = (roughness + 1.0);\n"
    "    k = k*k/8.0;\n"
    "    return nDot/max(nDot*(1.0 - k) + k, 0.0001);\n"
    "}\n"
    "float geometrySmith(float nDotV, float nDotL, float roughness)\n"
    "{\n"
    "    return geometrySchlickGgx(nDotV, roughness)*\n"
    "           geometrySchlickGgx(nDotL, roughness);\n"
    "}\n"
    "vec3 fresnelSchlick(float cosine, vec3 f0)\n"
    "{\n"
    "    return f0 + (vec3(1.0) - f0)*pow(1.0 - clamp(cosine, 0.0, 1.0), 5.0);\n"
    "}\n"
    "void main()\n"
    "{\n"
    "    vec4 texel = texture(texture0, fragTexCoord)*colDiffuse*fragColor;\n"
    "    if (texel.a < 0.005) discard;\n"
    "    vec3 baseColor = max(texel.rgb, vec3(0.0));\n"
    "    vec3 normal = normalize(fragNormal);\n"
    "    vec3 viewDirection = normalize(cameraPosition - fragPosition);\n"
    "    float nDotV = max(dot(normal, viewDirection), 0.001);\n"
    "    float roughness = clamp(materialRoughness, 0.045, 1.0);\n"
    "    float specularStrength = clamp(materialSpecular, 0.0, 1.0);\n"
    "    float emissionMask = 1.0;\n"
    "    int surfaceType = 0;\n"
    "    if (materialMapEnabled != 0)\n"
    "    {\n"
    "        vec4 materialSample = texture(materialMap, fragTexCoord);\n"
    "        roughness = clamp(materialSample.r, 0.045, 1.0);\n"
    "        specularStrength = clamp(materialSample.g, 0.0, 1.0);\n"
    "        emissionMask = clamp(materialSample.b, 0.0, 1.0);\n"
    "        ivec2 materialSize = textureSize(materialMap, 0);\n"
    "        vec2 wrappedMaterialUv = fract(fragTexCoord);\n"
    "        ivec2 typeCoord = ivec2(wrappedMaterialUv*vec2(materialSize));\n"
    "        float encodedSurfaceType = texelFetch(materialMap, typeCoord, 0).a;\n"
    "        surfaceType = int(floor(encodedSurfaceType*"
        PLANET_RENDER_STRINGIFY(PLANET_SURFACE_TYPE_MAX_VALUE)
        ".0 + 0.5));\n"
    "    }\n"
    "    float metallic = clamp(materialMetallic, 0.0, 1.0);\n"
    "    if (materialModel == 4)\n"
    "    {\n"
    "        float band = 0.5 + 0.5*sin(normal.y*56.0 + baseColor.r*4.0);\n"
    "        roughness = mix(roughness, roughness*0.70, band*0.35);\n"
    "    }\n"
    "    vec3 dielectricF0 = vec3(0.02 + 0.02*specularStrength);\n"
    "    vec3 f0 = mix(dielectricF0, baseColor, metallic);\n"
    "    vec3 reflectedLight = vec3(0.0);\n"
    "    for (int i = 0; i < 3; i++)\n"
    "    {\n"
    "        if (i >= lightCount) break;\n"
    "        vec3 lightResponse = vec3(0.0);\n"
    "        vec3 lightDirection = normalize(lightPosition[i] - fragPosition);\n"
    "        float incidence = dot(normal, lightDirection);\n"
    "        float daylight = smoothstep(-0.025, 0.055, incidence);\n"
    "        float directLight = max(incidence, 0.0)*daylight;\n"
    "        float nDotL = max(incidence, 0.0);\n"
    "        if (nDotL > 0.0001)\n"
    "        {\n"
    "            vec3 halfway = normalize(lightDirection + viewDirection);\n"
    "            float nDotH = max(dot(normal, halfway), 0.0);\n"
    "            float hDotV = max(dot(halfway, viewDirection), 0.0);\n"
    "            float distribution = distributionGgx(nDotH, roughness);\n"
    "            float geometry = geometrySmith(nDotV, nDotL, roughness);\n"
    "            vec3 fresnel = fresnelSchlick(hDotV, f0);\n"
    "            vec3 specular = distribution*geometry*fresnel /\n"
    "                            max(4.0*nDotV*nDotL, 0.001);\n"
    "            vec3 diffuse = (vec3(1.0) - fresnel)*(1.0 - metallic)*\n"
    "                           baseColor/3.14159265359;\n"
    "            lightResponse = diffuse + specular*specularStrength;\n"
    "        }\n"
    "        if (cloudShadowEnabled != 0 && directLight > 0.001)\n"
    "        {\n"
    "            float shellHeight = 0.014;\n"
    "            float projection = -incidence + sqrt(max(0.0,\n"
    "                incidence*incidence + shellHeight*(2.0 + shellHeight)));\n"
    "            vec3 shadowNormal = normalize(normal + lightDirection*projection);\n"
    "            float cloudOpacity = texture(cloudShadowMap,\n"
    "                                         cloudUvForNormal(shadowNormal)).a;\n"
    "            float transmission = 1.0 - cloudOpacity*cloudShadowStrength;\n"
    "            directLight *= clamp(transmission, 0.08, 1.0);\n"
    "        }\n"
    "        float ringOpacity = ringShadowOpacity(fragPosition, lightDirection);\n"
    "        directLight *= clamp(1.0 - ringOpacity, 0.10, 1.0);\n"
    "        reflectedLight += lightColor[i]*lightIntensity[i]*lightResponse*directLight;\n"
    "    }\n"
    "    float emissionScale = materialMapEnabled != 0 ? 1.0 : emissiveStrength;\n"
    "    vec3 emission = texel.rgb*emissionScale*emissionMask;\n"
    "    vec3 ambient = baseColor*ambientLight*(0.72 + specularStrength*0.18);\n"
    "    vec3 environmentColor = vec3(0.035, 0.055, 0.09);\n"
    "    if (surfaceType == 1) environmentColor = vec3(0.035, 0.090, 0.16);\n"
    "    else if (surfaceType == 2) environmentColor = vec3(0.075, 0.105, 0.14);\n"
    "    else if (surfaceType == 5) environmentColor = vec3(0.085, 0.035, 0.018);\n"
    "    else if (materialModel == 4)\n"
    "        environmentColor = mix(vec3(0.065, 0.085, 0.12), baseColor, 0.16);\n"
    "    float environmentStrength = (0.35 + specularStrength*0.65)*\n"
    "                                (1.05 - roughness*0.55);\n"
    "    vec3 environmentReflection = fresnelSchlick(nDotV, f0)*\n"
    "                                 environmentColor*environmentStrength;\n"
    "    vec3 linearColor = ambient + reflectedLight + environmentReflection + emission;\n"
    "    vec3 mappedColor = vec3(1.0) - exp(-linearColor*sceneExposure);\n"
    "    finalColor = vec4(mappedColor, texel.a);\n"
    "}\n";

static const char *planetAtmosphereVertexShader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec3 vertexNormal;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "out vec3 fragPosition;\n"
    "out vec3 fragNormal;\n"
    "void main()\n"
    "{\n"
    "    vec4 worldPosition = matModel*vec4(vertexPosition, 1.0);\n"
    "    fragPosition = worldPosition.xyz;\n"
    "    fragNormal = normalize((matModel*vec4(vertexNormal, 0.0)).xyz);\n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *planetAtmosphereFragmentShader =
    "#version 330\n"
    "in vec3 fragPosition;\n"
    "in vec3 fragNormal;\n"
    "uniform int lightCount;\n"
    "uniform vec3 lightPosition[3];\n"
    "uniform vec3 lightColor[3];\n"
    "uniform float lightIntensity[3];\n"
    "uniform vec3 cameraPosition;\n"
    "uniform vec3 rayleighColor;\n"
    "uniform vec3 horizonColor;\n"
    "uniform float opticalDepth;\n"
    "uniform float mieStrength;\n"
    "uniform float atmosphereAlpha;\n"
    "uniform float sceneExposure;\n"
    "out vec4 finalColor;\n"
    "float miePhase(float cosine, float anisotropy)\n"
    "{\n"
    "    float g2 = anisotropy*anisotropy;\n"
    "    float denominator = max(1.0 + g2 - 2.0*anisotropy*cosine, 0.025);\n"
    "    return (1.0 - g2)/pow(denominator, 1.5);\n"
    "}\n"
    "void main()\n"
    "{\n"
    "    vec3 normal = normalize(fragNormal);\n"
    "    vec3 viewDirection = normalize(cameraPosition - fragPosition);\n"
    "    float viewFacing = max(dot(normal, viewDirection), 0.0);\n"
    "    float limb = pow(1.0 - viewFacing, 0.68);\n"
    "    float opticalPath = 0.10 + limb*0.90;\n"
    "    vec3 scatterSum = vec3(0.0);\n"
    "    float scatterWeight = 0.0;\n"
    "    for (int i = 0; i < 3; i++)\n"
    "    {\n"
    "        if (i >= lightCount) break;\n"
    "        vec3 lightDirection = normalize(lightPosition[i] - fragPosition);\n"
    "        float incidence = dot(normal, lightDirection);\n"
    "        float daySide = smoothstep(-0.18, 0.10, incidence);\n"
    "        float terminator = exp(-pow(abs(incidence)/0.115, 2.0));\n"
    "        terminator *= smoothstep(-0.24, 0.035, incidence);\n"
    "        float scatterCosine = clamp(dot(-lightDirection, viewDirection), -1.0, 1.0);\n"
    "        float rayleighPhase = 0.55 + 0.45*scatterCosine*scatterCosine;\n"
    "        float forwardMie = min(miePhase(scatterCosine, 0.58)*0.085, 1.8);\n"
    "        float intensity = max(lightIntensity[i], 0.0);\n"
    "        float rayleighWeight = intensity*daySide*(0.48 + rayleighPhase*0.52);\n"
    "        float twilightWeight = intensity*terminator*(0.34 + mieStrength*0.72);\n"
    "        float mieWeight = intensity*daySide*mieStrength*forwardMie*0.16;\n"
    "        vec3 daylightColor = mix(rayleighColor, lightColor[i], 0.20);\n"
    "        vec3 warmColor = mix(vec3(1.0, 0.24, 0.055), horizonColor, 0.34);\n"
    "        warmColor = mix(warmColor, lightColor[i], 0.16);\n"
    "        scatterSum += daylightColor*rayleighWeight;\n"
    "        scatterSum += warmColor*twilightWeight;\n"
    "        scatterSum += lightColor[i]*mieWeight;\n"
    "        scatterWeight += rayleighWeight + twilightWeight + mieWeight;\n"
    "    }\n"
    "    float illuminated = 1.0 - exp(-max(scatterWeight, 0.0)*sceneExposure);\n"
    "    vec3 scatteredColor = scatterWeight > 0.0001\n"
    "        ? scatterSum/scatterWeight : rayleighColor*0.08;\n"
    "    float nightAir = opticalDepth*opticalPath*0.012;\n"
    "    scatteredColor = mix(rayleighColor*0.10, scatteredColor,\n"
    "                         clamp(illuminated, 0.0, 1.0));\n"
    "    float alpha = atmosphereAlpha*opticalDepth*opticalPath;\n"
    "    alpha *= clamp(0.018 + illuminated*0.34 + nightAir, 0.0, 0.72);\n"
    "    alpha *= smoothstep(0.0, 0.08, 1.0 - viewFacing);\n"
    "    if (alpha < 0.002) discard;\n"
    "    finalColor = vec4(scatteredColor, clamp(alpha, 0.0, 0.82));\n"
    "}\n";

#undef PLANET_RENDER_STRINGIFY
#undef PLANET_RENDER_STRINGIFY_IMPL

static Vector3 PlanetSpherePoint(float u, float v)
{
    float longitude = u * 2.0f * PI;
    float latitude = (0.5f - v) * PI;
    float cosLatitude = cosf(latitude);
    return (Vector3){ cosLatitude * cosf(longitude), sinf(latitude),
                      cosLatitude * sinf(longitude) };
}

static Mesh MakePlanetSphereMesh(void)
{
    const int rings = 32;
    const int slices = 64;
    const int columns = slices + 1;
    Mesh mesh = { 0 };
    mesh.vertexCount = (rings + 1) * columns;
    mesh.triangleCount = rings * slices * 2;
    mesh.vertices = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.normals = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = malloc((size_t)mesh.vertexCount * 2 * sizeof(float));
    mesh.indices = malloc((size_t)mesh.triangleCount * 3 * sizeof(unsigned short));
    if (!mesh.vertices || !mesh.normals || !mesh.texcoords || !mesh.indices) {
        free(mesh.vertices);
        free(mesh.normals);
        free(mesh.texcoords);
        free(mesh.indices);
        return (Mesh){ 0 };
    }

    for (int ring = 0; ring <= rings; ring++) {
        float v = (float)ring / (float)rings;
        for (int slice = 0; slice <= slices; slice++) {
            float u = (float)slice / (float)slices;
            int vertex = ring * columns + slice;
            Vector3 point = PlanetSpherePoint(u, v);
            mesh.vertices[vertex * 3] = point.x;
            mesh.vertices[vertex * 3 + 1] = point.y;
            mesh.vertices[vertex * 3 + 2] = point.z;
            mesh.normals[vertex * 3] = point.x;
            mesh.normals[vertex * 3 + 1] = point.y;
            mesh.normals[vertex * 3 + 2] = point.z;
            mesh.texcoords[vertex * 2] = u;
            mesh.texcoords[vertex * 2 + 1] = v;
        }
    }

    int index = 0;
    for (int ring = 0; ring < rings; ring++) {
        for (int slice = 0; slice < slices; slice++) {
            unsigned short topLeft = (unsigned short)(ring * columns + slice);
            unsigned short topRight = (unsigned short)(topLeft + 1);
            unsigned short bottomLeft = (unsigned short)(topLeft + columns);
            unsigned short bottomRight = (unsigned short)(bottomLeft + 1);
            mesh.indices[index++] = topLeft;
            mesh.indices[index++] = bottomRight;
            mesh.indices[index++] = bottomLeft;
            mesh.indices[index++] = topLeft;
            mesh.indices[index++] = topRight;
            mesh.indices[index++] = bottomRight;
        }
    }
    UploadMesh(&mesh, false);
    return mesh;
}

static void UnloadPlanetRendererResources(PlanetRendererResources *resources)
{
    if (!resources) return;
    if (resources->sphere.meshCount > 0) UnloadModel(resources->sphere);
    if (resources->lightingShader.id != 0) UnloadShader(resources->lightingShader);
    if (resources->atmosphereShader.id != 0) {
        UnloadShader(resources->atmosphereShader);
    }
    *resources = (PlanetRendererResources){ 0 };
}

void PlanetRendererEnsureResources(void)
{
    if (renderer.initialized) return;
    renderer.initialized = true;

    Mesh sphereMesh = MakePlanetSphereMesh();
    if (sphereMesh.vertexCount > 0) renderer.sphere = LoadModelFromMesh(sphereMesh);
    renderer.lightingShader = LoadShaderFromMemory(planetLightingVertexShader,
                                                    planetLightingFragmentShader);
    renderer.lightCountLoc = GetShaderLocation(renderer.lightingShader, "lightCount");
    renderer.lightPositionLoc = GetShaderLocation(renderer.lightingShader,
                                                   "lightPosition[0]");
    renderer.lightColorLoc = GetShaderLocation(renderer.lightingShader,
                                                "lightColor[0]");
    renderer.lightIntensityLoc = GetShaderLocation(renderer.lightingShader,
                                                    "lightIntensity[0]");
    renderer.cameraPositionLoc = GetShaderLocation(renderer.lightingShader,
                                                   "cameraPosition");
    renderer.ambientLightLoc = GetShaderLocation(renderer.lightingShader, "ambientLight");
    renderer.emissiveStrengthLoc = GetShaderLocation(renderer.lightingShader,
                                                      "emissiveStrength");
    renderer.exposureLoc = GetShaderLocation(renderer.lightingShader, "sceneExposure");
    renderer.materialRoughnessLoc = GetShaderLocation(renderer.lightingShader,
                                                       "materialRoughness");
    renderer.materialSpecularLoc = GetShaderLocation(renderer.lightingShader,
                                                      "materialSpecular");
    renderer.materialMetallicLoc = GetShaderLocation(renderer.lightingShader,
                                                      "materialMetallic");
    renderer.materialModelLoc = GetShaderLocation(renderer.lightingShader, "materialModel");
    renderer.materialMapLoc = GetShaderLocation(renderer.lightingShader, "materialMap");
    renderer.materialMapEnabledLoc = GetShaderLocation(renderer.lightingShader,
                                                        "materialMapEnabled");
    renderer.cloudShadowMapLoc = GetShaderLocation(renderer.lightingShader,
                                                    "cloudShadowMap");
    renderer.cloudShadowEnabledLoc = GetShaderLocation(renderer.lightingShader,
                                                        "cloudShadowEnabled");
    renderer.cloudShadowRotationLoc = GetShaderLocation(renderer.lightingShader,
                                                         "cloudShadowRotation");
    renderer.cloudShadowStrengthLoc = GetShaderLocation(renderer.lightingShader,
                                                         "cloudShadowStrength");
    renderer.ringShadowEnabledLoc = GetShaderLocation(renderer.lightingShader,
                                                       "ringShadowEnabled");
    renderer.ringShadowCenterLoc = GetShaderLocation(renderer.lightingShader,
                                                      "ringShadowCenter");
    renderer.ringShadowNormalLoc = GetShaderLocation(renderer.lightingShader,
                                                      "ringShadowNormal");
    renderer.ringShadowRadiiLoc = GetShaderLocation(renderer.lightingShader,
                                                     "ringShadowRadii");
    renderer.ringShadowParamsLoc = GetShaderLocation(renderer.lightingShader,
                                                      "ringShadowParams");
    renderer.lightingReady = renderer.lightingShader.id != 0 &&
                             renderer.lightCountLoc >= 0 &&
                             renderer.lightPositionLoc >= 0 &&
                             renderer.lightColorLoc >= 0 &&
                             renderer.lightIntensityLoc >= 0 &&
                             renderer.cameraPositionLoc >= 0 &&
                             renderer.ambientLightLoc >= 0 &&
                             renderer.emissiveStrengthLoc >= 0 &&
                             renderer.exposureLoc >= 0 &&
                             renderer.materialRoughnessLoc >= 0 &&
                             renderer.materialSpecularLoc >= 0 &&
                             renderer.materialMetallicLoc >= 0 &&
                             renderer.materialModelLoc >= 0 &&
                             renderer.materialMapLoc >= 0 &&
                             renderer.materialMapEnabledLoc >= 0 &&
                             renderer.cloudShadowMapLoc >= 0 &&
                             renderer.cloudShadowEnabledLoc >= 0 &&
                             renderer.cloudShadowRotationLoc >= 0 &&
                             renderer.cloudShadowStrengthLoc >= 0 &&
                             renderer.ringShadowEnabledLoc >= 0 &&
                             renderer.ringShadowCenterLoc >= 0 &&
                             renderer.ringShadowNormalLoc >= 0 &&
                             renderer.ringShadowRadiiLoc >= 0 &&
                             renderer.ringShadowParamsLoc >= 0;
    if (renderer.lightingReady && renderer.sphere.materialCount > 0) {
        renderer.sphere.materials[0].shader = renderer.lightingShader;
    }

    renderer.atmosphereShader = LoadShaderFromMemory(planetAtmosphereVertexShader,
                                                      planetAtmosphereFragmentShader);
    renderer.atmosphereLightCountLoc = GetShaderLocation(renderer.atmosphereShader,
                                                          "lightCount");
    renderer.atmosphereLightPositionLoc = GetShaderLocation(renderer.atmosphereShader,
                                                             "lightPosition[0]");
    renderer.atmosphereLightColorLoc = GetShaderLocation(renderer.atmosphereShader,
                                                          "lightColor[0]");
    renderer.atmosphereLightIntensityLoc = GetShaderLocation(renderer.atmosphereShader,
                                                              "lightIntensity[0]");
    renderer.atmosphereCameraPositionLoc = GetShaderLocation(renderer.atmosphereShader,
                                                              "cameraPosition");
    renderer.atmosphereRayleighColorLoc = GetShaderLocation(renderer.atmosphereShader,
                                                             "rayleighColor");
    renderer.atmosphereHorizonColorLoc = GetShaderLocation(renderer.atmosphereShader,
                                                            "horizonColor");
    renderer.atmosphereOpticalDepthLoc = GetShaderLocation(renderer.atmosphereShader,
                                                            "opticalDepth");
    renderer.atmosphereMieStrengthLoc = GetShaderLocation(renderer.atmosphereShader,
                                                           "mieStrength");
    renderer.atmosphereAlphaLoc = GetShaderLocation(renderer.atmosphereShader,
                                                     "atmosphereAlpha");
    renderer.atmosphereExposureLoc = GetShaderLocation(renderer.atmosphereShader,
                                                        "sceneExposure");
    renderer.atmosphereReady = renderer.atmosphereShader.id != 0 &&
                               renderer.atmosphereLightCountLoc >= 0 &&
                               renderer.atmosphereLightPositionLoc >= 0 &&
                               renderer.atmosphereLightColorLoc >= 0 &&
                               renderer.atmosphereLightIntensityLoc >= 0 &&
                               renderer.atmosphereCameraPositionLoc >= 0 &&
                               renderer.atmosphereRayleighColorLoc >= 0 &&
                               renderer.atmosphereHorizonColorLoc >= 0 &&
                               renderer.atmosphereOpticalDepthLoc >= 0 &&
                               renderer.atmosphereMieStrengthLoc >= 0 &&
                               renderer.atmosphereAlphaLoc >= 0 &&
                               renderer.atmosphereExposureLoc >= 0;

    if (renderer.sphere.meshCount <= 0 || renderer.lightingShader.id == 0 ||
        renderer.atmosphereShader.id == 0) {
        UnloadPlanetRendererResources(&renderer);
    }
}

void PlanetRendererShutdown(void)
{
    if (!renderer.initialized) return;
    UnloadPlanetRendererResources(&renderer);
}

void PlanetRendererDrawSurface(const PlanetSurfaceDrawParams *params)
{
    if (!params) return;
    PlanetRendererEnsureResources();
    if (renderer.sphere.meshCount <= 0 || params->textures.albedo.id == 0) {
        DrawSphere(params->center, params->radius, params->fallback);
        return;
    }

    if (renderer.lightingReady) {
        int lightCount = params->lighting ? params->lighting->count : 0;
        SetShaderValue(renderer.lightingShader, renderer.lightCountLoc,
                       &lightCount, SHADER_UNIFORM_INT);
        if (lightCount > 0) {
            SetShaderValueV(renderer.lightingShader, renderer.lightPositionLoc,
                            params->lighting->positions, SHADER_UNIFORM_VEC3, lightCount);
            SetShaderValueV(renderer.lightingShader, renderer.lightColorLoc,
                            params->lighting->colors, SHADER_UNIFORM_VEC3, lightCount);
            SetShaderValueV(renderer.lightingShader, renderer.lightIntensityLoc,
                            params->lighting->intensities, SHADER_UNIFORM_FLOAT, lightCount);
        }

        PlanetMaterialResponse defaultMaterial = {
            .roughness = 0.78f,
            .specular = 0.24f,
            .metallic = 0.0f,
            .model = 0
        };
        const PlanetMaterialResponse *material = params->material ?
                                                    params->material : &defaultMaterial;
        SetShaderValue(renderer.lightingShader, renderer.cameraPositionLoc,
                       &params->cameraPosition, SHADER_UNIFORM_VEC3);
        SetShaderValue(renderer.lightingShader, renderer.ambientLightLoc,
                       &params->ambientLight, SHADER_UNIFORM_FLOAT);
        SetShaderValue(renderer.lightingShader, renderer.emissiveStrengthLoc,
                       &params->emissiveStrength, SHADER_UNIFORM_FLOAT);
        float exposure = params->lighting && params->lighting->exposure > 0.0f ?
                             params->lighting->exposure : params->sceneExposure;
        if (exposure <= 0.0f) exposure = 1.12f;
        SetShaderValue(renderer.lightingShader, renderer.exposureLoc,
                       &exposure, SHADER_UNIFORM_FLOAT);
        SetShaderValue(renderer.lightingShader, renderer.materialRoughnessLoc,
                       &material->roughness, SHADER_UNIFORM_FLOAT);
        SetShaderValue(renderer.lightingShader, renderer.materialSpecularLoc,
                       &material->specular, SHADER_UNIFORM_FLOAT);
        SetShaderValue(renderer.lightingShader, renderer.materialMetallicLoc,
                       &material->metallic, SHADER_UNIFORM_FLOAT);
        SetShaderValue(renderer.lightingShader, renderer.materialModelLoc,
                       &material->model, SHADER_UNIFORM_INT);

        int materialMapEnabled = params->textures.material.id != 0;
        SetShaderValue(renderer.lightingShader, renderer.materialMapEnabledLoc,
                       &materialMapEnabled, SHADER_UNIFORM_INT);
        if (materialMapEnabled) {
            SetShaderValueTexture(renderer.lightingShader, renderer.materialMapLoc,
                                  params->textures.material);
        }

        int cloudShadowEnabled = params->cloudLayer &&
                                 params->cloudLayer->texture.id != 0 &&
                                 params->cloudLayer->shadowStrength > 0.001f;
        float cloudShadowRotation = cloudShadowEnabled ?
                                        params->cloudLayer->rotation * DEG2RAD : 0.0f;
        float cloudShadowStrength = cloudShadowEnabled ?
                                        params->cloudLayer->shadowStrength : 0.0f;
        SetShaderValue(renderer.lightingShader, renderer.cloudShadowEnabledLoc,
                       &cloudShadowEnabled, SHADER_UNIFORM_INT);
        SetShaderValue(renderer.lightingShader, renderer.cloudShadowRotationLoc,
                       &cloudShadowRotation, SHADER_UNIFORM_FLOAT);
        SetShaderValue(renderer.lightingShader, renderer.cloudShadowStrengthLoc,
                       &cloudShadowStrength, SHADER_UNIFORM_FLOAT);
        if (cloudShadowEnabled) {
            SetShaderValueTexture(renderer.lightingShader, renderer.cloudShadowMapLoc,
                                  params->cloudLayer->texture);
        }

        int ringShadowEnabled = params->ringLayer &&
                                params->ringLayer->shadowParams.y > 0.001f;
        SetShaderValue(renderer.lightingShader, renderer.ringShadowEnabledLoc,
                       &ringShadowEnabled, SHADER_UNIFORM_INT);
        if (ringShadowEnabled) {
            SetShaderValue(renderer.lightingShader, renderer.ringShadowCenterLoc,
                           &params->ringLayer->center, SHADER_UNIFORM_VEC3);
            SetShaderValue(renderer.lightingShader, renderer.ringShadowNormalLoc,
                           &params->ringLayer->normal, SHADER_UNIFORM_VEC3);
            SetShaderValue(renderer.lightingShader, renderer.ringShadowRadiiLoc,
                           &params->ringLayer->radii, SHADER_UNIFORM_VEC2);
            SetShaderValue(renderer.lightingShader, renderer.ringShadowParamsLoc,
                           &params->ringLayer->shadowParams, SHADER_UNIFORM_VEC2);
        }
    }

    SetMaterialTexture(&renderer.sphere.materials[0], MATERIAL_MAP_DIFFUSE,
                       params->textures.albedo);
    DrawModelEx(renderer.sphere, params->center, (Vector3){ 0.0f, 1.0f, 0.0f },
                params->rotation,
                (Vector3){ params->radius, params->radius, params->radius }, WHITE);
}

static Vector3 PlanetShaderColor(Color color)
{
    return (Vector3){ (float)color.r / 255.0f,
                      (float)color.g / 255.0f,
                      (float)color.b / 255.0f };
}

void PlanetRendererDrawAtmosphere(const PlanetAtmosphereDrawParams *params)
{
    if (!params || params->alpha <= 0.0f) return;
    PlanetRendererEnsureResources();
    if (!renderer.atmosphereReady || renderer.sphere.meshCount <= 0) return;

    int lightCount = params->lighting ? params->lighting->count : 0;
    SetShaderValue(renderer.atmosphereShader, renderer.atmosphereLightCountLoc,
                   &lightCount, SHADER_UNIFORM_INT);
    if (lightCount > 0) {
        SetShaderValueV(renderer.atmosphereShader,
                        renderer.atmosphereLightPositionLoc,
                        params->lighting->positions, SHADER_UNIFORM_VEC3, lightCount);
        SetShaderValueV(renderer.atmosphereShader,
                        renderer.atmosphereLightColorLoc,
                        params->lighting->colors, SHADER_UNIFORM_VEC3, lightCount);
        SetShaderValueV(renderer.atmosphereShader,
                        renderer.atmosphereLightIntensityLoc,
                        params->lighting->intensities, SHADER_UNIFORM_FLOAT, lightCount);
    }

    Vector3 rayleighColor = PlanetShaderColor(params->rayleighColor);
    Vector3 horizonColor = PlanetShaderColor(params->horizonColor);
    float strength = params->alpha * Clamp(0.48f + params->opticalDepth * 0.52f,
                                           0.0f, 1.0f);
    SetShaderValue(renderer.atmosphereShader, renderer.atmosphereCameraPositionLoc,
                   &params->cameraPosition, SHADER_UNIFORM_VEC3);
    SetShaderValue(renderer.atmosphereShader, renderer.atmosphereRayleighColorLoc,
                   &rayleighColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(renderer.atmosphereShader, renderer.atmosphereHorizonColorLoc,
                   &horizonColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(renderer.atmosphereShader, renderer.atmosphereOpticalDepthLoc,
                   &params->opticalDepth, SHADER_UNIFORM_FLOAT);
    SetShaderValue(renderer.atmosphereShader, renderer.atmosphereMieStrengthLoc,
                   &params->mieStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(renderer.atmosphereShader, renderer.atmosphereAlphaLoc,
                   &strength, SHADER_UNIFORM_FLOAT);
    float exposure = params->lighting && params->lighting->exposure > 0.0f ?
                         params->lighting->exposure : params->sceneExposure;
    if (exposure <= 0.0f) exposure = 1.12f;
    SetShaderValue(renderer.atmosphereShader, renderer.atmosphereExposureLoc,
                   &exposure, SHADER_UNIFORM_FLOAT);

    float density = Clamp(params->density, 0.0f, 1.0f);
    float shellRadius = params->radius *
                        (1.028f + params->scaleHeight * 0.025f + density * 0.020f);
    Shader surfaceShader = renderer.sphere.materials[0].shader;
    renderer.sphere.materials[0].shader = renderer.atmosphereShader;
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    DrawModelEx(renderer.sphere, params->center, (Vector3){ 0.0f, 1.0f, 0.0f },
                0.0f, (Vector3){ shellRadius, shellRadius, shellRadius }, WHITE);
    rlEnableDepthMask();
    EndBlendMode();
    renderer.sphere.materials[0].shader = surfaceShader;
}
