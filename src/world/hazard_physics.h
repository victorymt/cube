#ifndef VOXELCRAFT_HAZARD_PHYSICS_H
#define VOXELCRAFT_HAZARD_PHYSICS_H

#include <stdbool.h>

typedef struct HazardFireState {
    double fuelKg;
    double moistureKg;
    double temperatureK;
    double smokeKg;
} HazardFireState;

typedef struct HazardFireParameters {
    double ignitionTemperatureK;
    double burnRatePerSecond;
    double heatReleaseMJPerKg;
    double moistureHeatMJPerKg;
    double heatCapacityMJPerK;
    double coolingPerSecond;
    double smokeYield;
    double smokeLossPerSecond;
    double windSpreadGain;
    double moistureExtinctionRatio;
} HazardFireParameters;

typedef struct HazardFireEnvironment {
    double airTemperatureK;
    double windSpeedMps;
    double relativeHumidity;
    double rainfallMmPerHour;
} HazardFireEnvironment;

typedef struct HazardFireFlux {
    double burnedFuelKg;
    double evaporatedMoistureKg;
    double heatReleasedMJ;
    double smokeProducedKg;
    double smokeRemovedKg;
    double spreadPotential;
} HazardFireFlux;

HazardFireParameters HazardFireDefaultParameters(void);
HazardFireEnvironment HazardFireDefaultEnvironment(void);
bool HazardFireStateValid(const HazardFireState *state);
bool HazardFireStep(HazardFireState *state,
                    const HazardFireParameters *parameters,
                    const HazardFireEnvironment *environment,
                    double seconds,
                    HazardFireFlux *flux);
double HazardFireSpreadPotential(const HazardFireState *state,
                                 const HazardFireParameters *parameters,
                                 const HazardFireEnvironment *environment,
                                 double distanceMeters);

typedef struct HazardSupercellState {
    double availableEnergyJ;
    double angularVelocityRadPerSecond;
    double radiusMeters;
    double pressureDeficitPa;
    double positionX;
    double positionZ;
    double velocityX;
    double velocityZ;
    double ageSeconds;
    bool active;
} HazardSupercellState;

typedef struct HazardSupercellParameters {
    double airDensityKgPerM3;
    double formationEnergyJ;
    double dissipationPerSecond;
    double buoyancyEnergyPerSecond;
    double minimumShearMps;
    double steeringPerSecond;
    double minimumRadiusMeters;
    double maximumRadiusMeters;
    double maximumPressureDeficitPa;
} HazardSupercellParameters;

typedef struct HazardSupercellEnvironment {
    double surfaceTemperatureK;
    double dewPointTemperatureK;
    double verticalShearMps;
    double ambientWindX;
    double ambientWindZ;
} HazardSupercellEnvironment;

typedef struct HazardSupercellFlux {
    double buoyancyEnergyJ;
    double dissipatedEnergyJ;
    double pressureDeficitPa;
    bool formed;
    bool dissipated;
} HazardSupercellFlux;

HazardSupercellParameters HazardSupercellDefaultParameters(void);
HazardSupercellEnvironment HazardSupercellDefaultEnvironment(void);
bool HazardSupercellStateValid(const HazardSupercellState *state);
bool HazardSupercellStep(HazardSupercellState *state,
                         const HazardSupercellParameters *parameters,
                         const HazardSupercellEnvironment *environment,
                         double seconds,
                         HazardSupercellFlux *flux);

#endif
