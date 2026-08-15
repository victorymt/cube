#ifndef VOXELCRAFT_SPACE_H
#define VOXELCRAFT_SPACE_H

#include "planet_profile.h"
#include "space_barycenter.h"
#include "space_orbit.h"
#include "space_remnant.h"
#include "stellar.h"
#include "space_satellite.h"
#include "space_coordinates.h"
#include "types.h"
#include "render_resources.h"

#include <stdio.h>
#include <stdint.h>

#define MAX_SPACE_CHUNKS ((SPACE_RENDER_DISTANCE_CHUNKS * 2 + 1) * (SPACE_RENDER_DISTANCE_CHUNKS * 2 + 1))
#define MAX_SPACE_EDITS 65536
#define MAX_SPACE_GEN_JOBS 16

#define STAR_SYSTEM_SPACING 1400
#define STAR_SYSTEM_PROBABILITY 65u
#define STAR_NAVIGATION_RANGE 8000.0f
#define STAR_NAVIGATION_MAX_SYSTEMS 128
#define STAR_SYSTEM_QUERY_MAX 384

#define MAX_SOLAR_LIGHTS PLANET_PROFILE_MAX_STARS
#define MAX_SOLAR_PLANETS 8
#define MAX_SOLAR_SATELLITES 24

typedef struct SolarLightSource {
    Vector3 center;
    StellarProfile stellar;
    SpectrumType spectrum;
    float spaceProxyRadius;
    float luminosity;
    bool primary;
} SolarLightSource;

typedef struct SolarStellarBody {
    CelestialPosition celestialPosition;
    CelestialVector3 celestialVelocityKmPerSecond;
    Vector3 center;
    Vector3 velocity;
    StellarProfile stellar;
    SpectrumType spectrum;
    float spaceProxyRadius;
    float luminosity;
    SpaceRemnantState remnant;
    int index;
    bool primary;
} SolarStellarBody;

typedef struct PlanetLightState {
    Vector3 sunDirection;
    Vector3 moonDirection;
    Vector3 sourceDirections[MAX_SOLAR_LIGHTS];
    Color sourceColors[MAX_SOLAR_LIGHTS];
    float sourceIntensities[MAX_SOLAR_LIGHTS];
    float sourceVisibility[MAX_SOLAR_LIGHTS];
    float sourceOccultations[MAX_SOLAR_LIGHTS];
    Color starColor;
    float daylight;
    float sunset;
    float ringShadow;
    float eclipse;
    float moonIllumination;
    float moonAngularRadius;
    float moonUmbra;
    float solarDeclination;
    float dayLengthFraction;
    float incidentIrradiance;
    float totalIntensity;
    int sourceCount;
    bool hasMoon;
    bool specialEclipse;
} PlanetLightState;

typedef struct SolarPlanetDef {
    // Canonical orbital and body dimensions. Proxy size is presentation only.
    uint32_t bodyId;
    char name[24];
    double semiMajorAxisKm;
    double physicalRadiusKm;
    double physicalMassKg;
    double rotationPeriodSeconds;
    float axialTiltRad;
    float formationMassEarth;
    float spaceProxyRadius;
    int yOffset;
    SolarBodyStyle style;
    bool formationGasGiant;
    bool hasCanonicalOrbit;
    double orbitalEccentricity;
    double orbitalInclinationRad;
    double orbitalLongitudeAscendingNodeRad;
    double orbitalArgumentPeriapsisRad;
    double orbitalMeanAnomalyAtEpochRad;
} SolarPlanetDef;

typedef struct SolarSatelliteDef {
    uint32_t bodyId;
    int parentPlanetIndex;
    char name[24];
    SpaceSatelliteOrbit orbit;
} SolarSatelliteDef;

typedef enum SolarPlanetDynamicalStatus {
    SOLAR_PLANET_STABLE = 0,
    SOLAR_PLANET_ENGULFED,
    SOLAR_PLANET_EJECTED
} SolarPlanetDynamicalStatus;

typedef struct SolarSystemPhysicalSummary {
    int stellarCount;
    double totalMassKg;
    float totalLuminositySolar;
    float stellarLuminositiesSolar[MAX_SOLAR_LIGHTS];
    float ageGyr;
} SolarSystemPhysicalSummary;

typedef struct SolarSystemPhysicalSnapshot {
    bool valid;
    bool satellitesBuilt;
    uint32_t stellarHash;
    StellarProfile stellarProfiles[MAX_SOLAR_LIGHTS];
    SpaceBarycenterOrbit stellarOrbit;
    SolarSystemPhysicalSummary summary;
    SolarPlanetDynamicalStatus planetStatuses[MAX_SOLAR_PLANETS];
    SpaceKeplerOrbit planetOrbits[MAX_SOLAR_PLANETS];
    SpaceSatelliteOrbit satelliteOrbits[MAX_SOLAR_PLANETS];
    int satelliteCount;
    int satelliteParentPlanetIndices[MAX_SOLAR_SATELLITES];
    SpaceSatelliteOrbit allSatelliteOrbits[MAX_SOLAR_SATELLITES];
    float minimumPlanetOrbitGame;
} SolarSystemPhysicalSnapshot;

typedef struct SolarSystemDef {
    bool exists;
    int anchorX;
    int anchorZ;
    Vector3 center; // Rebasing-aware scene position in game distance units.
    char name[32];
    StellarProfile star;
    SpectrumType spectrum;
    int starProxyRadius;
    int planetCount;
    float formationMetallicity;
    float formationDiskMassEarth;
    double snowLineKm;
    double habitableZoneInnerKm;
    double habitableZoneOuterKm;
    SolarPlanetDef planets[MAX_SOLAR_PLANETS];
    int satelliteCount;
    SolarSatelliteDef satellites[MAX_SOLAR_SATELLITES];
    SolarSystemPhysicalSnapshot physicalSnapshot;
} SolarSystemDef;

typedef struct SolarPlanetOrbitalState {
    CelestialPosition celestialPosition;
    CelestialVector3 celestialVelocityKmPerSecond;
    Vector3 center;
    Vector3 velocity;
} SolarPlanetOrbitalState;

typedef struct SolarPlanetRuntimeState {
    bool valid;
    SolarPlanetDynamicalStatus dynamicalStatus;
    CelestialPosition celestialPosition;
    CelestialVector3 celestialVelocityKmPerSecond;
    Vector3 center;
    Vector3 velocity;
    double semiMajorAxisKm;
    PlanetProfile profile;
    float currentIrradianceEarth;
    SpaceSatelliteOrbit satelliteOrbit;
    SpaceSatelliteState satelliteState;
    Vector3 satelliteCenter;
    Vector3 satelliteVelocity;
    SpaceRemnantEnvironment remnantEnvironment;
} SolarPlanetRuntimeState;

typedef struct SolarSystemRuntimeState {
    bool valid;
    double simulationTime;
    double totalStellarMassKg;
    int stellarCount;
    int planetCount;
    SolarStellarBody stars[MAX_SOLAR_LIGHTS];
    SolarPlanetRuntimeState planets[MAX_SOLAR_PLANETS];
} SolarSystemRuntimeState;

typedef struct SpaceBodyInfo {
    CelestialPosition celestialPosition;
    CelestialVector3 celestialVelocityKmPerSecond;
    Vector3 center; // Scene position in game distance units.
    Vector3 velocity; // Scene velocity in game distance units per game time unit.
    double physicalRadiusKm;
    float physicalRadiusGame;
    double semiMajorAxisKm;
    double parentMassKg;
    float spaceProxyRadius;
    float landingProxyRadius;
    float encounterRadiusGame;
    double currentIrradianceEarth;
    float dist; // Observer distance in game distance units.
    bool isStar;
    uint32_t bodyId;
    int index;
    int systemAnchorX;
    int systemAnchorZ;
    uint32_t worldSeed;
    SpaceRemnantState remnant;
    SpaceRemnantEnvironment remnantEnvironment;
    char name[32];
    StellarProfile hostStar;
    SpectrumType spectrum;
    SolarBodyStyle style;
    PlanetProfile profile;
} SpaceBodyInfo;

typedef struct SpaceSatelliteInfo {
    Vector3 center;
    Vector3 velocity;
    double physicalRadiusKm;
    double massKg;
    double semiMajorAxisKm;
    float encounterRadiusGame;
    float dist;
    bool isSatellite;
    int index;
    uint32_t bodyId;
    int parentPlanetIndex;
    int systemAnchorX;
    int systemAnchorZ;
    uint32_t worldSeed;
    char name[40];
    SpaceSatelliteOrbit orbit;
    SpaceSatelliteState state;
} SpaceSatelliteInfo;

typedef struct SpaceSatelliteScaleDiagnostics {
    char bodyName[40];
    Vector3 center;
    Vector3 velocity;
    double physicalRadiusKm;
    double physicalRadiusGame;
    double physicalGravityMetersPerSecondSquared;
    double orbitalSpeedKilometersPerSecond;
    double sphereOfInfluenceKm;
    double hillSphereKm;
    float encounterRadiusGame;
    float distanceGame;
    bool withinErrorBudget;
} SpaceSatelliteScaleDiagnostics;

typedef struct SpaceScaleDiagnostics {
    char bodyName[40];
    double physicalRadiusKm;
    double physicalRadiusGame;
    float visualRadiusGame;
    float landingRadiusGame;
    double landingRadiusScale;
    double physicalGravityMetersPerSecondSquared;
    double physicalGravityEarth;
    double gameplaySurfaceGravity;
    double orbitalSpeedKilometersPerSecond;
    double orbitalSpeedGame;
    double sphereOfInfluenceKm;
    double hillSphereKm;
    double physicalSphereOfInfluenceGame;
    float encounterRadiusGame;
    double encounterRadiusScale;
    double currentIrradianceEarth;
    double climateIrradianceEarth;
    float radiativeTemperatureK;
    float surfaceTemperatureK;
    double maxRelativeError;
    bool encounterRadiusClamped;
    bool withinErrorBudget;
} SpaceScaleDiagnostics;

typedef struct SpaceQueryCacheStats {
    uint64_t definitionHits;
    uint64_t definitionMisses;
    uint64_t runtimeHits;
    uint64_t runtimeMisses;
} SpaceQueryCacheStats;

typedef enum SpaceGravityPrimaryKind {
    SPACE_GRAVITY_PRIMARY_NONE = 0,
    SPACE_GRAVITY_PRIMARY_STAR,
    SPACE_GRAVITY_PRIMARY_PLANET,
    SPACE_GRAVITY_PRIMARY_HOME
} SpaceGravityPrimaryKind;

typedef struct SpaceGravitySample {
    bool active;
    SpaceGravityPrimaryKind kind;
    Vector3 center;
    Vector3 primaryVelocity; // Game distance units per game time unit.
    Vector3 acceleration; // Game distance units per game time unit squared.
    float distance; // Game distance units.
    float surfaceDistance; // Relative to the encounter proxy, in game units.
    float encounterRadiusGame;
    float gravitationalParameterGame;
    char name[40];
} SpaceGravitySample;

typedef struct SpaceChunk {
    bool loaded;
    bool generating;
    bool dirty;
    bool hasModel;
    bool hasWaterModel;
    int cx;
    int cz;
    Model model;
    Model waterModel;
    unsigned short blocks[CHUNK_SIZE][SPACE_LAYER_HEIGHT][CHUNK_SIZE];
} SpaceChunk;

extern SpaceChunk spaceChunks[MAX_SPACE_CHUNKS];

void SpaceInit(void);
void SpaceShutdown(void);
void SpaceReset(void);
void SpaceAdvanceTime(float gameTimeDelta);
double SpaceSimulationTime(void); // Monotonic game time for public consumers.
double SpacePeriodicSimulationTime(double elapsedTime); // Bounded phase time.
double SpaceElapsedSimulationTime(void); // Compatibility monotonic clock.
bool SpaceRebasePlayer(Player *player);
int SpaceOriginX(void);
int SpaceOriginZ(void);
void SpaceResetOrigin(void);
void SpaceSaveOrigin(FILE *file);
bool SpaceLoadOrigin(FILE *file);
bool SpaceSaveState(FILE *file);
bool SpaceLoadState(FILE *file);
bool SpaceLoadLegacyState(FILE *file);
void UpdateSpaceChunks(Vector3 playerPosition, int groundRenderDistance, int generationPerFrame);
void SpaceProcessFinishedGenJobs(void);
void SpaceUpdateSolarGlow(Vector3 playerPosition);
BlockType SpaceBlockAt(int x, int y, int z);
bool SpaceBlockReadyAt(int x, int y, int z);
void SpaceSetBlock(int x, int y, int z, BlockType type);
bool SpaceSaveEdits(FILE *file);
bool SpaceLoadEdits(FILE *file, int storedLayerY);
void UnloadAllSpaceChunks(void);
int GetActiveSpaceChunkCount(void);
RenderResourceSnapshot SpaceGetRenderResourceSnapshot(void);
int GetSpaceEditCount(void);
void SpaceRebuildTorchList(void);
void SpaceQueryCacheClear(void);
SpaceQueryCacheStats SpaceQueryCacheGetStats(void);

bool StarSystemAt(int ax, int az, SolarSystemDef *out);
Vector3 SolarSystemApparentDirection(const SolarSystemDef *sys, Vector3 observer);
Vector3 SolarSystemPlanetCenter(const SolarSystemDef *sys, int index);
Vector3 SolarSystemPlanetPositionAtTime(const SolarSystemDef *sys, int index,
                                        double simulationTime);
bool SolarSystemPlanetStateAtTime(const SolarSystemDef *sys, int index,
                                  double simulationTime,
                                  SolarPlanetOrbitalState *out);
double SolarSystemPlanetOrbitPeriodSeconds(const SolarSystemDef *sys, int index);
double SolarSystemPlanetOrbitPeriodGameTime(const SolarSystemDef *sys, int index);
float SolarSystemParkingRadiusGame(const SolarSystemDef *sys);
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
                                      SolarSystemRuntimeState *out); // Game days.
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
bool PlanetProfileSaveState(FILE *file, const PlanetProfile *profile);
bool PlanetProfileLoadState(FILE *file, PlanetProfile *outProfile);
bool SolarPlanetSatelliteOrbit(const SolarSystemDef *sys, int index,
                               const PlanetProfile *profile,
                               SpaceSatelliteOrbit *out);
Vector3 PlanetWorldSpaceReference(void);
Vector3 PlanetWorldSkyDirection(Vector3 worldDirection);
bool SurfaceHostSystem(SolarSystemDef *out);
float SolarBodyTerrainProxyRadius(float spaceProxyRadius);
int StarSystemsNear(Vector3 pos, float maxDist, SolarSystemDef *out, int maxCount);
bool FindNearestSystem(Vector3 pos, float maxDist, SolarSystemDef *out, float *outDist);
int SpaceBodiesNear(Vector3 pos, float maxDist, SpaceBodyInfo *out, int maxCount);
int SpaceSatellitesNear(Vector3 pos, float maxDist,
                        SpaceSatelliteInfo *out, int maxCount);
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
bool HomeWorldSurfaceIsActive(void);
Vector3 HomeWorldCenter(void);
float HomeWorldProxyRadius(void);
float HomeWorldSpaceFade(Vector3 position);
float PlanetWorldAtmosphereFade(Vector3 position);
bool HomeWorldTryLaunch(Player *player);
bool HomeWorldCanEnter(Vector3 position);
bool HomeWorldBeginDescent(Player *player, Vector3 *outLandingPosition);
bool HomeWorldTryEnter(Player *player);
void HomeWorldReset(void);
void HomeWorldRestoreLegacyState(const Player *player);
void HomeWorldRestoreLegacyStateForSpaceLayer(const Player *player,
                                              int storedLayerY);
bool HomeWorldSaveState(FILE *file);
bool HomeWorldLoadState(FILE *file);
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
bool PlanetWorldLandingTarget(Vector3 position, SpaceBodyInfo *out);
bool PlanetWorldBeginDescent(Player *player, Vector3 *outLandingPosition);
bool PlanetWorldTryEnter(Player *player);
bool PlanetWorldTryLaunch(Player *player);
void PlanetWorldReset(void);
void PlanetWorldMigrateSpaceLayer(int storedLayerY);
bool PlanetWorldSaveState(FILE *file);
bool PlanetWorldLoadState(FILE *file);
Color SpectrumColor(SpectrumType type);
const char *SpectrumName(SpectrumType type);
const char *SolarStyleName(SolarBodyStyle style);
const char *PlanetAtmosphereName(PlanetAtmosphereType type);

#endif
