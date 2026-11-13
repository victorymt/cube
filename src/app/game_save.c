#define _POSIX_C_SOURCE 200809L
#include "app/game_save.h"
#include "app/game_ecology.h"
#include "app/game_internal.h"

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
#include "gameplay/player_state.h"
#include "gameplay/camera_distance.h"
#include "gameplay/ship.h"
#include "gameplay/ship_locator.h"
#include "space/space_chunks.h"
#include "space/space_persistence.h"
#include "space/space_state.h"
#include "world/chunks.h"
#include "world/cube_address_save.h"
#include "world/world_cube.h"
#include "world/nether.h"
#include "world/hydrology_runtime.h"
#include "world/radiation_runtime.h"
#include "world/save_format.h"
#include "world/surface_save.h"
#include "world/terrain.h"
#include "world/terrain_generation.h"
#include "world/world_state.h"
#include "world/block_lighting.h"
#include "world/block_edits.h"
#include "world/world_environment.h"
#include "world/world_surface_snapshot.h"
#include "world/world_persistence.h"
#include "world/fluid.h"
#include "world/weather_impact.h"
#include "world/volcano_runtime.h"
#include "world/lava_runtime.h"
#include "world/ash_runtime.h"

#include "raymath.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#define SAVE_FILE_BAK "voxelcraft_save.bak"
#define SAVE_MAX_FILE_BYTES (256u * 1024u * 1024u)
#define MAX_LOAD_EDIT_COUNT 1000000u

typedef struct GameSaveContext {
    const Player *player;
} GameSaveContext;

typedef struct LoadedGameSave {
    TerrainMode terrain;
    uint32_t seed;
    uint32_t terrainGenerationVersion;
    WorldSurfaceTopologyPolicy topologyPolicy;
    WorldDimension dimension;
    Player player;
    bool hasSurfacePose;
    SurfacePose surfacePose;
    CubeAddressBlock editBlock;
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
    CubeAddressBlockRelease(&data->editBlock);
}

static bool GameSaveWriteCellEdits(FILE *file)
{
    int legacyCount = WorldGetEditCount();
    unsigned cubeCount = WorldCubeEditCount();
    if (!file || legacyCount < 0 ||
        (uint64_t)legacyCount + cubeCount > MAX_LOAD_EDIT_COUNT) return false;
    CubeAddressBlock block = {
        .count = (uint32_t)legacyCount + cubeCount
    };
    if (block.count > 0u) {
        block.addresses = calloc(block.count, sizeof(*block.addresses));
        block.cells = calloc(block.count, sizeof(*block.cells));
        if (!block.addresses || !block.cells) {
            CubeAddressBlockRelease(&block);
            return false;
        }
    }
    uint32_t out = 0u;
    for (int index = 0; index < legacyCount; index++, out++) {
        const BlockEdit *edit = WorldGetEditAt(index);
        uint32_t editBodyId = WorldGetEditDimensionAt(index);
        WorldSurfaceContext context = WorldCurrentSurfaceContext();
        bool currentSurface = context.active &&
            editBodyId == context.bodyId;
        WorldDimension dimension = currentSurface
            ? (context.kind == WORLD_SURFACE_HOME
                ? WORLD_DIMENSION_HOME : WORLD_DIMENSION_PLANET)
            : (editBodyId == 0u
                ? WORLD_DIMENSION_HOME : WORLD_DIMENSION_PLANET);
        if (!edit) goto fail;
        WorldBlockAddress address = { .dimension = dimension };
        SurfaceMapCell mapCell = { 0 };
        if (!WorldGetEditSurfaceAddressAt(index, &address.at.surface) ||
            !WorldGetEditSurfaceMapCellAt(index, &mapCell)) goto fail;
        WorldCell cell = WorldCellFromLegacyBlock(edit->type);
        if (currentSurface) {
            int localX = mapCell.x - WorldSurfaceMapOriginX();
            int localZ = mapCell.z - WorldSurfaceMapOriginZ();
            cell.waterVolume = FluidGetVolumeAt(localX, edit->y, localZ);
        }
        if (!WorldCellIsValid(cell)) goto fail;
        block.addresses[out] = address;
        block.cells[out] = cell;
    }
    for (unsigned index = 0; index < cubeCount; index++, out++) {
        if (!WorldCubeCellEditAt(index, &block.addresses[out], &block.cells[out]))
            goto fail;
    }
    bool ok = CubeAddressSaveWrite(file, &block);
    CubeAddressBlockRelease(&block);
    return ok;
fail:
    CubeAddressBlockRelease(&block);
    return false;
}

#ifdef GAME_SAVE_TESTING
static bool GameSavePolicyUsesCubeGrid(void)
{
    WorldSurfaceTopologyPolicy policy = WorldCurrentTopologyPolicy();
    return policy.home.kind == SURFACE_TOPOLOGY_CUBE_GRID ||
           policy.planets.kind == SURFACE_TOPOLOGY_CUBE_GRID;
}
#endif

bool GameSavePolicyOwnsCubeAddress(WorldSurfaceTopologyPolicy policy,
                                   WorldBlockAddress address)
{
    if (address.dimension == WORLD_DIMENSION_HOME)
        return policy.home.kind == SURFACE_TOPOLOGY_CUBE_GRID;
    if (address.dimension == WORLD_DIMENSION_PLANET)
        return policy.planets.kind == SURFACE_TOPOLOGY_CUBE_GRID;
    return false;
}


static bool GameSaveWriteRadiationRuntimeState(FILE *file)
{
    return RadiationRuntimeGlobalSaveState(file);
}

static const char *GameLoadRadiationRuntimeState(
    FILE *file, WorldSaveFormat format)
{
    if (WorldSaveFormatHasRadiationRuntime(format)) {
        return RadiationRuntimeGlobalLoadState(file)
            ? NULL
            : "Load failed: radiation state is corrupted.";
    }
    return RadiationRuntimeGlobalReset(NULL)
        ? NULL
        : "Load failed: radiation state could not be reset.";
}

static bool GameSaveWriteHydrologyRuntimeState(FILE *file)
{
    return HydrologyRuntimeGlobalSaveState(file);
}

static const char *GameLoadHydrologyRuntimeState(
    FILE *file, WorldSaveFormat format)
{
    if (WorldSaveFormatHasHydrologyRuntime(format)) {
        return HydrologyRuntimeGlobalLoadState(file)
            ? NULL
            : "Load failed: hydrology state is corrupted.";
    }
    return HydrologyRuntimeGlobalReset(NULL)
        ? NULL
        : "Load failed: hydrology state could not be reset.";
}

static const char *GameLoadVolcanoState(FILE *file, WorldSaveFormat format)
{
    if (WorldSaveFormatHasVolcanoes(format)) {
        return VolcanoRuntimeLoad(file) ? NULL : "Load failed: volcano state is corrupted.";
    }
    VolcanoRuntimeReset();
    return NULL;
}

static const char *GameLoadLavaState(FILE *file, WorldSaveFormat format)
{
    if (WorldSaveFormatHasLava(format)) {
        return LavaRuntimeLoad(file) ? NULL : "Load failed: lava state is corrupted.";
    }
    LavaRuntimeReset();
    return NULL;
}

static const char *GameLoadAshState(FILE *file, WorldSaveFormat format)
{
    if (WorldSaveFormatHasAsh(format)) {
        return AshRuntimeLoad(file) ? NULL : "Load failed: ash state is corrupted.";
    }
    AshRuntimeReset();
    return NULL;
}

static const char *GameLoadInventoryRemainders(FILE *file, WorldSaveFormat format)
{
    if (WorldSaveFormatHasInventoryRemainders(format)) {
        return InventoryLoadRemainders(file) ? NULL : "Load failed: collected material fractions are corrupted.";
    }
    InventoryResetRemainders();
    return NULL;
}


static bool GameSaveWritePlayerState(FILE *file, const Player *player,
                                     WorldDimension dimension)
{
    if (!file || !player) return false;
    float local[9] = {
        player->position.x, player->position.y, player->position.z,
        player->velocity.x, player->velocity.y, player->velocity.z,
        player->yaw, player->pitch, player->floating ? 1.0f : 0.0f
    };
    bool hasPose = WorldIsSurfaceDimension(dimension);
    SurfacePose pose = { 0 };
    if (hasPose) {
        WorldSurfaceContext context = WorldCurrentSurfaceContext();
        SurfaceMapProjection projection = SurfaceProjectMapCoordinates(
            (float)context.originX + player->position.x,
            (float)context.originZ + player->position.z);
        if (!SurfacePoseFromGeographic(
                context.bodyId, projection.longitude, projection.latitude,
                player->position.y, player->yaw, player->pitch, &pose)) {
            hasPose = false;
        }
        if (hasPose) {
            SurfaceBasis basis;
            if (!SurfacePoseBasis(&pose, &basis)) hasPose = false;
            else pose.velocity = SurfaceVectorAdd(
                SurfaceVectorAdd(
                    SurfaceVectorScale(basis.right, player->velocity.x),
                    SurfaceVectorScale(basis.forward, player->velocity.z)),
                SurfaceVectorScale(basis.up, player->velocity.y));
        }
    }
    uint8_t posePresent = hasPose ? 1u : 0u;
    if (fwrite(local, sizeof(local), 1, file) != 1 ||
        fwrite(&posePresent, sizeof(posePresent), 1, file) != 1) return false;
    if (!hasPose) return true;
    double poseData[10] = {
        pose.position.x, pose.position.y, pose.position.z,
        pose.velocity.x, pose.velocity.y, pose.velocity.z,
        pose.heading.x, pose.heading.y, pose.heading.z, pose.pitch
    };
    return fwrite(&pose.bodyId, sizeof(pose.bodyId), 1, file) == 1 &&
           fwrite(poseData, sizeof(poseData), 1, file) == 1;
}

static bool GameSaveWriteFile(FILE *file, void *opaque)
{
    const GameSaveContext *context = opaque;
    const Player *player = context ? context->player : NULL;
    if (!file || !player) return false;

    uint32_t terrainGenerationVersion = TerrainGenerationVersion();
    uint32_t activeDimension = (uint32_t)WorldCurrentDimension();
    uint32_t seed = WorldGetSeed();
    uint32_t terrain = (uint32_t)WorldTerrainMode();
    bool ok = true;
    const char *stage = "header";
#define SAVE_STEP(expression, label) do { \
        if (ok) { stage = label; ok = (expression); } \
    } while (0)
    SAVE_STEP(WorldSaveWriteHeader(file, WorldCurrentTopologyPolicy()), "header");
    SAVE_STEP(fwrite(&terrainGenerationVersion,
                     sizeof(terrainGenerationVersion), 1, file) == 1,
              "generation");
    SAVE_STEP(fwrite(&activeDimension, sizeof(activeDimension), 1, file) == 1,
              "dimension");
    SAVE_STEP(fwrite(&seed, sizeof(seed), 1, file) == 1, "seed");
    SAVE_STEP(fwrite(&terrain, sizeof(terrain), 1, file) == 1, "terrain");
    SAVE_STEP(GameSaveWritePlayerState(
                  file, player, (WorldDimension)activeDimension), "player");
    SAVE_STEP(GameSaveWriteCellEdits(file), "cells");
    SAVE_STEP(InventorySave(file), "inventory");
    SAVE_STEP(ShipSaveState(file), "ship");
    SAVE_STEP(PlanetWorldSaveState(file), "planet-world");
    SAVE_STEP(HomeWorldSaveState(file), "home-world");
    SAVE_STEP(AlbumSave(file), "album");
    SAVE_STEP(SpaceSaveEdits(file), "space-edits");
    SAVE_STEP(NetherSaveEdits(file), "nether-edits");
    SAVE_STEP(SpaceSaveState(file), "space");
    SAVE_STEP(EntitiesSaveState(file), "entities");
    SAVE_STEP(PlanetEcologySaveState(file), "ecology");
    SAVE_STEP(EvolutionCatalogSaveState(file), "evolution");
    SAVE_STEP(ShipLocatorSaveState(file), "ship-locator");
    SAVE_STEP(WorldPersistenceSaveExtension(file), "fluid");
    SAVE_STEP(WeatherImpactSaveState(file), "weather-impact");
    SAVE_STEP(GameSaveWriteRadiationRuntimeState(file), "radiation");
    SAVE_STEP(GameSaveWriteHydrologyRuntimeState(file), "hydrology");
    SAVE_STEP(MapMarkersSaveState(file), "map-markers");
    SAVE_STEP(GameEcologySaveClock(file), "ecology-clock");
    SAVE_STEP(VolcanoRuntimeSave(file), "volcano");
    SAVE_STEP(LavaRuntimeSave(file), "lava");
    SAVE_STEP(AshRuntimeSave(file), "ash");
    SAVE_STEP(InventorySaveRemainders(file), "inventory-fractions");
    SAVE_STEP(!ferror(file), "stream");
#undef SAVE_STEP
    if (!ok) TraceLog(LOG_ERROR, "Save failed at stage %s", stage);
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

static bool GameLoadPlayerState(FILE *file, LoadedGameSave *data)
{
    float local[9];
    uint8_t posePresent = 0u;
    if (!file || !data ||
        fread(local, sizeof(local), 1, file) != 1 ||
        fread(&posePresent, sizeof(posePresent), 1, file) != 1 ||
        posePresent > 1u) return false;
    for (int index = 0; index < 8; index++) {
        if (!isfinite(local[index])) return false;
    }
    if (local[8] != 0.0f && local[8] != 1.0f) return false;
    data->player.position = (Vector3){ local[0], local[1], local[2] };
    data->player.velocity = (Vector3){ local[3], local[4], local[5] };
    data->player.yaw = local[6];
    data->player.pitch = local[7];
    data->player.floating = local[8] != 0.0f;
    data->player.onGround = false;
    data->hasSurfacePose = posePresent != 0u;
    if (!data->hasSurfacePose) return true;

    uint32_t bodyId = 0u;
    double poseData[10];
    if (fread(&bodyId, sizeof(bodyId), 1, file) != 1 ||
        fread(poseData, sizeof(poseData), 1, file) != 1) return false;
    for (unsigned index = 0; index < 10u; index++) {
        if (!isfinite(poseData[index])) return false;
    }
    SurfacePose pose = {
        .bodyId = bodyId,
        .position = { poseData[0], poseData[1], poseData[2] },
        .velocity = { poseData[3], poseData[4], poseData[5] },
        .heading = { poseData[6], poseData[7], poseData[8] },
        .pitch = poseData[9]
    };
    if (!SurfacePoseIsValid(&pose)) return false;
    data->surfacePose = pose;
    return true;
}

static bool GameLoadCurrentCorePayload(FILE *file, LoadedGameSave *data)
{
    uint32_t terrainGenerationVersion = 0u;
    uint32_t activeDimension = 0u;
    uint32_t seed = DEFAULT_WORLD_SEED;
    uint32_t terrain = 0u;
    if (!file || !data ||
        fread(&terrainGenerationVersion, sizeof(terrainGenerationVersion),
              1, file) != 1 ||
        terrainGenerationVersion < TERRAIN_GENERATION_MIN ||
        terrainGenerationVersion > TERRAIN_GENERATION_CURRENT ||
        fread(&activeDimension, sizeof(activeDimension), 1, file) != 1 ||
        activeDimension > (uint32_t)WORLD_DIMENSION_NETHER ||
        fread(&seed, sizeof(seed), 1, file) != 1 ||
        fread(&terrain, sizeof(terrain), 1, file) != 1 ||
        terrain > (uint32_t)TERRAIN_FLAT ||
        !GameLoadPlayerState(file, data) ||
        !CubeAddressSaveRead(file, &data->editBlock) ||
        !InventoryLoad(file) || !ShipLoadState(file) ||
        !PlanetWorldLoadState(file) || !HomeWorldLoadState(file)) {
        LoadedGameSaveRelease(data);
        return false;
    }
    data->terrainGenerationVersion = terrainGenerationVersion;
    data->dimension = (WorldDimension)activeDimension;
    data->terrain = (TerrainMode)terrain;
    data->seed = seed == 0u ? DEFAULT_WORLD_SEED : seed;
    return true;
}


#ifdef GAME_SAVE_TESTING
bool GameSaveTestPolicyUsesCubeGrid(void)
{
    return GameSavePolicyUsesCubeGrid();
}
bool GameSaveTestWriteCubeEdits(FILE *file)
{
    return GameSaveWriteCellEdits(file);
}
const char *GameSaveTestLoadInventoryRemainders(FILE *file, WorldSaveFormat format)
{
    return GameLoadInventoryRemainders(file, format);
}
const char *GameSaveTestLoadAshState(FILE *file, WorldSaveFormat format)
{
    return GameLoadAshState(file, format);
}
const char *GameSaveTestLoadLavaState(FILE *file, WorldSaveFormat format)
{
    return GameLoadLavaState(file, format);
}
const char *GameSaveTestLoadVolcanoState(FILE *file, WorldSaveFormat format)
{
    return GameLoadVolcanoState(file, format);
}


bool GameSaveTestWriteRadiationRuntimeState(FILE *file)
{
    return GameSaveWriteRadiationRuntimeState(file);
}

const char *GameSaveTestLoadRadiationRuntimeState(
    FILE *file, WorldSaveFormat format)
{
    return GameLoadRadiationRuntimeState(file, format);
}

bool GameSaveTestWriteHydrologyRuntimeState(FILE *file)
{
    return GameSaveWriteHydrologyRuntimeState(file);
}

const char *GameSaveTestLoadHydrologyRuntimeState(
    FILE *file, WorldSaveFormat format)
{
    return GameLoadHydrologyRuntimeState(file, format);
}
#endif

static bool GameLoadResolvePlayerPose(LoadedGameSave *data)
{
    if (!data || !data->hasSurfacePose) return true;
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    if (!context.active || data->surfacePose.bodyId != context.bodyId) {
        return false;
    }
    Vector3 direction = Vector3Normalize((Vector3){
        (float)data->surfacePose.position.x,
        (float)data->surfacePose.position.y,
        (float)data->surfacePose.position.z
    });
    Vector2 map;
    SurfaceBasis basis;
    if (!SurfaceMapCoordinatesFromDirection(direction, &map) ||
        !SurfacePoseBasis(&data->surfacePose, &basis)) return false;
    data->player.position = (Vector3){
        map.x - (float)context.originX,
        (float)SurfacePoseAltitude(&data->surfacePose),
        map.y - (float)context.originZ
    };
    data->player.velocity = (Vector3){
        (float)SurfaceVectorDot(data->surfacePose.velocity, basis.right),
        (float)SurfaceVectorDot(data->surfacePose.velocity, basis.up),
        (float)SurfaceVectorDot(data->surfacePose.velocity, basis.forward)
    };
    data->player.yaw = atan2f(
        (float)SurfaceVectorDot(data->surfacePose.heading, basis.right),
        (float)SurfaceVectorDot(data->surfacePose.heading, basis.forward));
    data->player.pitch = (float)data->surfacePose.pitch;
    return isfinite(data->player.position.x) &&
           isfinite(data->player.position.y) &&
           isfinite(data->player.position.z);
}

static bool GameLoadInstallCellEdits(LoadedGameSave *data,
                                     int *outLoadedCount)
{
    if (outLoadedCount) *outLoadedCount = 0;
    if (!data || data->editBlock.count > MAX_LOAD_EDIT_COUNT) return false;
    int legacyCount = 0;
    unsigned cubeCount = 0;
    for (uint32_t index = 0; index < data->editBlock.count; index++) {
        if (GameSavePolicyOwnsCubeAddress(
                data->topologyPolicy, data->editBlock.addresses[index])) {
            cubeCount++;
        } else {
            legacyCount++;
        }
    }
    BlockEdit *edits = legacyCount > 0 ? calloc(legacyCount, sizeof(*edits)) : NULL;
    uint32_t *dimensions = legacyCount > 0 ? calloc(legacyCount, sizeof(*dimensions)) : NULL;
    SurfaceAddress *addresses = legacyCount > 0 ? calloc(legacyCount, sizeof(*addresses)) : NULL;
    SurfaceMapCell *mapCells = legacyCount > 0 ? calloc(legacyCount, sizeof(*mapCells)) : NULL;
    if (legacyCount > 0 && (!edits || !dimensions || !addresses || !mapCells)) {
        free(edits); free(dimensions); free(addresses); free(mapCells);
        return false;
    }
    int legacyOut = 0;
    for (uint32_t index = 0; index < data->editBlock.count; index++) {
        WorldBlockAddress address = data->editBlock.addresses[index];
        WorldCell cell = data->editBlock.cells[index];
        if (GameSavePolicyOwnsCubeAddress(data->topologyPolicy, address)) {
            if (!WorldCubeSetCell(address, cell)) {
                free(edits); free(dimensions); free(addresses); free(mapCells);
                return false;
            }
            continue;
        }
        SurfaceMapCell mapCell;
        if (address.dimension == WORLD_DIMENSION_HOME ||
            address.dimension == WORLD_DIMENSION_PLANET) {
            if (!SurfaceAddressCanonicalMapCell(address.at.surface, &mapCell) ||
                !SurfaceAddressEqual(
                    address.at.surface,
                    SurfaceAddressFromMapCoordinates(
                        address.at.surface.bodyId, (float)mapCell.x,
                        (float)mapCell.z, address.at.surface.radial))) {
                free(edits); free(dimensions); free(addresses); free(mapCells);
                return false;
            }
            addresses[legacyOut] = address.at.surface;
        } else {
            mapCell = SurfaceCanonicalMapCell(
                (float)address.at.cartesian.x, (float)address.at.cartesian.z);
            addresses[legacyOut] = SurfaceAddressFromMapCoordinates(
                (uint32_t)address.dimension, (float)mapCell.x,
                (float)mapCell.z, address.at.cartesian.y);
        }
        dimensions[legacyOut] =
            address.dimension == WORLD_DIMENSION_HOME ||
                    address.dimension == WORLD_DIMENSION_PLANET
                ? address.at.surface.bodyId : (uint32_t)address.dimension;
        mapCells[legacyOut] = mapCell;
        edits[legacyOut] = (BlockEdit){
            mapCell.x,
            address.dimension == WORLD_DIMENSION_HOME ||
                    address.dimension == WORLD_DIMENSION_PLANET
                ? address.at.surface.radial : address.at.cartesian.y,
            mapCell.z,
            cell.block
        };
        legacyOut++;
    }
    bool ok = WorldPersistenceInstallEdits(
        edits, dimensions, addresses, mapCells, legacyCount);
    free(edits); free(dimensions); free(addresses); free(mapCells);
    if (ok && outLoadedCount) *outLoadedCount = (int)(legacyCount + cubeCount);
    return ok;
}

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
    if (WorldSaveFormatHasWeatherImpact(format)) {
        if (!WeatherImpactLoadState(file)) {
            return "Load failed: weather impact state is corrupted.";
        }
    } else {
        WeatherImpactReset();
    }
    const char *radiationError =
        GameLoadRadiationRuntimeState(file, format);
    if (radiationError) return radiationError;
    const char *hydrologyError =
        GameLoadHydrologyRuntimeState(file, format);
    if (hydrologyError) return hydrologyError;
    if (WorldSaveFormatHasMapMarkers(format) &&
        !MapMarkersReadState(file, &data->mapMarkers)) {
        return "Load failed: map marker state is corrupted.";
    }
    if (!MapMarkersMigrateLegacyState(
            &data->mapMarkers, PlanetWorldSeed(),
            PlanetWorldOriginX(), PlanetWorldOriginZ())) {
        return "Load failed: map marker coordinates are corrupted.";
    }
    if (WorldSaveFormatHasEcologyClock(format)) {
        if (!GameEcologyLoadClock(file)) {
            return "Load failed: ecology clock is corrupted.";
        }
    } else {
        GameEcologyResetClock();
    }
    const char *volcanoError = GameLoadVolcanoState(file, format);
    if (volcanoError) return volcanoError;
    const char *lavaError = GameLoadLavaState(file, format);
    if (lavaError) return lavaError;
    const char *ashError = GameLoadAshState(file, format);
    if (ashError) return ashError;
    const char *fractionError = GameLoadInventoryRemainders(file, format);
    if (fractionError) return fractionError;
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

    LoadedGameSave data = {
        .terrain = TERRAIN_VARIED,
        .seed = DEFAULT_WORLD_SEED,
        .dimension = WORLD_DIMENSION_HOME
    };
    MapMarkersEmptyState(&data.mapMarkers);

    const char *headerError = WorldSaveReadHeader(file, &data.topologyPolicy);
    if (headerError) return GameLoadFail(transaction, headerError);
    const WorldSaveFormat format = WORLD_SAVE_FORMAT_V30;
    DrainChunkGen();
    UnloadAllSpaceChunks();
    bool cubePolicy = data.topologyPolicy.home.kind == SURFACE_TOPOLOGY_CUBE_GRID ||
                      data.topologyPolicy.planets.kind == SURFACE_TOPOLOGY_CUBE_GRID;
    WorldSetCubeRuntimeEnabled(cubePolicy);
    if (!WorldInstallTopologyPolicy(data.topologyPolicy)) {
        return GameLoadFail(transaction, "Load failed: unsupported surface topology policy.");
    }
    if (!GameLoadCurrentCorePayload(file, &data)) {
        LoadedGameSaveRelease(&data);
        return GameLoadFail(
            transaction, "Load failed: save file is corrupted.");
    }
    TerrainGenerationSetVersion(data.terrainGenerationVersion);
    GameSyncWorldSurfaceSnapshot(data.seed);

    const char *loadError =
        GameLoadCurrentExtendedPayload(file, format, &data);
    if (loadError || fgetc(file) != EOF || ferror(file)) {
        LoadedGameSaveRelease(&data);
        return GameLoadFail(
            transaction, loadError ? loadError
                                   : "Load failed: save file has trailing data.");
    }
    DrainChunkGen();
    UnloadAllChunks();
    UnloadAllSpaceChunks();
    UnloadAllNetherChunks();
    WorldSetTerrainMode(data.terrain);
    WorldSetSeed(data.seed);
    GameSyncWorldSurfaceSnapshot(data.seed);
    if (!GameLoadResolvePlayerPose(&data)) {
        LoadedGameSaveRelease(&data);
        return GameLoadFail(transaction, "Load failed: surface pose is incompatible with this world.");
    }

    bool savedInNether = data.dimension == WORLD_DIMENSION_NETHER;
    if (data.terrainGenerationVersion < TERRAIN_GENERATION_LEGACY &&
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

    WorldCubeClearEdits();
    int loadedEditCount = 0;
    bool editIndexReady = GameLoadInstallCellEdits(&data, &loadedEditCount);
    PlayerRestoreState(player, &data.player);
    WorldSetNetherActive(savedInNether);
    PlayerResetRuntimeState(player);
    ShipLocatorRecord shipLocator = data.shipLocator;
    MapMarkerState mapMarkers = data.mapMarkers;
    LoadedGameSaveRelease(&data);

    if (!editIndexReady) {
        return GameLoadFail(
            transaction, "Load failed: cell edit payload could not be installed.");
    }
    RebuildTorchList();
    SpaceRebuildTorchList();
    ClearUndoHistory();
    ShipLocatorSetRecord(&shipLocator);
    MapMarkersInstallState(&mapMarkers);
    WorldResetSurfaceRebaseEvent();

    if (WorldIsSurfaceActive()) {
        GameRequestWorldStreaming(
            player->position, player->velocity,
            CameraStreamingDistanceForHeight(
                player->position.y + EYE_HEIGHT), 8u);
    }
    transaction->loadedEditCount = loadedEditCount;
    return true;
}

bool GameLoadMap(Player *player)
{
    if (!player) {
        GameNoticePost("Load failed: player state is unavailable.");
        return false;
    }
    FILE *file = fopen(SAVE_FILE, "rb");
    if (!file) {
        GameNoticePost("Load failed: voxelcraft_save.txt was not found.");
        return false;
    }

    struct stat saveStat;
    if (fstat(fileno(file), &saveStat) != 0 || saveStat.st_size < 0 ||
        (uint64_t)saveStat.st_size > SAVE_MAX_FILE_BYTES) {
        fclose(file);
        GameNoticePost("Load failed: save file is too large or unreadable.");
        return false;
    }

    // Reject retired files and unsupported topologies before even taking a
    // checkpoint: rejected loads must not reset the live world's transients.
    WorldSurfaceTopologyPolicy policy;
    const char *headerError = WorldSaveReadHeader(file, &policy);
    if (headerError || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        GameNoticePost(headerError ? headerError : "Load failed: save file is unreadable.");
        return false;
    }

    GameLoadTransaction transaction = { .player = player };
    SaveIoTransactionResult result = SaveIoReadTransactional(
        file, GameSaveCheckpoint, GameLoadApplyFile, &transaction);
    fclose(file);

    if (result == SAVE_IO_TRANSACTION_CHECKPOINT_FAILED) {
        GameNoticePost("Load failed: current game state could not be protected.");
        return false;
    }
    if (result == SAVE_IO_TRANSACTION_ROLLBACK_FAILED) {
        GameNoticePost(
            "Load failed and the previous game state could not be restored.");
        return false;
    }
    if (result == SAVE_IO_TRANSACTION_READ_FAILED) {
        GameNoticePost(transaction.error ? transaction.error
                                         : "Load failed: save file is corrupted.");
        return false;
    }

    GameNoticePost(TextFormat("Loaded %s (%d edits).", SAVE_FILE,
                              transaction.loadedEditCount));
    return true;
}
