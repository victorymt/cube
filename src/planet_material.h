#ifndef VOXELCRAFT_PLANET_MATERIAL_H
#define VOXELCRAFT_PLANET_MATERIAL_H

#include "planet_surface.h"

#define PLANET_SURFACE_TYPE_MAX_VALUE 6

// Material texture RGBA stores roughness, specular, emissive, and normalized surface type.
typedef enum PlanetSurfaceType {
    PLANET_SURFACE_GENERIC = 0,
    PLANET_SURFACE_OCEAN,
    PLANET_SURFACE_ICE,
    PLANET_SURFACE_ROCK,
    PLANET_SURFACE_SAND,
    PLANET_SURFACE_LAVA,
    PLANET_SURFACE_GAS,
    PLANET_SURFACE_TYPE_COUNT = PLANET_SURFACE_TYPE_MAX_VALUE + 1
} PlanetSurfaceType;

unsigned char PlanetColorChannel(float value);
float PlanetLavaFissure(const PlanetSurfaceSample *surface);
PlanetSurfaceType PlanetSurfaceTypeFor(const PlanetProfile *profile,
                                       const PlanetSurfaceSample *surface);
Color PlanetMaterialPixel(const PlanetProfile *profile,
                          const PlanetSurfaceSample *surface);

#endif
