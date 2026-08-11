#include "planet_surface.h"

#include "raymath.h"

#include <math.h>

static unsigned int PlanetSurfaceHash2D(int x, int z)
{
    unsigned int hash = 2166136261u;
    hash = (hash ^ (unsigned int)x) * 16777619u;
    hash = (hash ^ ((unsigned int)z * 374761393u)) * 16777619u;
    hash ^= hash >> 13;
    hash *= 1274126177u;
    return hash ^ (hash >> 16);
}

static float PlanetNoiseSmooth(float value)
{
    return value * value * (3.0f - 2.0f * value);
}

static unsigned int PlanetHash3D(uint32_t seed, int x, int y, int z,
                                 unsigned int lane)
{
    unsigned int hash = PlanetSurfaceHash2D(x + (int)(lane * 101u),
                                            z - (int)(lane * 173u));
    hash ^= (unsigned int)y * 0x9e3779b9u;
    hash ^= seed + 0x85ebca6bu + (hash << 6) + (hash >> 2);
    hash ^= hash >> 16;
    hash *= 2246822519u;
    return hash ^ (hash >> 13);
}

static float PlanetHashUnit3D(uint32_t seed, int x, int y, int z,
                              unsigned int lane)
{
    return (float)(PlanetHash3D(seed, x, y, z, lane) & 0x00ffffffu) / 16777215.0f;
}

static float PlanetValueNoise3D(uint32_t seed, float x, float y, float z,
                                unsigned int lane)
{
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int z0 = (int)floorf(z);
    float tx = PlanetNoiseSmooth(x - (float)x0);
    float ty = PlanetNoiseSmooth(y - (float)y0);
    float tz = PlanetNoiseSmooth(z - (float)z0);
    float x00 = Lerp(PlanetHashUnit3D(seed, x0, y0, z0, lane),
                     PlanetHashUnit3D(seed, x0 + 1, y0, z0, lane), tx);
    float x10 = Lerp(PlanetHashUnit3D(seed, x0, y0 + 1, z0, lane),
                     PlanetHashUnit3D(seed, x0 + 1, y0 + 1, z0, lane), tx);
    float x01 = Lerp(PlanetHashUnit3D(seed, x0, y0, z0 + 1, lane),
                     PlanetHashUnit3D(seed, x0 + 1, y0, z0 + 1, lane), tx);
    float x11 = Lerp(PlanetHashUnit3D(seed, x0, y0 + 1, z0 + 1, lane),
                     PlanetHashUnit3D(seed, x0 + 1, y0 + 1, z0 + 1, lane), tx);
    return Lerp(Lerp(x00, x10, ty), Lerp(x01, x11, ty), tz);
}

static float PlanetFractalNoise3D(uint32_t seed, Vector3 point, float frequency,
                                  unsigned int lane)
{
    float x = point.x * frequency;
    float y = point.y * frequency;
    float z = point.z * frequency;
    float value = 0.0f;
    float amplitude = 0.58f;
    float total = 0.0f;
    for (int octave = 0; octave < 4; octave++) {
        value += PlanetValueNoise3D(seed, x, y, z,
                                    lane + (unsigned int)octave * 17u) * amplitude;
        total += amplitude;
        x = x * 2.07f + 11.3f;
        y = y * 2.07f - 7.9f;
        z = z * 2.07f + 5.3f;
        amplitude *= 0.48f;
    }
    return value / total;
}

static float PlanetSmoothStep(float edge0, float edge1, float value)
{
    if (edge1 < edge0) return 1.0f - PlanetSmoothStep(edge1, edge0, value);
    float t = Clamp((value - edge0) / fmaxf(edge1 - edge0, 0.0001f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static unsigned int PlanetSeedHash2D(uint32_t seed, int x, int z,
                                     unsigned int lane)
{
    unsigned int hash = PlanetSurfaceHash2D(x + (int)(lane * 101u),
                                            z - (int)(lane * 173u));
    hash ^= seed + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    hash ^= hash >> 16;
    hash *= 2246822519u;
    return hash ^ (hash >> 13);
}

static float PlanetSeedHashUnit2D(uint32_t seed, int x, int z,
                                  unsigned int lane)
{
    return (float)(PlanetSeedHash2D(seed, x, z, lane) & 0x00ffffffu) / 16777215.0f;
}

static Vector3 PlanetSeedDirection(uint32_t seed, unsigned int lane)
{
    float longitude = PlanetSeedHashUnit2D(seed, (int)(seed ^ (lane * 37u)),
                                           (int)(seed >> 7), lane + 301u) * 2.0f * PI - PI;
    float latitude = (PlanetSeedHashUnit2D(seed, (int)(seed >> 11),
                                           (int)(seed ^ (lane * 73u)), lane + 307u) - 0.5f) * PI;
    float cosine = cosf(latitude);
    return (Vector3){ cosine * cosf(longitude), sinf(latitude),
                      cosine * sinf(longitude) };
}

static void PlanetSampleImpactFields(uint32_t seed, const PlanetProfile *profile,
                                     Vector3 point, PlanetSurfaceSample *sample)
{
    float impactRate = profile ? Clamp(profile->impactRate, 0.0f, 1.0f) : 0.0f;
    int craterCount = 2 +
                      (int)(PlanetSeedHash2D(seed, (int)seed, (int)(seed >> 16), 401u) % 5u);
    for (int i = 0; i < craterCount; i++) {
        Vector3 center = PlanetSeedDirection(seed, 410u + (unsigned int)i * 11u);
        float angularDistance = sqrtf(fmaxf(0.0f, 2.0f *
                                             (1.0f - Vector3DotProduct(point, center))));
        float radius = 0.028f + PlanetSeedHashUnit2D(seed,
                                                     (int)(seed + (uint32_t)i * 131u),
                                                     (int)(seed >> 9),
                                                     431u + (unsigned int)i) * 0.095f;
        radius *= 0.72f + impactRate * 0.66f;
        float floor = 1.0f - PlanetSmoothStep(0.52f, 1.0f, angularDistance / radius);
        float rim = PlanetSmoothStep(0.68f, 0.96f, angularDistance / radius) *
                    (1.0f - PlanetSmoothStep(0.96f, 1.18f, angularDistance / radius));
        float ejecta = PlanetSmoothStep(0.98f, 1.08f, angularDistance / radius) *
                       (1.0f - PlanetSmoothStep(1.08f, 1.72f, angularDistance / radius));
        float scale = 0.35f + impactRate * 0.65f;
        sample->impactDepth = fmaxf(sample->impactDepth, floor * scale);
        sample->impactRim = fmaxf(sample->impactRim, rim * scale);
        sample->ejecta = fmaxf(sample->ejecta, ejecta * scale);
    }
}

static PlanetSurfaceSample PlanetSampleGlobalSurfaceInternal(
    uint32_t seed, const PlanetProfile *profile, float longitude, float latitude,
    double simulationTime, bool applySeason)
{
    PlanetSurfaceSample sample = { 0 };
    SolarBodyStyle style = profile ? profile->style : SOLAR_STYLE_TEMPERATE;
    float oceanCoverage = profile ? Clamp(profile->oceanCoverage, 0.0f, 1.0f) : 0.0f;
    float cosLatitude = cosf(latitude);
    Vector3 point = { cosLatitude * cosf(longitude), sinf(latitude),
                      cosLatitude * sinf(longitude) };
    sample.continentalness = PlanetFractalNoise3D(seed, point, 1.55f, 21u);
    sample.regionalness = PlanetFractalNoise3D(seed, point, 4.35f, 101u);
    sample.detail = PlanetFractalNoise3D(seed, point, 11.0f, 47u);
    float latitudeAbs = fabsf(point.y);
    float equilibrium = profile ? profile->equilibriumTempK : 282.0f;
    // The global solver has already applied albedo and greenhouse forcing. This
    // zero-mean latitude redistribution keeps its planetary average intact.
    float meanTemperature = equilibrium + 29.67f -
                            latitudeAbs * (34.0f + latitudeAbs * 38.0f);
    float axialTilt = profile ? Clamp(profile->axialTilt, 0.0f, 0.75f) : 0.0f;
    float season = profile ? profile->seasonPhase : 0.0f;
    float yearLength = profile ? fmaxf(profile->yearLength, 1.0f) : 1.0f;
    if (profile && applySeason && isfinite(simulationTime)) {
        double period = (double)yearLength;
        double periodicTime = fmod(simulationTime, period);
        season += (float)(periodicTime * (2.0 * PI / period));
    }
    float seasonalDelta = sinf(latitude) * sinf(axialTilt) * sinf(season) * 70.0f;
    sample.meanTemperature = meanTemperature;
    sample.seasonalAmplitude = fabsf(sinf(latitude) * sinf(axialTilt) * 70.0f);
    sample.temperature = meanTemperature + (applySeason ? seasonalDelta : 0.0f);
    float thermalNoise = PlanetFractalNoise3D(seed, point, 2.35f, 137u);
    float cloudCoverage = profile ? Clamp(profile->cloudCoverage, 0.0f, 1.0f) : 0.35f;
    float globalMoisture = Clamp(oceanCoverage * 0.72f + cloudCoverage * 0.28f,
                                 0.0f, 1.0f);
    float moistureNoise = PlanetFractalNoise3D(seed, point, 2.70f, 157u);
    sample.moisture = Clamp(moistureNoise * (0.10f + globalMoisture * 0.38f) +
                            globalMoisture * 0.52f, 0.0f, 1.0f);
    float thermalBand = Clamp((meanTemperature - 178.0f) / 175.0f, 0.0f, 1.0f);
    sample.climate = Clamp(thermalNoise * 0.42f + thermalBand * 0.36f +
                           (1.0f - latitudeAbs) * 0.22f,
                           0.0f, 1.0f);
    float polar = PlanetSmoothStep(0.55f, 0.96f, latitudeAbs);
    float cold = PlanetSmoothStep(264.0f, 214.0f, meanTemperature);
    float globalIce = profile ? Clamp(profile->iceCoverage, 0.0f, 1.0f) : 0.0f;
    sample.iceCoverage = Clamp(globalIce * (0.35f + polar * 0.65f) +
                               polar * cold * (1.0f - globalIce) * 0.72f,
                               0.0f, 1.0f);
    if (style == SOLAR_STYLE_ICE) {
        sample.iceCoverage = fmaxf(sample.iceCoverage, polar * 0.62f + 0.15f);
    }

    PlanetSampleImpactFields(seed, profile, point, &sample);

    float volcanic = profile ? Clamp(profile->volcanicActivity, 0.0f, 1.0f) : 0.0f;
    float volcanicNoise = PlanetFractalNoise3D(seed, point, 3.15f, 251u);
    float volcanoPotential = PlanetSmoothStep(0.68f, 0.90f, volcanicNoise) * volcanic;
    sample.volcanicActivity = Clamp(volcanoPotential + volcanic * 0.12f, 0.0f, 1.0f);
    sample.volcanicCone = volcanoPotential * (0.55f + sample.regionalness * 0.45f);
    sample.caldera = sample.volcanicCone *
                     PlanetSmoothStep(0.73f, 0.91f,
                                      PlanetFractalNoise3D(seed, point, 7.2f, 269u));
    Vector3 lavaAxis = PlanetSeedDirection(seed, 281u);
    Vector3 lavaSide = Vector3Normalize((Vector3){ -lavaAxis.z, 0.25f, lavaAxis.x });
    float flowAlong = Vector3DotProduct(point, lavaAxis);
    float flowAcross = Vector3DotProduct(point, lavaSide);
    float flowRidge = 0.5f + 0.5f * sinf(flowAlong * 32.0f +
                                            flowAcross * 8.0f + volcanicNoise * 9.0f);
    sample.lavaFlow = Clamp(volcanic * PlanetSmoothStep(0.54f, 0.76f, volcanicNoise) *
                            flowRidge, 0.0f, 1.0f);

    float windAngle = profile ? profile->prevailingWindAngle : 0.0f;
    Vector3 windAxis = { cosf(windAngle), 0.0f, sinf(windAngle) };
    Vector3 crossAxis = { -sinf(windAngle), 0.0f, cosf(windAngle) };
    float windCoord = Vector3DotProduct(point, windAxis);
    float crossWind = Vector3DotProduct(point, crossAxis);
    float duneSignal = 0.5f + 0.5f * sinf(windCoord * 34.0f + crossWind * 5.0f +
                                            sample.detail * 8.0f);
    float windStrength = profile ? Clamp(profile->windStrength, 0.0f, 1.0f) : 0.4f;
    sample.duneBand = style == SOLAR_STYLE_DESERT
                          ? Clamp(duneSignal * (0.24f + windStrength * 0.58f) +
                                  sample.moisture * 0.10f, 0.0f, 1.0f)
                          : 0.0f;

    float glacierPotential = polar * Clamp(globalIce * 0.70f +
        PlanetSmoothStep(275.0f, 218.0f, meanTemperature) * 0.72f, 0.0f, 1.0f);
    float glacierNoise = PlanetFractalNoise3D(seed, point, 5.8f, 317u);
    sample.glacierFlow = Clamp(glacierPotential * (0.54f + glacierNoise * 0.46f),
                               0.0f, 1.0f);
    Vector3 glacierAxis = { cosf(windAngle + 0.9f), 0.0f,
                            sinf(windAngle + 0.9f) };
    float glacierCoord = Vector3DotProduct(point, glacierAxis) + point.y * 0.37f;
    float crackSignal = 0.5f + 0.5f * sinf(glacierCoord * 58.0f + sample.detail * 12.0f);
    sample.glacierCracks = sample.glacierFlow *
                           PlanetSmoothStep(0.74f, 0.96f, crackSignal);

    switch (style) {
    case SOLAR_STYLE_LAVA:
        sample.biome = sample.continentalness < 0.18f + oceanCoverage * 0.30f
                           ? PLANET_BIOME_LAVA_SEA
                           : (sample.regionalness > 0.68f ? PLANET_BIOME_VOLCANIC_RIDGE
                                                           : PLANET_BIOME_BASALT_PLAINS);
        break;
    case SOLAR_STYLE_ICE:
        sample.biome = sample.glacierFlow > 0.28f || sample.continentalness < 0.34f
                           ? PLANET_BIOME_GLACIER : PLANET_BIOME_ICE_SHEET;
        break;
    case SOLAR_STYLE_DESERT:
        sample.biome = sample.continentalness < 0.12f && oceanCoverage > 0.18f
                           ? PLANET_BIOME_OASIS
                           : (sample.regionalness > 0.66f ? PLANET_BIOME_BADLANDS
                                                           : PLANET_BIOME_DUNES);
        break;
    case SOLAR_STYLE_CRATER:
        if (sample.glacierFlow > 0.38f) {
            sample.biome = PLANET_BIOME_GLACIER;
        } else if (sample.iceCoverage > 0.68f) {
            sample.biome = PLANET_BIOME_ICE_SHEET;
        } else {
            sample.biome = sample.impactDepth > 0.40f ||
                                   sample.regionalness < 0.22f || sample.climate > 0.76f
                               ? PLANET_BIOME_IMPACT_BASIN
                               : PLANET_BIOME_CRATER_HIGHLANDS;
        }
        break;
    case SOLAR_STYLE_TEMPERATE: {
        float waterline = 0.27f + oceanCoverage * 0.36f;
        if (oceanCoverage > 0.08f && sample.continentalness < waterline &&
            sample.iceCoverage < 0.64f) {
            sample.biome = PLANET_BIOME_OCEAN;
        } else if (oceanCoverage > 0.08f && sample.continentalness < waterline + 0.09f &&
                   sample.iceCoverage < 0.56f) {
            sample.biome = PLANET_BIOME_COAST;
        } else if (sample.iceCoverage > 0.52f) {
            sample.biome = PLANET_BIOME_ICE_SHEET;
        } else if (sample.regionalness > 0.72f) {
            sample.biome = PLANET_BIOME_ALPINE;
        } else {
            sample.biome = sample.climate > 0.56f ? PLANET_BIOME_FOREST
                                                  : PLANET_BIOME_PLAINS;
        }
        break;
    }
    case SOLAR_STYLE_GAS:
        sample.biome = PLANET_BIOME_STORM_BANDS;
        break;
    default:
        sample.biome = PLANET_BIOME_PLAINS;
        break;
    }
    return sample;
}

PlanetSurfaceSample PlanetSampleGlobalSurfaceAtTime(uint32_t seed,
                                                    const PlanetProfile *profile,
                                                    float longitude, float latitude,
                                                    double simulationTime)
{
    return PlanetSampleGlobalSurfaceInternal(seed, profile, longitude, latitude,
                                             simulationTime, true);
}

PlanetSurfaceSample PlanetSampleGlobalSurfaceBaseline(uint32_t seed,
                                                      const PlanetProfile *profile,
                                                      float longitude, float latitude)
{
    return PlanetSampleGlobalSurfaceInternal(seed, profile, longitude, latitude,
                                             0.0, false);
}

PlanetSurfaceSample PlanetSampleGlobalSurface(uint32_t seed, const PlanetProfile *profile,
                                              float longitude, float latitude)
{
    return PlanetSampleGlobalSurfaceAtTime(
        seed, profile, longitude, latitude,
        SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()));
}

const char *PlanetBiomeName(PlanetBiome biome)
{
    switch (biome) {
    case PLANET_BIOME_BASALT_PLAINS: return "Basalt plains";
    case PLANET_BIOME_LAVA_SEA: return "Lava sea";
    case PLANET_BIOME_VOLCANIC_RIDGE: return "Volcanic ridge";
    case PLANET_BIOME_ICE_SHEET: return "Ice sheet";
    case PLANET_BIOME_GLACIER: return "Glacier";
    case PLANET_BIOME_DUNES: return "Dune field";
    case PLANET_BIOME_BADLANDS: return "Badlands";
    case PLANET_BIOME_OASIS: return "Oasis";
    case PLANET_BIOME_IMPACT_BASIN: return "Impact basin";
    case PLANET_BIOME_CRATER_HIGHLANDS: return "Crater highlands";
    case PLANET_BIOME_OCEAN: return "Ocean";
    case PLANET_BIOME_COAST: return "Coast";
    case PLANET_BIOME_PLAINS: return "Open plains";
    case PLANET_BIOME_FOREST: return "Forest";
    case PLANET_BIOME_ALPINE: return "Alpine range";
    case PLANET_BIOME_STORM_BANDS: return "Storm bands";
    default: return "Unknown terrain";
    }
}
