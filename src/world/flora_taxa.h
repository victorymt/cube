#ifndef VOXELCRAFT_FLORA_TAXA_H
#define VOXELCRAFT_FLORA_TAXA_H

#include "world/world_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum FloraTaxonId {
    FLORA_TAXON_OAK = 0,
    FLORA_TAXON_BIRCH,
    FLORA_TAXON_ASPEN,
    FLORA_TAXON_SPRUCE,
    FLORA_TAXON_PINE,
    FLORA_TAXON_WILLOW,
    FLORA_TAXON_BIG_BLUESTEM,
    FLORA_TAXON_BRACKEN,
    FLORA_TAXON_COMMON_REED,
    FLORA_TAXON_SPHAGNUM,
    FLORA_TAXON_HEATHER,
    FLORA_TAXON_FIREWEED,
    FLORA_TAXON_SAGUARO,
    FLORA_TAXON_COUNT
} FloraTaxonId;

typedef enum FloraGrowthForm {
    FLORA_GROWTH_BROADLEAF = 0,
    FLORA_GROWTH_CONIFER,
    FLORA_GROWTH_GRASS,
    FLORA_GROWTH_FERN,
    FLORA_GROWTH_REED,
    FLORA_GROWTH_MOSS,
    FLORA_GROWTH_SHRUB,
    FLORA_GROWTH_SUCCULENT
} FloraGrowthForm;

typedef enum FloraSuccessionStage {
    FLORA_SUCCESSION_MATURE = 0,
    FLORA_SUCCESSION_PIONEER,
    FLORA_SUCCESSION_WOODY_PIONEER,
    FLORA_SUCCESSION_RECOVERING
} FloraSuccessionStage;

typedef enum FloraDisturbanceStage {
    FLORA_DISTURBANCE_UNBURNED = 0,
    FLORA_DISTURBANCE_FRESH_BURN,
    FLORA_DISTURBANCE_HERB_PIONEER,
    FLORA_DISTURBANCE_WOODY_PIONEER,
    FLORA_DISTURBANCE_RECOVERING_MATURE
} FloraDisturbanceStage;

typedef enum FloraSoilMask {
    FLORA_SOIL_MINERAL = 1u << 0,
    FLORA_SOIL_CHERNOZEM = 1u << 1,
    FLORA_SOIL_ALLUVIUM = 1u << 2,
    FLORA_SOIL_PODZOL = 1u << 3,
    FLORA_SOIL_PEAT = 1u << 4,
    FLORA_SOIL_TERRA_ROSSA = 1u << 5,
    FLORA_SOIL_ASH = 1u << 6,
    FLORA_SOIL_HUMUS = 1u << 7,
    FLORA_SOIL_ANY = 0xffu
} FloraSoilMask;

typedef struct FloraTaxon {
    FloraTaxonId id;
    const char *commonName;
    const char *scientificName;
    const char *family;
    FloraGrowthForm growthForm;
    FloraSuccessionStage succession;
    float temperatureMinK;
    float temperatureOptimumK;
    float temperatureMaxK;
    float moistureMin;
    float moistureOptimum;
    float moistureMax;
    float lightMin;
    float lightOptimum;
    float elevationMax;
    float slopeMax;
    uint32_t biomeMask;
    uint32_t soilMask;
    float heightMin;
    float heightMax;
    float crownRadius;
    float windResponse;
    float flammability;
    BlockType primaryBlock;
    BlockType accentBlock;
} FloraTaxon;

typedef struct FloraHabitat {
    float temperatureK;
    float moisture;
    float usableLight;
    float elevation;
    float slope;
    Biome biome;
    BlockType substrate;
    float burnSeverity;
    float burnRecovery;
} FloraHabitat;

const FloraTaxon *FloraTaxonAt(FloraTaxonId id);
const FloraTaxon *FloraTaxonCatalog(size_t *count);
const char *FloraGrowthFormName(FloraGrowthForm form);
const char *FloraSuccessionStageName(FloraSuccessionStage stage);
const char *FloraDisturbanceStageName(FloraDisturbanceStage stage);
FloraDisturbanceStage FloraDisturbanceStageForBurn(float severity,
                                                   float recovery);
bool FloraTaxonIsTree(FloraTaxonId id);
FloraTaxonId FloraTaxonIdForBlock(BlockType block);
bool FloraTaxonVisualDimensions(BlockType block, float *outHeight,
                                float *outHalfWidth);
float FloraTaxonSuitability(const FloraTaxon *taxon,
                            const FloraHabitat *habitat);
FloraTaxonId FloraSelectTaxon(const FloraHabitat *habitat,
                              uint32_t hash, bool treesOnly);

#endif
