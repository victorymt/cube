#ifndef VOXELCRAFT_SPACE_H
#define VOXELCRAFT_SPACE_H

#include "stellar.h"
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
    float equilibriumTempK;
    float atmosphereDensity;
    float oceanCoverage;
    float terrainRoughness;
    // Degrees per game time unit; one game time unit is defined in space_units.
    float rotationRate;
    float tidalLockFactor;
    float ringTilt;
    float albedo;
    float greenhouseEffect;
    float axialTilt;
    float seasonPhase;
    float yearLength; // Game time units.
    float prevailingWindAngle;
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

typedef struct PlanetLightState {
    Vector3 sunDirection;
    Vector3 moonDirection;
    Vector3 sourceDirections[MAX_SOLAR_LIGHTS];
    Color sourceColors[MAX_SOLAR_LIGHTS];
    float sourceIntensities[MAX_SOLAR_LIGHTS];
    float sourceVisibility[MAX_SOLAR_LIGHTS];
    Color starColor;
    float daylight;
    float sunset;
    float ringShadow;
    float eclipse;
    float moonIllumination;
    float totalIntensity;
    int sourceCount;
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
    double physicalRadiusKm;
    double semiMajorAxisKm;
    float spaceProxyRadius;
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
    bool hasStar;
    int starX;
    int starY;
    int starZ;
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
void UpdateSpaceChunks(Vector3 playerPosition, int groundRenderDistance, int generationPerFrame);
void SpaceProcessFinishedGenJobs(void);
void SpaceUpdateStarGlow(Vector3 playerPosition);
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
float SolarLightIrradianceAt(const SolarLightSource *source, Vector3 point);
float SolarSystemIrradianceAt(const SolarLightSource *sources, int sourceCount,
                              Vector3 point);
PlanetProfile SolarPlanetProfile(const SolarSystemDef *sys, int index);
Vector3 PlanetWorldSpaceReference(void);
Vector3 PlanetWorldSkyDirection(Vector3 worldDirection);
bool SurfaceHostSystem(SolarSystemDef *out);
float SolarBodyTerrainProxyRadius(float spaceProxyRadius);
int StarSystemsNear(Vector3 pos, float maxDist, SolarSystemDef *out, int maxCount);
bool FindNearestSystem(Vector3 pos, float maxDist, SolarSystemDef *out, float *outDist);
int SpaceBodiesNear(Vector3 pos, float maxDist, SpaceBodyInfo *out, int maxCount);
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
