#ifndef VOXELCRAFT_LOCAL_CLIMATE_H
#define VOXELCRAFT_LOCAL_CLIMATE_H

#include <stdbool.h>

typedef enum ClimateRegime {
    CLIMATE_REGIME_VACUUM = 0,
    CLIMATE_REGIME_ICE_CAP,
    CLIMATE_REGIME_TUNDRA,
    CLIMATE_REGIME_BOREAL,
    CLIMATE_REGIME_HUMID_CONTINENTAL,
    CLIMATE_REGIME_OCEANIC,
    CLIMATE_REGIME_SEASONAL_TEMPERATE,
    CLIMATE_REGIME_MEDITERRANEAN,
    CLIMATE_REGIME_STEPPE,
    CLIMATE_REGIME_DESERT,
    CLIMATE_REGIME_SAVANNA,
    CLIMATE_REGIME_MONSOON,
    CLIMATE_REGIME_TROPICAL_RAINFOREST,
    CLIMATE_REGIME_HOT_GREENHOUSE,
    CLIMATE_REGIME_COUNT
} ClimateRegime;

typedef struct LocalClimateInput {
    float meanTemperatureK;
    float seasonalTemperatureDeltaK;
    float daylightTemperatureDeltaK;
    float surfacePressureAtm;
    float atmosphereDensity;
    float moisture;
    float cloudPotential;
    float windStrength;
    float latitude;
    float elevation;
    float waterCoverage;
    float iceCoverage;
    float seasonalAmplitudeK;
    bool hasAtmosphere;
    bool supportsWaterCycle;
} LocalClimateInput;

typedef struct LocalClimateState {
    ClimateRegime regime;
    float temperatureK;
    float pressureAtm;
    float relativeHumidity;
    float dewPointK;
    float wetBulbK;
    float aridity;
    float instability;
    float orographicLift;
    float cloudPotential;
    float windStrength;
    bool atmosphereActive;
    bool waterCycleActive;
} LocalClimateState;

bool LocalClimateEvaluate(const LocalClimateInput *input,
                          LocalClimateState *out);
const char *ClimateRegimeName(ClimateRegime regime);

#endif
