#ifndef VOXELCRAFT_ATMOSPHERE_MODEL_H
#define VOXELCRAFT_ATMOSPHERE_MODEL_H

#include <stdbool.h>

#define ATMOSPHERE_LEVEL_COUNT 12

typedef enum AtmosphereLayer {
    ATMOSPHERE_LAYER_VACUUM = 0,
    ATMOSPHERE_LAYER_TROPOSPHERE,
    ATMOSPHERE_LAYER_STRATOSPHERE,
    ATMOSPHERE_LAYER_MESOSPHERE,
    ATMOSPHERE_LAYER_THERMOSPHERE,
    ATMOSPHERE_LAYER_EXOSPHERE,
    ATMOSPHERE_LAYER_COUNT
} AtmosphereLayer;

/* SI units except pressure (atm). Altitude is above the reference surface,
 * never a compressed game coordinate. Ozone and ionization are proxies. */
typedef struct AtmosphereInput {
    double surfaceTemperatureK;
    double surfacePressureAtm;
    double gravityMps2;
    double molarMassKgMol;
    double ozoneStrength;
    double exosphereTemperatureK;
} AtmosphereInput;

typedef struct AtmosphereProfile {
    AtmosphereInput input;
    double altitudeM[ATMOSPHERE_LEVEL_COUNT];
    double temperatureK[ATMOSPHERE_LEVEL_COUNT];
    double pressureAtm[ATMOSPHERE_LEVEL_COUNT];
    double gravityMps2[ATMOSPHERE_LEVEL_COUNT];
    double molarMassKgMol[ATMOSPHERE_LEVEL_COUNT];
    double surfaceDensityKgM3;
    bool valid;
} AtmosphereProfile;

typedef struct AtmosphereSample {
    AtmosphereLayer layer;
    double altitudeM;
    double temperatureK;
    double pressureAtm;
    double densityKgM3;
    double densityRatio;
    double columnFraction;
    double molarMassKgMol;
    double ozone;
    double ionization;
    double weatherActivity;
    bool valid;
} AtmosphereSample;

AtmosphereInput AtmosphereEarthInput(void);
bool AtmosphereProfileBuild(const AtmosphereInput *input, AtmosphereProfile *out);
AtmosphereSample AtmosphereSampleAt(const AtmosphereProfile *profile,
                                    double altitudeM);
const char *AtmosphereLayerName(AtmosphereLayer layer);

#endif
