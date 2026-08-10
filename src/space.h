#ifndef VOXELCRAFT_SPACE_H
#define VOXELCRAFT_SPACE_H

#include "stellar.h"
#include "space_satellite.h"
#include "types.h"

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

typedef enum SolarBodyStyle {
    SOLAR_STYLE_SUN = 0,
    SOLAR_STYLE_LAVA,
    SOLAR_STYLE_ICE,
    SOLAR_STYLE_DESERT,
    SOLAR_STYLE_GAS,
    SOLAR_STYLE_CRATER,
    SOLAR_STYLE_TEMPERATE
} SolarBodyStyle;

typedef enum PlanetAtmosphereType {
    PLANET_ATMOSPHERE_NONE = 0,
    PLANET_ATMOSPHERE_THIN,
    PLANET_ATMOSPHERE_BREATHABLE,
    PLANET_ATMOSPHERE_DENSE,
    PLANET_ATMOSPHERE_CORROSIVE
} PlanetAtmosphereType;

typedef struct PlanetProfile {
    uint32_t seed;
    SolarBodyStyle style;
    PlanetAtmosphereType atmosphereType;
    double physicalRadiusKm;
    double massKg;
    float spaceProxyRadius;
    float surfaceGravity;
    double receivedIrradiance; // Earth solar-constant units.
    float radiativeTempK;
    // Mean surface temperature after albedo and atmospheric greenhouse feedback.
    float equilibriumTempK;
    float surfacePressureAtm;
    float atmosphereDensity;
    float oceanCoverage;
    float iceCoverage;
    float cloudCoverage;
    float terrainRoughness;
    float ageGyr;
    // Degrees per game time unit; one game time unit is defined in space_units.
    float rotationRate;
    float tidalLockFactor;
    float ringTilt;
    float albedo;
    // Grey-atmosphere optical depth, not an additional temperature offset.
    float greenhouseEffect;
    float axialTilt;
    float seasonPhase;
    float yearLength; // Game time units.
    float prevailingWindAngle;
    float windStrength;
    float volcanicActivity;
    float impactRate;
    bool hasSolidSurface;
    bool hasRings;
    bool tidallyLocked;
} PlanetProfile;

#define MAX_SOLAR_LIGHTS 3

typedef struct SolarLightSource {
    Vector3 center;
    StellarProfile stellar;
    SpectrumType spectrum;
    float spaceProxyRadius;
    float luminosity;
    bool primary;
} SolarLightSource;

typedef struct SolarStellarBody {
    Vector3 center;
    Vector3 velocity;
    StellarProfile stellar;
    SpectrumType spectrum;
    float spaceProxyRadius;
    float luminosity;
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
    float totalIntensity;
    int sourceCount;
    bool hasMoon;
    bool specialEclipse;
} PlanetLightState;

typedef struct SolarPlanetDef {
    // Canonical orbital and body dimensions. Proxy size is presentation only.
    double semiMajorAxisKm;
    double physicalRadiusKm;
    float spaceProxyRadius;
    int yOffset;
    SolarBodyStyle style;
} SolarPlanetDef;

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
    SolarPlanetDef planets[6];
} SolarSystemDef;

typedef struct SpaceBodyInfo {
    Vector3 center; // Scene position in game distance units.
    Vector3 velocity; // Scene velocity in game distance units per game time unit.
    double physicalRadiusKm;
    double semiMajorAxisKm;
    double parentMassKg;
    float spaceProxyRadius;
    float landingProxyRadius;
    float encounterRadiusGame;
    double currentIrradianceEarth;
    float dist; // Observer distance in game distance units.
    bool isStar;
    int index;
    int systemAnchorX;
    int systemAnchorZ;
    uint32_t worldSeed;
    char name[32];
    StellarProfile hostStar;
    SpectrumType spectrum;
    SolarBodyStyle style;
    PlanetProfile profile;
} SpaceBodyInfo;

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
double SpaceSimulationTime(void); // Game time units.
bool SpaceRebasePlayer(Player *player);
int SpaceOriginX(void);
int SpaceOriginZ(void);
void SpaceResetOrigin(void);
void SpaceSaveOrigin(FILE *file);
bool SpaceLoadOrigin(FILE *file);
bool SpaceSaveState(FILE *file);
bool SpaceLoadState(FILE *file);
void UpdateSpaceChunks(Vector3 playerPosition, int groundRenderDistance, int generationPerFrame);
void SpaceProcessFinishedGenJobs(void);
void SpaceUpdateSolarGlow(Vector3 playerPosition);
BlockType SpaceBlockAt(int x, int y, int z);
bool SpaceBlockReadyAt(int x, int y, int z);
void SpaceSetBlock(int x, int y, int z, BlockType type);
void SpaceSaveEdits(FILE *file);
void SpaceLoadEdits(FILE *file);
void UnloadAllSpaceChunks(void);
int GetActiveSpaceChunkCount(void);
int GetSpaceEditCount(void);
void SpaceRebuildTorchList(void);

bool StarSystemAt(int ax, int az, SolarSystemDef *out);
Vector3 SolarSystemApparentDirection(const SolarSystemDef *sys, Vector3 observer);
Vector3 SolarSystemPlanetCenter(const SolarSystemDef *sys, int index);
Vector3 SolarSystemPlanetPositionAtTime(const SolarSystemDef *sys, int index,
                                        double simulationTime);
double SolarSystemPlanetOrbitPeriodSeconds(const SolarSystemDef *sys, int index);
double SolarSystemPlanetOrbitPeriodGameTime(const SolarSystemDef *sys, int index);
int SolarSystemLightSources(const SolarSystemDef *sys, SolarLightSource *out,
                            int maxCount);
int SolarSystemStellarBodiesAtTime(const SolarSystemDef *sys,
                                   double simulationTime,
                                   SolarStellarBody *out, int maxCount);
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
int StarSystemsNear(Vector3 pos, float maxDist, SolarSystemDef *out, int maxCount);
bool FindNearestSystem(Vector3 pos, float maxDist, SolarSystemDef *out, float *outDist);
int SpaceBodiesNear(Vector3 pos, float maxDist, SpaceBodyInfo *out, int maxCount);
bool SpaceBodyScaleDiagnostics(const SpaceBodyInfo *body,
                               SpaceScaleDiagnostics *out);
bool SpaceScaleDiagnosticsAt(Vector3 observer, SpaceScaleDiagnostics *out);
bool SpaceBodyPick(Vector3 origin, Vector3 direction, SpaceBodyInfo *out);
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
bool HomeWorldSaveState(FILE *file);
bool HomeWorldLoadState(FILE *file);
bool PlanetWorldIsActive(void);
uint32_t PlanetWorldSeed(void);
SolarBodyStyle PlanetWorldStyle(void);
const PlanetProfile *PlanetWorldProfile(void);
float PlanetWorldGravityScale(void);
bool PlanetWorldIsDarkSide(void);
bool PlanetWorldLightStateAt(Vector3 surfacePosition, PlanetLightState *out);
float PlanetWorldDaylightAt(Vector3 surfacePosition);
int PlanetWorldOriginX(void);
int PlanetWorldOriginZ(void);
const char *PlanetWorldName(void);
bool PlanetWorldLandingTarget(Vector3 position, SpaceBodyInfo *out);
bool PlanetWorldBeginDescent(Player *player, Vector3 *outLandingPosition);
bool PlanetWorldTryEnter(Player *player);
bool PlanetWorldTryLaunch(Player *player);
void PlanetWorldReset(void);
bool PlanetWorldSaveState(FILE *file);
bool PlanetWorldLoadState(FILE *file);
Color SpectrumColor(SpectrumType type);
const char *SpectrumName(SpectrumType type);
const char *SolarStyleName(SolarBodyStyle style);
const char *PlanetAtmosphereName(PlanetAtmosphereType type);

#endif
