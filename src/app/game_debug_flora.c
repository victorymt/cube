#include "app/game_debug_flora.h"

#include "app/game_runtime.h"
#include "gameplay/player_motion.h"
#include "world/flora_taxa.h"
#include "world/chunks.h"
#include "world/terrain.h"
#include "world/weather_impact.h"
#include "world/block_types.h"
#include "world/block_edits.h"
#include "world/surface_contract.h"
#include "world/surface_pose.h"
#include "world/world_environment.h"
#include "world/world_surface_snapshot.h"

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
#define FLORA_NEAR_MAX_RESULTS 64u

typedef struct FloraGalleryBlock {
    int x;
    int y;
    int z;
    BlockType type;
} FloraGalleryBlock;

typedef struct FloraNearbyEntry {
    FloraStructureInstance structure;
    int chunkX;
    int chunkZ;
    ChunkLodLevel activeLod;
    float distance;
} FloraNearbyEntry;

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

static const char *FloraStructureKindName(FloraStructureKind kind)
{
    switch (kind) {
    case FLORA_STRUCTURE_ALIEN_CANOPY: return "alien_canopy";
    case FLORA_STRUCTURE_CRYSTAL: return "crystal";
    case FLORA_STRUCTURE_SPORE: return "spore";
    case FLORA_STRUCTURE_THERMAL_VENT: return "thermal_vent";
    case FLORA_STRUCTURE_HOME_TREE: return "home_tree";
    }
    return "unknown";
}

static const char *FloraLodName(ChunkLodLevel lod)
{
    switch (lod) {
    case CHUNK_LOD_EXACT: return "exact";
    case CHUNK_LOD_HALF: return "half";
    case CHUNK_LOD_QUARTER: return "quarter";
    case CHUNK_LOD_COUNT: break;
    }
    return "unknown";
}

static float FloraStructureHorizontalDistance(
    const FloraStructureInstance *structure, Vector3 position)
{
    float dx = position.x < (float)structure->minX
        ? (float)structure->minX - position.x
        : position.x > (float)structure->maxX
            ? position.x - (float)structure->maxX : 0.0f;
    float dz = position.z < (float)structure->minZ
        ? (float)structure->minZ - position.z
        : position.z > (float)structure->maxZ
            ? position.z - (float)structure->maxZ : 0.0f;
    return sqrtf(dx * dx + dz * dz);
}

static bool FloraNearbyBefore(const FloraNearbyEntry *left,
                              const FloraNearbyEntry *right)
{
    if (left->distance != right->distance) return left->distance < right->distance;
    if (left->structure.cubeNative && right->structure.cubeNative) {
        SurfaceAddress a = left->structure.rootAddress;
        SurfaceAddress b = right->structure.rootAddress;
        if (a.face != b.face) return a.face < b.face;
        if (a.u != b.u) return a.u < b.u;
        if (a.v != b.v) return a.v < b.v;
        if (a.radial != b.radial) return a.radial < b.radial;
        return left->structure.kind < right->structure.kind;
    }
    if (left->structure.cubeNative != right->structure.cubeNative)
        return !left->structure.cubeNative;
    if (left->structure.rootX != right->structure.rootX)
        return left->structure.rootX < right->structure.rootX;
    if (left->structure.rootZ != right->structure.rootZ)
        return left->structure.rootZ < right->structure.rootZ;
    if (left->structure.groundY != right->structure.groundY)
        return left->structure.groundY < right->structure.groundY;
    return left->structure.kind < right->structure.kind;
}

static void FloraNearbyInsert(FloraNearbyEntry entries[], unsigned *count,
                              FloraNearbyEntry entry)
{
    unsigned insert = *count;
    for (unsigned index = 0u; index < *count; index++) {
        if (FloraNearbyBefore(&entry, &entries[index])) {
            insert = index;
            break;
        }
    }
    if (insert >= FLORA_NEAR_MAX_RESULTS) return;
    unsigned last = *count < FLORA_NEAR_MAX_RESULTS
        ? *count : FLORA_NEAR_MAX_RESULTS - 1u;
    for (unsigned index = last; index > insert; index--) {
        entries[index] = entries[index - 1u];
    }
    entries[insert] = entry;
    if (*count < FLORA_NEAR_MAX_RESULTS) (*count)++;
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

static void FloraNear(GameRuntime *game)
{
    FloraNearbyEntry entries[FLORA_NEAR_MAX_RESULTS] = { 0 };
    unsigned returned = 0u;
    unsigned total = 0u;
    float radius = (float)game->debugControl.floraNearbyRadius;
    float radiusSquared = radius * radius;
    const Chunk *chunks = ChunksView();
    unsigned loadedChunks = 0u;
    unsigned loadedStructures = 0u;
    unsigned cubeStructures = 0u;
    unsigned cubeRootChunks = 0u;
    float nearestCubeRoot = -1.0f;
    SurfaceAddress nearestCubeAddress = {0};
    const SurfaceContract *contract = WorldCurrentSurfaceContract();
    for (int chunkIndex = 0; chunkIndex < MAX_ACTIVE_CHUNKS; chunkIndex++) {
        const Chunk *chunk = &chunks[chunkIndex];
        if (!chunk->loaded) continue;
        loadedChunks++;
        loadedStructures += (unsigned)chunk->floraStructureCount;
        if (chunk->floraStructureCount <= 0) continue;
        int minChunkX = chunk->cx * CHUNK_SIZE;
        int minChunkZ = chunk->cz * CHUNK_SIZE;
        for (int structureIndex = 0;
             structureIndex < chunk->floraStructureCount; structureIndex++) {
            const FloraStructureInstance *structure =
                &chunk->floraStructures[structureIndex];
            /* A structure is repeated in every intersecting chunk. Count it
             * through the chunk that contains its root so seam trees appear
             * once in this diagnostic. */
            Vector3 rootLocal = {0};
            Vector3 boundsMin = {0};
            Vector3 boundsMax = {0};
            if (structure->cubeNative) {
                cubeStructures++;
                SurfaceChunkHandle rootHandle;
                SurfaceChunkHandle chunkHandle;
                if (!contract ||
                    !SurfaceContractChunkFromAddress(
                        contract, structure->rootAddress, &rootHandle) ||
                    !SurfaceContractChunkFromAddress(
                        contract, chunk->surfaceAddress, &chunkHandle) ||
                    !SurfaceContractChunkEqual(contract, rootHandle, chunkHandle))
                    continue;
                cubeRootChunks++;
                SurfacePose rootPose;
                if (!SurfacePoseFromAddress(structure->rootAddress, 0.0, 0.0,
                                            &rootPose) ||
                    !PlayerLocalFromSurfacePose(&rootPose, &rootLocal)) continue;
                boundsMin = (Vector3){ rootLocal.x + structure->minU,
                    (float)structure->minY, rootLocal.z + structure->minV };
                boundsMax = (Vector3){ rootLocal.x + structure->maxU,
                    (float)structure->maxY, rootLocal.z + structure->maxV };
            } else {
                if (structure->rootX < minChunkX ||
                    structure->rootX >= minChunkX + CHUNK_SIZE ||
                    structure->rootZ < minChunkZ ||
                    structure->rootZ >= minChunkZ + CHUNK_SIZE) continue;
                rootLocal = (Vector3){ (float)structure->rootX + 0.5f,
                    (float)structure->groundY,
                    (float)structure->rootZ + 0.5f };
                boundsMin = (Vector3){ (float)structure->minX,
                    (float)structure->minY, (float)structure->minZ };
                boundsMax = (Vector3){ (float)structure->maxX,
                    (float)structure->maxY, (float)structure->maxZ };
            }
            float rootDx = rootLocal.x - game->player.position.x;
            float rootDz = rootLocal.z - game->player.position.z;
            if (structure->cubeNative) {
                float candidate = sqrtf(rootDx * rootDx + rootDz * rootDz);
                if (nearestCubeRoot < 0.0f || candidate < nearestCubeRoot) {
                    nearestCubeRoot = candidate;
                    nearestCubeAddress = structure->rootAddress;
                }
            }
            float distance = FloraStructureHorizontalDistance(
                structure, game->player.position);
            if (structure->cubeNative) {
                float dx = game->player.position.x < boundsMin.x
                    ? boundsMin.x - game->player.position.x
                    : game->player.position.x > boundsMax.x
                        ? game->player.position.x - boundsMax.x : 0.0f;
                float dz = game->player.position.z < boundsMin.z
                    ? boundsMin.z - game->player.position.z
                    : game->player.position.z > boundsMax.z
                        ? game->player.position.z - boundsMax.z : 0.0f;
                distance = sqrtf(dx * dx + dz * dz);
            }
            if (rootDx * rootDx + rootDz * rootDz > radiusSquared) continue;
            total++;
            FloraNearbyInsert(entries, &returned, (FloraNearbyEntry){
                .structure = *structure,
                .chunkX = chunk->cx,
                .chunkZ = chunk->cz,
                .activeLod = chunk->activeLod,
                .distance = distance
            });
        }
    }

    game->floraNearbyActive = true;
    game->floraNearbyCount = total;
    game->floraNearbyNearestRoot = (Vector3){ 0 };
    game->floraNearbyNearestBoundsMin = (Vector3){ 0 };
    game->floraNearbyNearestBoundsMax = (Vector3){ 0 };
    game->floraNearbyNearestPrimary = BLOCK_AIR;
    game->floraNearbyNearestAccent = BLOCK_AIR;
    game->floraNearbyNearestKind = FLORA_STRUCTURE_ALIEN_CANOPY;
    game->floraNearbyNearestTaxon = FLORA_TAXON_COUNT;
    game->floraNearbyNearestDistance = 0.0f;
    game->floraNearbyNearestWindResponse = 0.0f;
    if (returned > 0u) {
        const FloraNearbyEntry *nearest = &entries[0];
        if (nearest->structure.cubeNative) {
            SurfacePose rootPose;
            Vector3 local = {0};
            (void)SurfacePoseFromAddress(nearest->structure.rootAddress, 0.0, 0.0,
                                         &rootPose);
            (void)PlayerLocalFromSurfacePose(&rootPose, &local);
            game->floraNearbyNearestRoot = (Vector3){ local.x,
                (float)nearest->structure.groundY, local.z };
            game->floraNearbyNearestBoundsMin = (Vector3){
                local.x + nearest->structure.minU,
                (float)nearest->structure.minY,
                local.z + nearest->structure.minV };
            game->floraNearbyNearestBoundsMax = (Vector3){
                local.x + nearest->structure.maxU,
                (float)nearest->structure.maxY,
                local.z + nearest->structure.maxV };
        } else {
            game->floraNearbyNearestRoot = (Vector3){
                (float)nearest->structure.rootX,
                (float)nearest->structure.groundY,
                (float)nearest->structure.rootZ
            };
            game->floraNearbyNearestBoundsMin = (Vector3){
                (float)nearest->structure.minX,
                (float)nearest->structure.minY,
                (float)nearest->structure.minZ
            };
            game->floraNearbyNearestBoundsMax = (Vector3){
                (float)nearest->structure.maxX,
                (float)nearest->structure.maxY,
                (float)nearest->structure.maxZ
            };
        }
        game->floraNearbyNearestPrimary = nearest->structure.primaryBlock;
        game->floraNearbyNearestAccent = nearest->structure.accentBlock;
        game->floraNearbyNearestKind = nearest->structure.kind;
        game->floraNearbyNearestTaxon =
            (FloraTaxonId)nearest->structure.taxonId;
        game->floraNearbyNearestDistance = nearest->distance;
        game->floraNearbyNearestWindResponse = nearest->structure.windResponse;
    }

    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL flora near ok radius=%u count=%u "
                      "returned=%u\n",
                      game->debugControl.floraNearbyRadius, total, returned);
    DebugControlReply(&game->debugControl,
                      "DEBUG_CONTROL flora near scan loaded_chunks=%u "
                      "structures=%u cube_structures=%u cube_root_chunks=%u "
                      "nearest_cube_root=%.3f nearest_cube_address=%d,%d,%d,%d\n",
                      loadedChunks, loadedStructures, cubeStructures,
                      cubeRootChunks, nearestCubeRoot,
                      nearestCubeAddress.face, nearestCubeAddress.u,
                      nearestCubeAddress.v, nearestCubeAddress.radial);
    for (unsigned index = 0u; index < returned; index++) {
        const FloraNearbyEntry *entry = &entries[index];
        const FloraStructureInstance *structure = &entry->structure;
        if (structure->cubeNative) {
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL flora near item index=%u kind=%s "
                "taxon=\"%s\" root_address=%d,%d,%d,%d "
                "local_bounds=%d,%d,%d,%d,%d,%d distance=%.3f "
                "primary=\"%s\" accent=\"%s\" wind=%.3f chunk=%d,%d lod=%s\n",
                index, FloraStructureKindName(structure->kind),
                FloraTaxonName((FloraTaxonId)structure->taxonId),
                structure->rootAddress.face, structure->rootAddress.u,
                structure->rootAddress.v, structure->rootAddress.radial,
                structure->minU, structure->minY, structure->minV,
                structure->maxU, structure->maxY, structure->maxV,
                entry->distance, BlockName(structure->primaryBlock),
                BlockName(structure->accentBlock), structure->windResponse,
                entry->chunkX, entry->chunkZ,
                FloraLodName(entry->activeLod));
        } else {
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL flora near item index=%u kind=%s "
                "taxon=\"%s\" root=%d,%d,%d bounds=%d,%d,%d,%d,%d,%d "
                "distance=%.3f primary=\"%s\" accent=\"%s\" wind=%.3f "
                "chunk=%d,%d lod=%s\n",
                index, FloraStructureKindName(structure->kind),
                FloraTaxonName((FloraTaxonId)structure->taxonId),
                structure->rootX, structure->groundY, structure->rootZ,
                structure->minX, structure->minY, structure->minZ,
                structure->maxX, structure->maxY, structure->maxZ,
                entry->distance, BlockName(structure->primaryBlock),
                BlockName(structure->accentBlock), structure->windResponse,
                entry->chunkX, entry->chunkZ,
                FloraLodName(entry->activeLod));
        }
    }
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
    if (command == DEBUG_CONTROL_COMMAND_FLORA_NEAR) {
        FloraNear(game);
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
    if (strncmp(name, "flora.flood_", 12u) == 0) {
        ChunkFloraFloodStats stats = ChunksGetFloraFloodStats();
        if (strcmp(name, "flora.flood_affected") == 0) {
            return FloraDslNumber(outValue, stats.affected);
        }
        if (strcmp(name, "flora.flood_damaged") == 0) {
            return FloraDslNumber(outValue, stats.damaged);
        }
        if (strcmp(name, "flora.flood_dead") == 0) {
            return FloraDslNumber(outValue, stats.dead);
        }
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
    if (strcmp(name, "flora.near_active") == 0) {
        return FloraDslBool(outValue, game->floraNearbyActive);
    }
    if (strcmp(name, "flora.near_count") == 0) {
        return FloraDslNumber(outValue, game->floraNearbyActive ?
                              game->floraNearbyCount : 0u);
    }
    if (strcmp(name, "flora.near_nearest_kind") == 0) {
        return FloraDslString(outValue, game->floraNearbyActive ?
                              FloraStructureKindName(
                                  game->floraNearbyNearestKind) : "none");
    }
    if (strcmp(name, "flora.near_nearest_taxon") == 0) {
        return FloraDslString(outValue, game->floraNearbyActive ?
                              FloraTaxonName(game->floraNearbyNearestTaxon) :
                              "none");
    }
    if (strcmp(name, "flora.near_nearest_root") == 0) {
        return FloraDslVec3(outValue, game->floraNearbyActive ?
                            game->floraNearbyNearestRoot : (Vector3){ 0 });
    }
    if (strcmp(name, "flora.near_nearest_bounds_min") == 0) {
        return FloraDslVec3(outValue, game->floraNearbyActive ?
                            game->floraNearbyNearestBoundsMin : (Vector3){ 0 });
    }
    if (strcmp(name, "flora.near_nearest_bounds_max") == 0) {
        return FloraDslVec3(outValue, game->floraNearbyActive ?
                            game->floraNearbyNearestBoundsMax : (Vector3){ 0 });
    }
    if (strcmp(name, "flora.near_nearest_distance") == 0) {
        return FloraDslNumber(outValue, game->floraNearbyActive ?
                              game->floraNearbyNearestDistance : -1.0);
    }
    if (strcmp(name, "flora.near_nearest_primary") == 0) {
        return FloraDslString(outValue, game->floraNearbyActive ?
                              BlockName(game->floraNearbyNearestPrimary) :
                              "air");
    }
    if (strcmp(name, "flora.near_nearest_accent") == 0) {
        return FloraDslString(outValue, game->floraNearbyActive ?
                              BlockName(game->floraNearbyNearestAccent) :
                              "air");
    }
    if (strcmp(name, "flora.near_nearest_wind_response") == 0) {
        return FloraDslNumber(outValue, game->floraNearbyActive ?
                              game->floraNearbyNearestWindResponse : 0.0);
    }
    return false;
}
