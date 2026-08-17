#ifndef VOXELCRAFT_SPACE_STATE_H
#define VOXELCRAFT_SPACE_STATE_H

#include "space/space_types.h"

double SpaceSimulationTime(void);
double SpacePeriodicSimulationTime(double elapsedTime);
double SpaceElapsedSimulationTime(void);
int SpaceOriginX(void);
int SpaceOriginZ(void);

bool HomeWorldSurfaceIsActive(void);
Vector3 HomeWorldCenter(void);
float HomeWorldProxyRadius(void);
float HomeWorldSpaceFade(Vector3 position);

bool PlanetWorldIsActive(void);
uint32_t PlanetWorldSeed(void);
SolarBodyStyle PlanetWorldStyle(void);
const PlanetProfile *PlanetWorldProfile(void);
SpaceRemnantEnvironment PlanetWorldRemnantEnvironment(void);
float PlanetWorldGravityScale(void);
bool PlanetWorldIsDarkSide(void);
bool PlanetWorldLightStateAt(Vector3 surfacePosition, PlanetLightState *out);
bool PlanetWorldLightStateAtTime(Vector3 surfacePosition,
                                 double simulationTime,
                                 PlanetLightState *out);
float PlanetWorldDaylightAt(Vector3 surfacePosition);
int PlanetWorldOriginX(void);
int PlanetWorldOriginZ(void);
const char *PlanetWorldName(void);
float PlanetWorldAtmosphereFade(Vector3 position);

Color SpectrumColor(SpectrumType type);
const char *SpectrumName(SpectrumType type);
const char *SolarStyleName(SolarBodyStyle style);
const char *PlanetAtmosphereName(PlanetAtmosphereType type);

#endif
