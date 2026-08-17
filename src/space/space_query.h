#ifndef VOXELCRAFT_SPACE_QUERY_H
#define VOXELCRAFT_SPACE_QUERY_H

#include "space/space_types.h"

bool StarSystemAt(int ax, int az, SolarSystemDef *out);
Vector3 SolarSystemApparentDirection(const SolarSystemDef *sys,
                                     Vector3 observer);
Vector3 SolarSystemPlanetCenter(const SolarSystemDef *sys, int index);
Vector3 SolarSystemPlanetPositionAtTime(const SolarSystemDef *sys, int index,
                                        double simulationTime);
bool SolarSystemPlanetStateAtTime(const SolarSystemDef *sys, int index,
                                  double simulationTime,
                                  SolarPlanetOrbitalState *out);
double SolarSystemPlanetOrbitPeriodSeconds(const SolarSystemDef *sys,
                                           int index);
double SolarSystemPlanetOrbitPeriodGameTime(const SolarSystemDef *sys,
                                            int index);
float SolarSystemParkingRadiusGame(const SolarSystemDef *sys);
float SolarSystemPlanetEncounterRadiusGame(const SolarSystemDef *sys,
                                           int index);
float SolarSystemPlanetSupercruiseExitRadiusGame(const SolarSystemDef *sys,
                                                 int index);
float SolarSystemPlanetParkingRadiusGame(const SolarSystemDef *sys, int index);
float HomeWorldParkingRadiusGame(void);
int SolarSystemLightSources(const SolarSystemDef *sys, SolarLightSource *out,
                            int maxCount);
int SolarSystemStellarBodiesAtTime(const SolarSystemDef *sys,
                                   double simulationTime,
                                   SolarStellarBody *out, int maxCount);
bool SolarSystemEvaluateAtTime(const SolarSystemDef *sys,
                               double simulationTime,
                               SolarSystemRuntimeState *out);
bool SolarSystemEvaluateAtElapsedTime(const SolarSystemDef *sys,
                                      double elapsedTime,
                                      SolarSystemRuntimeState *out);
bool SolarSystemRemnantEnvironmentAt(
    const SolarSystemRuntimeState *runtime, Vector3 position,
    SpaceRemnantEnvironment *out);
bool SpaceRemnantEnvironmentAt(Vector3 position,
                               SpaceRemnantEnvironment *out);
int SolarSystemRuntimeLightSources(const SolarSystemRuntimeState *runtime,
                                   SolarLightSource *out, int maxCount);
bool SolarSystemPhysicalSummaryForSystem(
    const SolarSystemDef *sys, SolarSystemPhysicalSummary *out);
double SolarSystemStellarMassKg(const SolarSystemDef *sys);
float SolarLightIrradianceAt(const SolarLightSource *source, Vector3 point);
float SolarSystemIrradianceAt(const SolarLightSource *sources, int sourceCount,
                              Vector3 point);
PlanetProfile SolarPlanetProfile(const SolarSystemDef *sys, int index);
bool SolarPlanetSatelliteOrbit(const SolarSystemDef *sys, int index,
                               const PlanetProfile *profile,
                               SpaceSatelliteOrbit *out);
Vector3 PlanetWorldSpaceReference(void);
Vector3 PlanetWorldSkyDirection(Vector3 worldDirection);
bool SurfaceHostSystem(SolarSystemDef *out);
float SolarBodyTerrainProxyRadius(float spaceProxyRadius);
int StarSystemsNear(Vector3 pos, float maxDist, SolarSystemDef *out,
                    int maxCount);
bool FindNearestSystem(Vector3 pos, float maxDist, SolarSystemDef *out,
                       float *outDist);
int SpaceBodiesNear(Vector3 pos, float maxDist, SpaceBodyInfo *out,
                    int maxCount);
bool SpacePlanetBodyAt(int systemAnchorX, int systemAnchorZ, int planetIndex,
                       Vector3 observer, SpaceBodyInfo *out);
int SpaceSatellitesNear(Vector3 pos, float maxDist, SpaceSatelliteInfo *out,
                        int maxCount);
bool SpaceBodyScaleDiagnostics(const SpaceBodyInfo *body,
                               SpaceScaleDiagnostics *out);
bool SpaceScaleDiagnosticsAt(Vector3 observer, SpaceScaleDiagnostics *out);
bool SpaceSatelliteScaleDiagnosticsAt(
    Vector3 observer, SpaceSatelliteScaleDiagnostics *out);
bool SpaceBodyPick(Vector3 origin, Vector3 direction, SpaceBodyInfo *out);
bool SpacePlanetNavigationPick(Vector3 origin, Vector3 direction,
                               SpaceBodyInfo *out);
float PlanetBodyTextureRotation(const SpaceBodyInfo *body);
bool PlanetSurfaceAt(Vector3 position, Vector3 *gravityDir, float *surfaceDist,
                     float *gravityScale);
bool SpaceGravityAt(Vector3 position, SpaceGravitySample *out);

void SpaceQueryCacheClear(void);
SpaceQueryCacheStats SpaceQueryCacheGetStats(void);

#endif
