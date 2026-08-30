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

/*
 * Optional local water-cycle feedback supplied by a runtime simulation.
 *
 * `soilMoisture` is a normalized 0..1 saturation fraction. Surface and
 * groundwater depths are metres, runoff and evaporation are metres per
 * second (runoff is per cell area when supplied by a grid model). The
 * `active` flag keeps legacy callers, which zero-initialize or omit this
 * trailing field, exactly equivalent to having no hydrology model.
 */
typedef struct LocalClimateHydrologyFeedback {
    bool active;
    float surfaceWaterM;
    float soilMoisture;
    float groundwaterM;
    float runoffM3PerS;
    float evaporationMPerS;
} LocalClimateHydrologyFeedback;

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
    LocalClimateHydrologyFeedback hydrology;
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
