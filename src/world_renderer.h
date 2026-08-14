#ifndef VOXELCRAFT_WORLD_RENDERER_H
#define VOXELCRAFT_WORLD_RENDERER_H

#include "types.h"
#include "game_settings.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum WorldMaterialKind {
    WORLD_MATERIAL_OPAQUE = 0,
    WORLD_MATERIAL_WATER,
    WORLD_MATERIAL_GLASS,
    WORLD_MATERIAL_METAL
} WorldMaterialKind;

typedef struct WorldMaterialProfile {
    float roughness;
    float specular;
    float emission;
    WorldMaterialKind kind;
} WorldMaterialProfile;

typedef struct WorldLightingState {
    Vector3 sunDirection;
    Color sunColor;
    Color ambientColor;
    Color fogColor;
    Vector3 cameraPosition;
    float directStrength;
    float ambientStrength;
    float shadowStrength;
    float fogDensity;
    float fogStart;
    float underwaterAmount;
    float underwaterDepth;
    float causticStrength;
    float wetness;
    float exposure;
    float saturation;
    float warmth;
    float waveStrength;
    float time;
    bool shadowsEnabled;
} WorldLightingState;

WorldMaterialProfile WorldMaterialForTexture(BlockTexture texture);
WorldLightingState WorldLightingStateSanitize(WorldLightingState state);

bool WorldRendererInit(GraphicsQuality quality);
bool WorldRendererSetQuality(GraphicsQuality quality);
GraphicsQuality WorldRendererQuality(void);
int WorldRendererShadowChunkRadius(void);
void WorldRendererShutdown(void);
bool WorldRendererIsReady(void);
bool WorldRendererShadowsReady(void);
uint64_t WorldRendererTextureBytes(void);

void WorldRendererPrepare(const WorldLightingState *state);
void WorldRendererDrawModel(const Model *model, Vector3 translation, Color fallbackTint,
                            bool transparent);

bool WorldRendererBeginShadow(const Camera3D *camera,
                              const WorldLightingState *state);
void WorldRendererDrawShadowModel(const Model *model, Vector3 translation);
void WorldRendererEndShadow(void);

#endif
