#include "world/terrain.h"
#include "world/surface_topology.h"
#include "raymath.h"
#include <math.h>

static float SmoothRange(float low, float high, float value)
{
    if (value <= low) return 0.0f;
    if (value >= high) return 1.0f;
    float t = (value - low) / (high - low);
    return t * t * (3.0f - 2.0f * t);
}

static int PlanetSeabedFromSignals(int seaLevel, float ocean,
                                              float trench, float seamount,
                                              float detail)
{
    BathymetrySample sample = {
        .seaLevel = seaLevel,
        .seabedY = seaLevel,
        .zone = BATHYMETRY_ZONE_LAND,
        .material = BATHYMETRY_MATERIAL_ROCK
    };
    if (seaLevel < 0 || ocean <= 0.0f) return sample.seabedY;

    ocean = Clamp(ocean, 0.0f, 1.0f);
    trench = Clamp(trench, 0.0f, 1.0f);
    seamount = Clamp(seamount, 0.0f, 1.0f);
    float deepOcean = SmoothRange(0.48f, 0.92f, ocean);
    float trenchRelief = trench * SmoothRange(0.52f, 0.82f, ocean) * 12.0f;
    float seamountRelief = seamount * deepOcean * 22.0f;
    float depth = 1.0f;
    depth += SmoothRange(0.00f, 0.16f, ocean) * 5.0f;
    depth += SmoothRange(0.10f, 0.36f, ocean) * 14.0f;
    depth += SmoothRange(0.30f, 0.67f, ocean) * 24.0f;
    depth += SmoothRange(0.58f, 0.92f, ocean) * 16.0f;
    depth += trenchRelief;
    depth -= seamountRelief;
    depth += Clamp(detail, -0.5f, 0.5f) * 4.0f;
    depth = Clamp(depth, 1.0f, (float)BATHYMETRY_MAX_WATER_DEPTH);

    sample.waterDepth = (int)lroundf(depth);
    sample.seabedY = seaLevel - sample.waterDepth;
    if (sample.seabedY < BATHYMETRY_MIN_SEABED_Y) {
        sample.seabedY = BATHYMETRY_MIN_SEABED_Y;
        sample.waterDepth = seaLevel - sample.seabedY;
    }
    return sample.seabedY;
}

static int TerrainPlanetBaseAtGeographic(uint32_t seed, const PlanetProfile *profile,
                                      float longitude, float latitude, PlanetSurfaceSample *out)
{
    PlanetSurfaceSample surface = PlanetSampleGlobalSurfaceBaseline(seed, profile, longitude, latitude);
    if (out) *out = surface;
    float roughness = Clamp(profile->terrainRoughness, 0.35f, 1.55f);
    float continents = surface.continentalness;
    float localDetail = PlanetSurfaceFractalNoiseAt(seed, longitude, latitude, SURFACE_RADIUS_BLOCKS * 0.024f, 47u);
    float hills = surface.regionalness * 0.62f + surface.detail * 0.23f +
                  localDetail * 0.15f;
    float coast = 0.27f + profile->oceanCoverage * 0.36f;
    int seaLevel = PlanetSeaLevelForProfile(profile->style, profile);
    bool oceanColumn = continents < coast && seaLevel >= 0;
    float height;
    if (oceanColumn) {
        float ocean = Clamp((coast - continents) / fmaxf(coast, 0.08f),
                            0.0f, 1.0f);
        float seamount = SmoothRange(
            0.62f, 0.90f,
            PlanetSurfaceFractalNoiseAt(seed, longitude, latitude, SURFACE_RADIUS_BLOCKS * 0.00105f, 353u));
        height = (float)PlanetSeabedFromSignals(
            seaLevel, ocean, surface.trench, seamount, hills - 0.5f);
    } else {
        float land = Clamp((continents - coast) / fmaxf(1.0f - coast, 0.08f),
                           0.0f, 1.0f);
        float mountainMask = SmoothRange(0.08f, 0.62f, land) *
                             (1.0f - surface.erosion * 0.48f);
        height = 84.0f + land * 30.0f + (hills - 0.5f) * 13.0f;
        height += surface.ridge * mountainMask * 78.0f * roughness;
        height += surface.peak * mountainMask * 44.0f * roughness;
    }

    switch (surface.biome) {
    case PLANET_BIOME_LAVA_SEA:
        height = fminf(height, 74.0f + (hills - 0.5f) * 4.0f);
        break;
    case PLANET_BIOME_VOLCANIC_RIDGE:
        height += 14.0f;
        break;
    case PLANET_BIOME_BASALT_PLAINS:
        height += (PlanetSurfaceFractalNoiseAt(seed, longitude, latitude, SURFACE_RADIUS_BLOCKS * 0.14f, 367u) - 0.5f) *
                  5.0f;
        break;
    case PLANET_BIOME_GLACIER:
        height = fminf(height, 76.0f + (hills - 0.5f) * 7.0f);
        {
            // Ice flows downhill from each pole toward the equator. The
            // latitude gradient gives the cut plane a consistent flow axis.
            height += surface.glacierFlow * (fabsf(latitude) - 0.70f) * 18.0f;
        }
        height -= surface.glacierCracks * 1.4f;
        break;
    case PLANET_BIOME_ICE_SHEET:
        height += 8.0f +
                  (PlanetSurfaceFractalNoiseAt(seed, longitude, latitude, SURFACE_RADIUS_BLOCKS * 0.008f, 373u) - 0.5f) *
                      12.0f;
        break;
    case PLANET_BIOME_DUNES:
        {
            float wind = profile->prevailingWindAngle;
            float latitudeCos = cosf(latitude);
            Vector3 point = { latitudeCos * cosf(longitude), sinf(latitude),
                              latitudeCos * sinf(longitude) };
            Vector3 windAxis = { cosf(wind), 0.0f, sinf(wind) };
            Vector3 crossAxis = { -sinf(wind), 0.0f, cosf(wind) };
            float windCoord = Vector3DotProduct(point, windAxis);
            float crossWind = Vector3DotProduct(point, crossAxis);
            float duneShape = 0.5f + 0.5f * sinf(windCoord * 24.0f +
                                                    crossWind * 5.0f);
            height += duneShape * surface.duneBand * 12.0f;
        }
        break;
    case PLANET_BIOME_BADLANDS:
        height += 8.0f +
                  PlanetSurfaceFractalNoiseAt(seed, longitude, latitude, SURFACE_RADIUS_BLOCKS * 0.024f, 379u) * 16.0f;
        break;
    case PLANET_BIOME_OASIS:
        height = fminf(height, 84.0f + (hills - 0.5f) * 4.0f);
        break;
    case PLANET_BIOME_MAGMA_MIRE:
        height = fminf(height, 84.0f + (hills - 0.5f) * 3.0f);
        break;
    case PLANET_BIOME_FROZEN_MIRE:
        height = fminf(height, 86.0f + (hills - 0.5f) * 3.0f);
        break;
    case PLANET_BIOME_SALT_MARSH:
        height = fminf(height, 84.0f + (hills - 0.5f) * 3.0f);
        break;
    case PLANET_BIOME_CRATER_BOG:
        height = fminf(height, 82.0f + (hills - 0.5f) * 3.0f);
        break;
    case PLANET_BIOME_TEMPERATE_MARSH:
        height = fminf(height, 86.0f + (hills - 0.5f) * 3.0f);
        break;
    case PLANET_BIOME_IMPACT_BASIN:
        height -= 14.0f + (1.0f - hills) * 10.0f;
        break;
    case PLANET_BIOME_CRATER_HIGHLANDS:
        height += PlanetSurfaceFractalNoiseAt(seed, longitude, latitude, SURFACE_RADIUS_BLOCKS * 0.020f, 383u) * 10.0f;
        break;
    case PLANET_BIOME_OCEAN:
        if (!oceanColumn) {
            height = fminf(height, 76.0f + (hills - 0.5f) * 5.0f);
        }
        break;
    case PLANET_BIOME_COAST:
        height = fminf(height, 87.0f + (hills - 0.5f) * 6.0f);
        break;
    case PLANET_BIOME_ALPINE:
        height += 18.0f + (hills - 0.35f) * 20.0f * roughness;
        break;
    case PLANET_BIOME_STORM_BANDS:
        {
            height = 112.0f + sinf(latitude * 64.0f) * 14.0f +
                     sinf(longitude * 23.0f) *
                         cosf(latitude) * 10.0f;
        }
        break;
    case PLANET_BIOME_PLAINS:
    case PLANET_BIOME_FOREST:
    default:
        height += (PlanetSurfaceFractalNoiseAt(seed, longitude, latitude, SURFACE_RADIUS_BLOCKS * 0.023f, 389u) - 0.5f) *
                  10.0f;
        break;
    }

    switch (profile->canonicalBodyId) {
    case 1u:
        height += (surface.detail - 0.5f) * 12.0f;
        break;
    case 2u:
        height = 82.0f + (height - 82.0f) * 0.62f;
        height += surface.ridge * 9.0f;
        break;
    case 4u:
        height -= surface.trench * 24.0f;
        height += surface.peak * 16.0f;
        break;
    default:
        break;
    }

    // These fields are shared with the orbital map: the same crater walls,
    // ejecta blankets, volcanic cones and lava channels continue at landing.
    height -= surface.impactDepth * 26.0f;
    height += surface.impactRim * 18.0f + surface.ejecta * 7.0f;
    height += surface.lavaFlow * (profile->style == SOLAR_STYLE_LAVA ? 7.0f : 2.5f);

    height = roundf(height);
    if (oceanColumn) {
        height = Clamp(height, (float)BATHYMETRY_MIN_SEABED_Y,
                       (float)(seaLevel - 1));
    } else if (height < 5.0f) {
        height = 5.0f;
    }
    if (height > 240.0f) height = 240.0f;
    return (int)height;
}

int TerrainPlanetBaselineAtGeographic(uint32_t seed, const PlanetProfile *profile,
                                      float longitude, float latitude, PlanetSurfaceSample *out)
{
    PlanetSurfaceSample sample;
    float base = TerrainPlanetBaseAtGeographic(seed, profile, longitude, latitude, &sample);
    if (out) *out = sample;
    if (sample.volcano.height <= 0) return (int)base;
    float centerLongitude, centerLatitude;
    SurfaceAddressLatLon(sample.volcano.definition.center, &centerLongitude, &centerLatitude);
    float foundation = TerrainPlanetBaseAtGeographic(seed, profile, centerLongitude, centerLatitude, NULL);
    /* Smooth the inherited hills under the edifice; leave room for an intact
       crater rim within the terrain's existing 240-block elevation budget. */
    foundation = fminf(foundation, 136.0f);
    return (int)roundf(Clamp(Lerp(base, foundation, sample.volcano.foundation) +
                             sample.volcano.height, BATHYMETRY_MIN_SEABED_Y, 240));
}
