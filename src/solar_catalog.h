#ifndef VOXELCRAFT_SOLAR_CATALOG_H
#define VOXELCRAFT_SOLAR_CATALOG_H

#include "planet_profile.h"
#include "space_satellite.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct SolarCatalogPlanet {
    uint32_t bodyId;
    const char *name;
    double massEarth;
    double radiusKm;
    double semiMajorAxisAu;
    double eccentricity;
    double inclinationDeg;
    double longitudeAscendingNodeDeg;
    double argumentPeriapsisDeg;
    double meanAnomalyAtEpochDeg;
    double rotationPeriodHours;
    double axialTiltDeg;
    SolarBodyStyle style;
    bool gasGiant;
} SolarCatalogPlanet;

typedef struct SolarCatalogSatellite {
    uint32_t bodyId;
    uint32_t parentBodyId;
    const char *name;
    SpaceSatelliteOrbit orbit;
} SolarCatalogSatellite;

int SolarCatalogPlanetCount(void);
const SolarCatalogPlanet *SolarCatalogPlanetAt(int index);
int SolarCatalogSatelliteCount(void);
const SolarCatalogSatellite *SolarCatalogSatelliteAt(int index);
bool SolarCatalogValidate(void);

#endif
