#include "space/planet_atmosphere.h"

#include "core/config.h"

#include <math.h>

#define ATMOSPHERE_REFERENCE_Y 12.0f
#define ATMOSPHERE_COMPRESSION_START ((float)SURFACE_GENERATION_MAX_Y_EXCLUSIVE + 18.0f)

AtmosphereInput PlanetAtmosphereInput(const PlanetProfile *planet)
{
    AtmosphereInput input = AtmosphereEarthInput();
    if (!planet) return input;
    input.surfaceTemperatureK = planet->equilibriumTempK;
    input.surfacePressureAtm = planet->atmosphereType == PLANET_ATMOSPHERE_NONE ?
        0.0 : planet->surfacePressureAtm;
    /* Zero gravity in old/test profiles predates this optional physics input. */
    input.gravityMps2 *= planet->surfaceGravity == 0.0f ? 1.0 : planet->surfaceGravity;
    input.ozoneStrength = planet->atmosphereType == PLANET_ATMOSPHERE_BREATHABLE ? 1.0 : 0.0;
    if (planet->style == SOLAR_STYLE_GAS) input.molarMassKgMol = 0.0023;
    else if (planet->atmosphereType == PLANET_ATMOSPHERE_CORROSIVE ||
             planet->atmosphereType == PLANET_ATMOSPHERE_THIN)
        input.molarMassKgMol = 0.04401;
    else if (planet->style == SOLAR_STYLE_ICE) input.molarMassKgMol = 0.0280134;
    double flux = isfinite(planet->receivedIrradiance) ?
        fmax(planet->receivedIrradiance, 0.01) : 1.0;
    input.exosphereTemperatureK = fmin(3000.0, fmax(200.0, 1000.0 * pow(flux, 0.25)));
    return input;
}

float PlanetAtmosphereTopHeight(const PlanetProfile *planet)
{
    if (!planet) return SPACE_ENTER_Y;
    float typeScale = 1.0f;
    switch (planet->atmosphereType) {
    case PLANET_ATMOSPHERE_NONE: typeScale = 0.55f; break;
    case PLANET_ATMOSPHERE_THIN: typeScale = 0.78f; break;
    case PLANET_ATMOSPHERE_BREATHABLE: typeScale = 1.00f; break;
    case PLANET_ATMOSPHERE_DENSE: typeScale = 1.22f; break;
    case PLANET_ATMOSPHERE_CORROSIVE: typeScale = 1.12f; break;
    default: break;
    }
    float density = isfinite(planet->atmosphereDensity) ?
        fminf(fmaxf(planet->atmosphereDensity, 0.0f), 1.0f) : 0.0f;
    return ATMOSPHERE_COMPRESSION_START + 64.0f + density * 48.0f * typeScale;
}

static double AtmosphereAltitudeForHeight(const AtmosphereProfile *profile,
                                          float topHeight, float worldY)
{
    if (!isfinite(worldY)) return NAN;
    /* Terrain keeps metre-per-voxel altitude. Only the air above the maximum
     * terrain height is compressed into the existing surface/space corridor.
     * Both home and planet scenes use the same layer fractions. */
    if (worldY <= ATMOSPHERE_COMPRESSION_START)
        return fmax((double)worldY - ATMOSPHERE_REFERENCE_Y, 0.0);
    static const double fractions[] = { 0.0, 0.20, 0.40, 0.55, 0.85, 1.0 };
    double heights[] = {
        ATMOSPHERE_COMPRESSION_START - ATMOSPHERE_REFERENCE_Y,
        profile->altitudeM[1], profile->altitudeM[5], profile->altitudeM[7],
        profile->altitudeM[10], profile->altitudeM[11]
    };
    /* Extremely compact columns may end below the tallest terrain. Do not
     * reverse physical altitude when entering the compressed corridor. */
    for (int i = 1; i < 6; i++) heights[i] = fmax(heights[i], heights[i - 1]);
    double t = ((double)worldY - ATMOSPHERE_COMPRESSION_START) /
               (topHeight - ATMOSPHERE_COMPRESSION_START);
    if (t >= 1.0) return heights[5];
    for (int i = 0; i < 5; i++) {
        if (t <= fractions[i + 1]) {
            double f = (t - fractions[i]) / (fractions[i + 1] - fractions[i]);
            return heights[i] + (heights[i + 1] - heights[i]) * f;
        }
    }
    return heights[5];
}

double PlanetAtmosphereAltitudeM(const PlanetProfile *planet, float worldY)
{
    AtmosphereInput input = PlanetAtmosphereInput(planet);
    AtmosphereProfile profile;
    if (!AtmosphereProfileBuild(&input, &profile)) return NAN;
    return AtmosphereAltitudeForHeight(&profile, PlanetAtmosphereTopHeight(planet), worldY);
}

AtmosphereSample PlanetAtmosphereAtHeight(const PlanetProfile *planet, float worldY)
{
    AtmosphereInput input = PlanetAtmosphereInput(planet);
    AtmosphereProfile profile;
    if (!AtmosphereProfileBuild(&input, &profile)) return (AtmosphereSample){ 0 };
    return AtmosphereSampleAt(&profile, AtmosphereAltitudeForHeight(
        &profile, PlanetAtmosphereTopHeight(planet), worldY));
}

float PlanetAtmosphereSkyVisibility(AtmosphereSample sample)
{
    if (!sample.valid || sample.layer == ATMOSPHERE_LAYER_VACUUM) return 0.0f;
    /* Perceptual response to the remaining air column; pressure, density and
     * world-transition thresholds retain their independent meanings. */
    return (float)pow(fmin(fmax(sample.columnFraction, 0.0), 1.0), 0.35);
}

float PlanetAtmosphereFlightDensity(AtmosphereSample sample)
{
    if (!sample.valid) return 0.0f;
    return (float)fmin(fmax(sample.densityKgM3 / 1.225 * 0.78, 0.0), 1.0);
}
