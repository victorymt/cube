#ifndef VOXELCRAFT_STELLAR_H
#define VOXELCRAFT_STELLAR_H

#include <stdbool.h>
#include <stdint.h>

typedef enum SpectrumType {
    SPECTRUM_RED_DWARF = 0,
    SPECTRUM_ORANGE,
    SPECTRUM_YELLOW,
    SPECTRUM_BLUE_WHITE,
    SPECTRUM_RED_GIANT,
    SPECTRUM_WHITE_DWARF,
    SPECTRUM_NEUTRON_STAR,
    SPECTRUM_BLACK_HOLE
} SpectrumType;

typedef enum StellarEvolutionStage {
    STELLAR_STAGE_MAIN_SEQUENCE = 0,
    STELLAR_STAGE_RED_GIANT,
    STELLAR_STAGE_WHITE_DWARF,
    STELLAR_STAGE_NEUTRON_STAR,
    STELLAR_STAGE_BLACK_HOLE
} StellarEvolutionStage;

typedef struct StellarProfile {
    SpectrumType spectrum;
    StellarEvolutionStage stage;
    uint32_t evolutionSeed;
    float initialMassSolar;
    double massKg;
    double radiusKm;
    // Solar ratios remain useful for generation and UI, but not mechanics.
    float massSolar;
    float radiusSolar;
    float temperatureK;
    float luminositySolar;
    float ageGyr;
    float mainSequenceLifetimeGyr;
    // For remnants this is the formation age of the compact object.
    float luminousLifetimeGyr;
} StellarProfile;

float StellarSampleInitialMass(uint32_t seed);
bool StellarProfileAtAge(float initialMassSolar, double ageGyr, uint32_t seed,
                         StellarProfile *out);
StellarProfile StellarGenerate(uint32_t seed);
StellarProfile StellarSolarProfile(void);

#endif
