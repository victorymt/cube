#ifndef VOXELCRAFT_PLANET_RENDERER_H
#define VOXELCRAFT_PLANET_RENDERER_H

#include "space/space_types.h"

typedef struct PlanetTextureSet {
    Texture2D albedo;
    Texture2D material;
} PlanetTextureSet;

typedef struct PlanetSpaceLighting {
    int count;
    Vector3 positions[MAX_SOLAR_LIGHTS];
    Vector3 colors[MAX_SOLAR_LIGHTS];
    float intensities[MAX_SOLAR_LIGHTS];
    float exposure;
} PlanetSpaceLighting;

typedef struct PlanetMaterialResponse {
    float roughness;
    float specular;
    float metallic;
    int model;
} PlanetMaterialResponse;

typedef struct PlanetCloudLayer {
    Texture2D texture;
    float rotation;
    float shadowStrength;
} PlanetCloudLayer;

typedef struct PlanetRingLayer {
    Vector3 center;
    Vector3 normal;
    Vector2 radii;
    Vector2 shadowParams;
} PlanetRingLayer;

typedef struct PlanetSurfaceDrawParams {
    Vector3 center;
    float radius;
    PlanetTextureSet textures;
    float rotation;
    Color fallback;
    Vector3 cameraPosition;
    const PlanetSpaceLighting *lighting;
    const PlanetMaterialResponse *material;
    float ambientLight;
    float emissiveStrength;
    float sceneExposure;
    const PlanetCloudLayer *cloudLayer;
    const PlanetRingLayer *ringLayer;
} PlanetSurfaceDrawParams;

typedef struct PlanetAtmosphereDrawParams {
    Vector3 center;
    float radius;
    Vector3 cameraPosition;
    Color rayleighColor;
    Color horizonColor;
    float density;
    float opticalDepth;
    float mieStrength;
    float scaleHeight;
    float alpha;
    float sceneExposure;
    const PlanetSpaceLighting *lighting;
} PlanetAtmosphereDrawParams;

void PlanetRendererEnsureResources(void);
void PlanetRendererShutdown(void);
void PlanetRendererDrawSurface(const PlanetSurfaceDrawParams *params);
void PlanetRendererDrawAtmosphere(const PlanetAtmosphereDrawParams *params);

#ifdef PLANET_RENDERER_TESTING
typedef struct PlanetRendererTestBackend {
    void (*uploadMesh)(Mesh *mesh, bool dynamic);
    Model (*loadModelFromMesh)(Mesh mesh);
    Shader (*loadShaderFromMemory)(const char *vsCode, const char *fsCode);
    int (*getShaderLocation)(Shader shader, const char *uniformName);
    void (*unloadModel)(Model model);
    void (*unloadShader)(Shader shader);
} PlanetRendererTestBackend;

void PlanetRendererTestSetBackend(const PlanetRendererTestBackend *backend);
bool PlanetRendererTestIsInitialized(void);
#endif

#endif
