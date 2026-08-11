#ifndef VOXELCRAFT_SPACE_SYSTEM_PHYSICS_H
#define VOXELCRAFT_SPACE_SYSTEM_PHYSICS_H

#include "space.h"

bool SolarSystemPhysicalSnapshotBuild(
    const SolarSystemDef *sys, SolarSystemPhysicalSnapshot *out);
bool SolarSystemPhysicalSnapshotBuildSatellites(
    const SolarSystemDef *sys, SolarSystemPhysicalSnapshot *out);
const SolarSystemPhysicalSnapshot *SolarSystemPhysicalSnapshotForSystem(
    const SolarSystemDef *sys, SolarSystemPhysicalSnapshot *scratch);
int SolarSystemPhysicalSnapshotStellarBodiesAtTime(
    const SolarSystemDef *sys, const SolarSystemPhysicalSnapshot *snapshot,
    double simulationTime, SolarStellarBody *out, int maxCount);
int SolarSystemStellarVisualRadius(const StellarProfile *star);
uint32_t SolarSystemPlanetOrbitHash(const SolarSystemDef *sys, int index);
uint32_t SolarSystemPlanetPlaneHash(const SolarSystemDef *sys);

#endif
