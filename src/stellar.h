#ifndef VOXELCRAFT_STELLAR_H
#define VOXELCRAFT_STELLAR_H

#include <stdbool.h>
#include <stdint.h>

typedef enum SpectrumType {
    SPECTRUM_RED_DWARF = 0,
    SPECTRUM_ORANGE,
    SPECTRUM_YELLOW,
    SPECTRUM_BLUE_WHITE,
    SPECTRUM_RED_GIANT
} SpectrumType;

typedef enum StellarEvolutionStage {
    STELLAR_STAGE_MAIN_SEQUENCE = 0,
    STELLAR_STAGE_RED_GIANT
} StellarEvolutionStage;

typedef struct StellarProfile {
    SpectrumType spectrum;
    StellarEvolutionStage stage;
    float initialMassSolar;
    float massSolar;
    float radiusSolar;
    float temperatureK;
    float luminositySolar;
    float ageGyr;
    float mainSequenceLifetimeGyr;
    float luminousLifetimeGyr;
} StellarProfile;

float StellarSampleInitialMass(uint32_t seed);
bool StellarProfileAtAge(float initialMassSolar, float ageGyr, uint32_t seed,
                         StellarProfile *out);
StellarProfile StellarGenerate(uint32_t seed);
StellarProfile StellarSolarProfile(void);

#endif
