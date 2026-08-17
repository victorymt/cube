#ifndef VOXELCRAFT_SPACE_TYPES_H
#define VOXELCRAFT_SPACE_TYPES_H

#include "space/planet_profile.h"
#include "space/space_barycenter.h"
#include "space/space_coordinates.h"
#include "space/space_orbit.h"
#include "space/space_remnant.h"
#include "space/space_satellite.h"
#include "space/stellar.h"

#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

#define STAR_SYSTEM_SPACING 70000
#define STAR_SYSTEM_PROBABILITY 65u
#define STAR_NAVIGATION_RANGE 400000.0f
#define STAR_NAVIGATION_MAX_SYSTEMS 128
#define STAR_SYSTEM_QUERY_MAX 384
#define SOLAR_SYSTEM_QUERY_RADIUS (STAR_SYSTEM_SPACING * 0.5f)

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
    Vector3 center;
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
    Vector3 center;
    Vector3 velocity;
    double physicalRadiusKm;
    float physicalRadiusGame;
    double semiMajorAxisKm;
    double parentMassKg;
    float spaceProxyRadius;
    float landingProxyRadius;
    float encounterRadiusGame;
    double currentIrradianceEarth;
    float dist;
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

typedef struct SpaceTravelPose {
    Vector3 position;
    Vector3 velocity;
    float yaw;
    float pitch;
} SpaceTravelPose;

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
    Vector3 primaryVelocity;
    Vector3 acceleration;
    float distance;
    float surfaceDistance;
    float encounterRadiusGame;
    float gravitationalParameterGame;
    char name[40];
} SpaceGravitySample;

#endif
