#include "app/game_debug_flora.h"

#include "app/game_runtime.h"
#include "ecology/flora_taxa.h"
#include "world/terrain.h"
#include "world/weather_impact.h"
#include "world/world.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLORA_GALLERY_TREE_SPACING 8
#define FLORA_GALLERY_GROUND_SPACING 5
#define FLORA_GALLERY_WIDTH 45
#define FLORA_GALLERY_DEPTH 12
#define FLORA_GALLERY_MAX_BLOCKS 1024

typedef struct FloraGalleryBlock {
    int x;
    int y;
    int z;
    BlockType type;
} FloraGalleryBlock;

static void FloraCommandError(GameRuntime *game, const char *reason)
{
    game->debugCommandFailed = true;
    snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure),
             "%s", reason);
}

static bool FloraNamesEqual(const char *left, const char *right)
{
    if (!left || !right) return false;
    for (;;) {
        while (*left == ' ' || *left == '-' || *left == '_') left++;
        while (*right == ' ' || *right == '-' || *right == '_') right++;
        int a = tolower((unsigned char)*left);
        int b = tolower((unsigned char)*right);
        if (a != b) return false;
        if (a == '\0') return true;
        left++;
        right++;
    }
}

static bool FloraResolve(const char *query, FloraTaxonId *outId)
{
    if (!query || !outId) return false;
    errno = 0;
    char *end = NULL;
    long numeric = strtol(query, &end, 10);
    if (end != query && *end == '\0' && errno == 0 &&
        numeric >= 0 && numeric < FLORA_TAXON_COUNT) {
        *outId = (FloraTaxonId)numeric;
        return true;
    }
    for (int value = 0; value < FLORA_TAXON_COUNT; value++) {
        const FloraTaxon *taxon = FloraTaxonAt((FloraTaxonId)value);
        if (FloraNamesEqual(query, taxon->commonName) ||
            FloraNamesEqual(query, taxon->scientificName)) {
            *outId = taxon->id;
            return true;
        }
    }
    return false;
}

static const char *FloraBiomeName(Biome biome)
{
    switch (biome) {
    case BIOME_PLAINS: return "plains";
    case BIOME_FOREST: return "forest";
    case BIOME_DESERT: return "desert";
    case BIOME_SNOW: return "snow";
    case BIOME_MOUNTAIN: return "mountain";
    case BIOME_SWAMP: return "swamp";
    default: return "unknown";
    }
}

static const char *FloraTaxonName(FloraTaxonId id)
{
    const FloraTaxon *taxon = FloraTaxonAt(id);
    return taxon ? taxon->commonName : "none";
}

static void FloraInspect(GameRuntime *game)
{
    FloraTaxonId id = FLORA_TAXON_COUNT;
    if (!FloraResolve(game->debugControl.floraQuery, &id)) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL flora inspect error "
                          "reason=unknown_taxon query=%s\n",
                          game->debugControl.floraQuery);
        FloraCommandError(game, "unknown_taxon");
        return;
    }
    const FloraTaxon *taxon = FloraTaxonAt(id);
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL flora inspect ok id=%d common=\"%s\" "
        "scientific=\"%s\" family=%s growth=%s succession=%s "
        "temperature=%.2f,%.2f,%.2f moisture=%.3f,%.3f,%.3f "
        "light=%.3f,%.3f elevation_max=%.2f slope_max=%.3f "
        "height=%.2f,%.2f crown=%.2f wind=%.3f flammability=%.3f "
        "primary=%s accent=%s\n",
        (int)taxon->id, taxon->commonName, taxon->scientificName,
        taxon->family, FloraGrowthFormName(taxon->growthForm),
        FloraSuccessionStageName(taxon->succession), taxon->temperatureMinK,
        taxon->temperatureOptimumK, taxon->temperatureMaxK,
        taxon->moistureMin, taxon->moistureOptimum, taxon->moistureMax,
        taxon->lightMin, taxon->lightOptimum, taxon->elevationMax,
        taxon->slopeMax, taxon->heightMin, taxon->heightMax,
        taxon->crownRadius, taxon->windResponse, taxon->flammability,
        BlockName(taxon->primaryBlock), BlockName(taxon->accentBlock));
}

static void FloraSample(GameRuntime *game)
{
    int x = game->debugControl.floraSampleX;
    int z = game->debugControl.floraSampleZ;
    FloraHabitat habitat = TerrainHomeFloraHabitatAt(
        x, z, game->selectedTerrain);
    WeatherBurnSiteState burn = { 0 };
    bool hasBurn = WeatherImpactBurnSiteAt(
        x, TerrainHeight(x, z, game->selectedTerrain), z, &burn);
    if (!hasBurn) burn = (WeatherBurnSiteState){ 0 };
    habitat.burnSeverity = burn.severity;
    habitat.burnRecovery = burn.recovery;
    FloraTaxonId tree = TerrainHomeTreeTaxonAt(x, z, game->selectedTerrain);
    FloraTaxonId ground = TerrainHomeGroundTaxonAt(
        x, z, game->selectedTerrain, habitat.substrate, WorldHash2D(x, z));
    game->floraSampleTree = tree;
    game->floraSampleGround = ground;
    game->floraSampleActive = true;
    game->floraSampleX = x;
    game->floraSampleZ = z;
    game->floraSampleBurnStage = FloraDisturbanceStageForBurn(
        burn.severity, burn.recovery);
    game->floraSampleHabitat = habitat;
    float treeSuitability = tree < FLORA_TAXON_COUNT
        ? FloraTaxonSuitability(FloraTaxonAt(tree), &habitat) : 0.0f;
    float groundSuitability = ground < FLORA_TAXON_COUNT
        ? FloraTaxonSuitability(FloraTaxonAt(ground), &habitat) : 0.0f;
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL flora sample ok position=%d,%d biome=%s "
        "substrate=%s temperature=%.3f moisture=%.3f light=%.3f "
        "elevation=%.3f slope=%.3f burn=%.3f recovery=%.3f stage=%s "
        "tree=%s tree_suitability=%.3f ground=%s ground_suitability=%.3f\n",
        x, z, FloraBiomeName(habitat.biome), BlockName(habitat.substrate),
        habitat.temperatureK, habitat.moisture, habitat.usableLight,
        habitat.elevation, habitat.slope, habitat.burnSeverity,
        habitat.burnRecovery,
        FloraDisturbanceStageName(game->floraSampleBurnStage),
        FloraTaxonName(tree), treeSuitability, FloraTaxonName(ground),
        groundSuitability);
}

static bool FloraGalleryAppend(FloraGalleryBlock *blocks, unsigned *count,
                               int x, int y, int z, BlockType type)
{
    for (unsigned index = 0u; index < *count; index++) {
        if (blocks[index].x == x && blocks[index].y == y &&
            blocks[index].z == z) {
            blocks[index].type = type;
            return true;
        }
    }
    if (*count >= FLORA_GALLERY_MAX_BLOCKS) return false;
    blocks[(*count)++] = (FloraGalleryBlock){ x, y, z, type };
    return true;
}

static bool FloraGalleryDisc(FloraGalleryBlock *blocks, unsigned *count,
                             int x, int y, int z, int radius,
                             BlockType type)
{
    for (int dx = -radius; dx <= radius; dx++) {
        for (int dz = -radius; dz <= radius; dz++) {
            if (dx * dx + dz * dz > radius * radius + 1) continue;
            if (!FloraGalleryAppend(blocks, count, x + dx, y, z + dz,
                                    type)) return false;
        }
    }
    return true;
}

static bool FloraGalleryTrunk(FloraGalleryBlock *blocks, unsigned *count,
                              int x, int baseY, int z, int height,
                              BlockType type)
{
    for (int y = 1; y <= height; y++) {
        if (!FloraGalleryAppend(blocks, count, x, baseY + y, z, type)) {
            return false;
        }
    }
    return true;
}

static bool FloraGalleryTree(FloraGalleryBlock *blocks, unsigned *count,
                             int x, int baseY, int z,
                             const FloraTaxon *taxon)
{
    switch (taxon->id) {
    case FLORA_TAXON_OAK:
        return FloraGalleryTrunk(blocks, count, x, baseY, z, 8,
                                 taxon->primaryBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 7, z, 3,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 8, z, 3,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 9, z, 2,
                             taxon->accentBlock);
    case FLORA_TAXON_BIRCH:
        return FloraGalleryTrunk(blocks, count, x, baseY, z, 11,
                                 taxon->primaryBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 8, z, 1,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 9, z, 2,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 10, z, 1,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 11, z, 1,
                             taxon->accentBlock);
    case FLORA_TAXON_ASPEN:
        return FloraGalleryTrunk(blocks, count, x, baseY, z, 12,
                                 taxon->primaryBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 7, z, 1,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 8, z, 2,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 9, z, 2,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 10, z, 2,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 11, z, 1,
                             taxon->accentBlock) &&
            FloraGalleryAppend(blocks, count, x, baseY + 12, z,
                               taxon->accentBlock);
    case FLORA_TAXON_SPRUCE:
        return FloraGalleryTrunk(blocks, count, x, baseY, z, 12,
                                 taxon->primaryBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 5, z, 3,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 6, z, 2,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 7, z, 3,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 8, z, 2,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 9, z, 2,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 10, z, 1,
                             taxon->accentBlock) &&
            FloraGalleryAppend(blocks, count, x, baseY + 11, z,
                               taxon->accentBlock) &&
            FloraGalleryAppend(blocks, count, x, baseY + 12, z,
                               taxon->accentBlock);
    case FLORA_TAXON_PINE:
        return FloraGalleryTrunk(blocks, count, x, baseY, z, 12,
                                 taxon->primaryBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 9, z, 2,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 10, z, 2,
                             taxon->accentBlock) &&
            FloraGalleryDisc(blocks, count, x, baseY + 11, z, 1,
                             taxon->accentBlock) &&
            FloraGalleryAppend(blocks, count, x, baseY + 12, z,
                               taxon->accentBlock);
    case FLORA_TAXON_WILLOW:
        if (!FloraGalleryTrunk(blocks, count, x, baseY, z, 7,
                               taxon->primaryBlock) ||
            !FloraGalleryDisc(blocks, count, x, baseY + 6, z, 3,
                              taxon->accentBlock) ||
            !FloraGalleryDisc(blocks, count, x, baseY + 7, z, 3,
                              taxon->accentBlock) ||
            !FloraGalleryDisc(blocks, count, x, baseY + 8, z, 2,
                              taxon->accentBlock)) return false;
        for (int direction = -1; direction <= 1; direction += 2) {
            if (!FloraGalleryAppend(blocks, count, x + direction * 3,
                                    baseY + 5, z, taxon->accentBlock) ||
                !FloraGalleryAppend(blocks, count, x,
                                    baseY + 5, z + direction * 3,
                                    taxon->accentBlock)) return false;
        }
        return true;
    default:
        return false;
    }
}

static bool FloraGalleryBuild(FloraGalleryBlock *blocks, unsigned *count,
                              int originX, int originY, int originZ)
{
    *count = 0u;
    for (int value = 0; value < FLORA_TAXON_COUNT; value++) {
        const FloraTaxon *taxon = FloraTaxonAt((FloraTaxonId)value);
        bool tree = FloraTaxonIsTree(taxon->id);
        int x = tree ? originX + value * FLORA_GALLERY_TREE_SPACING :
                       originX + (value - 6) * FLORA_GALLERY_GROUND_SPACING;
        int z = tree ? originZ : originZ + 8;
        if (FloraTaxonIsTree(taxon->id)) {
            if (!FloraGalleryTree(blocks, count, x, originY, z,
                                  taxon)) return false;
        } else if (taxon->id == FLORA_TAXON_SAGUARO) {
            for (int y = 1; y <= 5; y++) {
                if (!FloraGalleryAppend(blocks, count, x, originY + y,
                                        z, taxon->primaryBlock)) return false;
            }
            if (!FloraGalleryAppend(blocks, count, x - 1, originY + 3,
                                    z, taxon->primaryBlock) ||
                !FloraGalleryAppend(blocks, count, x + 1, originY + 4,
                                    z, taxon->primaryBlock)) return false;
        } else {
            if (!FloraGalleryAppend(blocks, count, x, originY + 1,
                                    z, taxon->primaryBlock)) return false;
        }
    }
    return true;
}

static void FloraGallery(GameRuntime *game)
{
    FloraGalleryBlock blocks[FLORA_GALLERY_MAX_BLOCKS];
    unsigned count = 0u;
    int originX = game->debugControl.floraGalleryX;
    int originY = game->debugControl.floraGalleryY;
    int originZ = game->debugControl.floraGalleryZ;
    if (!WorldIsSurfaceActive() || !WorldCanAccessBlockY(originY) ||
        originX < -1000000 + 3 || originX > 1000000 - FLORA_GALLERY_WIDTH ||
        originZ < -1000000 + 3 || originZ > 1000000 - FLORA_GALLERY_DEPTH) {
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL flora gallery error "
                          "reason=invalid_region\n");
        FloraCommandError(game, "invalid_flora_gallery_region");
        return;
    }
    if (!FloraGalleryBuild(blocks, &count, originX, originY, originZ)) {
        FloraCommandError(game, "flora_gallery_overflow");
        return;
    }
    for (unsigned index = 0u; index < count; index++) {
        FloraGalleryBlock *block = &blocks[index];
        if (!SurfaceBlockReadyAt(block->x, block->y, block->z) ||
            GetBlockAt(block->x, block->y, block->z) != BLOCK_AIR) {
            DebugControlReply(&game->debugControl,
                              "DEBUG_CONTROL flora gallery error "
                              "reason=region_unloaded_or_occupied position=%d,%d,%d\n",
                              block->x, block->y, block->z);
            FloraCommandError(game, "flora_gallery_region_unavailable");
            return;
        }
    }
    WorldBeginUndoGroup();
    for (unsigned index = 0u; index < count; index++) {
        FloraGalleryBlock *block = &blocks[index];
        if (!SetBlock(block->x, block->y, block->z, block->type)) {
            WorldEndUndoGroup();
            UndoBlockEdit();
            FloraCommandError(game, "flora_gallery_mutation_failed");
            return;
        }
    }
    WorldEndUndoGroup();
    game->floraGalleryActive = true;
    game->floraGalleryOrigin = (Vector3){ originX, originY, originZ };
    game->floraGalleryPlaced = count;
    game->floraGalleryTreeCount = 6u;
    game->floraGalleryGroundCount = FLORA_TAXON_COUNT - 6u;
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL flora gallery ok origin=%d,%d,%d "
                      "placed=%u trees=%u ground=%u taxa=%d\n",
                      originX, originY, originZ, count,
                      game->floraGalleryTreeCount,
                      game->floraGalleryGroundCount, FLORA_TAXON_COUNT);
}

bool GameDebugFloraDispatch(GameRuntime *game, DebugControlCommand command)
{
    if (!game) return false;
    if (command == DEBUG_CONTROL_COMMAND_FLORA_INSPECT) {
        FloraInspect(game);
        return true;
    }
    if (command == DEBUG_CONTROL_COMMAND_FLORA_SAMPLE) {
        FloraSample(game);
        return true;
    }
    if (command == DEBUG_CONTROL_COMMAND_FLORA_GALLERY) {
        FloraGallery(game);
        return true;
    }
    return false;
}

static bool FloraDslNumber(DebugDslValue *outValue, double value)
{
    *outValue = (DebugDslValue){ .type = DEBUG_DSL_VALUE_NUMBER,
                                 .as.number = value };
    return true;
}

static bool FloraDslBool(DebugDslValue *outValue, bool value)
{
    *outValue = (DebugDslValue){ .type = DEBUG_DSL_VALUE_BOOL,
                                 .as.boolean = value };
    return true;
}

static bool FloraDslString(DebugDslValue *outValue, const char *value)
{
    *outValue = (DebugDslValue){ .type = DEBUG_DSL_VALUE_STRING,
                                 .as.string = value ? value : "none" };
    return true;
}

static bool FloraDslVec3(DebugDslValue *outValue, Vector3 value)
{
    *outValue = (DebugDslValue){ .type = DEBUG_DSL_VALUE_VEC3,
                                 .as.vec3 = { value.x, value.y, value.z } };
    return true;
}

bool GameDebugFloraDslResolve(const GameRuntime *game, const char *name,
                              DebugDslValue *outValue)
{
    if (!game || !name || !outValue) return false;
    if (strcmp(name, "flora.catalog_count") == 0) {
        return FloraDslNumber(outValue, FLORA_TAXON_COUNT);
    }
    if (strcmp(name, "flora.sample_tree") == 0) {
        return FloraDslString(outValue, game->floraSampleActive ?
                              FloraTaxonName(game->floraSampleTree) : "none");
    }
    if (strcmp(name, "flora.sample_ground") == 0) {
        return FloraDslString(outValue, game->floraSampleActive ?
                              FloraTaxonName(game->floraSampleGround) : "none");
    }
    if (strcmp(name, "flora.sample_burn_stage") == 0) {
        return FloraDslString(outValue, game->floraSampleActive ?
                              FloraDisturbanceStageName(game->floraSampleBurnStage) :
                              "unavailable");
    }
    if (strcmp(name, "flora.sample_biome") == 0) {
        return FloraDslString(outValue, game->floraSampleActive ?
                              FloraBiomeName(game->floraSampleHabitat.biome) :
                              "unavailable");
    }
    if (strcmp(name, "flora.sample_substrate") == 0) {
        return FloraDslString(outValue, game->floraSampleActive ?
                              BlockName(game->floraSampleHabitat.substrate) :
                              "unavailable");
    }
    if (strcmp(name, "flora.sample_habitat") == 0) {
        return FloraDslVec3(outValue, (Vector3){
            game->floraSampleHabitat.temperatureK,
            game->floraSampleHabitat.moisture,
            game->floraSampleHabitat.usableLight });
    }
    if (strcmp(name, "flora.sample_temperature") == 0) {
        return FloraDslNumber(outValue, game->floraSampleHabitat.temperatureK);
    }
    if (strcmp(name, "flora.sample_moisture") == 0) {
        return FloraDslNumber(outValue, game->floraSampleHabitat.moisture);
    }
    if (strcmp(name, "flora.gallery_active") == 0) {
        return FloraDslBool(outValue, game->floraGalleryActive);
    }
    if (strcmp(name, "flora.gallery_origin") == 0) {
        return FloraDslVec3(outValue, game->floraGalleryOrigin);
    }
    if (strcmp(name, "flora.gallery_placed") == 0) {
        return FloraDslNumber(outValue, game->floraGalleryPlaced);
    }
    if (strcmp(name, "flora.gallery_trees") == 0) {
        return FloraDslNumber(outValue, game->floraGalleryTreeCount);
    }
    if (strcmp(name, "flora.gallery_ground") == 0) {
        return FloraDslNumber(outValue, game->floraGalleryGroundCount);
    }
    return false;
}
