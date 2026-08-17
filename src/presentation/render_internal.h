#ifndef VOXELCRAFT_RENDER_INTERNAL_H
#define VOXELCRAFT_RENDER_INTERNAL_H

#include "space/space_types.h"

typedef struct PlanetAtmosphereVisual {
    Color zenith;
    Color horizon;
    Color haze;
    Color groundLight;
    float opticalDepth;
    float mieStrength;
    float scaleHeight;
} PlanetAtmosphereVisual;

bool FindSystemForGuide(Vector3 pos, SolarSystemDef *sys, float *dist);
Color SolarStyleColor(SolarBodyStyle style);
Color PlanetAtmosphereBaseColor(const PlanetProfile *profile);
PlanetAtmosphereVisual PlanetAtmosphereVisualFor(
    const PlanetProfile *profile);
float CelestialRadiusPixels(const Camera3D *camera, Vector3 center,
                            float radius);
extern float planetSceneExposure;
extern Model cloudModel;

#endif
