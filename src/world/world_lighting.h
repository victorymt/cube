#ifndef VOXELCRAFT_WORLD_LIGHTING_H
#define VOXELCRAFT_WORLD_LIGHTING_H

#include "raylib.h"

#include <stdbool.h>

/* Physical lighting values cross from simulation into presentation by value. */
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

#endif
