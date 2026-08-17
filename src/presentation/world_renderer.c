#include "presentation/world_renderer.h"

#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stddef.h>

#define WORLD_SHADOW_SPAN 128.0f
#define WORLD_SHADOW_DISTANCE 300.0f

typedef struct WorldRendererResources {
    Shader surfaceShader;
    Shader waterShader;
    Shader shadowShader;
    Texture2D materialAtlas;
    Texture2D normalAtlas;
    RenderTexture2D shadowTarget;
    Matrix lightMatrix;
    WorldLightingState state;
    GraphicsQuality quality;
    GraphicsQualityProfile qualityProfile;
    int shadowMapSize;
    int frame;
    bool surfaceReady;
    bool waterReady;
    bool shadowReady;
    bool shadowPassActive;
} WorldRendererResources;

static WorldRendererResources resources = { 0 };

static const char *worldVertexShader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec2 vertexTexCoord2;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 lightMatrix;\n"
    "out vec2 fragTexCoord;\n"
    "out vec2 fragLighting;\n"
    "out vec4 fragColor;\n"
    "out vec3 fragPosition;\n"
    "out vec3 fragNormal;\n"
    "out vec4 fragLightPosition;\n"
    "void main() {\n"
    "    vec4 world = matModel*vec4(vertexPosition, 1.0);\n"
    "    fragPosition = world.xyz;\n"
    "    fragNormal = normalize((matModel*vec4(vertexNormal, 0.0)).xyz);\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragLighting = vertexTexCoord2;\n"
    "    fragColor = vertexColor;\n"
    "    fragLightPosition = lightMatrix*world;\n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *worldFragmentShader =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec2 fragLighting;\n"
    "in vec4 fragColor;\n"
    "in vec3 fragPosition;\n"
    "in vec3 fragNormal;\n"
    "in vec4 fragLightPosition;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D materialMap;\n"
    "uniform sampler2D normalMap;\n"
    "uniform sampler2D shadowMap;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 sunDirection;\n"
    "uniform vec3 sunColor;\n"
    "uniform vec3 ambientColor;\n"
    "uniform vec3 fogColor;\n"
    "uniform vec3 cameraPosition;\n"
    "uniform float directStrength;\n"
    "uniform float ambientStrength;\n"
    "uniform float shadowStrength;\n"
    "uniform float fogDensity;\n"
    "uniform float fogStart;\n"
    "uniform float wetness;\n"
    "uniform float exposure;\n"
    "uniform float saturation;\n"
    "uniform float warmth;\n"
    "uniform float sceneTime;\n"
    "uniform float underwaterAmount;\n"
    "uniform float underwaterDepth;\n"
    "uniform float causticStrength;\n"
    "uniform int shadowEnabled;\n"
    "uniform vec2 shadowTexelSize;\n"
    "out vec4 finalColor;\n"
    "float unpackDepth(vec3 encodedDepth) {\n"
    "    return dot(encodedDepth, vec3(1.0, 1.0/255.0, 1.0/65025.0));\n"
    "}\n"
    "mat3 cotangentFrame(vec3 n, vec3 p, vec2 uv) {\n"
    "    vec3 dp1 = dFdx(p); vec3 dp2 = dFdy(p);\n"
    "    vec2 duv1 = dFdx(uv); vec2 duv2 = dFdy(uv);\n"
    "    vec3 dp2perp = cross(dp2, n);\n"
    "    vec3 dp1perp = cross(n, dp1);\n"
    "    vec3 t = dp2perp*duv1.x + dp1perp*duv2.x;\n"
    "    vec3 b = dp2perp*duv1.y + dp1perp*duv2.y;\n"
    "    float inv = inversesqrt(max(max(dot(t,t), dot(b,b)), 0.0001));\n"
    "    return mat3(t*inv, b*inv, n);\n"
    "}\n"
    "float sampleShadow(vec3 normal) {\n"
    "    if (shadowEnabled == 0 || fragLightPosition.w <= 0.0) return 1.0;\n"
    "    vec3 coord = fragLightPosition.xyz/fragLightPosition.w;\n"
    "    coord = coord*0.5 + 0.5;\n"
    "    if (coord.x <= 0.0 || coord.x >= 1.0 || coord.y <= 0.0 ||\n"
    "        coord.y >= 1.0 || coord.z <= 0.0 || coord.z >= 1.0) return 1.0;\n"
    "    float slope = 1.0 - max(dot(normal, sunDirection), 0.0);\n"
    "    float bias = 0.00020 + slope*0.00055;\n"
    "    float visible = 0.0;\n"
    "    for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {\n"
    "        vec3 encodedDepth = texture(shadowMap, coord.xy + vec2(x,y)*shadowTexelSize).rgb;\n"
    "        visible += coord.z - bias <= unpackDepth(encodedDepth) ? 1.0 : 0.0;\n"
    "    }\n"
    "    return mix(1.0 - shadowStrength, 1.0, visible/9.0);\n"
    "}\n"
    "void main() {\n"
    "    vec4 texel = texture(texture0, fragTexCoord)*fragColor*colDiffuse;\n"
    "    if (texel.a < 0.08) discard;\n"
    "    vec4 material = texture(materialMap, fragTexCoord);\n"
    "    vec3 sampledNormal = texture(normalMap, fragTexCoord).xyz*2.0 - 1.0;\n"
    "    vec3 geometricNormal = normalize(fragNormal);\n"
    "    vec3 normal = normalize(cotangentFrame(geometricNormal, fragPosition, fragTexCoord)*sampledNormal);\n"
    "    float ao = fragLighting.x > 0.01 ? clamp(fragLighting.x, 0.42, 1.0) : 1.0;\n"
    "    float localLight = clamp(fragLighting.y, 0.0, 1.5);\n"
    "    float roughness = clamp(material.r - wetness*0.18, 0.08, 1.0);\n"
    "    float specularStrength = clamp(material.g + wetness*0.22, 0.0, 1.0);\n"
    "    float metallic = step(0.70, material.a);\n"
    "    vec3 viewDir = normalize(cameraPosition - fragPosition);\n"
    "    vec3 lightDir = normalize(sunDirection);\n"
    "    float nDotL = max(dot(normal, lightDir), 0.0);\n"
    "    float shadow = sampleShadow(geometricNormal);\n"
    "    vec3 halfDir = normalize(lightDir + viewDir);\n"
    "    float specPower = mix(8.0, 128.0, 1.0 - roughness);\n"
    "    float specular = pow(max(dot(normal, halfDir), 0.0), specPower)*specularStrength;\n"
    "    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 5.0);\n"
    "    vec3 base = max(texel.rgb, vec3(0.0));\n"
    "    vec3 diffuse = base*sunColor*directStrength*nDotL*shadow*(1.0 - metallic*0.62);\n"
    "    vec3 specularColor = mix(vec3(0.04), base, metallic);\n"
    "    vec3 highlights = specularColor*sunColor*directStrength*specular*shadow*(0.4 + fresnel);\n"
    "    vec3 ambient = base*ambientColor*ambientStrength*ao;\n"
    "    vec3 torch = base*vec3(1.0, 0.58, 0.24)*localLight*1.55;\n"
    "    vec3 emission = base*material.b*2.4;\n"
    "    vec3 color = ambient + diffuse + highlights + torch + emission;\n"
    "    float causticA = sin(fragPosition.x*1.17 + sceneTime*1.55 + sin(fragPosition.z*0.71));\n"
    "    float causticB = sin(fragPosition.z*1.31 - sceneTime*1.23 + sin(fragPosition.x*0.63));\n"
    "    float caustic = pow(max(causticA*causticB, 0.0), 3.0);\n"
    "    caustic *= max(geometricNormal.y, 0.0)*causticStrength*underwaterAmount;\n"
    "    color += base*vec3(0.20, 0.58, 0.64)*caustic*directStrength;\n"
    "    color *= exposure;\n"
    "    color = color/(color + vec3(1.0));\n"
    "    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));\n"
    "    color = mix(vec3(luma), color, saturation);\n"
    "    color *= mix(vec3(1.0), vec3(1.08, 0.99, 0.90), warmth);\n"
    "    color = pow(color, vec3(1.0/2.2));\n"
    "    float distanceToCamera = length(cameraPosition - fragPosition);\n"
    "    vec3 absorption = exp(-distanceToCamera*underwaterAmount*\n"
    "        vec3(0.052, 0.017, 0.008)*(1.0 + underwaterDepth*0.012));\n"
    "    color *= absorption;\n"
    "    float fogDistance = max(distanceToCamera - fogStart, 0.0);\n"
    "    float fog = 1.0 - exp(-fogDistance*fogDensity);\n"
    "    finalColor = vec4(mix(color, fogColor, clamp(fog, 0.0, 0.96)), texel.a);\n"
    "}\n";

static const char *waterFragmentShader =
    "#version 330\n"
    "in vec2 fragTexCoord; in vec2 fragLighting; in vec4 fragColor;\n"
    "in vec3 fragPosition; in vec3 fragNormal; in vec4 fragLightPosition;\n"
    "uniform sampler2D texture0; uniform sampler2D materialMap;\n"
    "uniform vec4 colDiffuse; uniform vec3 sunDirection; uniform vec3 sunColor;\n"
    "uniform vec3 ambientColor; uniform vec3 fogColor; uniform vec3 cameraPosition;\n"
    "uniform float directStrength; uniform float ambientStrength; uniform float fogDensity;\n"
    "uniform float fogStart; uniform float wetness; uniform float sceneTime;\n"
    "uniform float exposure; uniform float saturation; uniform float warmth;\n"
    "uniform float waveStrength; uniform float underwaterAmount;\n"
    "uniform float underwaterDepth; uniform float causticStrength;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 texel = texture(texture0, fragTexCoord)*fragColor*colDiffuse;\n"
    "    if (texel.a < 0.04) discard;\n"
    "    vec4 material = texture(materialMap, fragTexCoord);\n"
    "    float water = 1.0 - smoothstep(0.06, 0.16, abs(material.a - 0.25));\n"
    "    vec3 normal = normalize(fragNormal);\n"
    "    if (!gl_FrontFacing) normal = -normal;\n"
    "    if (water > 0.5) {\n"
    "        float waveX = sin(fragPosition.x*0.42 + sceneTime*1.25) + sin(fragPosition.z*0.23 - sceneTime*0.71);\n"
    "        float waveZ = cos(fragPosition.z*0.37 + sceneTime*1.04) + cos(fragPosition.x*0.19 + sceneTime*0.63);\n"
    "        normal = normalize(normal + vec3(waveX*waveStrength*0.11, 0.0, waveZ*waveStrength*0.11));\n"
    "    }\n"
    "    vec3 viewDir = normalize(cameraPosition - fragPosition);\n"
    "    vec3 lightDir = normalize(sunDirection);\n"
    "    float fresnel = pow(1.0 - abs(dot(normal, viewDir)), 5.0);\n"
    "    vec3 halfDir = normalize(viewDir + lightDir);\n"
    "    float highlight = pow(max(dot(normal, halfDir), 0.0), mix(72.0, 150.0, water))*directStrength;\n"
    "    vec3 base = texel.rgb*(ambientColor*ambientStrength + 0.34);\n"
    "    vec3 reflected = mix(fogColor, sunColor, highlight)*fresnel;\n"
    "    vec3 color = (base + reflected*0.62 + sunColor*highlight*0.55)*exposure;\n"
    "    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));\n"
    "    color = mix(vec3(luma), color, saturation);\n"
    "    color *= mix(vec3(1.0), vec3(1.08, 0.99, 0.90), warmth);\n"
    "    float distanceToCamera = length(cameraPosition - fragPosition);\n"
    "    float fog = 1.0 - exp(-max(distanceToCamera - fogStart, 0.0)*fogDensity);\n"
    "    float alpha = mix(texel.a, clamp(0.54 + fresnel*0.32, 0.0, 0.92), water);\n"
    "    finalColor = vec4(mix(color, fogColor, clamp(fog, 0.0, 0.94)), alpha);\n"
    "}\n";

static const char *shadowVertexShader =
    "#version 330\n"
    "in vec3 vertexPosition; in vec2 vertexTexCoord;\n"
    "uniform mat4 mvp; out vec2 fragTexCoord;\n"
    "void main() { fragTexCoord = vertexTexCoord; gl_Position = mvp*vec4(vertexPosition,1.0); }\n";

static const char *shadowFragmentShader =
    "#version 330\n"
    "in vec2 fragTexCoord; uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
    "out vec4 finalColor;\n"
    "vec3 packDepth(float depth) {\n"
    "    vec3 encodedDepth = fract(depth*vec3(1.0,255.0,65025.0));\n"
    "    encodedDepth.xy -= encodedDepth.yz/255.0; return encodedDepth;\n"
    "}\n"
    "void main() { if (texture(texture0,fragTexCoord).a*colDiffuse.a < 0.08) discard;\n"
    "    finalColor = vec4(packDepth(gl_FragCoord.z), 1.0); }\n";

static float ClampFinite(float value, float minimum, float maximum, float fallback)
{
    if (!isfinite(value)) return fallback;
    return Clamp(value, minimum, maximum);
}

WorldMaterialProfile WorldMaterialForTexture(BlockTexture texture)
{
    WorldMaterialProfile profile = { 0.82f, 0.10f, 0.0f,
                                     WORLD_MATERIAL_OPAQUE };
    switch (texture) {
    case TEX_WATER:
        return (WorldMaterialProfile){ 0.12f, 0.88f, 0.0f,
                                       WORLD_MATERIAL_WATER };
    case TEX_GLASS:
    case TEX_ICE:
    case TEX_NETHER_PORTAL:
        profile.roughness = texture == TEX_ICE ? 0.24f : 0.10f;
        profile.specular = 0.82f;
        profile.emission = texture == TEX_NETHER_PORTAL ? 0.62f : 0.0f;
        profile.kind = WORLD_MATERIAL_GLASS;
        return profile;
    case TEX_IRON_ORE:
    case TEX_GOLD_ORE:
    case TEX_COPPER_ORE:
    case TEX_TIN_ORE:
    case TEX_SILVER_ORE:
    case TEX_NICKEL_ORE:
    case TEX_METEORITE:
    case TEX_SPACESHIP:
        return (WorldMaterialProfile){ 0.34f, 0.72f, 0.0f,
                                       WORLD_MATERIAL_METAL };
    case TEX_LAVA:
    case TEX_STAR_MATTER:
    case TEX_GLOWSTONE:
    case TEX_TORCH:
        profile.roughness = 0.48f;
        profile.specular = 0.22f;
        profile.emission = texture == TEX_TORCH ? 0.82f : 1.0f;
        return profile;
    case TEX_SNOW:
        profile.roughness = 0.88f;
        profile.specular = 0.22f;
        return profile;
    case TEX_STONE:
    case TEX_BEDROCK:
    case TEX_MOON_ROCK:
    case TEX_NETHERRACK:
    case TEX_STONE_BRICKS:
    case TEX_OBSIDIAN:
    case TEX_GRAVEL:
    case TEX_MOSSY_STONE:
    case TEX_BASALT:
    case TEX_GRANITE:
    case TEX_LIMESTONE:
    case TEX_SHALE:
    case TEX_MARBLE:
    case TEX_PUMICE:
    case TEX_SULFUR_ORE:
    case TEX_CHALK:
    case TEX_GNEISS:
    case TEX_SCORIA:
        profile.roughness = texture == TEX_OBSIDIAN ? 0.30f : 0.90f;
        profile.specular = texture == TEX_OBSIDIAN ? 0.58f : 0.08f;
        return profile;
    case TEX_GRASS_TOP:
    case TEX_GRASS_SIDE:
    case TEX_DIRT:
    case TEX_SAND:
    case TEX_MOON_SAND:
    case TEX_SOUL_SAND:
    case TEX_CLAY:
    case TEX_MUD:
    case TEX_RED_SAND:
    case TEX_PEAT:
    case TEX_PERMAFROST:
    case TEX_ROCK_SALT:
    case TEX_VOLCANIC_ASH:
    case TEX_LOAM:
    case TEX_PODZOL:
    case TEX_SILT:
    case TEX_LATERITE:
    case TEX_REGOLITH:
    case TEX_SALT_CRUST:
        profile.roughness = 0.96f;
        profile.specular = 0.04f;
        return profile;
    case TEX_WOOD_SIDE:
    case TEX_WOOD_TOP:
    case TEX_PLANK:
    case TEX_FENCE:
    case TEX_DOOR:
        profile.roughness = 0.78f;
        profile.specular = 0.08f;
        return profile;
    case TEX_CRYSTAL:
    case TEX_PACKED_ICE:
    case TEX_QUARTZ_ORE:
        profile.roughness = 0.28f;
        profile.specular = 0.64f;
        return profile;
    default:
        return profile;
    }
}

WorldLightingState WorldLightingStateSanitize(WorldLightingState state)
{
    if (!isfinite(state.sunDirection.x) || !isfinite(state.sunDirection.y) ||
        !isfinite(state.sunDirection.z) ||
        Vector3LengthSqr(state.sunDirection) < 0.0001f) {
        state.sunDirection = (Vector3){ 0.3f, 0.9f, 0.2f };
    }
    state.sunDirection = Vector3Normalize(state.sunDirection);
    if (!isfinite(state.cameraPosition.x) || !isfinite(state.cameraPosition.y) ||
        !isfinite(state.cameraPosition.z)) state.cameraPosition = Vector3Zero();
    state.directStrength = ClampFinite(state.directStrength, 0.0f, 4.0f, 0.0f);
    state.ambientStrength = ClampFinite(state.ambientStrength, 0.02f, 2.0f, 0.2f);
    state.shadowStrength = ClampFinite(state.shadowStrength, 0.0f, 0.92f, 0.0f);
    state.fogDensity = ClampFinite(state.fogDensity, 0.0f, 0.20f, 0.0f);
    state.fogStart = ClampFinite(state.fogStart, 0.0f, 10000.0f, 48.0f);
    state.underwaterAmount = ClampFinite(state.underwaterAmount, 0.0f, 1.0f, 0.0f);
    state.underwaterDepth = ClampFinite(state.underwaterDepth, 0.0f, 512.0f, 0.0f);
    state.causticStrength = ClampFinite(state.causticStrength, 0.0f, 1.0f, 0.0f);
    state.wetness = ClampFinite(state.wetness, 0.0f, 1.0f, 0.0f);
    state.exposure = ClampFinite(state.exposure, 0.25f, 2.5f, 1.0f);
    state.saturation = ClampFinite(state.saturation, 0.0f, 1.5f, 1.0f);
    state.warmth = ClampFinite(state.warmth, 0.0f, 1.0f, 0.0f);
    state.waveStrength = ClampFinite(state.waveStrength, 0.0f, 1.0f, 0.18f);
    state.time = ClampFinite(state.time, -1000000.0f, 1000000.0f, 0.0f);
    if (state.shadowStrength <= 0.001f || state.directStrength <= 0.001f) {
        state.shadowsEnabled = false;
    }
    return state;
}

static unsigned char ByteFromUnit(float value)
{
    return (unsigned char)Clamp(value*255.0f + 0.5f, 0.0f, 255.0f);
}

static Texture2D MakeMaterialAtlas(bool normalMap)
{
    int width = ATLAS_CELL_SIZE*ATLAS_COLUMNS;
    int height = ATLAS_CELL_SIZE*ATLAS_ROWS;
    Image image = GenImageColor(width, height,
                                normalMap ? (Color){128,128,255,255} : BLANK);
    if (!image.data) return (Texture2D){ 0 };
    for (int tile = 0; tile < TEX_COUNT; tile++) {
        WorldMaterialProfile profile = WorldMaterialForTexture((BlockTexture)tile);
        int cellX = (tile%ATLAS_COLUMNS)*ATLAS_CELL_SIZE;
        int cellY = (tile/ATLAS_COLUMNS)*ATLAS_CELL_SIZE;
        for (int y = 0; y < ATLAS_CELL_SIZE; y++) {
            for (int x = 0; x < ATLAS_CELL_SIZE; x++) {
                Color pixel;
                if (normalMap) {
                    unsigned int hash = (unsigned int)(tile*73856093u) ^
                                        (unsigned int)(x*19349663u) ^
                                        (unsigned int)(y*83492791u);
                    float amount = profile.roughness*0.055f;
                    if (profile.kind == WORLD_MATERIAL_WATER ||
                        profile.kind == WORLD_MATERIAL_GLASS) amount *= 0.20f;
                    float nx = ((float)(hash & 255u)/255.0f - 0.5f)*amount;
                    float ny = ((float)((hash >> 8) & 255u)/255.0f - 0.5f)*amount;
                    Vector3 normal = Vector3Normalize((Vector3){nx, ny, 1.0f});
                    pixel = (Color){ ByteFromUnit(normal.x*0.5f + 0.5f),
                                     ByteFromUnit(normal.y*0.5f + 0.5f),
                                     ByteFromUnit(normal.z*0.5f + 0.5f), 255 };
                } else {
                    float kind = (float)profile.kind/4.0f;
                    pixel = (Color){ ByteFromUnit(profile.roughness),
                                     ByteFromUnit(profile.specular),
                                     ByteFromUnit(profile.emission),
                                     ByteFromUnit(kind) };
                }
                ImageDrawPixel(&image, cellX + x, cellY + y, pixel);
            }
        }
    }
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (texture.id != 0) {
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
    }
    return texture;
}

static void ConfigureShaderLocations(Shader *shader)
{
    if (!shader || shader->id == 0 || !shader->locs) return;
    shader->locs[SHADER_LOC_VERTEX_TEXCOORD02] =
        GetShaderLocationAttrib(*shader, "vertexTexCoord2");
}

static bool RequiredLocationsPresent(Shader shader)
{
    return shader.id != 0 &&
           GetShaderLocation(shader, "cameraPosition") >= 0 &&
           GetShaderLocation(shader, "sunDirection") >= 0;
}

static void UnloadWorldShader(Shader shader)
{
    if (shader.id != 0) {
        UnloadShader(shader);
    } else if (shader.locs) {
        // A failed LoadShaderFromMemory still allocates its location table.
        MemFree(shader.locs);
    }
}

void WorldRendererShutdown(void)
{
    if (resources.shadowTarget.id != 0) UnloadRenderTexture(resources.shadowTarget);
    if (resources.materialAtlas.id != 0) UnloadTexture(resources.materialAtlas);
    if (resources.normalAtlas.id != 0) UnloadTexture(resources.normalAtlas);
    UnloadWorldShader(resources.surfaceShader);
    UnloadWorldShader(resources.waterShader);
    UnloadWorldShader(resources.shadowShader);
    resources = (WorldRendererResources){ 0 };
}

bool WorldRendererInit(GraphicsQuality quality)
{
    WorldRendererShutdown();
    if (quality < GRAPHICS_QUALITY_LOW || quality >= GRAPHICS_QUALITY_COUNT) {
        quality = GRAPHICS_QUALITY_MEDIUM;
    }
    resources.quality = quality;
    resources.qualityProfile = GraphicsQualityProfileFor(quality);
    resources.surfaceShader = LoadShaderFromMemory(worldVertexShader,
                                                    worldFragmentShader);
    resources.waterShader = LoadShaderFromMemory(worldVertexShader,
                                                  waterFragmentShader);
    resources.shadowShader = LoadShaderFromMemory(shadowVertexShader,
                                                   shadowFragmentShader);
    ConfigureShaderLocations(&resources.surfaceShader);
    ConfigureShaderLocations(&resources.waterShader);
    resources.materialAtlas = MakeMaterialAtlas(false);
    resources.normalAtlas = MakeMaterialAtlas(true);
    resources.surfaceReady = RequiredLocationsPresent(resources.surfaceShader) &&
                             resources.materialAtlas.id != 0 &&
                             resources.normalAtlas.id != 0;
    resources.waterReady = RequiredLocationsPresent(resources.waterShader) &&
                           resources.materialAtlas.id != 0;
    bool shadowShaderReady = resources.shadowShader.id != 0 &&
        GetShaderLocation(resources.shadowShader, "texture0") >= 0;
    if (resources.surfaceReady && shadowShaderReady &&
        resources.qualityProfile.shadowMapSize > 0) {
        resources.shadowMapSize = resources.qualityProfile.shadowMapSize;
        resources.shadowTarget = LoadRenderTexture(resources.shadowMapSize,
                                                    resources.shadowMapSize);
        resources.shadowReady = resources.shadowTarget.id != 0 &&
                                resources.shadowTarget.texture.id != 0;
    }
    if (!resources.surfaceReady) {
        TraceLog(LOG_WARNING, "WORLD: realtime lighting unavailable; using legacy rendering");
    } else if (!resources.shadowReady && resources.qualityProfile.shadowMapSize > 0) {
        TraceLog(LOG_WARNING, "WORLD: shadow map unavailable; using unshadowed lighting");
    }
    return resources.surfaceReady;
}

bool WorldRendererSetQuality(GraphicsQuality quality)
{
    if (quality == resources.quality && resources.surfaceReady) return true;
    GraphicsQuality previous = resources.quality;
    if (WorldRendererInit(quality)) return true;
    (void)WorldRendererInit(previous);
    return false;
}

GraphicsQuality WorldRendererQuality(void) { return resources.quality; }

int WorldRendererShadowChunkRadius(void)
{
    return resources.qualityProfile.shadowChunkRadius;
}

bool WorldRendererIsReady(void) { return resources.surfaceReady; }
bool WorldRendererShadowsReady(void) { return resources.shadowReady; }

uint64_t WorldRendererTextureBytes(void)
{
    uint64_t atlasBytes = (uint64_t)(ATLAS_CELL_SIZE*ATLAS_COLUMNS) *
                          (uint64_t)(ATLAS_CELL_SIZE*ATLAS_ROWS) * 4u;
    uint64_t total = 0;
    if (resources.materialAtlas.id != 0) total += atlasBytes;
    if (resources.normalAtlas.id != 0) total += atlasBytes;
    if (resources.shadowReady) {
        uint64_t pixels = (uint64_t)resources.shadowMapSize *
                          (uint64_t)resources.shadowMapSize;
        total += pixels*8u;
    }
    return total;
}

static Vector3 ColorVector(Color color)
{
    return (Vector3){ color.r/255.0f, color.g/255.0f, color.b/255.0f };
}

static void SetCommonUniforms(Shader shader)
{
    WorldLightingState *state = &resources.state;
    Vector3 sunColor = ColorVector(state->sunColor);
    Vector3 ambient = ColorVector(state->ambientColor);
    Vector3 fog = ColorVector(state->fogColor);
    int shadowEnabled = resources.shadowReady && state->shadowsEnabled ? 1 : 0;
    int shadowMapSize = resources.shadowMapSize > 0 ? resources.shadowMapSize : 1;
    Vector2 shadowTexel = { 1.0f/(float)shadowMapSize,
                            1.0f/(float)shadowMapSize };
    SetShaderValue(shader, GetShaderLocation(shader, "sunDirection"),
                   &state->sunDirection, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "sunColor"),
                   &sunColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "ambientColor"),
                   &ambient, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "fogColor"),
                   &fog, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "cameraPosition"),
                   &state->cameraPosition, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "directStrength"),
                   &state->directStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "ambientStrength"),
                   &state->ambientStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "shadowStrength"),
                   &state->shadowStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "fogDensity"),
                   &state->fogDensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "fogStart"),
                   &state->fogStart, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "underwaterAmount"),
                   &state->underwaterAmount, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "underwaterDepth"),
                   &state->underwaterDepth, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "causticStrength"),
                   &state->causticStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "wetness"),
                   &state->wetness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "exposure"),
                   &state->exposure, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "saturation"),
                   &state->saturation, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "warmth"),
                   &state->warmth, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "waveStrength"),
                   &state->waveStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "sceneTime"),
                   &state->time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "shadowEnabled"),
                   &shadowEnabled, SHADER_UNIFORM_INT);
    SetShaderValue(shader, GetShaderLocation(shader, "shadowTexelSize"),
                   &shadowTexel, SHADER_UNIFORM_VEC2);
    SetShaderValueMatrix(shader, GetShaderLocation(shader, "lightMatrix"),
                         resources.lightMatrix);
    SetShaderValueTexture(shader, GetShaderLocation(shader, "materialMap"),
                          resources.materialAtlas);
    if (shader.id == resources.surfaceShader.id) {
        SetShaderValueTexture(shader, GetShaderLocation(shader, "normalMap"),
                              resources.normalAtlas);
        if (resources.shadowReady) {
            SetShaderValueTexture(shader, GetShaderLocation(shader, "shadowMap"),
                                  resources.shadowTarget.texture);
        }
    }
}

void WorldRendererPrepare(const WorldLightingState *state)
{
    if (!state) return;
    resources.state = WorldLightingStateSanitize(*state);
    if (resources.surfaceReady) SetCommonUniforms(resources.surfaceShader);
    if (resources.waterReady) SetCommonUniforms(resources.waterShader);
}

void WorldRendererDrawModel(const Model *model, Vector3 translation,
                            Color fallbackTint,
                            bool transparent)
{
    if (!model || model->materialCount <= 0 || !model->materials) return;
    Shader shader = transparent && resources.waterReady ? resources.waterShader :
                    resources.surfaceShader;
    if (!resources.surfaceReady || shader.id == 0) {
        DrawModel(*model, translation, 1.0f, fallbackTint);
        return;
    }
    Model drawModel = *model;
    Material material = model->materials[0];
    material.shader = shader;
    drawModel.materials = &material;
    drawModel.materialCount = 1;
    if (transparent) rlDisableBackfaceCulling();
    DrawModel(drawModel, translation, 1.0f, WHITE);
    if (transparent) rlEnableBackfaceCulling();
}

void WorldRendererDrawModelTransformed(const Model *model, Matrix transform,
                                       Color fallbackTint, bool transparent)
{
    if (!model || model->materialCount <= 0 || !model->materials) return;
    Shader shader = transparent && resources.waterReady ? resources.waterShader :
                    resources.surfaceShader;
    Model drawModel = *model;
    drawModel.transform = transform;
    if (!resources.surfaceReady || shader.id == 0) {
        DrawModel(drawModel, Vector3Zero(), 1.0f, fallbackTint);
        return;
    }
    Material material = model->materials[0];
    material.shader = shader;
    drawModel.materials = &material;
    drawModel.materialCount = 1;
    if (transparent) rlDisableBackfaceCulling();
    DrawModel(drawModel, Vector3Zero(), 1.0f, WHITE);
    if (transparent) rlEnableBackfaceCulling();
}

void WorldRendererBeginWaterPass(void)
{
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    rlDisableDepthMask();
}

void WorldRendererEndWaterPass(void)
{
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

bool WorldRendererBeginShadow(const Camera3D *camera,
                              const WorldLightingState *state)
{
    resources.frame++;
    if (!camera || !state || !resources.shadowReady ||
        !state->shadowsEnabled || state->shadowStrength <= 0.001f ||
        resources.qualityProfile.shadowUpdateInterval <= 0 ||
        (resources.frame > 1 &&
         (resources.frame - 1) % resources.qualityProfile.shadowUpdateInterval != 0)) {
        return false;
    }
    resources.state = WorldLightingStateSanitize(*state);
    float texelWorld = WORLD_SHADOW_SPAN/(float)resources.shadowMapSize;
    Vector3 target = camera->position;
    target.x = roundf(target.x/texelWorld)*texelWorld;
    target.z = roundf(target.z/texelWorld)*texelWorld;
    Vector3 lightDirection = resources.state.sunDirection;
    Vector3 up = fabsf(lightDirection.y) > 0.96f ?
                     (Vector3){ 0.0f, 0.0f, 1.0f } :
                     (Vector3){ 0.0f, 1.0f, 0.0f };
    Camera3D lightCamera = {
        .position = Vector3Add(target,
                               Vector3Scale(lightDirection,
                                            WORLD_SHADOW_DISTANCE)),
        .target = target,
        .up = up,
        .fovy = WORLD_SHADOW_SPAN,
        .projection = CAMERA_ORTHOGRAPHIC
    };
    Matrix view = GetCameraMatrix(lightCamera);
    Matrix projection = MatrixOrtho(-WORLD_SHADOW_SPAN*0.5,
                                     WORLD_SHADOW_SPAN*0.5,
                                    -WORLD_SHADOW_SPAN*0.5,
                                     WORLD_SHADOW_SPAN*0.5,
                                     0.05, 4000.0);
    resources.lightMatrix = MatrixMultiply(view, projection);
    BeginTextureMode(resources.shadowTarget);
    ClearBackground(WHITE);
    BeginMode3D(lightCamera);
    resources.shadowPassActive = true;
    return true;
}

void WorldRendererDrawShadowModel(const Model *model, Vector3 translation)
{
    if (!resources.shadowPassActive || !model || model->materialCount <= 0 ||
        !model->materials) return;
    Model drawModel = *model;
    Material material = model->materials[0];
    material.shader = resources.shadowShader;
    drawModel.materials = &material;
    drawModel.materialCount = 1;
    DrawModel(drawModel, translation, 1.0f, WHITE);
}

void WorldRendererDrawShadowModelTransformed(const Model *model,
                                             Matrix transform)
{
    if (!resources.shadowPassActive || !model || model->materialCount <= 0 ||
        !model->materials) return;
    Model drawModel = *model;
    Material material = model->materials[0];
    material.shader = resources.shadowShader;
    drawModel.materials = &material;
    drawModel.materialCount = 1;
    drawModel.transform = transform;
    DrawModel(drawModel, Vector3Zero(), 1.0f, WHITE);
}

void WorldRendererEndShadow(void)
{
    if (!resources.shadowPassActive) return;
    EndMode3D();
    EndTextureMode();
    resources.shadowPassActive = false;
}
