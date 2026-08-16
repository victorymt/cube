#ifndef VOXELCRAFT_SPACE_INTERNAL_H
#define VOXELCRAFT_SPACE_INTERNAL_H

#include "space/space_dependencies.h"

#define ASTEROID_SPACING 26
#define ASTEROID_PROBABILITY 55u
#define SPACE_MESH_REBUILDS_PER_FRAME 2
#define STAR_SYSTEM_MID_Y \
    ((float)SPACE_LAYER_Y + (float)SPACE_LAYER_HEIGHT * 0.5f)
#define STAR_SYSTEM_VERTICAL_RANGE 46.0f
#define STAR_SKY_PHYSICAL_DISTANCE 700.0f
#define STAR_SKY_FULL_LATITUDE_DISTANCE 5000.0f
#define STAR_SKY_LATITUDE_SCALE 0.92f
#define SPACE_GRAVITY_QUERY_RADIUS (STAR_SYSTEM_SPACING * 0.58f)
#define SPACE_STAR_ENCOUNTER_RADIUS_GAME SPACE_GRAVITY_QUERY_RADIUS
#define SPACE_MAX_PLANET_ENCOUNTER_RADIUS_GAME 8500.0f
#define PLANET_LANDING_QUERY_RADIUS (STAR_SYSTEM_SPACING * (4.0f / 35.0f))
#define SOLAR_GLOW_QUERY_RADIUS (STAR_SYSTEM_SPACING * (3.0f / 70.0f))
#define SPACE_MAX_SYSTEM_QUERY_DISTANCE (STAR_NAVIGATION_RANGE * 4.0f)
#define PLANET_WORLD_STATE_VERSION 3u
#define PLANET_PROFILE_STATE_MAGIC 0x504c4e54u
#define PLANET_PROFILE_STATE_VERSION 3u
#define SPACE_STATE_MAGIC 0x53504345u
#define SPACE_STATE_VERSION 3u
#define SPACE_STATE_LEGACY_PROJECTION_VERSION 2u
#define HOME_WORLD_PROXY_RADIUS 6.0f
#define HOME_WORLD_CENTER_Y (-30.0f)
#define HOME_WORLD_LANDING_MARGIN 20.0f
#define PLANET_ATMOSPHERE_FADE_START \
    ((float)SURFACE_GENERATION_MAX_Y_EXCLUSIVE + 18.0f)
#define PLANET_ATMOSPHERE_MIN_DEPTH 64.0f
#define SPACE_REBASE_THRESHOLD (STAR_SYSTEM_SPACING * 12)

typedef struct PlanetWorldContext {
    bool active;
    uint32_t seed;
    SolarBodyStyle style;
    PlanetProfile profile;
    int originX;
    int originZ;
    int planetIndex;
    Vector3 bodyCenter;
    Vector3 returnPosition;
    float spaceProxyRadius;
    SpaceRemnantEnvironment remnantEnvironment;
    double remnantEnvironmentSimulationTime;
    char name[32];
} PlanetWorldContext;

typedef struct HomeWorldContext {
    bool surfaceActive;
    Vector3 returnPosition;
} HomeWorldContext;

extern PlanetWorldContext planetWorld;
extern HomeWorldContext homeWorld;
extern double solarElapsedSimulationTime;
extern int spaceOriginX;
extern int spaceOriginZ;
extern SpaceLoadError spaceLastLoadError;

bool SpaceVectorIsFinite(Vector3 value);
int ClampCoordinate(int64_t value);
int SpaceLocalToGlobalX(int localX);
int SpaceLocalToGlobalZ(int localZ);
int SpaceGlobalToLocalX(int globalX);
int SpaceGlobalToLocalZ(int globalZ);
int SpaceSystemGlobalCoordinate(int anchor);
int SpaceAnchorForLocalCoordinate(float local, int origin);

float PlanetEncounterRadiusGame(double semiMajorAxisKm, double bodyMassKg,
                                double parentMassKg,
                                double physicalRadiusKm);
Vector3 PlanetWorldSpaceDirection(Vector3 skyDirection);
bool SolarSystemApplyFormation(SolarSystemDef *sys, uint32_t seed);
bool PlanetProfileIsValid(const PlanetProfile *profile);
void PlanetProfileDeriveSeasonalFields(PlanetProfile *profile);
float PlanetAtmosphereDepth(const PlanetProfile *profile);
SpaceRemnantEnvironment PlanetWorldCurrentRemnantEnvironment(void);
bool SpaceQueryVectorIsFinite(Vector3 value);
bool SpacePointInSolarSystemBubble(int x, int z);
void BuildStarName(unsigned int hash, char *out, size_t outSize);
void ApplyPrimaryStar(SolarSystemDef *system, StellarProfile star);
void BuildSolSystem(SolarSystemDef *out);
bool SolarSystemPlanetIndexIsValid(const SolarSystemDef *sys, int index);
PlanetProfile SolarPlanetProfileForSnapshot(
    const SolarSystemDef *sys, int index,
    const SolarSystemPhysicalSnapshot *snapshot);

#endif
