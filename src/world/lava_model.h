#ifndef VOXELCRAFT_LAVA_MODEL_H
#define VOXELCRAFT_LAVA_MODEL_H
#include <stdbool.h>
#include <stdint.h>

#define LAVA_CAPACITY 1024u
#define LAVA_STEP_SECONDS 0.1
#define LAVA_REFERENCE_K 273.0
#define LAVA_FREEZING_K 1100.0
#define LAVA_LATENT_K 400.0
#define LAVA_SOURCE_K 1450.0
/* G3 parcel approximation: water enters at 293 K. In lava enthalpy units,
   1.5 * (373 - 293) sensible + 900 latent per block gives 4096 per 1/255
   water unit. No water-temperature field is stored or transported. */
#define LAVA_WATER_CAPACITY 255u
#define LAVA_WATER_BOILING_K 373.0
#define LAVA_WATER_UNIT_ENERGY 4096.0

/* Energy is heat-capacity-normalized enthalpy, in volume-unit kelvins.
   Constant density/heat capacity; an isothermal latent interval at 1100 K. */
typedef struct LavaThermalState {
    uint32_t volume;
    double energy;
} LavaThermalState;

double LavaSpecificEnthalpy(double temperatureK);
double LavaTemperature(LavaThermalState state);
unsigned LavaLiquidVolume(LavaThermalState state);
bool LavaThermalValid(LavaThermalState state);
double LavaLiquidEnthalpy(LavaThermalState state);
unsigned LavaFlowBudget(LavaThermalState state);
double LavaCool(LavaThermalState *state, double ambientK, double seconds);
#endif
