#include "world/terrain.h"
#include "world/landform_runtime.h"
#include "world/world.h"
#include "world/world_surface_snapshot.h"
#include <math.h>

int PlanetSeaLevelForProfile(SolarBodyStyle style,
                                    const PlanetProfile *profile)
{
    if (profile->oceanCoverage <= 0.05f) return -1;
    if (style == SOLAR_STYLE_LAVA || style == SOLAR_STYLE_ICE ||
        style == SOLAR_STYLE_TEMPERATE) return 80;
    return -1;
}

int PlanetTerrainSeaLevel(void)
{
    const WorldSurfaceSnapshot *snapshot = WorldCurrentSurfaceSnapshot();
    if (snapshot->context.kind != WORLD_SURFACE_PLANET) {
        return TerrainSeaLevel(WorldTerrainMode());
    }
    const LandformAtlas *atlas = LandformRuntimeAtlas();
    if (atlas) return (int)lroundf(atlas->seaLevel);
    return PlanetSeaLevelForProfile(
        snapshot->planetProfile.style, &snapshot->planetProfile);
}

SurfaceTerrainSample PlanetTerrainSampleAt(int x, int z)
{
    const WorldSurfaceSnapshot *snapshot = WorldCurrentSurfaceSnapshot();
    if (snapshot->context.kind != WORLD_SURFACE_PLANET)
        return SurfaceTerrainAt(x, z, WorldTerrainMode());
    PlanetSurfaceSample planet = { 0 };
    SurfaceTerrainSample sample = { 0 };
    sample.elevation = TerrainPlanetBaselineElevation(
        snapshot->context.seed, &snapshot->planetProfile, x, z, &planet);
    sample.seaLevel = PlanetSeaLevelForProfile(snapshot->planetProfile.style,
                                              &snapshot->planetProfile);
    sample.continentalness = planet.continentalness;
    sample.erosion = planet.erosion;
    sample.ridge = planet.ridge;
    sample.peak = planet.peak;
    sample.trench = planet.trench;
    LandformApplyTerrainSample(x, z, &sample);
    return sample;
}

int PlanetTerrainHeight(int x, int z)
{
    return (int)lroundf(PlanetTerrainSampleAt(x, z).elevation);
}

PlanetSurfaceSample PlanetSurfaceBaselineAt(int x, int z)
{
    const WorldSurfaceSnapshot *snapshot = WorldCurrentSurfaceSnapshot();
    float longitude = 0.0f;
    float latitude = 0.0f;
    PlanetSurfaceLatLonAt(x, z, &longitude, &latitude);
    return PlanetSampleGlobalSurfaceBaseline(
        snapshot->context.seed, &snapshot->planetProfile,
        longitude, latitude);
}

PlanetSurfaceSample PlanetSurfaceAtTime(int x, int z, double simulationTime)
{
    const WorldSurfaceSnapshot *snapshot = WorldCurrentSurfaceSnapshot();
    float longitude = 0.0f;
    float latitude = 0.0f;
    PlanetSurfaceLatLonAt(x, z, &longitude, &latitude);
    return PlanetSampleGlobalSurfaceAtTime(
        snapshot->context.seed, &snapshot->planetProfile,
        longitude, latitude, simulationTime);
}
