#define _POSIX_C_SOURCE 200809L
#include "app/game_save.h"

#include "core/config.h"
#include "core/game_notice.h"
#include "core/save_io.h"
#include "ecology/ecology.h"
#include "ecology/entity.h"
#include "ecology/evolution_catalog.h"
#include "gameplay/album.h"
#include "gameplay/inventory.h"
#include "gameplay/map_markers.h"
#include "gameplay/player.h"
#include "gameplay/ship.h"
#include "gameplay/ship_locator.h"
#include "space/space_chunks.h"
#include "space/space_persistence.h"
#include "space/space_state.h"
#include "world/chunks.h"
#include "world/nether.h"
#include "world/save_format.h"
#include "world/surface_save.h"
#include "world/terrain.h"
#include "world/world.h"
#include "world/world_environment.h"
#include "world/world_persistence.h"
#include "world/weather_impact.h"

#include "raymath.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#define SAVE_FILE_BAK "voxelcraft_save.bak"
#define TERRAIN_GENERATION_VERSION 6u
#define MIN_SUPPORTED_TERRAIN_GENERATION_VERSION 2u
#define SAVE_MAX_FILE_BYTES (256u * 1024u * 1024u)
#define MAX_LOAD_EDIT_COUNT 1000000u

typedef struct GameSaveContext {
    const Player *player;
} GameSaveContext;

typedef struct LoadedGameSave {
    TerrainMode terrain;
    uint32_t seed;
    uint32_t terrainGenerationVersion;
    WorldDimension dimension;
    Player player;
    int editCount;
    BlockEdit *edits;
    uint32_t *dimensions;
    SurfaceAddress playerAddress;
    SurfaceAddress *editAddresses;
    SurfaceMapCell *editMapCells;
    ShipLocatorRecord shipLocator;
    MapMarkerState mapMarkers;
} LoadedGameSave;

typedef struct GameLoadTransaction {
    Player *player;
    const char *error;
    int loadedEditCount;
} GameLoadTransaction;

static void LoadedGameSaveRelease(LoadedGameSave *data)
{
    if (!data) return;
    free(data->edits);
    free(data->dimensions);
    free(data->editAddresses);
    free(data->editMapCells);
    data->edits = NULL;
    data->dimensions = NULL;
    data->editAddresses = NULL;
    data->editMapCells = NULL;
}

static bool GameSaveWriteEdits(FILE *file)
{
    if (!file) return false;
    int count = WorldGetEditCount();
    if (count < 0) return false;
    uint32_t editCount = (uint32_t)count;
    if (fwrite(&editCount, sizeof(editCount), 1, file) != 1) return false;

    for (int index = 0; index < count; index++) {
        const BlockEdit *edit = WorldGetEditAt(index);
        if (!edit || fwrite(edit, sizeof(*edit), 1, file) != 1) return false;
    }
    for (int index = 0; index < count; index++) {
        uint32_t dimension = WorldGetEditDimensionAt(index);
        if (fwrite(&dimension, sizeof(dimension), 1, file) != 1) return false;
    }
    return true;
}

static bool GameSaveWriteSphericalTrailer(FILE *file, const Player *player)
{
    if (!file || !player) return false;
    WorldDimension dimension = WorldCurrentDimension();
    bool playerHasSurfaceAddress = WorldIsSurfaceDimension(dimension);
    SurfaceAddress playerAddress = playerHasSurfaceAddress
        ? SurfaceAddressAtWorld(player->position.x, player->position.z,
                                (int)floorf(player->position.y))
        : SurfaceAddressFromMapCoordinates(0u, 0.0f, 0.0f, 0);

    int editCount = WorldGetEditCount();
    SurfaceAddress *addresses = NULL;
    SurfaceMapCell *mapCells = NULL;
    if (editCount > 0) {
        addresses = malloc((size_t)editCount * sizeof(*addresses));
        mapCells = malloc((size_t)editCount * sizeof(*mapCells));
        if (!addresses || !mapCells) {
            free(addresses);
            free(mapCells);
            return false;
        }
    }
    bool ok = editCount >= 0;
    for (int index = 0; ok && index < editCount; index++) {
        const BlockEdit *edit = WorldGetEditAt(index);
        uint32_t editDimension = WorldGetEditDimensionAt(index);
        ok = edit && WorldGetEditSurfaceAddressAt(index, &addresses[index]) &&
             WorldGetEditSurfaceMapCellAt(index, &mapCells[index]) &&
             addresses[index].bodyId == editDimension &&
             addresses[index].radial == edit->y;
    }
    ok = ok && SurfaceSaveWriteTrailer(
        file, playerHasSurfaceAddress, playerAddress, addresses, mapCells,
        (uint32_t)editCount);
    free(addresses);
    free(mapCells);
    return ok;
}

static bool GameSaveWriteFile(FILE *file, void *opaque)
{
    const GameSaveContext *context = opaque;
    const Player *player = context ? context->player : NULL;
    if (!file || !player) return false;

    uint32_t terrainGenerationVersion = TERRAIN_GENERATION_VERSION;
    uint32_t activeDimension = (uint32_t)WorldCurrentDimension();
    uint32_t seed = WorldGetSeed();
    uint32_t terrain = (uint32_t)WorldTerrainMode();
    float playerData[6] = {
        player->position.x, player->position.y, player->position.z,
        player->yaw, player->pitch, player->floating ? 1.0f : 0.0f
    };

    bool ok = WorldSaveFormatWriteCurrent(file);
    ok = ok && fwrite(&terrainGenerationVersion,
                      sizeof(terrainGenerationVersion), 1, file) == 1;
    ok = ok && fwrite(&activeDimension, sizeof(activeDimension), 1, file) == 1;
    ok = ok && fwrite(&seed, sizeof(seed), 1, file) == 1;
    ok = ok && fwrite(&terrain, sizeof(terrain), 1, file) == 1;
    ok = ok && fwrite(playerData, sizeof(playerData), 1, file) == 1;
    ok = ok && GameSaveWriteEdits(file);
    ok = ok && InventorySave(file) && ShipSaveState(file) &&
         PlanetWorldSaveState(file) && HomeWorldSaveState(file);
    ok = ok && AlbumSave(file) && SpaceSaveEdits(file) &&
         NetherSaveEdits(file);
    ok = ok && SpaceSaveState(file) && EntitiesSaveState(file) &&
         PlanetEcologySaveState(file) && EvolutionCatalogSaveState(file) &&
         ShipLocatorSaveState(file) &&
         WorldPersistenceSaveExtension(file) && WeatherImpactSaveState(file) &&
         MapMarkersSaveState(file) &&
         GameSaveWriteSphericalTrailer(file, player) && !ferror(file);
    return ok;
}

void GameSaveMap(const Player *player)
{
    if (!player) {
        GameNoticePost("Save failed: player state is unavailable.");
        return;
    }

    GameSaveContext context = { .player = player };
    if (!SaveIoWriteAtomic(
            SAVE_FILE, SAVE_FILE_BAK, GameSaveWriteFile, &context)) {
        GameNoticePost("Save failed: existing save was kept intact.");
        return;
    }
    GameNoticePost(TextFormat("Saved map to %s (%d edits).", SAVE_FILE,
                                WorldGetEditCount()));
}

static bool GameLoadBlockEditPayload(FILE *file, LoadedGameSave *data)
{
    uint32_t seed = DEFAULT_WORLD_SEED;
    uint32_t terrain = 0u;
    float playerData[6];
    uint32_t count = 0u;
    if (!file || !data ||
        fread(&seed, sizeof(seed), 1, file) != 1 ||
        fread(&terrain, sizeof(terrain), 1, file) != 1 ||
        terrain > (uint32_t)TERRAIN_FLAT ||
        fread(playerData, sizeof(playerData), 1, file) != 1 ||
        fread(&count, sizeof(count), 1, file) != 1 ||
        count > MAX_LOAD_EDIT_COUNT) {
        return false;
    }
    for (int index = 0; index < 6; index++) {
        if (!isfinite(playerData[index])) return false;
    }
    if (playerData[5] != 0.0f && playerData[5] != 1.0f) return false;

    BlockEdit *edits = NULL;
    uint32_t *dimensions = NULL;
    if (count > 0u) {
        edits = malloc((size_t)count * sizeof(*edits));
        dimensions = malloc((size_t)count * sizeof(*dimensions));
        if (!edits || !dimensions ||
            fread(edits, sizeof(*edits), (size_t)count, file) != count ||
            fread(dimensions, sizeof(*dimensions), (size_t)count, file) !=
                count) {
            free(edits);
            free(dimensions);
            return false;
        }
        if (!WorldPersistenceEditsValid(edits, (int)count)) {
            free(edits);
            free(dimensions);
            return false;
        }
    }

    data->terrain = (TerrainMode)terrain;
    data->seed = seed == 0u ? DEFAULT_WORLD_SEED : seed;
    data->player.position = (Vector3){
        playerData[0], playerData[1], playerData[2]
    };
    data->player.yaw = playerData[3];
    data->player.pitch = playerData[4];
    data->player.floating = playerData[5] != 0.0f;
    data->player.velocity = Vector3Zero();
    data->player.onGround = false;
    data->editCount = (int)count;
    data->edits = edits;
    data->dimensions = dimensions;
    return true;
}

static bool GameLoadCurrentCorePayload(FILE *file, LoadedGameSave *data)
{
    uint32_t terrainGenerationVersion = 0u;
    uint32_t activeDimension = 0u;
    if (!file || !data ||
        fread(&terrainGenerationVersion, sizeof(terrainGenerationVersion),
              1, file) != 1 ||
        terrainGenerationVersion < MIN_SUPPORTED_TERRAIN_GENERATION_VERSION ||
        terrainGenerationVersion > TERRAIN_GENERATION_VERSION ||
        fread(&activeDimension, sizeof(activeDimension), 1, file) != 1 ||
        activeDimension > (uint32_t)WORLD_DIMENSION_NETHER ||
        !GameLoadBlockEditPayload(file, data)) {
        return false;
    }
    if (!InventoryLoad(file) || !ShipLoadState(file) ||
        !PlanetWorldLoadState(file) || !HomeWorldLoadState(file)) {
        LoadedGameSaveRelease(data);
        return false;
    }
    data->terrainGenerationVersion = terrainGenerationVersion;
    data->dimension = (WorldDimension)activeDimension;
    return true;
}

static bool GameLoadSphericalTrailer(FILE *file, LoadedGameSave *data)
{
    if (!file || !data || data->editCount < 0) return false;
    bool playerHasSurfaceAddress = false;
    uint32_t schemaVersion = 0u;
    SurfaceAddress playerAddress = { 0 };
    SurfaceAddress *addresses = NULL;
    SurfaceMapCell *mapCells = NULL;
    if (!SurfaceSaveReadTrailer(
            file, (uint32_t)data->editCount, &schemaVersion,
            &playerHasSurfaceAddress,
            &playerAddress, &addresses, &mapCells)) {
        return false;
    }

    bool surfaceDimension = WorldIsSurfaceDimension(data->dimension);
    uint32_t expectedBodyId = data->dimension == WORLD_DIMENSION_PLANET
        ? PlanetWorldSeed() : 0u;
    if (playerHasSurfaceAddress != surfaceDimension ||
        (surfaceDimension && playerAddress.bodyId != expectedBodyId)) {
        free(addresses);
        free(mapCells);
        return false;
    }
    if (surfaceDimension) {
        float mapX = data->player.position.x;
        float mapZ = data->player.position.z;
        if (schemaVersion == 1u &&
            data->dimension == WORLD_DIMENSION_PLANET) {
            mapX += (float)PlanetWorldOriginX();
            mapZ += (float)PlanetWorldOriginZ();
        }
        float northDirection = 1.0f;
        Vector2 canonical = SurfaceCanonicalMapPosition(
            mapX, mapZ, &northDirection);
        SurfaceAddress expected = SurfaceAddressFromMapCoordinates(
            expectedBodyId, canonical.x, canonical.y,
            (int)floorf(data->player.position.y));
        if (!SurfaceAddressEqual(playerAddress, expected)) {
            free(addresses);
            free(mapCells);
            return false;
        }
        data->player.position.x = canonical.x;
        data->player.position.z = canonical.y;
        if (northDirection < 0.0f) {
            data->player.yaw = atan2f(
                sinf(data->player.yaw), -cosf(data->player.yaw));
        }
    }
    for (int index = 0; index < data->editCount; index++) {
        if (!data->edits || !data->dimensions ||
            addresses[index].bodyId != data->dimensions[index] ||
            addresses[index].radial != data->edits[index].y) {
            free(addresses);
            free(mapCells);
            return false;
        }
        uint32_t bodyId = data->dimensions[index];
        if (schemaVersion == 1u) {
            bool originKnown = bodyId == 0u || bodyId == PlanetWorldSeed();
            int originX = bodyId == 0u ? 0 : PlanetWorldOriginX();
            int originZ = bodyId == 0u ? 0 : PlanetWorldOriginZ();
            if (!originKnown && !SurfaceAddressCanonicalMapCell(
                    addresses[index], &mapCells[index])) {
                free(addresses);
                free(mapCells);
                return false;
            }
            if (originKnown) {
                mapCells[index] = SurfaceCanonicalMapCell(
                    (float)originX + (float)data->edits[index].x,
                    (float)originZ + (float)data->edits[index].z);
            }
        } else {
            SurfaceMapCell expectedCell = SurfaceCanonicalMapCell(
                (float)data->edits[index].x,
                (float)data->edits[index].z);
            if (mapCells[index].x != expectedCell.x ||
                mapCells[index].z != expectedCell.z) {
                free(addresses);
                free(mapCells);
                return false;
            }
        }
        SurfaceAddress expectedAddress = SurfaceAddressFromMapCoordinates(
            bodyId, (float)mapCells[index].x, (float)mapCells[index].z,
            data->edits[index].y);
        if (!SurfaceAddressEqual(addresses[index], expectedAddress)) {
            free(addresses);
            free(mapCells);
            return false;
        }
        data->edits[index].x = mapCells[index].x;
        data->edits[index].z = mapCells[index].z;
    }
    data->playerAddress = playerAddress;
    data->editAddresses = addresses;
    data->editMapCells = mapCells;
    return true;
}

#ifdef GAME_SAVE_TESTING
bool GameSaveTestLoadSphericalTrailer(
    FILE *file, WorldDimension dimension, Player *player,
    BlockEdit *edits, uint32_t *dimensions, int editCount,
    SurfaceAddress **outEditAddresses, SurfaceMapCell **outEditMapCells)
{
    if (!player || !outEditAddresses || !outEditMapCells) return false;
    *outEditAddresses = NULL;
    *outEditMapCells = NULL;
    LoadedGameSave data = {
        .dimension = dimension,
        .player = *player,
        .editCount = editCount,
        .edits = edits,
        .dimensions = dimensions
    };
    if (!GameLoadSphericalTrailer(file, &data)) return false;
    *player = data.player;
    *outEditAddresses = data.editAddresses;
    *outEditMapCells = data.editMapCells;
    return true;
}
#endif

static const char *GameLoadCurrentExtendedPayload(
    FILE *file, WorldSaveFormat format, LoadedGameSave *data)
{
    if (!AlbumLoad(file) || !SpaceLoadEdits(file, SPACE_LAYER_Y) ||
        !NetherLoadEdits(file)) {
        return "Load failed: save file is corrupted.";
    }
    if (!SpaceLoadState(file)) {
        return SpaceLastLoadError() == SPACE_LOAD_ERROR_INCOMPATIBLE_SCALE
            ? "Load failed: save uses the retired 20 u/AU space scale."
            : "Load failed: save file is corrupted.";
    }
    if (!EntitiesLoadState(file)) {
        return "Load failed: entity state is corrupted.";
    }
    if (!PlanetEcologyLoadState(file)) {
        return "Load failed: ecology state is corrupted.";
    }
    if (!EvolutionCatalogLoadState(file)) {
        return "Load failed: evolution catalog state is corrupted.";
    }
    if (!ShipLocatorReadStateForSpaceLayer(
            file, &data->shipLocator, SPACE_LAYER_Y)) {
        return "Load failed: ship locator state is corrupted.";
    }
    if (!WorldPersistenceLoadExtension(file)) {
        return "Load failed: fluid state is corrupted.";
    }
    if (format == WORLD_SAVE_FORMAT_V20) {
        if (!WeatherImpactLoadState(file)) {
            return "Load failed: weather impact state is corrupted.";
        }
    } else {
        WeatherImpactReset();
    }
    if (WorldSaveFormatHasMapMarkers(format) &&
        !MapMarkersReadState(file, &data->mapMarkers)) {
        return "Load failed: map marker state is corrupted.";
    }
    if (!MapMarkersMigrateLegacyState(
            &data->mapMarkers, PlanetWorldSeed(),
            PlanetWorldOriginX(), PlanetWorldOriginZ())) {
        return "Load failed: map marker coordinates are corrupted.";
    }
    if (!GameLoadSphericalTrailer(file, data)) {
        return "Load failed: spherical save state is corrupted.";
    }
    return NULL;
}

static bool GameLoadFail(GameLoadTransaction *transaction, const char *message)
{
    if (transaction && !transaction->error) transaction->error = message;
    return false;
}

static bool GameSaveCheckpoint(FILE *file, void *opaque)
{
    GameLoadTransaction *transaction = opaque;
    GameSaveContext context = {
        .player = transaction ? transaction->player : NULL
    };
    return GameSaveWriteFile(file, &context);
}

static bool GameLoadApplyFile(FILE *file, void *opaque)
{
    GameLoadTransaction *transaction = opaque;
    Player *player = transaction ? transaction->player : NULL;
    if (!file || !player) {
        return GameLoadFail(
            transaction, "Load failed: player state is unavailable.");
    }

    DrainChunkGen();
    UnloadAllSpaceChunks();

    LoadedGameSave data = {
        .terrain = TERRAIN_VARIED,
        .seed = DEFAULT_WORLD_SEED,
        .dimension = WORLD_DIMENSION_HOME
    };
    MapMarkersEmptyState(&data.mapMarkers);

    WorldSaveFormat format = WorldSaveFormatRead(file);
    if (format == WORLD_SAVE_FORMAT_UNSUPPORTED) {
        return GameLoadFail(
            transaction,
            "Load failed: V17 and older flat saves are incompatible with spherical worlds.");
    }
    if (!GameLoadCurrentCorePayload(file, &data)) {
        LoadedGameSaveRelease(&data);
        return GameLoadFail(
            transaction, "Load failed: save file is corrupted.");
    }

    const char *loadError =
        GameLoadCurrentExtendedPayload(file, format, &data);
    if (loadError || fgetc(file) != EOF || ferror(file)) {
        LoadedGameSaveRelease(&data);
        return GameLoadFail(
            transaction, loadError ? loadError
                                   : "Load failed: save file has trailing data.");
    }
    if (!WorldPersistenceReserveEdits(data.editCount)) {
        LoadedGameSaveRelease(&data);
        return GameLoadFail(
            transaction, "Load failed: not enough memory to apply save.");
    }

    DrainChunkGen();
    UnloadAllChunks();
    UnloadAllSpaceChunks();
    UnloadAllNetherChunks();
    WorldSetTerrainMode(data.terrain);
    WorldSetSeed(data.seed);

    bool savedInNether = data.dimension == WORLD_DIMENSION_NETHER;
    if (data.terrainGenerationVersion != TERRAIN_GENERATION_VERSION &&
        WorldIsSurfaceActive() && !savedInNether) {
        int landingX = (int)floorf(data.player.position.x);
        int landingZ = (int)floorf(data.player.position.z);
        int groundY = 0;
        if (FindSafeSurfaceLanding(landingX, landingZ, 128, 0,
                                   &landingX, &landingZ, &groundY)) {
            data.player.position = (Vector3){
                (float)landingX + 0.5f, (float)groundY + 3.0f,
                (float)landingZ + 0.5f
            };
        }
    }

    *player = data.player;
    WorldSetNetherActive(savedInNether);
    PlayerResetRuntimeState(player);
    bool editIndexReady = WorldPersistenceInstallEdits(
        data.edits, data.dimensions, data.editAddresses, data.editMapCells,
        data.editCount);
    ShipLocatorRecord shipLocator = data.shipLocator;
    MapMarkerState mapMarkers = data.mapMarkers;
    int loadedEditCount = data.editCount;
    LoadedGameSaveRelease(&data);

    if (!editIndexReady) {
        return GameLoadFail(
            transaction, "Load failed: edit index rebuild failed.");
    }
    RebuildTorchList();
    SpaceRebuildTorchList();
    ClearUndoHistory();
    ShipLocatorSetRecord(&shipLocator);
    MapMarkersInstallState(&mapMarkers);
    WorldResetSurfaceRebaseEvent();

    if (WorldIsSurfaceActive()) {
        UpdateChunks(player->position,
                     EffectiveRenderDistanceForHeight(
                         player->position.y + EYE_HEIGHT));
    }
    transaction->loadedEditCount = loadedEditCount;
    return true;
}

void GameLoadMap(Player *player)
{
    if (!player) {
        GameNoticePost("Load failed: player state is unavailable.");
        return;
    }
    FILE *file = fopen(SAVE_FILE, "rb");
    if (!file) {
        GameNoticePost("Load failed: voxelcraft_save.txt was not found.");
        return;
    }

    struct stat saveStat;
    if (fstat(fileno(file), &saveStat) != 0 || saveStat.st_size < 0 ||
        (uint64_t)saveStat.st_size > SAVE_MAX_FILE_BYTES) {
        fclose(file);
        GameNoticePost("Load failed: save file is too large or unreadable.");
        return;
    }

    GameLoadTransaction transaction = { .player = player };
    SaveIoTransactionResult result = SaveIoReadTransactional(
        file, GameSaveCheckpoint, GameLoadApplyFile, &transaction);
    fclose(file);

    if (result == SAVE_IO_TRANSACTION_CHECKPOINT_FAILED) {
        GameNoticePost("Load failed: current game state could not be protected.");
        return;
    }
    if (result == SAVE_IO_TRANSACTION_ROLLBACK_FAILED) {
        GameNoticePost(
            "Load failed and the previous game state could not be restored.");
        return;
    }
    if (result == SAVE_IO_TRANSACTION_READ_FAILED) {
        GameNoticePost(transaction.error ? transaction.error
                                         : "Load failed: save file is corrupted.");
        return;
    }

    GameNoticePost(TextFormat("Loaded %s (%d edits).", SAVE_FILE,
                              transaction.loadedEditCount));
}
