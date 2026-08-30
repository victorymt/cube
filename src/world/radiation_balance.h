#ifndef VOXELCRAFT_RADIATION_BALANCE_H
#define VOXELCRAFT_RADIATION_BALANCE_H

#include <stdbool.h>

/* Mean solar irradiance at one astronomical unit, in SI units. */
#define RADIATION_SOLAR_CONSTANT_WM2 1361.0
#define RADIATION_STEFAN_BOLTZMANN_WM2K4 5.670374419e-8

typedef struct RadiationBalanceInput {
    /* Stellar luminosity relative to the present-day Sun. */
    double stellarLuminositySolar;
    double semiMajorAxisAu;
    double orbitalEccentricity;
    /* Mean anomaly and periapsis-to-solstice phase, in radians. */
    double orbitalMeanAnomalyRad;
    double seasonPhaseRad;
    double axialTiltRad;
    double latitudeRad;
    /* Local solar hour angle, zero at local solar noon. */
    double localHourAngleRad;
    double albedo;
    /* Surface long-wave emissivity before grey greenhouse attenuation. */
    double longwaveEmissivity;
    double greenhouseOpticalDepth;
    double surfaceTemperatureK;
} RadiationBalanceInput;

typedef struct RadiationBalanceOutput {
    double eccentricAnomalyRad;
    double trueAnomalyRad;
    double orbitalDistanceAu;
    double solarDeclinationRad;
    double solarZenithCosine;
    double solarIrradianceWm2;
    double instantaneousInsolationWm2;
    double dailyMeanInsolationWm2;
    double absorbedShortwaveWm2;
    double effectiveLongwaveEmissivity;
    double outgoingLongwaveWm2;
    double netRadiativeFluxWm2;
    double equilibriumTemperatureK;
} RadiationBalanceOutput;

/* Solve Kepler's equation E - e sin(E) = M for an elliptical orbit. */
double RadiationSolveKepler(double meanAnomalyRad, double eccentricity);

double RadiationOrbitalDistanceAu(double semiMajorAxisAu,
                                  double eccentricity,
                                  double eccentricAnomalyRad);

double RadiationSolarDeclinationRad(double axialTiltRad,
                                    double orbitalLongitudeRad);

double RadiationSolarZenithCosine(double latitudeRad,
                                  double declinationRad,
                                  double localHourAngleRad);

double RadiationDailyMeanInsolationWm2(double solarIrradianceWm2,
                                       double latitudeRad,
                                       double declinationRad);

double RadiationEffectiveLongwaveEmissivity(double surfaceEmissivity,
                                            double greenhouseOpticalDepth);

bool RadiationBalanceEvaluate(const RadiationBalanceInput *input,
                              RadiationBalanceOutput *out);

typedef struct RadiationColumnState {
    /* Column-integrated sensible/latent energy, in joules per square metre. */
    double energyJm2;
    double temperatureK;
    double heatCapacityJm2K;
} RadiationColumnState;

bool RadiationColumnStateInit(RadiationColumnState *state,
                              double initialTemperatureK,
                              double heatCapacityJm2K);

/* Evaluate forcing at the state's temperature and conserve net flux energy. */
bool RadiationColumnStateStep(RadiationColumnState *state,
                              const RadiationBalanceInput *forcing,
                              double deltaSeconds,
                              RadiationBalanceOutput *out);

#endif
