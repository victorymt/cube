#include "planet_material.h"

#include "raymath.h"

#include <math.h>

unsigned char PlanetColorChannel(float value)
{
    return (unsigned char)Clamp(value, 0.0f, 255.0f);
}

float PlanetLavaFissure(const PlanetSurfaceSample *surface)
{
    float fissure = 1.0f - Clamp(fabsf(surface->continentalness - 0.53f) / 0.045f,
                                 0.0f, 1.0f);
    return fmaxf(fissure,
                 1.0f - Clamp(fabsf(surface->detail - 0.66f) / 0.022f, 0.0f, 1.0f));
}

PlanetSurfaceType PlanetSurfaceTypeFor(const PlanetProfile *profile,
                                       const PlanetSurfaceSample *surface)
{
    if (!profile->hasSolidSurface || profile->style == SOLAR_STYLE_GAS ||
        surface->biome == PLANET_BIOME_STORM_BANDS) {
        return PLANET_SURFACE_GAS;
    }
    if (surface->biome == PLANET_BIOME_LAVA_SEA || surface->lavaFlow > 0.48f ||
        (profile->style == SOLAR_STYLE_LAVA && PlanetLavaFissure(surface) > 0.48f)) {
        return PLANET_SURFACE_LAVA;
    }
    if (surface->iceCoverage > 0.34f || surface->glacierFlow > 0.24f ||
        surface->biome == PLANET_BIOME_ICE_SHEET ||
        surface->biome == PLANET_BIOME_GLACIER || profile->style == SOLAR_STYLE_ICE) {
        return PLANET_SURFACE_ICE;
    }

    switch (surface->biome) {
    case PLANET_BIOME_OCEAN:
        return PLANET_SURFACE_OCEAN;
    case PLANET_BIOME_DUNES:
    case PLANET_BIOME_COAST:
        return PLANET_SURFACE_SAND;
    case PLANET_BIOME_OASIS:
        return PLANET_SURFACE_GENERIC;
    case PLANET_BIOME_BADLANDS:
        return surface->duneBand > 0.42f ? PLANET_SURFACE_SAND : PLANET_SURFACE_ROCK;
    case PLANET_BIOME_BASALT_PLAINS:
    case PLANET_BIOME_VOLCANIC_RIDGE:
    case PLANET_BIOME_IMPACT_BASIN:
    case PLANET_BIOME_CRATER_HIGHLANDS:
    case PLANET_BIOME_ALPINE:
        return PLANET_SURFACE_ROCK;
    default:
        break;
    }

    if (profile->style == SOLAR_STYLE_DESERT) return PLANET_SURFACE_SAND;
    if (profile->style == SOLAR_STYLE_CRATER || profile->style == SOLAR_STYLE_LAVA) {
        return PLANET_SURFACE_ROCK;
    }
    return PLANET_SURFACE_GENERIC;
}

Color PlanetMaterialPixel(const PlanetProfile *profile,
                          const PlanetSurfaceSample *surface)
{
    PlanetSurfaceType type = PlanetSurfaceTypeFor(profile, surface);
    float roughness = 0.76f;
    float specular = 0.22f;
    float emissive = 0.0f;

    switch (type) {
    case PLANET_SURFACE_OCEAN:
        roughness = 0.045f + surface->detail * 0.040f;
        specular = 0.90f + surface->detail * 0.06f;
        break;
    case PLANET_SURFACE_ICE:
        roughness = 0.20f + surface->iceCoverage * 0.12f +
                    surface->glacierCracks * 0.24f;
        specular = 0.86f - surface->glacierCracks * 0.24f;
        break;
    case PLANET_SURFACE_ROCK:
        roughness = 0.84f + surface->impactDepth * 0.10f -
                    surface->volcanicActivity * 0.10f;
        specular = 0.14f + surface->volcanicActivity * 0.12f;
        break;
    case PLANET_SURFACE_SAND:
        roughness = 0.86f + surface->duneBand * 0.10f;
        specular = 0.13f + surface->moisture * 0.08f;
        break;
    case PLANET_SURFACE_LAVA: {
        float molten = fmaxf(surface->lavaFlow,
                             surface->biome == PLANET_BIOME_LAVA_SEA ? 0.78f : 0.0f);
        if (profile->style == SOLAR_STYLE_LAVA) {
            molten = fmaxf(molten, PlanetLavaFissure(surface));
        }
        molten = Clamp(molten, 0.0f, 1.0f);
        roughness = 0.76f - molten * 0.49f;
        specular = 0.22f + molten * 0.42f;
        emissive = 0.22f + molten * 0.76f;
        break;
    }
    case PLANET_SURFACE_GAS:
        roughness = 0.36f + surface->detail * 0.12f;
        specular = 0.66f + surface->regionalness * 0.14f;
        break;
    case PLANET_SURFACE_GENERIC:
    default:
        if (surface->biome == PLANET_BIOME_FOREST) {
            roughness = 0.88f;
            specular = 0.15f + surface->moisture * 0.08f;
        } else if (surface->biome == PLANET_BIOME_OASIS) {
            roughness = 0.48f;
            specular = 0.48f;
        } else {
            roughness = 0.72f + surface->detail * 0.12f;
            specular = 0.19f + surface->moisture * 0.09f;
        }
        break;
    }

    float encodedType = (float)type / (float)(PLANET_SURFACE_TYPE_COUNT - 1);
    return (Color){
        PlanetColorChannel(Clamp(roughness, 0.045f, 1.0f) * 255.0f),
        PlanetColorChannel(Clamp(specular, 0.0f, 1.0f) * 255.0f),
        PlanetColorChannel(Clamp(emissive, 0.0f, 1.0f) * 255.0f),
        PlanetColorChannel(encodedType * 255.0f)
    };
}
