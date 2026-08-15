#include "solar_catalog.h"

#include <math.h>
#include <string.h>

// Approximate J2000 osculating elements. The runtime intentionally uses a
// two-body Kepler model; long-period perturbations are outside this catalog.
static const SolarCatalogPlanet solarPlanets[] = {
    { 1u, "Mercury", 0.0553, 2439.7, 0.38709927, 0.20563593,
      7.00497902, 48.33076593, 29.12703035, 174.79252722,
      1407.6, 0.034, SOLAR_STYLE_CRATER, false },
    { 2u, "Venus", 0.8150, 6051.8, 0.72333566, 0.00677672,
      3.39467605, 76.67984255, 54.92262463, 50.37663232,
      -5832.5, 177.36, SOLAR_STYLE_LAVA, false },
    { 3u, "Earth", 1.0, 6371.0, 1.00000261, 0.01671123,
      -0.00001531, 0.0, 102.93768193, 357.52688973,
      23.9345, 23.4393, SOLAR_STYLE_TEMPERATE, false },
    { 4u, "Mars", 0.10745, 3389.5, 1.52371034, 0.09339410,
      1.84969142, 49.55953891, 286.49683150, 19.39019754,
      24.6229, 25.19, SOLAR_STYLE_DESERT, false },
    { 5u, "Jupiter", 317.83, 69911.0, 5.20288700, 0.04838624,
      1.30439695, 100.47390909, 274.25457074, 19.66796068,
      9.9250, 3.13, SOLAR_STYLE_GAS, true },
    { 6u, "Saturn", 95.159, 58232.0, 9.53667594, 0.05386179,
      2.48599187, 113.66242448, 338.93645383, 317.35536592,
      10.656, 26.73, SOLAR_STYLE_GAS, true },
    { 7u, "Uranus", 14.536, 25362.0, 19.18916464, 0.04725744,
      0.77263783, 74.01692503, 96.93735127, 142.28382821,
      -17.24, 97.77, SOLAR_STYLE_ICE, true },
    { 8u, "Neptune", 17.147, 24622.0, 30.06992276, 0.00859048,
      1.77004347, 131.78422574, 273.18053653, 259.91520804,
      16.11, 28.32, SOLAR_STYLE_ICE, true }
};

#define SAT(ID, PARENT, NAME, A, E, I, R, M, PHASE) \
    { ID, PARENT, NAME, { true, A, E, I * 0.017453292519943295, 0.0, 0.0, \
                           PHASE * 0.017453292519943295, R, M } }

static const SolarCatalogSatellite solarSatellites[] = {
    SAT(101u, 3u, "Moon", 384400.0, 0.0549, 5.145, 1737.4, 7.342e22, 135.27),
    SAT(102u, 4u, "Phobos", 9376.0, 0.0151, 1.075, 11.267, 1.0659e16, 20.0),
    SAT(103u, 4u, "Deimos", 23463.0, 0.0002, 1.788, 6.2, 1.4762e15, 210.0),
    SAT(104u, 5u, "Io", 421700.0, 0.0041, 0.05, 1821.6, 8.9319e22, 40.0),
    SAT(105u, 5u, "Europa", 671034.0, 0.0094, 0.471, 1560.8, 4.7998e22, 120.0),
    SAT(106u, 5u, "Ganymede", 1070412.0, 0.0013, 0.204, 2634.1, 1.4819e23, 200.0),
    SAT(107u, 5u, "Callisto", 1882709.0, 0.0074, 0.205, 2410.3, 1.0759e23, 300.0),
    SAT(108u, 6u, "Mimas", 185539.0, 0.0196, 1.574, 198.2, 3.7493e19, 30.0),
    SAT(109u, 6u, "Enceladus", 238042.0, 0.0047, 0.009, 252.1, 1.0802e20, 80.0),
    SAT(110u, 6u, "Tethys", 294672.0, 0.0001, 1.091, 531.1, 6.1745e20, 130.0),
    SAT(111u, 6u, "Dione", 377415.0, 0.0022, 0.028, 561.4, 1.0955e21, 180.0),
    SAT(112u, 6u, "Rhea", 527068.0, 0.0010, 0.333, 763.8, 2.3065e21, 230.0),
    SAT(113u, 6u, "Titan", 1221865.0, 0.0288, 0.349, 2574.7, 1.3452e23, 280.0),
    SAT(114u, 6u, "Iapetus", 3560854.0, 0.0286, 15.47, 734.5, 1.8056e21, 330.0),
    SAT(115u, 7u, "Miranda", 129390.0, 0.0013, 4.338, 235.8, 6.59e19, 25.0),
    SAT(116u, 7u, "Ariel", 190900.0, 0.0012, 0.041, 578.9, 1.353e21, 95.0),
    SAT(117u, 7u, "Umbriel", 266000.0, 0.0039, 0.128, 584.7, 1.172e21, 165.0),
    SAT(118u, 7u, "Titania", 436300.0, 0.0011, 0.079, 788.9, 3.527e21, 235.0),
    SAT(119u, 7u, "Oberon", 583500.0, 0.0014, 0.068, 761.4, 3.014e21, 305.0),
    SAT(120u, 8u, "Triton", 354759.0, 0.000016, 156.865, 1353.4, 2.139e22, 45.0)
};

#undef SAT

int SolarCatalogPlanetCount(void)
{
    return (int)(sizeof(solarPlanets) / sizeof(solarPlanets[0]));
}

const SolarCatalogPlanet *SolarCatalogPlanetAt(int index)
{
    return index >= 0 && index < SolarCatalogPlanetCount()
        ? &solarPlanets[index] : NULL;
}

int SolarCatalogSatelliteCount(void)
{
    return (int)(sizeof(solarSatellites) / sizeof(solarSatellites[0]));
}

const SolarCatalogSatellite *SolarCatalogSatelliteAt(int index)
{
    return index >= 0 && index < SolarCatalogSatelliteCount()
        ? &solarSatellites[index] : NULL;
}

bool SolarCatalogValidate(void)
{
    for (int i = 0; i < SolarCatalogPlanetCount(); i++) {
        const SolarCatalogPlanet *planet = &solarPlanets[i];
        if (!planet->name || !planet->name[0] || !(planet->massEarth > 0.0) ||
            !(planet->radiusKm > 0.0) || !(planet->semiMajorAxisAu > 0.0) ||
            planet->eccentricity < 0.0 || planet->eccentricity >= 1.0) {
            return false;
        }
        if (i > 0 && planet->semiMajorAxisAu <=
                         solarPlanets[i - 1].semiMajorAxisAu) return false;
    }
    for (int i = 0; i < SolarCatalogSatelliteCount(); i++) {
        const SolarCatalogSatellite *satellite = &solarSatellites[i];
        bool parentFound = false;
        for (int planet = 0; planet < SolarCatalogPlanetCount(); planet++) {
            parentFound |= solarPlanets[planet].bodyId == satellite->parentBodyId;
        }
        if (!parentFound || !satellite->name || !satellite->name[0] ||
            !satellite->orbit.exists ||
            !(satellite->orbit.semiMajorAxisKm > 0.0) ||
            satellite->orbit.eccentricity < 0.0 ||
            satellite->orbit.eccentricity >= 1.0 ||
            !(satellite->orbit.radiusKm > 0.0) ||
            !(satellite->orbit.massKg > 0.0)) return false;
    }
    return true;
}
