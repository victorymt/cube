#ifndef VOXELCRAFT_PLANET_ATMOSPHERE_H
#define VOXELCRAFT_PLANET_ATMOSPHERE_H

#include "space/planet_profile.h"
#include "world/atmosphere_model.h"

/* A null planet denotes Homeworld's Earth column. No mutable world state. */
AtmosphereInput PlanetAtmosphereInput(const PlanetProfile *planet);
float PlanetAtmosphereTopHeight(const PlanetProfile *planet);
double PlanetAtmosphereAltitudeM(const PlanetProfile *planet, float worldY);
AtmosphereSample PlanetAtmosphereAtHeight(const PlanetProfile *planet, float worldY);
float PlanetAtmosphereSkyVisibility(AtmosphereSample sample);
float PlanetAtmosphereFlightDensity(AtmosphereSample sample);

#endif
