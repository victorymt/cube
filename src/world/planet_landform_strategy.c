#include "world/planet_landform_strategy.h"
#include "world/terrain.h"
#include "space/space_units.h"
#include "raymath.h"
#include <math.h>

static const uint32_t waterCapabilities = LANDFORM_HAS_DRAINAGE |
    LANDFORM_HAS_LIQUID_WATER | LANDFORM_HAS_SEDIMENT;

PlanetLandformStrategy HomeworldLandformStrategy(uint32_t seed)
{
    return (PlanetLandformStrategy){ .id = LANDFORM_STRATEGY_HOMEWORLD,
        .capabilities = waterCapabilities, .seed = seed, .seaLevel = HOME_SEA_LEVEL,
        .physics = { .radiusMeters = 6371000, .metersPerBlock = 6371000.0 / SURFACE_RADIUS_BLOCKS,
            .gravity = 9.80665, .angularVelocity = LandformAngularVelocity(86400), .liquidWater = true } };
}

PlanetLandformStrategy WaterWorldLandformStrategy(uint32_t seed, const PlanetProfile *profile)
{
    PlanetLandformStrategy s = { .seed = seed, .seaLevel = -1 };
    if (!profile) return s;
    s.profile = *profile;
    s.seaLevel = PlanetSeaLevelForProfile(profile->style, profile);
    s.physics = (LandformPhysics){ .radiusMeters = profile->physicalRadiusKm * 1000,
        .metersPerBlock = profile->physicalRadiusKm * 1000 / SURFACE_RADIUS_BLOCKS,
        .gravity = profile->surfaceGravity,
        .angularVelocity = LandformDegreesPerSecondToRadians(profile->rotationRate) /
                           SPACE_PHYSICAL_SECONDS_PER_GAME_TIME };
    // First supported family. Ice/dry/lava/gas bodies keep their existing generator.
    // This uses mean climate and persistent coverage, never a seasonal thaw.
    if (profile->style != SOLAR_STYLE_TEMPERATE || !profile->hasSolidSurface ||
        !isfinite(profile->oceanCoverage) || profile->oceanCoverage <= .05f ||
        profile->oceanCoverage > 1 || !isfinite(profile->iceCoverage) ||
        profile->iceCoverage < 0 || profile->iceCoverage >= .95f ||
        !isfinite(profile->equilibriumTempK) || profile->equilibriumTempK < 273.15f ||
        profile->equilibriumTempK >= 373.15f || !isfinite(profile->rotationRate) ||
        !LandformPhysicsValid(s.physics)) return s;
    s.id = LANDFORM_STRATEGY_WATER_WORLD;
    s.capabilities = waterCapabilities;
    s.physics.liquidWater = true;
    return s;
}

PlanetLandformStrategy PlanetLandformStrategyForSurface(
    WorldSurfaceKind kind, uint32_t seed, const PlanetProfile *profile)
{
    switch (kind) {
    case WORLD_SURFACE_HOME:
        return HomeworldLandformStrategy(seed);
    case WORLD_SURFACE_PLANET:
        return WaterWorldLandformStrategy(seed, profile);
    case WORLD_SURFACE_NONE:
    default:
        return (PlanetLandformStrategy){ .seed = seed, .seaLevel = -1 };
    }
}

const char *LandformStrategyName(LandformStrategyId id)
{
    switch (id) {
    case LANDFORM_STRATEGY_HOMEWORLD: return "homeworld";
    case LANDFORM_STRATEGY_WATER_WORLD: return "water_world";
    default: return "none";
    }
}

LandformBaseline PlanetLandformBaseline(Vector3 direction, void *user)
{
    const PlanetLandformStrategy *s = user;
    if (!s || !(s->capabilities & LANDFORM_HAS_DRAINAGE)) return (LandformBaseline){0};
    int x = (int)floorf(atan2f(direction.z, direction.x) * SURFACE_RADIUS_BLOCKS);
    int z = (int)floorf(asinf(fmaxf(-1, fminf(1, direction.y))) * SURFACE_RADIUS_BLOCKS);
    if (s->id == LANDFORM_STRATEGY_HOMEWORLD) {
        SurfaceTerrainSample sample = {0};
        float elevation = TerrainHomeBaselineElevation(s->seed, x, z, &sample);
        float latitude = fabsf(direction.y);
        float rain = 0.65f - 0.56f * expf(-powf((latitude - .45f) / .19f, 2));
        return (LandformBaseline){ elevation, rain, .25f + .6f * sample.ridge };
    }
    PlanetSurfaceSample sample = {0};
    float elevation = TerrainPlanetBaselineElevation(s->seed, &s->profile, x, z, &sample);
    float warmth = Clamp((sample.meanTemperature - 263.15f) / 20, 0, 1);
    float runoff = Clamp(sample.moisture * warmth * (1 - sample.iceCoverage), 0, 1);
    return (LandformBaseline){ elevation, runoff, Clamp(.25f + .6f * sample.ridge, 0, 1) };
}

LandformBuildInput PlanetLandformBuildInput(PlanetLandformStrategy *s)
{
    if (!s) return (LandformBuildInput){0};
    return (LandformBuildInput){ .seaLevel = s->seaLevel,
        .baseline = s->capabilities & LANDFORM_HAS_DRAINAGE ? PlanetLandformBaseline : NULL,
        .user = s, .physics = s->physics, .strategyId = s->id };
}

uint64_t PlanetLandformStrategyFingerprint(const PlanetLandformStrategy *s)
{
    if (!s) return 0;
    uint64_t h = LandformPhysicsFingerprint(s->physics);
#define VALUE(v) h = LandformFingerprintValue(h, (double)(v))
    VALUE(s->id); VALUE(s->capabilities); VALUE(s->seed); VALUE(s->seaLevel);
    // Explicit fields make cache identity independent of compiler padding.
#define PROFILE(f) VALUE(s->profile.f)
    PROFILE(seed); PROFILE(canonicalBodyId); PROFILE(style); PROFILE(atmosphereType);
    PROFILE(physicalRadiusKm); PROFILE(massKg); PROFILE(spaceProxyRadius); PROFILE(surfaceGravity);
    PROFILE(receivedIrradiance); PROFILE(radiativeTempK); PROFILE(equilibriumTempK);
    PROFILE(surfacePressureAtm); PROFILE(atmosphereDensity); PROFILE(oceanCoverage);
    PROFILE(iceCoverage); PROFILE(cloudCoverage); PROFILE(terrainRoughness); PROFILE(ageGyr);
    PROFILE(rotationRate); PROFILE(tidalLockFactor); PROFILE(ringTilt); PROFILE(albedo);
    PROFILE(greenhouseEffect); PROFILE(orbitalEccentricity); PROFILE(orbitalMeanAnomalyAtEpoch);
    PROFILE(axialTilt); PROFILE(seasonPhase); PROFILE(yearLength);
    PROFILE(seasonalTemperatureAmplitudeK); PROFILE(orbitalTemperatureAmplitudeK);
    PROFILE(polarIceVariability); PROFILE(seasonalHumidityBias); PROFILE(prevailingWindAngle);
    PROFILE(windStrength); PROFILE(volcanicActivity); PROFILE(impactRate);
    PROFILE(hasSolidSurface); PROFILE(hasRings); PROFILE(tidallyLocked);
#undef PROFILE
#undef VALUE
    return h;
}

uint64_t PlanetLandformProfileFingerprint(const PlanetProfile *profile)
{
    if (!profile) return 0;
    uint64_t h = UINT64_C(14695981039346656037);
#define VALUE(v) h = LandformFingerprintValue(h, (double)(v))
    VALUE(profile->seed); VALUE(profile->canonicalBodyId); VALUE(profile->style);
    VALUE(profile->atmosphereType); VALUE(profile->physicalRadiusKm);
    VALUE(profile->massKg); VALUE(profile->spaceProxyRadius);
    VALUE(profile->surfaceGravity); VALUE(profile->receivedIrradiance);
    VALUE(profile->radiativeTempK); VALUE(profile->equilibriumTempK);
    VALUE(profile->surfacePressureAtm); VALUE(profile->atmosphereDensity);
    VALUE(profile->oceanCoverage); VALUE(profile->iceCoverage);
    VALUE(profile->cloudCoverage); VALUE(profile->terrainRoughness);
    VALUE(profile->ageGyr); VALUE(profile->rotationRate);
    VALUE(profile->tidalLockFactor); VALUE(profile->ringTilt);
    VALUE(profile->albedo); VALUE(profile->greenhouseEffect);
    VALUE(profile->orbitalEccentricity);
    VALUE(profile->orbitalMeanAnomalyAtEpoch);
    VALUE(profile->axialTilt); VALUE(profile->seasonPhase);
    VALUE(profile->yearLength);
    VALUE(profile->seasonalTemperatureAmplitudeK);
    VALUE(profile->orbitalTemperatureAmplitudeK);
    VALUE(profile->polarIceVariability); VALUE(profile->seasonalHumidityBias);
    VALUE(profile->prevailingWindAngle); VALUE(profile->windStrength);
    VALUE(profile->volcanicActivity); VALUE(profile->impactRate);
    VALUE(profile->hasSolidSurface); VALUE(profile->hasRings);
    VALUE(profile->tidallyLocked);
#undef VALUE
    return h;
}
