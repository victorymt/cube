#include "world/surface_legacy_contract.h"

#include "world/block_edits.h"
#include "world/fluid.h"
#include "world/chunks.h"
#include "world/surface_contract_internal.h"
#include "world/surface_topology.h"
#include "world/terrain.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>

#define LEGACY_CHUNK_TAG UINT64_C(0x4c45474143594d31)

typedef struct LegacyContract {
    WorldSurfaceSnapshot snapshot;
    TerrainMode mode;
} LegacyContract;

static bool LegacyCellIsKnown(const SurfaceContract *contract,
                              SurfaceAddress address);

static bool LegacyAddressMatches(const SurfaceContract *contract,
                                 SurfaceAddress address)
{
    return contract && contract->implementation &&
           SurfaceContractAddressIsValid(address) &&
           address.bodyId == contract->bodyId && InHeight(address.radial);
}

static bool LegacyMapCoordinates(const LegacyContract *legacy,
                                 SurfaceAddress address, int *outX,
                                 int *outZ)
{
    SurfaceMapCell canonical;
    if (!legacy || !outX || !outZ ||
        !SurfaceAddressCanonicalMapCell(address, &canonical)) return false;
    *outX = canonical.x - legacy->snapshot.context.originX;
    *outZ = canonical.z - legacy->snapshot.context.originZ;
    return true;
}

static bool LegacyChunkFromAddress(const SurfaceContract *contract,
                                   SurfaceAddress address,
                                   SurfaceChunkHandle *outChunk)
{
    if (!outChunk || !LegacyAddressMatches(contract, address)) return false;
    int x, z, cx, cz, lx, lz;
    if (!LegacyMapCoordinates(contract->implementation, address, &x, &z)) {
        return false;
    }
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    (void)lx;
    (void)lz;
    *outChunk = (SurfaceChunkHandle){ .opaque = {
        LEGACY_CHUNK_TAG, contract->bodyId, (uint64_t)(int64_t)cx,
        (uint64_t)(int64_t)cz, 0u
    } };
    return true;
}

static bool LegacyDecodeChunk(const SurfaceContract *contract,
                              SurfaceChunkHandle chunk, int *outCx,
                              int *outCz)
{
    if (!contract || !contract->implementation || !outCx || !outCz ||
        chunk.opaque[0] != LEGACY_CHUNK_TAG ||
        chunk.opaque[1] != contract->bodyId || chunk.opaque[4] != 0u) {
        return false;
    }
    int64_t cx = (int64_t)chunk.opaque[2];
    int64_t cz = (int64_t)chunk.opaque[3];
    if (cx < INT32_MIN || cx > INT32_MAX || cz < INT32_MIN || cz > INT32_MAX) {
        return false;
    }
    *outCx = (int)cx;
    *outCz = (int)cz;
    return true;
}

static bool LegacyChunkAnchorAddress(const SurfaceContract *contract,
                                     SurfaceChunkHandle chunk,
                                     SurfaceAddress *outAddress)
{
    LegacyContract *legacy = contract ? contract->implementation : NULL;
    int cx, cz;
    if (!outAddress || !LegacyDecodeChunk(contract, chunk, &cx, &cz)) {
        return false;
    }
    float mapX = (float)legacy->snapshot.context.originX +
                 ((float)cx + 0.5f) * (float)CHUNK_SIZE;
    float mapZ = (float)legacy->snapshot.context.originZ +
                 ((float)cz + 0.5f) * (float)CHUNK_SIZE;
    *outAddress = SurfaceAddressFromMapCoordinates(contract->bodyId, mapX,
                                                    mapZ, 0);
    return SurfaceContractAddressIsValid(*outAddress);
}

static bool LegacyChunkIsValid(const SurfaceContract *contract,
                               SurfaceChunkHandle chunk)
{
    int cx, cz;
    return LegacyDecodeChunk(contract, chunk, &cx, &cz);
}

static bool LegacyChunkEqual(const SurfaceContract *contract,
                             SurfaceChunkHandle a, SurfaceChunkHandle b)
{
    int acx, acz, bcx, bcz;
    return LegacyDecodeChunk(contract, a, &acx, &acz) &&
           LegacyDecodeChunk(contract, b, &bcx, &bcz) &&
           acx == bcx && acz == bcz;
}

static bool LegacyChunkNeighbor(const SurfaceContract *contract,
                                SurfaceChunkHandle chunk,
                                SurfaceDirection direction,
                                SurfaceChunkHandle *outChunk,
                                SurfaceDirection *outReturnDirection)
{
    LegacyContract *legacy = contract ? contract->implementation : NULL;
    int cx, cz;
    if (!outChunk || !legacy ||
        !LegacyDecodeChunk(contract, chunk, &cx, &cz) ||
        direction > SURFACE_DIRECTION_V_PLUS) return false;
    int deltaX = direction == SURFACE_DIRECTION_U_PLUS ? CHUNK_SIZE :
                 direction == SURFACE_DIRECTION_U_MINUS ? -CHUNK_SIZE : 0;
    int deltaZ = direction == SURFACE_DIRECTION_V_PLUS ? CHUNK_SIZE :
                 direction == SURFACE_DIRECTION_V_MINUS ? -CHUNK_SIZE : 0;
    float mapX = (float)legacy->snapshot.context.originX +
                 ((float)cx + 0.5f) * (float)CHUNK_SIZE + (float)deltaX;
    float mapZ = (float)legacy->snapshot.context.originZ +
                 ((float)cz + 0.5f) * (float)CHUNK_SIZE + (float)deltaZ;
    SurfaceMapCell canonical = SurfaceCanonicalMapCell(mapX, mapZ);
    int localX = canonical.x - legacy->snapshot.context.originX;
    int localZ = canonical.z - legacy->snapshot.context.originZ;
    int nextCx = FloorDivInt(localX, CHUNK_SIZE);
    int nextCz = FloorDivInt(localZ, CHUNK_SIZE);
    *outChunk = (SurfaceChunkHandle){ .opaque = {
        LEGACY_CHUNK_TAG, contract->bodyId, (uint64_t)(int64_t)nextCx,
        (uint64_t)(int64_t)nextCz, 0u
    } };
    if (outReturnDirection) {
        *outReturnDirection = (SurfaceDirection)((int)direction ^ 1);
    }
    return true;
}

static bool LegacySectionFromAddress(const SurfaceContract *contract,
                                     SurfaceAddress address,
                                     SurfaceSectionHandle *outSection)
{
    if (!outSection ||
        !LegacyChunkFromAddress(contract, address, &outSection->chunk)) {
        return false;
    }
    outSection->sectionY = FloorDivInt(address.radial,
                                       SURFACE_SECTION_HEIGHT);
    return SurfaceSectionInBounds(outSection->sectionY);
}

static bool LegacySectionIsValid(const SurfaceContract *contract,
                                 SurfaceSectionHandle section)
{
    return LegacyChunkIsValid(contract, section.chunk) &&
           SurfaceSectionInBounds(section.sectionY);
}

static bool LegacyLocateCell(const SurfaceContract *contract,
                             SurfaceAddress address, int x, int y, int z,
                             SurfaceCellRuntime *outRuntime)
{
    if (!outRuntime || !LegacyAddressMatches(contract, address) ||
        !InHeight(y)) return false;
    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    Chunk *chunk = FindChunk(cx, cz);
    if (!chunk || !chunk->loaded) return false;
    int sectionY = SurfaceSectionYFromBlockY(y);
    ChunkSection *section = ChunkGetSection(chunk, sectionY, false);
    if (!section && !LegacyCellIsKnown(contract, address)) return false;
    int ly = SurfaceSectionLocalYFromBlockY(y);
    *outRuntime = (SurfaceCellRuntime){
        .chunk = chunk,
        .section = section,
        .sectionY = sectionY,
        .localX = lx,
        .localY = ly,
        .localZ = lz,
        .block = section
            ? (BlockType)section->blocks[lx][ly][lz] : BLOCK_AIR
    };
    return true;
}

static bool LegacyStepCell(const SurfaceContract *contract,
                           SurfaceAddress address, int x, int y, int z,
                           SurfaceDirection direction,
                           SurfaceAddress *outAddress, int *outX, int *outY,
                           int *outZ, SurfaceDirection *outReturnDirection)
{
    LegacyContract *legacy = contract ? contract->implementation : NULL;
    if (!legacy || !outAddress || !LegacyAddressMatches(contract, address)) {
        return false;
    }
    SurfaceAddress next = address;
    if (direction <= SURFACE_DIRECTION_V_PLUS) {
        int deltaX = direction == SURFACE_DIRECTION_U_PLUS ? 1 :
                     direction == SURFACE_DIRECTION_U_MINUS ? -1 : 0;
        int deltaZ = direction == SURFACE_DIRECTION_V_PLUS ? 1 :
                     direction == SURFACE_DIRECTION_V_MINUS ? -1 : 0;
        SurfaceMapCell canonical = SurfaceCanonicalMapCell(
            (float)legacy->snapshot.context.originX + (float)x +
                (float)deltaX,
            (float)legacy->snapshot.context.originZ + (float)z +
                (float)deltaZ);
        x = canonical.x - legacy->snapshot.context.originX;
        z = canonical.z - legacy->snapshot.context.originZ;
        next = SurfaceAddressFromMapCoordinates(
            contract->bodyId, (float)canonical.x, (float)canonical.z, y);
    } else if (direction == SURFACE_DIRECTION_RADIAL_MINUS) {
        if (y == INT_MIN) return false;
        y--;
        next.radial = y;
    } else if (direction == SURFACE_DIRECTION_RADIAL_PLUS) {
        if (y == INT_MAX) return false;
        y++;
        next.radial = y;
    } else {
        return false;
    }
    if (!LegacyAddressMatches(contract, next)) return false;
    *outAddress = next;
    if (outX) *outX = x;
    if (outY) *outY = y;
    if (outZ) *outZ = z;
    if (outReturnDirection) {
        *outReturnDirection = (SurfaceDirection)((int)direction ^ 1);
    }
    return true;
}

static bool LegacyChunkCellAddress(const SurfaceContract *contract,
                                   const Chunk *chunk, int localX, int y,
                                   int localZ, SurfaceAddress *outAddress)
{
    LegacyContract *legacy = contract ? contract->implementation : NULL;
    if (!legacy || !chunk || !outAddress || !InHeight(y)) return false;
    *outAddress = SurfaceAddressFromMapCoordinates(
        contract->bodyId,
        (float)legacy->snapshot.context.originX +
            (float)(chunk->cx * CHUNK_SIZE + localX),
        (float)legacy->snapshot.context.originZ +
            (float)(chunk->cz * CHUNK_SIZE + localZ), y);
    return SurfaceContractAddressIsValid(*outAddress) &&
           LegacyAddressMatches(contract, *outAddress);
}

static bool LegacyChunkRuntime(const SurfaceContract *contract,
                               SurfaceChunkHandle handle,
                               Chunk **outChunk)
{
    int cx = 0;
    int cz = 0;
    if (!outChunk || !LegacyDecodeChunk(contract, handle, &cx, &cz)) {
        return false;
    }
    *outChunk = FindChunk(cx, cz);
    return *outChunk && (*outChunk)->loaded;
}

static bool LegacyCellEdit(const LegacyContract *legacy,
                           SurfaceAddress address, WorldCell *outCell)
{
    int x, z;
    BlockType block;
    if (!LegacyMapCoordinates(legacy, address, &x, &z) ||
        !WorldGetBlockEditForCurrentDimensionAt(x, address.radial, z, &block)) {
        return false;
    }
    *outCell = WorldCellFromLegacyBlock(block);
    return true;
}

static bool LegacyCellIsKnown(const SurfaceContract *contract,
                              SurfaceAddress address)
{
    if (!LegacyAddressMatches(contract, address)) return false;
    LegacyContract *legacy = contract->implementation;
    WorldCell ignored;
    if (LegacyCellEdit(legacy, address, &ignored)) return true;
    int x, z, cx, cz, lx, lz;
    if (!LegacyMapCoordinates(legacy, address, &x, &z)) return false;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    Chunk *chunk = FindChunk(cx, cz);
    if (!chunk || !chunk->loaded) return false;
    if (ChunkTryGetLocalCell(chunk, lx, address.radial, lz, &ignored)) {
        return true;
    }
    return ChunkTerrainSectionIsResolved(
        chunk, SurfaceSectionYFromBlockY(address.radial));
}

static bool LegacyCellRead(const SurfaceContract *contract,
                           SurfaceAddress address, WorldCell *outCell)
{
    if (!outCell || !LegacyAddressMatches(contract, address)) return false;
    LegacyContract *legacy = contract->implementation;
    int x, z, cx, cz, lx, lz;
    if (!LegacyMapCoordinates(legacy, address, &x, &z)) return false;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    Chunk *chunk = FindChunk(cx, cz);
    if (!chunk || !chunk->loaded) {
        if (!LegacyCellEdit(legacy, address, outCell)) return false;
    } else if (!ChunkTryGetLocalCell(chunk, lx, address.radial, lz,
                                     outCell)) {
        if (!ChunkTerrainSectionIsResolved(
                chunk, SurfaceSectionYFromBlockY(address.radial)) &&
            !LegacyCellEdit(legacy, address, outCell)) return false;
        if (!ChunkTerrainSectionIsResolved(
                chunk, SurfaceSectionYFromBlockY(address.radial))) {
            outCell->waterVolume = 0u;
            return WorldCellIsValid(*outCell);
        }
        WorldCell edited;
        *outCell = LegacyCellEdit(legacy, address, &edited)
            ? edited : (WorldCell){ BLOCK_AIR, 0u };
    }
    if (address.bodyId == contract->bodyId &&
        address.bodyId == WorldCurrentSurfaceId()) {
        outCell->waterVolume = FluidGetVolumeAt(x, address.radial, z);
    }
    return WorldCellIsValid(*outCell);
}

static bool LegacyCellWrite(const SurfaceContract *contract,
                            SurfaceAddress address, WorldCell cell)
{
    if (!LegacyAddressMatches(contract, address)) return false;
    if (cell.block == BLOCK_WATER) cell = WorldCellFromLegacyBlock(BLOCK_WATER);
    if (!WorldCellIsValid(cell)) return false;
    LegacyContract *legacy = contract->implementation;
    int x, z, cx, cz, lx, lz;
    if (!LegacyMapCoordinates(legacy, address, &x, &z)) return false;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    Chunk *chunk = FindChunk(cx, cz);
    if (!chunk || !chunk->loaded ||
        !ChunkSetLocalCell(chunk, lx, address.radial, lz, cell)) return false;
    if (!FluidSetVolumeAt(x, address.radial, z, cell.waterVolume)) {
        return false;
    }
    MarkChunkDirtyAtBlock(x, address.radial, z);
    return true;
}

static bool LegacyNeighbor(const SurfaceContract *contract,
                           SurfaceAddress address, SurfaceDirection direction,
                           SurfaceAddress *outAddress,
                           SurfaceDirection *outReturnDirection)
{
    if (!outAddress || !LegacyAddressMatches(contract, address)) return false;
    SurfaceAddress next = address;
    if (direction <= SURFACE_DIRECTION_V_PLUS) {
        SurfaceMapCell canonical;
        if (!SurfaceAddressCanonicalMapCell(address, &canonical)) return false;
        int deltaX = direction == SURFACE_DIRECTION_U_PLUS ? 1 :
                     direction == SURFACE_DIRECTION_U_MINUS ? -1 : 0;
        int deltaZ = direction == SURFACE_DIRECTION_V_PLUS ? 1 :
                     direction == SURFACE_DIRECTION_V_MINUS ? -1 : 0;
        next = SurfaceAddressFromMapCoordinates(
            contract->bodyId, (float)(canonical.x + deltaX),
            (float)(canonical.z + deltaZ), address.radial);
    } else if (direction == SURFACE_DIRECTION_RADIAL_MINUS) {
        if (address.radial == INT_MIN) return false;
        next.radial--;
    } else if (direction == SURFACE_DIRECTION_RADIAL_PLUS) {
        if (address.radial == INT_MAX) return false;
        next.radial++;
    } else {
        return false;
    }
    if (!LegacyAddressMatches(contract, next)) return false;
    *outAddress = next;
    if (outReturnDirection) *outReturnDirection = (SurfaceDirection)((int)direction ^ 1);
    return true;
}

static void LegacyMarkAddressDirty(const SurfaceContract *contract,
                                   SurfaceAddress address)
{
    if (!LegacyAddressMatches(contract, address)) return;
    int x, z;
    if (!LegacyMapCoordinates(contract->implementation, address, &x, &z)) return;
    MarkChunkDirtyAtBlock(x, address.radial, z);
}

static bool LegacySampleAt(const SurfaceContract *contract,
                           SurfaceAddress address,
                           SurfaceContractSample *outSample)
{
    LegacyContract *legacy = contract ? contract->implementation : NULL;
    int x, z;
    if (!outSample || !LegacyAddressMatches(contract, address) ||
        !LegacyMapCoordinates(legacy, address, &x, &z)) return false;
    SurfaceTerrainSample terrain = SurfaceTerrainAt(x, z, legacy->mode);
    bool planet = legacy->snapshot.context.kind == WORLD_SURFACE_PLANET;
    PlanetBiome planetBiome = PLANET_BIOME_PLAINS;
    if (planet) {
        terrain.bathymetry = PlanetBathymetryAt(x, z);
        terrain.slope = fmaxf(
            fabsf((float)(PlanetTerrainHeight(x + 1, z) -
                          PlanetTerrainHeight(x - 1, z))),
            fabsf((float)(PlanetTerrainHeight(x, z - 1) -
                          PlanetTerrainHeight(x, z + 1)))) * 0.5f;
        planetBiome = PlanetBiomeAt(x, z);
    }
    return SurfaceContractSampleFromTerrain(
        &terrain, planet, planetBiome, outSample);
}

static SurfaceRegionHandle LegacyRegionForAddress(
    const SurfaceContract *contract, SurfaceAddress address)
{
    if (!LegacyAddressMatches(contract, address) ||
        !contract->implementation) return (SurfaceRegionHandle){ 0 };
    SurfaceMapCell canonical;
    if (!SurfaceAddressCanonicalMapCell(address, &canonical)) {
        return (SurfaceRegionHandle){ 0 };
    }
    int regionX = FloorDivInt(canonical.x, 128);
    int regionZ = FloorDivInt(canonical.z, 128);
    uint64_t packed = ((uint64_t)(uint32_t)regionX << 32) |
                      (uint32_t)regionZ;
    return (SurfaceRegionHandle){ .opaque = {
        ((uint64_t)contract->bodyId << 32) | UINT32_C(0x4c454731), packed
    } };
}

static bool LegacySameRegion(const SurfaceContract *contract,
                             SurfaceAddress a, SurfaceAddress b)
{
    if (!LegacyAddressMatches(contract, a) ||
        !LegacyAddressMatches(contract, b)) return false;
    SurfaceRegionHandle ra = LegacyRegionForAddress(contract, a);
    SurfaceRegionHandle rb = LegacyRegionForAddress(contract, b);
    return ra.opaque[0] == rb.opaque[0] && ra.opaque[1] == rb.opaque[1];
}

static void LegacyRelease(SurfaceContract *contract)
{
    if (!contract) return;
    free(contract->implementation);
    free(contract);
}

static const SurfaceContractOps legacyOps = {
    .address_is_valid = SurfaceContractAddressIsValid,
    .address_equal = SurfaceContractAddressEqual,
    .address_hash = SurfaceContractAddressHash,
    .chunk_from_address = LegacyChunkFromAddress,
    .chunk_anchor_address = LegacyChunkAnchorAddress,
    .chunk_is_valid = LegacyChunkIsValid,
    .chunk_equal = LegacyChunkEqual,
    .chunk_neighbor = LegacyChunkNeighbor,
    .section_from_address = LegacySectionFromAddress,
    .section_is_valid = LegacySectionIsValid,
    .locate_cell = LegacyLocateCell,
    .step_cell = LegacyStepCell,
    .chunk_cell_address = LegacyChunkCellAddress,
    .chunk_runtime = LegacyChunkRuntime,
    .cell_is_known = LegacyCellIsKnown,
    .cell_read = LegacyCellRead,
    .cell_write = LegacyCellWrite,
    .sample_at = LegacySampleAt,
    .neighbor = LegacyNeighbor,
    .mark_address_dirty = LegacyMarkAddressDirty,
    .same_region = LegacySameRegion,
    .region_for_address = LegacyRegionForAddress,
    .supports_physical_raycast = false,
    .release = LegacyRelease
};

SurfaceContract *SurfaceLegacyContractCreate(
    const WorldSurfaceSnapshot *snapshot, TerrainMode mode, uint64_t generation)
{
    (void)generation;
    if (!snapshot || !snapshot->context.active ||
        snapshot->context.topology.kind != SURFACE_TOPOLOGY_LEGACY_MAP) {
        return NULL;
    }
    SurfaceContract *contract = calloc(1, sizeof(*contract));
    LegacyContract *legacy = calloc(1, sizeof(*legacy));
    if (!contract || !legacy) {
        free(contract);
        free(legacy);
        return NULL;
    }
    legacy->snapshot = *snapshot;
    legacy->mode = mode;
    contract->ops = &legacyOps;
    contract->implementation = legacy;
    contract->topology = snapshot->context.topology;
    contract->bodyId = snapshot->context.bodyId;
    return contract;
}
