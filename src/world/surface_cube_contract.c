#include "world/surface_cube_contract.h"

#include "world/chunks.h"
#include "world/chunks_internal.h"
#include "world/chunks_cube.h"
#include "world/surface_contract_internal.h"
#include "world/surface_coverage.h"
#include "world/surface_cube.h"
#include "world/surface_pose.h"
#include "world/terrain_cube.h"
#include "world/world_cube.h"

#include <stdlib.h>
#include <limits.h>

#define CUBE_CHUNK_TAG UINT64_C(0x4355424547524944)

typedef struct CubeContract {
    TerrainCubeContext *context;
} CubeContract;

static bool CubeCellIsKnown(const SurfaceContract *contract,
                            SurfaceAddress address);

static WorldDimension CubeDimension(const CubeContract *cube)
{
    return TerrainCubeContextSnapshot(cube->context)->context.kind ==
                   WORLD_SURFACE_HOME
        ? WORLD_DIMENSION_HOME : WORLD_DIMENSION_PLANET;
}

static bool CubeAddressMatches(const SurfaceContract *contract,
                               SurfaceAddress address)
{
    if (!contract || !contract->implementation ||
        !SurfaceContractAddressIsValid(address) ||
        address.bodyId != contract->bodyId) return false;
    const CubeContract *cube = contract->implementation;
    const WorldSurfaceSnapshot *snapshot =
        TerrainCubeContextSnapshot(cube->context);
    return snapshot && snapshot->context.active &&
           address.radial >= TERRAIN_CUBE_MIN_RADIAL &&
           address.radial < SURFACE_MAX_Y_EXCLUSIVE;
}

static SurfaceChunkHandle CubeChunkHandleForKey(uint32_t bodyId,
                                                SurfaceCubeChunkKey key)
{
    return (SurfaceChunkHandle){ .opaque = {
        CUBE_CHUNK_TAG, bodyId, (uint64_t)key.face,
        (uint64_t)key.u, (uint64_t)key.v
    } };
}

static bool CubeChunkFromAddress(const SurfaceContract *contract,
                                 SurfaceAddress address,
                                 SurfaceChunkHandle *outChunk)
{
    if (!outChunk || !CubeAddressMatches(contract, address)) return false;
    SurfaceCubeChunkKey key;
    if (!SurfaceCubeChunkAt(address, &key)) return false;
    *outChunk = CubeChunkHandleForKey(contract->bodyId, key);
    return true;
}

static bool CubeDecodeChunk(const SurfaceContract *contract,
                            SurfaceChunkHandle chunk, SurfaceCubeChunkKey *outKey)
{
    if (!contract || !contract->implementation || !outKey ||
        chunk.opaque[0] != CUBE_CHUNK_TAG ||
        chunk.opaque[1] != contract->bodyId ||
        chunk.opaque[2] >= SURFACE_FACE_COUNT) return false;
    SurfaceCubeChunkKey key = {
        contract->bodyId, (SurfaceFace)chunk.opaque[2],
        (int)chunk.opaque[3], (int)chunk.opaque[4]
    };
    if (!SurfaceCubeChunkKeyIsValid(key)) return false;
    *outKey = key;
    return true;
}

static bool CubeChunkAnchorAddress(const SurfaceContract *contract,
                                   SurfaceChunkHandle chunk,
                                   SurfaceAddress *outAddress)
{
    SurfaceCubeChunkKey key;
    if (!outAddress || !CubeDecodeChunk(contract, chunk, &key)) return false;
    *outAddress = (SurfaceAddress){key.bodyId, key.face,
        key.u * CHUNK_SIZE, key.v * CHUNK_SIZE, 0};
    return true;
}

static bool CubeChunkIsValid(const SurfaceContract *contract,
                             SurfaceChunkHandle chunk)
{
    SurfaceCubeChunkKey key;
    return CubeDecodeChunk(contract, chunk, &key);
}

static bool CubeChunkEqual(const SurfaceContract *contract,
                           SurfaceChunkHandle a, SurfaceChunkHandle b)
{
    SurfaceCubeChunkKey ak, bk;
    return CubeDecodeChunk(contract, a, &ak) &&
           CubeDecodeChunk(contract, b, &bk) &&
           SurfaceCubeChunkKeyEqual(ak, bk);
}

static bool CubeChunkNeighbor(const SurfaceContract *contract,
                              SurfaceChunkHandle chunk,
                              SurfaceDirection direction,
                              SurfaceChunkHandle *outChunk,
                              SurfaceDirection *outReturnDirection)
{
    SurfaceCubeChunkKey key, next;
    if (!outChunk || direction > SURFACE_DIRECTION_V_PLUS ||
        !CubeDecodeChunk(contract, chunk, &key)) return false;
    SurfaceCubeEdge returnEdge;
    if (!SurfaceCubeChunkNeighbor(key, (SurfaceCubeEdge)direction, &next,
                                  &returnEdge)) return false;
    *outChunk = (SurfaceChunkHandle){ .opaque = {
        CUBE_CHUNK_TAG, contract->bodyId, (uint64_t)next.face,
        (uint64_t)next.u, (uint64_t)next.v
    } };
    if (outReturnDirection) *outReturnDirection =
        (SurfaceDirection)returnEdge;
    return true;
}

static bool CubeSectionFromAddress(const SurfaceContract *contract,
                                   SurfaceAddress address,
                                   SurfaceSectionHandle *outSection)
{
    if (!outSection ||
        !CubeChunkFromAddress(contract, address, &outSection->chunk)) {
        return false;
    }
    outSection->sectionY = FloorDivInt(address.radial,
                                       SURFACE_SECTION_HEIGHT);
    return outSection->sectionY >=
               TERRAIN_CUBE_MIN_RADIAL / SURFACE_SECTION_HEIGHT &&
           outSection->sectionY <
               SURFACE_MAX_Y_EXCLUSIVE / SURFACE_SECTION_HEIGHT;
}

static bool CubeSectionIsValid(const SurfaceContract *contract,
                               SurfaceSectionHandle section)
{
    return CubeChunkIsValid(contract, section.chunk) &&
           section.sectionY >=
               TERRAIN_CUBE_MIN_RADIAL / SURFACE_SECTION_HEIGHT &&
           section.sectionY <
               SURFACE_MAX_Y_EXCLUSIVE / SURFACE_SECTION_HEIGHT;
}

static bool CubeLocateCell(const SurfaceContract *contract,
                           SurfaceAddress address, int x, int y, int z,
                           SurfaceCellRuntime *outRuntime)
{
    (void)x;
    (void)z;
    SurfaceCubeChunkKey key;
    if (!outRuntime || !CubeAddressMatches(contract, address) ||
        !SurfaceCubeChunkAt(address, &key)) return false;
    Chunk *chunk = FindCubeChunk(key);
    if (!chunk || !chunk->loaded) return false;
    int sectionY = SurfaceSectionYFromBlockY(y);
    ChunkSection *section = ChunkGetSection(chunk, sectionY, false);
    if (!section && !CubeCellIsKnown(contract, address)) return false;
    int localX = PositiveMod(address.u, CHUNK_SIZE);
    int localY = SurfaceSectionLocalYFromBlockY(y);
    int localZ = PositiveMod(address.v, CHUNK_SIZE);
    *outRuntime = (SurfaceCellRuntime){
        .chunk = chunk,
        .section = section,
        .sectionY = sectionY,
        .localX = localX,
        .localY = localY,
        .localZ = localZ,
        .block = section
            ? (BlockType)section->blocks[localX][localY][localZ] : BLOCK_AIR
    };
    return true;
}

static bool CubeStepCell(const SurfaceContract *contract,
                         SurfaceAddress address, int x, int y, int z,
                         SurfaceDirection direction,
                         SurfaceAddress *outAddress, int *outX, int *outY,
                         int *outZ, SurfaceDirection *outReturnDirection)
{
    (void)x;
    (void)z;
    if (!outAddress || !CubeAddressMatches(contract, address)) return false;
    SurfaceAddress next = address;
    if (direction <= SURFACE_DIRECTION_V_PLUS) {
        SurfaceCubeEdge back;
        if (!SurfaceCubeCellNeighbor(address, (SurfaceCubeEdge)direction,
                                     &next, &back)) return false;
        if (outReturnDirection) *outReturnDirection =
            (SurfaceDirection)back;
    } else if (direction == SURFACE_DIRECTION_RADIAL_MINUS) {
        if (y == INT_MIN) return false;
        next.radial = --y;
        if (outReturnDirection) *outReturnDirection =
            SURFACE_DIRECTION_RADIAL_PLUS;
    } else if (direction == SURFACE_DIRECTION_RADIAL_PLUS) {
        if (y == INT_MAX) return false;
        next.radial = ++y;
        if (outReturnDirection) *outReturnDirection =
            SURFACE_DIRECTION_RADIAL_MINUS;
    } else {
        return false;
    }
    if (!CubeAddressMatches(contract, next)) return false;
    SurfaceMapCell canonical;
    if (!SurfaceAddressCanonicalMapCell(next, &canonical)) return false;
    const CubeContract *cube = contract->implementation;
    const WorldSurfaceSnapshot *snapshot =
        TerrainCubeContextSnapshot(cube->context);
    if (!snapshot) return false;
    *outAddress = next;
    if (outX) *outX = canonical.x - snapshot->context.originX;
    if (outY) *outY = y;
    if (outZ) *outZ = canonical.z - snapshot->context.originZ;
    return true;
}

static bool CubeChunkCellAddress(const SurfaceContract *contract,
                                 const Chunk *chunk, int localX, int y,
                                 int localZ, SurfaceAddress *outAddress)
{
    if (!chunk || !chunk->cubeGrid || !outAddress || !InHeight(y) ||
        localX < 0 || localX >= CHUNK_SIZE ||
        localZ < 0 || localZ >= CHUNK_SIZE) return false;
    *outAddress = (SurfaceAddress){
        chunk->cubeKey.bodyId, chunk->cubeKey.face,
        chunk->cubeKey.u * CHUNK_SIZE + localX,
        chunk->cubeKey.v * CHUNK_SIZE + localZ, y
    };
    return CubeAddressMatches(contract, *outAddress);
}

static bool CubeChunkRuntime(const SurfaceContract *contract,
                             SurfaceChunkHandle handle, Chunk **outChunk)
{
    SurfaceCubeChunkKey key;
    if (!outChunk || !CubeDecodeChunk(contract, handle, &key)) return false;
    *outChunk = FindCubeChunk(key);
    return *outChunk && (*outChunk)->loaded;
}

static WorldBlockAddress CubeBlockAddress(const CubeContract *cube,
                                          SurfaceAddress address)
{
    return (WorldBlockAddress){
        .dimension = CubeDimension(cube),
        .at.surface = address
    };
}

static bool CubeCellKnownFromChunk(const CubeContract *cube,
                                   SurfaceAddress address)
{
    SurfaceCubeChunkKey key;
    if (!SurfaceCubeChunkAt(address, &key)) return false;
    Chunk *chunk = FindCubeChunk(key);
    if (!chunk || !chunk->loaded ||
        chunk->cubeWorldGeneration !=
            TerrainCubeContextGeneration(cube->context)) return false;
    int sectionY = SurfaceSectionYFromBlockY(address.radial);
    return ChunkGetSectionConst(chunk, sectionY) ||
           ChunkTerrainSectionIsResolved(chunk, sectionY);
}

static bool CubeCellIsKnown(const SurfaceContract *contract,
                            SurfaceAddress address)
{
    if (!CubeAddressMatches(contract, address)) return false;
    const CubeContract *cube = contract->implementation;
    BlockType block;
    return WorldCubeBlockAt(CubeBlockAddress(cube, address), &block) ||
           CubeCellKnownFromChunk(cube, address);
}

static bool CubeCellRead(const SurfaceContract *contract,
                         SurfaceAddress address, WorldCell *outCell)
{
    if (!outCell || !CubeAddressMatches(contract, address)) return false;
    const CubeContract *cube = contract->implementation;
    WorldBlockAddress blockAddress = CubeBlockAddress(cube, address);
    WorldCell cell;
    bool edited = WorldCubeGetEditCell(blockAddress, &cell);
    if (!edited) {
        if (WorldCubeCellAt(blockAddress, &cell)) {
            if (!WorldCellIsValid(cell)) return false;
            *outCell = cell;
            return true;
        }
        if (!CubeCellKnownFromChunk(cube, address)) return false;
        *outCell = (WorldCell){ BLOCK_AIR, 0u };
        return true;
    }

    /* Fluid volume can change without replacing the block edit. The resident
     * chunk owns that mutation, while the edit store owns the block identity. */
    SurfaceCubeChunkKey key;
    Chunk *chunk = SurfaceCubeChunkAt(address, &key)
        ? FindCubeChunk(key) : NULL;
    if (chunk && chunk->loaded &&
        chunk->cubeWorldGeneration ==
            TerrainCubeContextGeneration(cube->context)) {
        WorldCell resident;
        if (ChunkTryGetLocalCell(chunk, address.u % CHUNK_SIZE,
                                 address.radial,
                                 address.v % CHUNK_SIZE, &resident)) {
            cell.waterVolume = resident.waterVolume;
        }
    }
    if (!WorldCellIsValid(cell)) return false;
    *outCell = cell;
    return true;
}

static bool CubeCellWrite(const SurfaceContract *contract,
                          SurfaceAddress address, WorldCell cell)
{
    if (!CubeAddressMatches(contract, address)) return false;
    if (cell.block == BLOCK_WATER) cell = WorldCellFromLegacyBlock(BLOCK_WATER);
    if (!WorldCellIsValid(cell)) return false;
    const CubeContract *cube = contract->implementation;
    return WorldCubeSetCell(CubeBlockAddress(cube, address), cell);
}

static bool CubeNeighbor(const SurfaceContract *contract,
                         SurfaceAddress address, SurfaceDirection direction,
                         SurfaceAddress *outAddress,
                         SurfaceDirection *outReturnDirection)
{
    if (!outAddress || !CubeAddressMatches(contract, address)) return false;
    SurfaceAddress next = address;
    if (direction <= SURFACE_DIRECTION_V_PLUS) {
        SurfaceCubeEdge back;
        if (!SurfaceCubeCellNeighbor(address, (SurfaceCubeEdge)direction,
                                     &next, &back)) return false;
        if (outReturnDirection) *outReturnDirection = (SurfaceDirection)back;
    } else if (direction == SURFACE_DIRECTION_RADIAL_MINUS) {
        if (address.radial == INT_MIN) return false;
        next.radial--;
        if (outReturnDirection) *outReturnDirection =
            SURFACE_DIRECTION_RADIAL_PLUS;
    } else if (direction == SURFACE_DIRECTION_RADIAL_PLUS) {
        if (address.radial == INT_MAX) return false;
        next.radial++;
        if (outReturnDirection) *outReturnDirection =
            SURFACE_DIRECTION_RADIAL_MINUS;
    } else {
        return false;
    }
    if (!CubeAddressMatches(contract, next)) return false;
    *outAddress = next;
    return true;
}

static void CubeMarkAddressDirty(const SurfaceContract *contract,
                                 SurfaceAddress address)
{
    if (!CubeAddressMatches(contract, address)) return;
    SurfaceCubeChunkKey key;
    if (!SurfaceCubeChunkAt(address, &key)) return;
    Chunk *chunk = FindCubeChunk(key);
    if (chunk) MarkGeneratedSectionAndNeighborsDirty(
        chunk, SurfaceSectionYFromBlockY(address.radial));
}

static bool CubeSampleAt(const SurfaceContract *contract,
                         SurfaceAddress address,
                         SurfaceContractSample *outSample)
{
    if (!outSample || !CubeAddressMatches(contract, address)) return false;
    const CubeContract *cube = contract->implementation;
    TerrainCubeColumn column;
    if (!TerrainCubeSampleColumn(cube->context, address, &column)) return false;
    const WorldSurfaceSnapshot *snapshot =
        TerrainCubeContextSnapshot(cube->context);
    bool planet = snapshot &&
        snapshot->context.kind == WORLD_SURFACE_PLANET;
    return SurfaceContractSampleFromTerrain(
        &column.terrain, planet,
        planet ? column.planet.biome : PLANET_BIOME_PLAINS, outSample);
}

static SurfaceRegionHandle CubeRegionForAddress(
    const SurfaceContract *contract, SurfaceAddress address)
{
    if (!CubeAddressMatches(contract, address)) return (SurfaceRegionHandle){ 0 };
    return (SurfaceRegionHandle){ .opaque = {
        ((uint64_t)address.bodyId << 32) | UINT32_C(0x43554245),
        (uint64_t)address.face
    } };
}

static bool CubeSameRegion(const SurfaceContract *contract,
                           SurfaceAddress a, SurfaceAddress b)
{
    return CubeAddressMatches(contract, a) &&
           CubeAddressMatches(contract, b) &&
           a.bodyId == b.bodyId && a.face == b.face;
}

static bool CubeCoverageCollect(const SurfaceContract *contract,
                                SurfaceAddress focus, double radius,
                                SurfaceCoverageCandidate *out, unsigned capacity,
                                SurfaceCoverageResult *outResult)
{
    if (!CubeAddressMatches(contract, focus)) return false;
    SurfaceVector point;
    if (!SurfaceCellCenter(focus, &point)) return false;
    SurfaceCubeCoverageCandidate cube[625];
    if (capacity > sizeof(cube) / sizeof(cube[0])) return false;
    SurfaceCubeCoverageResult result;
    if (!SurfaceCoverageCollect(contract->bodyId, point, radius, cube,
                                capacity, &result)) return false;
    for (unsigned i = 0; i < result.admitted; i++) {
        out[i] = (SurfaceCoverageCandidate){
            .chunk = CubeChunkHandleForKey(contract->bodyId, cube[i].key),
            .centerDistance = cube[i].centerDistance,
            .minimumDistance = cube[i].minimumDistance
        };
    }
    *outResult = (SurfaceCoverageResult){
        result.requested, result.admitted, result.saturated
    };
    return true;
}

static bool CubeCoverageStatusForContract(const SurfaceContract *contract,
                               SurfaceCoverageStatus *outStatus)
{
    const CubeContract *cube = contract ? contract->implementation : NULL;
    CubeCoverageStatus status;
    if (!outStatus || !cube ||
        !ChunksLastCubeCoverage(&status)) return false;
    *outStatus = (SurfaceCoverageStatus){
        .requested = status.requested,
        .admitted = status.admitted,
        .ready = status.ready,
        .pending = status.pending,
        .missing = status.missing,
        .submitted = status.submitted,
        .released = status.released,
        .duplicates = status.duplicates,
        .stale = status.stale,
        .saturated = status.saturated
    };
    return true;
}

static bool CubeCoverageUpdateForContract(const SurfaceContract *contract,
                               SurfaceAddress focus, double radius,
                               unsigned submissionBudget,
                               SurfaceCoverageStatus *outStatus)
{
    const CubeContract *cube = contract ? contract->implementation : NULL;
    if (!outStatus || !cube || !CubeAddressMatches(contract, focus)) return false;
    SurfaceVector point;
    if (!SurfaceCellCenter(focus, &point)) return false;
    CubeCoverageStatus status;
    if (!ChunksUpdateCubeCoverage(cube->context, point, radius,
                                  submissionBudget, &status)) return false;
    *outStatus = (SurfaceCoverageStatus){
        .requested = status.requested,
        .admitted = status.admitted,
        .ready = status.ready,
        .pending = status.pending,
        .missing = status.missing,
        .submitted = status.submitted,
        .released = status.released,
        .duplicates = status.duplicates,
        .stale = status.stale,
        .saturated = status.saturated
    };
    return true;
}

static bool CubeCoverageObservationForContract(
    const SurfaceContract *contract, SurfaceCoverageObservation *outObservation)
{
    CubeCoverageObservation observation;
    if (!contract || !contract->implementation || !outObservation ||
        !ChunksCubeCoverageObservation(&observation) ||
        observation.bodyId != contract->bodyId) return false;
    *outObservation = (SurfaceCoverageObservation){
        .sequence = observation.sequence,
        .bodyId = observation.bodyId,
        .requested = observation.requested,
        .admitted = observation.admitted,
        .saturated = observation.saturated
    };
    return true;
}

static bool CubeCoverageAdmissionAtForContract(
    const SurfaceContract *contract, unsigned index,
    SurfaceCoverageAdmission *outAdmission)
{
    SurfaceCubeCoverageCandidate candidate;
    CubeCoverageObservation observation;
    if (!contract || !contract->implementation || !outAdmission ||
        !ChunksCubeCoverageObservation(&observation) ||
        observation.bodyId != contract->bodyId ||
        !ChunksCubeCoverageAdmissionAt(index, &candidate)) return false;
    *outAdmission = (SurfaceCoverageAdmission){
        .sequence = observation.sequence,
        .candidate = {
            .chunk = CubeChunkHandleForKey(contract->bodyId, candidate.key),
            .centerDistance = candidate.centerDistance,
            .minimumDistance = candidate.minimumDistance
        }
    };
    return true;
}

static bool CubeCoverageAdmissionContainsForContract(
    const SurfaceContract *contract, SurfaceChunkHandle chunk)
{
    SurfaceCubeChunkKey key;
    return contract && contract->implementation &&
        CubeDecodeChunk(contract, chunk, &key) &&
        ChunksCubeCoverageContains(key);
}

static void CubeRelease(SurfaceContract *contract)
{
    if (!contract) return;
    CubeContract *cube = contract->implementation;
    if (cube) TerrainCubeContextRelease(cube->context);
    free(cube);
    free(contract);
}

static const SurfaceContractOps cubeOps = {
    .address_is_valid = SurfaceContractAddressIsValid,
    .address_equal = SurfaceContractAddressEqual,
    .address_hash = SurfaceContractAddressHash,
    .chunk_from_address = CubeChunkFromAddress,
    .chunk_anchor_address = CubeChunkAnchorAddress,
    .chunk_is_valid = CubeChunkIsValid,
    .chunk_equal = CubeChunkEqual,
    .chunk_neighbor = CubeChunkNeighbor,
    .section_from_address = CubeSectionFromAddress,
    .section_is_valid = CubeSectionIsValid,
    .locate_cell = CubeLocateCell,
    .step_cell = CubeStepCell,
    .chunk_cell_address = CubeChunkCellAddress,
    .chunk_runtime = CubeChunkRuntime,
    .cell_is_known = CubeCellIsKnown,
    .cell_read = CubeCellRead,
    .cell_write = CubeCellWrite,
    .sample_at = CubeSampleAt,
    .neighbor = CubeNeighbor,
    .mark_address_dirty = CubeMarkAddressDirty,
    .same_region = CubeSameRegion,
    .region_for_address = CubeRegionForAddress,
    .coverage_collect = CubeCoverageCollect,
    .coverage_status = CubeCoverageStatusForContract,
    .coverage_update = CubeCoverageUpdateForContract,
    .coverage_observation = CubeCoverageObservationForContract,
    .coverage_admission_at = CubeCoverageAdmissionAtForContract,
    .coverage_admission_contains = CubeCoverageAdmissionContainsForContract,
    .supports_physical_raycast = true,
    .supports_physical_pose = true,
    .release = CubeRelease
};

SurfaceContract *SurfaceCubeContractCreate(
    const WorldSurfaceSnapshot *snapshot, TerrainMode mode, uint64_t generation)
{
    if (!snapshot || !snapshot->context.active ||
        snapshot->context.topology.kind != SURFACE_TOPOLOGY_CUBE_GRID) {
        return NULL;
    }
    TerrainCubeContext *context = TerrainCubeContextCreate(
        snapshot, mode, generation);
    if (!context) return NULL;
    SurfaceContract *contract = calloc(1, sizeof(*contract));
    CubeContract *cube = calloc(1, sizeof(*cube));
    if (!contract || !cube) {
        free(contract);
        free(cube);
        TerrainCubeContextRelease(context);
        return NULL;
    }
    cube->context = context;
    contract->ops = &cubeOps;
    contract->implementation = cube;
    contract->topology = snapshot->context.topology;
    contract->bodyId = snapshot->context.bodyId;
    return contract;
}
