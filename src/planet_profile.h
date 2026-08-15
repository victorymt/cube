#ifndef VOXELCRAFT_PLANET_PROFILE_H
#define VOXELCRAFT_PLANET_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#define PLANET_PROFILE_MAX_STARS 3

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
    // Zero for generated worlds; 1-8 identify the canonical planets of Sol.
    uint32_t canonicalBodyId;
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
    float orbitalEccentricity;
    float orbitalMeanAnomalyAtEpoch;
    float axialTilt;
    // Solstice orientation relative to the orbit, in radians.
    float seasonPhase;
    float yearLength; // Game time units.
    // Climate response terms derived from orbit, tilt, atmosphere, and oceans.
    float seasonalTemperatureAmplitudeK;
    float orbitalTemperatureAmplitudeK;
    float polarIceVariability;
    float seasonalHumidityBias;
    float prevailingWindAngle;
    float windStrength;
    float volcanicActivity;
    float impactRate;
    bool hasSolidSurface;
    bool hasRings;
    bool tidallyLocked;
} PlanetProfile;

typedef struct PlanetProfileGenerationInput {
    uint32_t seed;
    double semiMajorAxisKm;
    double physicalRadiusKm;
    float formationMassEarth;
    float spaceProxyRadius;
    float stellarAgeGyr;
    float stellarLuminositiesSolar[PLANET_PROFILE_MAX_STARS];
    double orbitalEccentricity;
    double orbitalMeanAnomalyAtEpochRad;
    float orbitalPeriodGameTime;
    int stellarCount;
    int planetIndex;
    bool formationGasGiant;
    bool forcedGasGiant;
} PlanetProfileGenerationInput;

typedef struct PlanetSeasonState {
    float meanAnomaly;
    float seasonAngle;
    float solarDeclination;
    float dayLengthFraction;
    float irradianceScale;
    float temperatureDeltaK;
    float seasonalAmplitudeK;
} PlanetSeasonState;

bool PlanetProfileGenerate(const PlanetProfileGenerationInput *input,
                           PlanetProfile *out);
PlanetProfile PlanetProfileGenerateLegacy(uint32_t seed, SolarBodyStyle style,
                                          float terrainRadius);
bool PlanetSeasonEvaluate(const PlanetProfile *profile, float latitude,
                          double simulationTime, PlanetSeasonState *out);

#endif
