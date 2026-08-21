#include "ecology/flora_taxa.h"

#include <math.h>

#define BIOME_MASK(value) (1u << (value))
#define TREE_BIOMES (BIOME_MASK(BIOME_PLAINS) | BIOME_MASK(BIOME_FOREST) | \
                     BIOME_MASK(BIOME_SNOW) | BIOME_MASK(BIOME_MOUNTAIN) | \
                     BIOME_MASK(BIOME_SWAMP))
#define ALL_BIOMES 0x3fu

static const FloraTaxon floraCatalog[FLORA_TAXON_COUNT] = {
    { FLORA_TAXON_OAK, "Pedunculate Oak", "Quercus robur", "Fagaceae",
      FLORA_GROWTH_BROADLEAF, FLORA_SUCCESSION_MATURE,
      276.0f, 288.0f, 304.0f, 0.38f, 0.68f, 0.94f, 0.35f, 0.68f,
      900.0f, 0.42f, TREE_BIOMES, FLORA_SOIL_CHERNOZEM | FLORA_SOIL_ALLUVIUM |
      FLORA_SOIL_MINERAL | FLORA_SOIL_ASH, 8.0f, 16.0f, 4.0f, 0.72f, 0.82f,
      BLOCK_OAK_LOG, BLOCK_OAK_LEAVES },
    { FLORA_TAXON_BIRCH, "Silver Birch", "Betula pendula", "Betulaceae",
      FLORA_GROWTH_BROADLEAF, FLORA_SUCCESSION_WOODY_PIONEER,
      258.0f, 280.0f, 298.0f, 0.22f, 0.54f, 0.84f, 0.46f, 0.76f,
      1400.0f, 0.58f, TREE_BIOMES, FLORA_SOIL_MINERAL | FLORA_SOIL_PODZOL |
      FLORA_SOIL_ASH, 7.0f, 15.0f, 3.0f, 0.64f, 0.90f,
      BLOCK_BIRCH_LOG, BLOCK_BIRCH_LEAVES },
    { FLORA_TAXON_ASPEN, "European Aspen", "Populus tremula", "Salicaceae",
      FLORA_GROWTH_BROADLEAF, FLORA_SUCCESSION_WOODY_PIONEER,
      258.0f, 279.0f, 302.0f, 0.30f, 0.64f, 0.92f, 0.42f, 0.80f,
      1200.0f, 0.62f, TREE_BIOMES, FLORA_SOIL_MINERAL | FLORA_SOIL_ALLUVIUM |
      FLORA_SOIL_ASH, 8.0f, 18.0f, 3.0f, 0.70f, 0.92f,
      BLOCK_ASPEN_LOG, BLOCK_ASPEN_LEAVES },
    { FLORA_TAXON_SPRUCE, "Norway Spruce", "Picea abies", "Pinaceae",
      FLORA_GROWTH_CONIFER, FLORA_SUCCESSION_MATURE,
      250.0f, 273.0f, 292.0f, 0.48f, 0.76f, 0.98f, 0.28f, 0.58f,
      1800.0f, 0.66f, BIOME_MASK(BIOME_SNOW) | BIOME_MASK(BIOME_MOUNTAIN) |
      BIOME_MASK(BIOME_FOREST), FLORA_SOIL_PODZOL | FLORA_SOIL_PEAT |
      FLORA_SOIL_MINERAL, 10.0f, 22.0f, 4.0f, 0.78f, 0.88f,
      BLOCK_SPRUCE_LOG, BLOCK_SPRUCE_NEEDLES },
    { FLORA_TAXON_PINE, "Scots Pine", "Pinus sylvestris", "Pinaceae",
      FLORA_GROWTH_CONIFER, FLORA_SUCCESSION_MATURE,
      248.0f, 276.0f, 304.0f, 0.12f, 0.40f, 0.72f, 0.48f, 0.82f,
      1900.0f, 0.76f, BIOME_MASK(BIOME_SNOW) | BIOME_MASK(BIOME_MOUNTAIN) |
      BIOME_MASK(BIOME_PLAINS), FLORA_SOIL_MINERAL | FLORA_SOIL_PODZOL |
      FLORA_SOIL_TERRA_ROSSA | FLORA_SOIL_ASH, 11.0f, 24.0f, 3.0f,
      0.84f, 0.95f, BLOCK_PINE_LOG, BLOCK_PINE_NEEDLES },
    { FLORA_TAXON_WILLOW, "White Willow", "Salix alba", "Salicaceae",
      FLORA_GROWTH_BROADLEAF, FLORA_SUCCESSION_MATURE,
      274.0f, 289.0f, 307.0f, 0.66f, 0.90f, 1.0f, 0.32f, 0.68f,
      500.0f, 0.38f, BIOME_MASK(BIOME_SWAMP) | BIOME_MASK(BIOME_PLAINS) |
      BIOME_MASK(BIOME_FOREST), FLORA_SOIL_ALLUVIUM | FLORA_SOIL_PEAT,
      6.0f, 13.0f, 4.0f, 0.56f, 0.76f, BLOCK_WILLOW_LOG,
      BLOCK_WILLOW_LEAVES },
    { FLORA_TAXON_BIG_BLUESTEM, "Big Bluestem", "Andropogon gerardii",
      "Poaceae", FLORA_GROWTH_GRASS, FLORA_SUCCESSION_MATURE,
      270.0f, 293.0f, 314.0f, 0.22f, 0.52f, 0.80f, 0.62f, 0.86f,
      900.0f, 0.72f, BIOME_MASK(BIOME_PLAINS), FLORA_SOIL_CHERNOZEM |
      FLORA_SOIL_MINERAL, 0.8f, 2.4f, 1.0f, 0.76f, 0.96f,
      BLOCK_BIG_BLUESTEM, BLOCK_BIG_BLUESTEM },
    { FLORA_TAXON_BRACKEN, "Bracken", "Pteridium aquilinum", "Dennstaedtiaceae",
      FLORA_GROWTH_FERN, FLORA_SUCCESSION_PIONEER,
      268.0f, 285.0f, 305.0f, 0.38f, 0.72f, 0.96f, 0.18f, 0.58f,
      1200.0f, 0.70f, BIOME_MASK(BIOME_FOREST) | BIOME_MASK(BIOME_SWAMP) |
      BIOME_MASK(BIOME_PLAINS), FLORA_SOIL_PODZOL | FLORA_SOIL_HUMUS |
      FLORA_SOIL_ASH, 0.5f, 1.8f, 1.0f, 0.70f, 0.88f,
      BLOCK_BRACKEN, BLOCK_BRACKEN },
    { FLORA_TAXON_COMMON_REED, "Common Reed", "Phragmites australis",
      "Poaceae", FLORA_GROWTH_REED, FLORA_SUCCESSION_MATURE,
      270.0f, 291.0f, 313.0f, 0.70f, 0.94f, 1.0f, 0.56f, 0.80f,
      300.0f, 0.35f, BIOME_MASK(BIOME_SWAMP), FLORA_SOIL_ALLUVIUM |
      FLORA_SOIL_PEAT, 1.0f, 3.5f, 1.0f, 0.54f, 0.94f,
      BLOCK_COMMON_REED, BLOCK_COMMON_REED },
    { FLORA_TAXON_SPHAGNUM, "Sphagnum Moss", "Sphagnum palustre",
      "Sphagnaceae", FLORA_GROWTH_MOSS, FLORA_SUCCESSION_MATURE,
      260.0f, 278.0f, 296.0f, 0.82f, 0.98f, 1.0f, 0.08f, 0.38f,
      700.0f, 0.30f, BIOME_MASK(BIOME_SWAMP) | BIOME_MASK(BIOME_SNOW),
      FLORA_SOIL_PEAT | FLORA_SOIL_ALLUVIUM, 0.05f, 0.35f, 1.0f,
      0.42f, 0.50f, BLOCK_SPHAGNUM, BLOCK_SPHAGNUM },
    { FLORA_TAXON_HEATHER, "Common Heather", "Calluna vulgaris",
      "Ericaceae", FLORA_GROWTH_SHRUB, FLORA_SUCCESSION_PIONEER,
      258.0f, 279.0f, 300.0f, 0.18f, 0.42f, 0.72f, 0.72f, 0.92f,
      1600.0f, 0.78f, BIOME_MASK(BIOME_MOUNTAIN) | BIOME_MASK(BIOME_SNOW),
      FLORA_SOIL_PODZOL | FLORA_SOIL_MINERAL, 0.3f, 1.2f, 1.0f,
      0.68f, 0.98f, BLOCK_HEATHER, BLOCK_HEATHER },
    { FLORA_TAXON_FIREWEED, "Fireweed", "Chamaenerion angustifolium",
      "Onagraceae", FLORA_GROWTH_SHRUB, FLORA_SUCCESSION_PIONEER,
      262.0f, 283.0f, 307.0f, 0.30f, 0.64f, 0.88f, 0.54f, 0.84f,
      1500.0f, 0.72f, ALL_BIOMES, FLORA_SOIL_ASH | FLORA_SOIL_MINERAL,
      0.4f, 1.8f, 1.0f, 0.62f, 0.94f, BLOCK_FIREWEED, BLOCK_FIREWEED },
    { FLORA_TAXON_SAGUARO, "Saguaro", "Carnegiea gigantea", "Cactaceae",
      FLORA_GROWTH_SUCCULENT, FLORA_SUCCESSION_MATURE,
      286.0f, 305.0f, 322.0f, 0.02f, 0.14f, 0.38f, 0.76f, 0.94f,
      1100.0f, 0.42f, BIOME_MASK(BIOME_DESERT), FLORA_SOIL_TERRA_ROSSA |
      FLORA_SOIL_MINERAL, 3.0f, 9.0f, 2.0f, 0.32f, 0.18f,
      BLOCK_SAGUARO, BLOCK_SAGUARO }
};

static float Clamp01(float value)
{
    if (!isfinite(value)) return 0.0f;
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float Bell(float value, float low, float optimum, float high)
{
    if (!isfinite(value) || value < low || value > high) return 0.0f;
    float span = value <= optimum ? optimum - low : high - optimum;
    if (span <= 0.0f) return 1.0f;
    return Clamp01(1.0f - fabsf(value - optimum) / span);
}

const FloraTaxon *FloraTaxonAt(FloraTaxonId id)
{
    if (id < 0 || id >= FLORA_TAXON_COUNT) return NULL;
    return &floraCatalog[id];
}

const FloraTaxon *FloraTaxonCatalog(size_t *count)
{
    if (count) *count = FLORA_TAXON_COUNT;
    return floraCatalog;
}

const char *FloraGrowthFormName(FloraGrowthForm form)
{
    static const char *names[] = {
        "broadleaf", "conifer", "grass", "fern", "reed", "moss",
        "shrub", "succulent"
    };
    if (form < 0 || form >= (int)(sizeof(names) / sizeof(names[0]))) {
        return "unknown";
    }
    return names[form];
}

const char *FloraSuccessionStageName(FloraSuccessionStage stage)
{
    static const char *names[] = {
        "mature", "pioneer", "woody_pioneer", "recovering"
    };
    if (stage < 0 || stage >= (int)(sizeof(names) / sizeof(names[0]))) {
        return "unknown";
    }
    return names[stage];
}

const char *FloraDisturbanceStageName(FloraDisturbanceStage stage)
{
    static const char *names[] = {
        "unburned", "fresh_burn", "herb_pioneer", "woody_pioneer",
        "recovering_mature"
    };
    if (stage < 0 || stage >= (int)(sizeof(names) / sizeof(names[0]))) {
        return "unknown";
    }
    return names[stage];
}

FloraDisturbanceStage FloraDisturbanceStageForBurn(float severity,
                                                   float recovery)
{
    severity = Clamp01(severity);
    recovery = Clamp01(recovery);
    if (severity <= 0.05f) return FLORA_DISTURBANCE_UNBURNED;
    if (recovery < 0.12f) return FLORA_DISTURBANCE_FRESH_BURN;
    if (recovery < 0.38f) return FLORA_DISTURBANCE_HERB_PIONEER;
    if (recovery < 0.72f) return FLORA_DISTURBANCE_WOODY_PIONEER;
    return FLORA_DISTURBANCE_RECOVERING_MATURE;
}

bool FloraTaxonIsTree(FloraTaxonId id)
{
    const FloraTaxon *taxon = FloraTaxonAt(id);
    return taxon && (taxon->growthForm == FLORA_GROWTH_BROADLEAF ||
                     taxon->growthForm == FLORA_GROWTH_CONIFER);
}

FloraTaxonId FloraTaxonIdForBlock(BlockType block)
{
    for (int index = 0; index < FLORA_TAXON_COUNT; index++) {
        if (floraCatalog[index].primaryBlock == block ||
            floraCatalog[index].accentBlock == block) {
            return (FloraTaxonId)index;
        }
    }
    return FLORA_TAXON_COUNT;
}

bool FloraTaxonVisualDimensions(BlockType block, float *outHeight,
                                float *outHalfWidth)
{
    if (!outHeight || !outHalfWidth) return false;
    switch (block) {
    case BLOCK_BIG_BLUESTEM:
        *outHeight = 0.96f; *outHalfWidth = 0.26f; return true;
    case BLOCK_BRACKEN:
        *outHeight = 0.68f; *outHalfWidth = 0.34f; return true;
    case BLOCK_COMMON_REED:
        *outHeight = 0.98f; *outHalfWidth = 0.22f; return true;
    case BLOCK_HEATHER:
        *outHeight = 0.62f; *outHalfWidth = 0.31f; return true;
    case BLOCK_FIREWEED:
        *outHeight = 0.90f; *outHalfWidth = 0.23f; return true;
    default:
        return false;
    }
}

static uint32_t SoilMaskForBlock(BlockType substrate)
{
    switch (substrate) {
    case BLOCK_GRASS:
    case BLOCK_DIRT:
    case BLOCK_SAND:
    case BLOCK_RED_SAND:
    case BLOCK_MUD:
    case BLOCK_LOAM:
    case BLOCK_LATERITE:
        return FLORA_SOIL_MINERAL;
    case BLOCK_CHERNOZEM: return FLORA_SOIL_CHERNOZEM;
    case BLOCK_ALLUVIUM: return FLORA_SOIL_ALLUVIUM;
    case BLOCK_PODZOL: return FLORA_SOIL_PODZOL;
    case BLOCK_PEAT: return FLORA_SOIL_PEAT;
    case BLOCK_TERRA_ROSSA: return FLORA_SOIL_TERRA_ROSSA;
    case BLOCK_FIRE_ASH:
    case BLOCK_CHARCOAL:
    case BLOCK_CHARRED_WOOD: return FLORA_SOIL_ASH;
    case BLOCK_HUMUS:
    case BLOCK_COMPOST: return FLORA_SOIL_HUMUS;
    default: return 0u;
    }
}

float FloraTaxonSuitability(const FloraTaxon *taxon,
                            const FloraHabitat *habitat)
{
    if (!taxon || !habitat || !isfinite(habitat->temperatureK) ||
        !isfinite(habitat->moisture) || !isfinite(habitat->usableLight) ||
        !isfinite(habitat->elevation) || !isfinite(habitat->slope)) {
        return 0.0f;
    }
    if (!(taxon->biomeMask & BIOME_MASK(habitat->biome)) ||
        !(taxon->soilMask & SoilMaskForBlock(habitat->substrate)) ||
        habitat->elevation < 0.0f || habitat->elevation > taxon->elevationMax ||
        habitat->slope < 0.0f || habitat->slope > taxon->slopeMax) {
        return 0.0f;
    }
    float score = Bell(habitat->temperatureK, taxon->temperatureMinK,
                       taxon->temperatureOptimumK, taxon->temperatureMaxK);
    score *= Bell(habitat->moisture, taxon->moistureMin,
                  taxon->moistureOptimum, taxon->moistureMax);
    score *= Bell(habitat->usableLight, taxon->lightMin,
                  taxon->lightOptimum, 1.0f);
    if (score <= 0.0f) return 0.0f;
    if (habitat->burnSeverity > 0.15f) {
        if (taxon->succession == FLORA_SUCCESSION_MATURE &&
            habitat->burnRecovery < 0.55f) score *= 0.12f;
        if (taxon->succession == FLORA_SUCCESSION_PIONEER) score *= 1.0f +
            Clamp01(habitat->burnSeverity) * 0.8f;
        if (taxon->succession == FLORA_SUCCESSION_WOODY_PIONEER) score *=
            Clamp01(habitat->burnRecovery) + 0.25f;
    }
    return Clamp01(score);
}

FloraTaxonId FloraSelectTaxon(const FloraHabitat *habitat,
                              uint32_t hash, bool treesOnly)
{
    float total = 0.0f;
    float weights[FLORA_TAXON_COUNT] = { 0 };
    for (int index = 0; index < FLORA_TAXON_COUNT; index++) {
        if (FloraTaxonIsTree((FloraTaxonId)index) != treesOnly) continue;
        weights[index] = FloraTaxonSuitability(&floraCatalog[index], habitat);
        total += weights[index];
    }
    if (total <= 0.0f) return FLORA_TAXON_COUNT;
    float pick = (float)(hash & 0x00ffffffu) / 16777215.0f * total;
    for (int index = 0; index < FLORA_TAXON_COUNT; index++) {
        pick -= weights[index];
        if (pick <= 0.0f) return (FloraTaxonId)index;
    }
    return (FloraTaxonId)(FLORA_TAXON_COUNT - 1);
}
