#include "ecology/flora_taxa.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static FloraHabitat OptimumFor(const FloraTaxon *taxon)
{
    BlockType substrate = BLOCK_GRASS;
    if (taxon->soilMask & FLORA_SOIL_CHERNOZEM) substrate = BLOCK_CHERNOZEM;
    else if (taxon->soilMask & FLORA_SOIL_ALLUVIUM) substrate = BLOCK_ALLUVIUM;
    else if (taxon->soilMask & FLORA_SOIL_PODZOL) substrate = BLOCK_PODZOL;
    else if (taxon->soilMask & FLORA_SOIL_PEAT) substrate = BLOCK_PEAT;
    else if (taxon->soilMask & FLORA_SOIL_TERRA_ROSSA) substrate = BLOCK_TERRA_ROSSA;
    else if (taxon->soilMask & FLORA_SOIL_ASH) substrate = BLOCK_FIRE_ASH;
    else if (taxon->soilMask & FLORA_SOIL_HUMUS) substrate = BLOCK_HUMUS;
    Biome biome = BIOME_PLAINS;
    for (int value = BIOME_PLAINS; value <= BIOME_SWAMP; value++) {
        if (taxon->biomeMask & (1u << value)) {
            biome = (Biome)value;
            break;
        }
    }
    return (FloraHabitat){
        .temperatureK = taxon->temperatureOptimumK,
        .moisture = taxon->moistureOptimum,
        .usableLight = taxon->lightOptimum,
        .elevation = taxon->elevationMax * 0.25f,
        .slope = taxon->slopeMax * 0.25f,
        .biome = biome,
        .substrate = substrate,
        .burnRecovery = 1.0f
    };
}

static void TestCatalog(void)
{
    size_t count = 0;
    const FloraTaxon *catalog = FloraTaxonCatalog(&count);
    assert(catalog != NULL && count == FLORA_TAXON_COUNT);
    assert(count == 13u);
    assert(BLOCK_STAGE05_END == 136);
    assert(BLOCK_STAGE06_START == 137);
    assert(BLOCK_STAGE06_END == 155);
    assert(BLOCK_STAGE06_END < BLOCK_COLOR_START);
    for (size_t index = 0; index < count; index++) {
        const FloraTaxon *taxon = &catalog[index];
        assert(taxon == FloraTaxonAt((FloraTaxonId)index));
        assert(taxon->id == (FloraTaxonId)index);
        assert(taxon->commonName && taxon->commonName[0]);
        assert(taxon->scientificName && taxon->scientificName[0]);
        assert(taxon->family && taxon->family[0]);
        assert(taxon->temperatureMinK < taxon->temperatureOptimumK);
        assert(taxon->temperatureOptimumK < taxon->temperatureMaxK);
        assert(taxon->moistureMin < taxon->moistureOptimum);
        assert(taxon->moistureOptimum < taxon->moistureMax);
        assert(taxon->lightMin <= taxon->lightOptimum);
        assert(taxon->lightOptimum <= 1.0f);
        assert(taxon->elevationMax > 0.0f && taxon->slopeMax > 0.0f);
        assert(taxon->heightMin > 0.0f && taxon->heightMin <= taxon->heightMax);
        assert(taxon->crownRadius > 0.0f);
        assert(taxon->windResponse >= 0.0f && taxon->windResponse <= 1.0f);
        assert(taxon->flammability >= 0.0f && taxon->flammability <= 1.0f);
        assert(taxon->primaryBlock >= BLOCK_STAGE06_START &&
               taxon->primaryBlock <= BLOCK_STAGE06_END);
        assert(taxon->accentBlock >= BLOCK_STAGE06_START &&
               taxon->accentBlock <= BLOCK_STAGE06_END);
        for (size_t prior = 0; prior < index; prior++) {
            assert(strcmp(taxon->commonName, catalog[prior].commonName) != 0);
            assert(strcmp(taxon->scientificName,
                          catalog[prior].scientificName) != 0);
        }
    }
    assert(FloraTaxonAt(FLORA_TAXON_COUNT) == NULL);
    assert(FloraTaxonIsTree(FLORA_TAXON_OAK));
    assert(FloraTaxonIsTree(FLORA_TAXON_PINE));
    assert(!FloraTaxonIsTree(FLORA_TAXON_FIREWEED));
}

static void TestBlockMappingsAndVisualDimensions(void)
{
    size_t count = 0;
    const FloraTaxon *catalog = FloraTaxonCatalog(&count);
    for (size_t index = 0; index < count; index++) {
        assert(FloraTaxonIdForBlock(catalog[index].primaryBlock) ==
               (FloraTaxonId)index);
        assert(FloraTaxonIdForBlock(catalog[index].accentBlock) ==
               (FloraTaxonId)index);
    }
    assert(FloraTaxonIdForBlock(BLOCK_STONE) == FLORA_TAXON_COUNT);

    static const struct {
        BlockType block;
        float height;
        float halfWidth;
    } expected[] = {
        {BLOCK_BIG_BLUESTEM, 0.92f, 0.24f},
        {BLOCK_BRACKEN, 0.72f, 0.29f},
        {BLOCK_COMMON_REED, 0.98f, 0.20f},
        {BLOCK_HEATHER, 0.58f, 0.27f},
        {BLOCK_FIREWEED, 0.86f, 0.22f}
    };
    for (size_t index = 0; index < sizeof expected / sizeof expected[0];
         index++) {
        float height = 0.0f;
        float halfWidth = 0.0f;
        assert(FloraTaxonVisualDimensions(expected[index].block, &height,
                                          &halfWidth));
        assert(fabsf(height - expected[index].height) < 0.0001f);
        assert(fabsf(halfWidth - expected[index].halfWidth) < 0.0001f);
    }

    float unchanged = 7.0f;
    assert(!FloraTaxonVisualDimensions(BLOCK_STONE, &unchanged, &unchanged));
    assert(unchanged == 7.0f);
    assert(!FloraTaxonVisualDimensions(BLOCK_FIREWEED, NULL, &unchanged));
    assert(!FloraTaxonVisualDimensions(BLOCK_FIREWEED, &unchanged, NULL));
}

static void TestSuitability(void)
{
    size_t count = 0;
    const FloraTaxon *catalog = FloraTaxonCatalog(&count);
    for (size_t index = 0; index < count; index++) {
        const FloraTaxon *taxon = &catalog[index];
        FloraHabitat optimum = OptimumFor(taxon);
        float optimumScore = FloraTaxonSuitability(taxon, &optimum);
        assert(isfinite(optimumScore) && optimumScore > 0.80f);
        FloraHabitat edge = optimum;
        edge.temperatureK = taxon->temperatureMinK + 0.01f;
        assert(FloraTaxonSuitability(taxon, &edge) < optimumScore);
        edge = optimum;
        edge.temperatureK = taxon->temperatureMaxK + 0.01f;
        assert(FloraTaxonSuitability(taxon, &edge) == 0.0f);
        edge = optimum;
        edge.substrate = BLOCK_BEDROCK;
        assert(FloraTaxonSuitability(taxon, &edge) == 0.0f);
        edge = optimum;
        edge.temperatureK = NAN;
        assert(FloraTaxonSuitability(taxon, &edge) == 0.0f);
    }

    FloraHabitat oak = OptimumFor(FloraTaxonAt(FLORA_TAXON_OAK));
    assert(FloraSelectTaxon(&oak, 123u, true) < FLORA_TAXON_COUNT);
    assert(FloraSelectTaxon(&oak, 123u, true) ==
           FloraSelectTaxon(&oak, 123u, true));
    FloraTaxonId cover = FloraSelectTaxon(&oak, 456u, false);
    assert(cover == FLORA_TAXON_COUNT || !FloraTaxonIsTree(cover));
}

static void TestDisturbanceStages(void)
{
    assert(FloraDisturbanceStageForBurn(0.0f, 0.0f) ==
           FLORA_DISTURBANCE_UNBURNED);
    assert(FloraDisturbanceStageForBurn(0.8f, 0.0f) ==
           FLORA_DISTURBANCE_FRESH_BURN);
    assert(FloraDisturbanceStageForBurn(0.8f, 0.2f) ==
           FLORA_DISTURBANCE_HERB_PIONEER);
    assert(FloraDisturbanceStageForBurn(0.8f, 0.5f) ==
           FLORA_DISTURBANCE_WOODY_PIONEER);
    assert(FloraDisturbanceStageForBurn(0.8f, 0.9f) ==
           FLORA_DISTURBANCE_RECOVERING_MATURE);
    assert(strcmp(FloraDisturbanceStageName(
                      FLORA_DISTURBANCE_HERB_PIONEER),
                  "herb_pioneer") == 0);
}

int main(void)
{
    TestCatalog();
    TestBlockMappingsAndVisualDimensions();
    TestSuitability();
    TestDisturbanceStages();
    puts("flora taxa tests passed");
    return 0;
}
