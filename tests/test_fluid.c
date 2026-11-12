#include "world/fluid.h"
#include "world/fluid_test.h"

#include "world/chunks.h"
#include "world/block_edits.h"
#include "world/surface_contract_internal.h"
#include "world/world_environment.h"
#include "world/world_surface_snapshot.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Chunk chunks[MAX_ACTIVE_CHUNKS];

static bool proceduralOceanEnabled = false;
static bool lavaOverlay = false;
static bool cubeTopology = false;
static WorldSurfaceKind surfaceKind = WORLD_SURFACE_HOME;

static bool TestContractChunkFromAddress(const SurfaceContract *contract,
                                         SurfaceAddress address,
                                         SurfaceChunkHandle *outChunk)
{
    if (!contract || !outChunk || address.bodyId != contract->bodyId ||
        !SurfaceContractAddressIsValid(address)) return false;
    if (cubeTopology) {
        *outChunk = (SurfaceChunkHandle){ .opaque = {
            UINT64_C(0x4355424554455354), address.bodyId,
            (uint64_t)address.face, (uint64_t)FloorDivInt(address.u, CHUNK_SIZE),
            (uint64_t)FloorDivInt(address.v, CHUNK_SIZE)
        } };
        return true;
    }
    SurfaceMapCell canonical;
    if (!SurfaceAddressCanonicalMapCell(address, &canonical)) return false;
    *outChunk = (SurfaceChunkHandle){ .opaque = {
        UINT64_C(0x4c45474143595453), address.bodyId,
        (uint64_t)(int64_t)FloorDivInt(canonical.x, CHUNK_SIZE),
        (uint64_t)(int64_t)FloorDivInt(canonical.z, CHUNK_SIZE), 0u
    } };
    return true;
}

static bool TestContractChunkIsValid(const SurfaceContract *contract,
                                     SurfaceChunkHandle chunk)
{
    return contract && chunk.opaque[0] != 0u &&
           chunk.opaque[1] == contract->bodyId;
}

static bool TestContractChunkEqual(const SurfaceContract *contract,
                                   SurfaceChunkHandle a, SurfaceChunkHandle b)
{
    return TestContractChunkIsValid(contract, a) &&
           TestContractChunkIsValid(contract, b) &&
           memcmp(a.opaque, b.opaque, sizeof(a.opaque)) == 0;
}

static bool TestContractLocateCell(const SurfaceContract *contract,
                                   SurfaceAddress address, int x, int y, int z,
                                   SurfaceCellRuntime *outRuntime)
{
    if (!contract || !outRuntime || address.bodyId != contract->bodyId ||
        !InHeight(y)) return false;
    if (contract->topology.kind == SURFACE_TOPOLOGY_CUBE_GRID) {
        SurfaceCubeChunkKey key;
        if (!SurfaceCubeChunkAt(address, &key)) return false;
        for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
            Chunk *chunk = &chunks[index];
            if (!chunk->loaded || !chunk->cubeGrid ||
                !SurfaceCubeChunkKeyEqual(chunk->cubeKey, key)) continue;
            int sectionY = SurfaceSectionYFromBlockY(y);
            ChunkSection *section = ChunkGetSection(chunk, sectionY, false);
            if (!section && !ChunkTerrainSectionIsResolved(chunk, sectionY)) {
                return false;
            }
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
                    ? (BlockType)section->blocks[localX][localY][localZ]
                    : BLOCK_AIR
            };
            return true;
        }
        return false;
    }
    int cx = 0;
    int cz = 0;
    int localX = 0;
    int localZ = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &localX, &localZ);
    Chunk *chunk = FindChunk(cx, cz);
    if (!chunk || !chunk->loaded) return false;
    int sectionY = SurfaceSectionYFromBlockY(y);
    ChunkSection *section = ChunkGetSection(chunk, sectionY, false);
    if (!section && surfaceKind == WORLD_SURFACE_HOME &&
        !ChunkTerrainSectionIsResolved(chunk, sectionY)) return false;
    int localY = SurfaceSectionLocalYFromBlockY(y);
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

static bool TestContractStepCell(const SurfaceContract *contract,
                                 SurfaceAddress address, int x, int y, int z,
                                 SurfaceDirection direction,
                                 SurfaceAddress *outAddress, int *outX,
                                 int *outY, int *outZ,
                                 SurfaceDirection *outReturnDirection)
{
    if (!contract || !outAddress || address.bodyId != contract->bodyId) {
        return false;
    }
    SurfaceAddress next = address;
    if (contract->topology.kind == SURFACE_TOPOLOGY_CUBE_GRID) {
        if (direction <= SURFACE_DIRECTION_V_PLUS) {
            SurfaceCubeEdge back;
            if (!SurfaceCubeCellNeighbor(address, (SurfaceCubeEdge)direction,
                                         &next, &back)) return false;
            if (outReturnDirection) *outReturnDirection =
                (SurfaceDirection)back;
        } else if (direction == SURFACE_DIRECTION_RADIAL_MINUS) {
            next.radial--;
            if (outReturnDirection) *outReturnDirection =
                SURFACE_DIRECTION_RADIAL_PLUS;
        } else if (direction == SURFACE_DIRECTION_RADIAL_PLUS) {
            next.radial++;
            if (outReturnDirection) *outReturnDirection =
                SURFACE_DIRECTION_RADIAL_MINUS;
        } else {
            return false;
        }
        SurfaceMapCell canonical;
        if (!SurfaceAddressCanonicalMapCell(next, &canonical)) return false;
        x = canonical.x;
        z = canonical.z;
    } else {
        int deltaX = direction == SURFACE_DIRECTION_U_PLUS ? 1 :
                     direction == SURFACE_DIRECTION_U_MINUS ? -1 : 0;
        int deltaZ = direction == SURFACE_DIRECTION_V_PLUS ? 1 :
                     direction == SURFACE_DIRECTION_V_MINUS ? -1 : 0;
        if (direction <= SURFACE_DIRECTION_V_PLUS) {
            SurfaceMapCell canonical = SurfaceCanonicalMapCell(
                (float)(x + deltaX), (float)(z + deltaZ));
            x = canonical.x;
            z = canonical.z;
            next = SurfaceAddressFromMapCoordinates(
                contract->bodyId, (float)x, (float)z, y);
        } else if (direction == SURFACE_DIRECTION_RADIAL_MINUS) {
            y--;
            next.radial = y;
        } else if (direction == SURFACE_DIRECTION_RADIAL_PLUS) {
            y++;
            next.radial = y;
        } else {
            return false;
        }
        if (outReturnDirection) *outReturnDirection =
            (SurfaceDirection)((int)direction ^ 1);
    }
    *outAddress = next;
    if (outX) *outX = x;
    if (outY) *outY = y;
    if (outZ) *outZ = z;
    return true;
}

static bool TestContractChunkCellAddress(const SurfaceContract *contract,
                                         const Chunk *chunk, int localX, int y,
                                         int localZ,
                                         SurfaceAddress *outAddress)
{
    if (!contract || !chunk || !outAddress || !InHeight(y)) return false;
    if (chunk->cubeGrid) {
        *outAddress = (SurfaceAddress){
            chunk->cubeKey.bodyId, chunk->cubeKey.face,
            chunk->cubeKey.u * CHUNK_SIZE + localX,
            chunk->cubeKey.v * CHUNK_SIZE + localZ, y
        };
    } else {
        *outAddress = SurfaceAddressFromMapCoordinates(
            contract->bodyId,
            (float)(chunk->cx * CHUNK_SIZE + localX),
            (float)(chunk->cz * CHUNK_SIZE + localZ), y);
    }
    return true;
}

static bool TestContractChunkRuntime(const SurfaceContract *contract,
                                     SurfaceChunkHandle handle,
                                     Chunk **outChunk)
{
    if (!contract || !outChunk) return false;
    for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
        Chunk *chunk = &chunks[index];
        SurfaceChunkHandle candidate;
        if (!chunk->loaded ||
            !TestContractChunkFromAddress(
                contract, chunk->surfaceAddress, &candidate)) continue;
        if (TestContractChunkEqual(contract, candidate, handle)) {
            *outChunk = chunk;
            return true;
        }
    }
    return false;
}

static void TestContractMarkDirty(const SurfaceContract *contract,
                                  SurfaceAddress address)
{
    (void)contract;
    (void)address;
}

static const SurfaceContractOps testSurfaceContractOps = {
    .chunk_from_address = TestContractChunkFromAddress,
    .chunk_is_valid = TestContractChunkIsValid,
    .chunk_equal = TestContractChunkEqual,
    .locate_cell = TestContractLocateCell,
    .step_cell = TestContractStepCell,
    .chunk_cell_address = TestContractChunkCellAddress,
    .chunk_runtime = TestContractChunkRuntime,
    .mark_address_dirty = TestContractMarkDirty
};

static SurfaceContract testSurfaceContract = {
    .ops = &testSurfaceContractOps,
    .implementation = &testSurfaceContract,
    .bodyId = 0u
};

const SurfaceContract *WorldCurrentSurfaceContract(void)
{
    testSurfaceContract.bodyId = WorldCurrentSurfaceId();
    testSurfaceContract.topology = cubeTopology
        ? (SurfaceTopologyVersion){ SURFACE_TOPOLOGY_CUBE_GRID, 1u }
        : (SurfaceTopologyVersion)SURFACE_TOPOLOGY_MAP_V1;
    return &testSurfaceContract;
}

BlockType GetBlockAt(int x, int y, int z)
{
    if (lavaOverlay && x == 2 && y == 2 && z == 2) return BLOCK_LAVA;
    int cx, cz, lx, lz;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    return ChunkGetLocalBlock(FindChunk(cx, cz), lx, y, lz);
}
static int proceduralOceanMinX = 0;
static int proceduralOceanMaxX = -1;
static int proceduralOceanMinY = 0;
static int proceduralOceanMaxY = -1;
static int proceduralOceanMinZ = 0;
static int proceduralOceanMaxZ = -1;

bool InHeight(int y)
{
    return y >= SURFACE_MIN_Y && y < SURFACE_MAX_Y_EXCLUSIVE;
}

int FloorDivInt(int value, int divisor)
{
    int quotient = value / divisor;
    int remainder = value % divisor;
    if (remainder < 0) quotient--;
    return quotient;
}

int PositiveMod(int value, int divisor)
{
    int result = value % divisor;
    return result < 0 ? result + divisor : result;
}

bool SurfaceSectionInBounds(int sectionY)
{
    return sectionY >= SURFACE_SECTION_MIN_Y &&
           sectionY < SURFACE_SECTION_MAX_Y_EXCLUSIVE;
}

int SurfaceSectionYFromBlockY(int y)
{
    return FloorDivInt(y, SURFACE_SECTION_HEIGHT);
}

int SurfaceSectionLocalYFromBlockY(int y)
{
    return PositiveMod(y, SURFACE_SECTION_HEIGHT);
}

void WorldToChunkLocal(int x, int z, int *cx, int *cz, int *lx, int *lz)
{
    SurfaceMapCell canonical = SurfaceCanonicalMapCell((float)x, (float)z);
    x = canonical.x;
    z = canonical.z;
    *cx = FloorDivInt(x, CHUNK_SIZE);
    *cz = FloorDivInt(z, CHUNK_SIZE);
    *lx = PositiveMod(x, CHUNK_SIZE);
    *lz = PositiveMod(z, CHUNK_SIZE);
}

Chunk *FindChunk(int cx, int cz)
{
    for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
        if (chunks[index].loaded && chunks[index].cx == cx &&
            chunks[index].cz == cz) {
            return &chunks[index];
        }
    }
    return NULL;
}

Chunk *FindHorizontalChunkNeighbor(int cx, int cz, int deltaCx, int deltaCz)
{
    int centerX = (cx + deltaCx) * CHUNK_SIZE + CHUNK_SIZE / 2;
    int centerZ = (cz + deltaCz) * CHUNK_SIZE + CHUNK_SIZE / 2;
    SurfaceMapCell canonical = SurfaceCanonicalMapCell(
        (float)centerX, (float)centerZ);
    return FindChunk(FloorDivInt(canonical.x, CHUNK_SIZE),
                     FloorDivInt(canonical.z, CHUNK_SIZE));
}

ChunkSection *ChunkGetSection(Chunk *chunk, int sectionY, bool create)
{
    if (!chunk) return NULL;
    int index = 0;
    while (index < chunk->sectionCount &&
           chunk->sections[index]->sectionY < sectionY) index++;
    if (index < chunk->sectionCount &&
        chunk->sections[index]->sectionY == sectionY) {
        return chunk->sections[index];
    }
    if (!create) return NULL;
    if (chunk->sectionCount == chunk->sectionCapacity) {
        int capacity = chunk->sectionCapacity > 0
            ? chunk->sectionCapacity * 2 : 4;
        ChunkSection **sections = realloc(
            chunk->sections, (size_t)capacity * sizeof(*sections));
        assert(sections);
        chunk->sections = sections;
        chunk->sectionCapacity = capacity;
    }
    memmove(&chunk->sections[index + 1], &chunk->sections[index],
            (size_t)(chunk->sectionCount - index) * sizeof(*chunk->sections));
    ChunkSection *section = calloc(1, sizeof(*section));
    assert(section);
    section->sectionY = sectionY;
    chunk->sections[index] = section;
    chunk->sectionCount++;
    return section;
}

const ChunkSection *ChunkGetSectionConst(const Chunk *chunk, int sectionY)
{
    if (!chunk) return NULL;
    for (int index = 0; index < chunk->sectionCount; index++) {
        if (chunk->sections[index]->sectionY == sectionY) {
            return chunk->sections[index];
        }
        if (chunk->sections[index]->sectionY > sectionY) break;
    }
    return NULL;
}

bool ChunkTerrainSectionIsResolved(const Chunk *chunk, int sectionY)
{
    for (int index = 0; chunk && index < chunk->resolvedTerrainSectionCount;
         index++) {
        if (chunk->resolvedTerrainSectionYs[index] == sectionY) return true;
    }
    return false;
}

static void ResolveSection(Chunk *chunk, int sectionY)
{
    if (ChunkTerrainSectionIsResolved(chunk, sectionY)) return;
    int count = chunk->resolvedTerrainSectionCount;
    int *resolved = realloc(chunk->resolvedTerrainSectionYs,
                            (size_t)(count + 1) * sizeof(*resolved));
    assert(resolved);
    chunk->resolvedTerrainSectionYs = resolved;
    chunk->resolvedTerrainSectionYs[count] = sectionY;
    chunk->resolvedTerrainSectionCount++;
}

static void ForgetSectionBaseline(Chunk *chunk, int sectionY)
{
    assert(!ChunkGetSectionConst(chunk, sectionY));
    for (int index = 0; index < chunk->resolvedTerrainSectionCount; index++) {
        if (chunk->resolvedTerrainSectionYs[index] != sectionY) continue;
        chunk->resolvedTerrainSectionYs[index] =
            chunk->resolvedTerrainSectionYs[--chunk->resolvedTerrainSectionCount];
        return;
    }
}

BlockType ChunkGetLocalBlock(const Chunk *chunk, int lx, int y, int lz)
{
    const ChunkSection *section = ChunkGetSectionConst(
        chunk, SurfaceSectionYFromBlockY(y));
    return section
        ? (BlockType)section->blocks[
              lx][SurfaceSectionLocalYFromBlockY(y)][lz]
        : BLOCK_AIR;
}

static int FixtureWaterCellIndex(int lx, int ly, int lz)
{
    return (lx * SURFACE_SECTION_HEIGHT + ly) * CHUNK_SIZE + lz;
}

static uint8_t FixtureWaterVolume(const Chunk *chunk, int lx, int y, int lz)
{
    const ChunkSection *section = ChunkGetSectionConst(
        chunk, SurfaceSectionYFromBlockY(y));
    if (!section) return 0u;
    int ly = SurfaceSectionLocalYFromBlockY(y);
    BlockType block = (BlockType)section->blocks[lx][ly][lz];
    if (block == BLOCK_WATER) return (uint8_t)FLUID_CAPACITY;
    return section->waterVolumes
        ? section->waterVolumes[FixtureWaterCellIndex(lx, ly, lz)] : 0u;
}

static bool FixtureEnsureWaterVolumes(ChunkSection *section)
{
    if (!section) return false;
    if (section->waterVolumes) return true;
    section->waterVolumes = malloc(
        CHUNK_SIZE * SURFACE_SECTION_HEIGHT * CHUNK_SIZE);
    if (!section->waterVolumes) return false;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int ly = 0; ly < SURFACE_SECTION_HEIGHT; ly++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                section->waterVolumes[FixtureWaterCellIndex(lx, ly, lz)] =
                    section->blocks[lx][ly][lz] == BLOCK_WATER
                        ? (unsigned char)FLUID_CAPACITY : 0u;
            }
        }
    }
    return true;
}

bool ChunkSetLocalBlock(Chunk *chunk, int lx, int y, int lz, BlockType type)
{
    if (!chunk) return false;
    int sectionY = SurfaceSectionYFromBlockY(y);
    ChunkSection *current = ChunkGetSection(chunk, sectionY, false);
    int ly = SurfaceSectionLocalYFromBlockY(y);
    BlockType previousBlock = current
        ? (BlockType)current->blocks[lx][ly][lz] : BLOCK_AIR;
    uint8_t previousVolume = previousBlock == BLOCK_WATER
        ? (uint8_t)FLUID_CAPACITY
        : (current && current->waterVolumes
               ? current->waterVolumes[FixtureWaterCellIndex(lx, ly, lz)]
               : 0u);
    if (previousBlock == BLOCK_WATER) previousBlock = BLOCK_AIR;
    WorldCell cell = WorldCellWithBlock(
        (WorldCell){ .block = previousBlock, .waterVolume = previousVolume },
        type);
    ChunkSection *section = ChunkGetSection(
        chunk, sectionY, cell.block != BLOCK_AIR || cell.waterVolume > 0u);
    if (!section) return cell.block == BLOCK_AIR && cell.waterVolume == 0u;
    if (cell.waterVolume > 0u && !FixtureEnsureWaterVolumes(section)) {
        return false;
    }
    section->blocks[lx][ly][lz] = (unsigned short)cell.block;
    if (section->waterVolumes) {
        section->waterVolumes[FixtureWaterCellIndex(lx, ly, lz)] =
            cell.waterVolume;
    }
    return true;
}

void MarkChunkDirtyAtBlock(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
}

bool WorldIsSurfaceActive(void)
{
    return true;
}

uint32_t WorldCurrentSurfaceId(void)
{
    return 0u;
}

WorldSurfaceContext WorldCurrentSurfaceContext(void)
{
    return (WorldSurfaceContext){
        .active = true,
        .kind = surfaceKind,
        .topology = cubeTopology
            ? (SurfaceTopologyVersion){ SURFACE_TOPOLOGY_CUBE_GRID, 1u }
            : (SurfaceTopologyVersion)SURFACE_TOPOLOGY_MAP_V1
    };
}

int WorldSurfaceMapOriginX(void)
{
    return 0;
}

int WorldSurfaceMapOriginZ(void)
{
    return 0;
}

bool WorldIsProceduralOceanWaterAt(int x, int y, int z)
{
    return proceduralOceanEnabled &&
           x >= proceduralOceanMinX && x <= proceduralOceanMaxX &&
           y >= proceduralOceanMinY && y <= proceduralOceanMaxY &&
           z >= proceduralOceanMinZ && z <= proceduralOceanMaxZ;
}

static void SetProceduralOceanBounds(int minX, int maxX, int minY, int maxY,
                                     int minZ, int maxZ)
{
    proceduralOceanEnabled = true;
    proceduralOceanMinX = minX;
    proceduralOceanMaxX = maxX;
    proceduralOceanMinY = minY;
    proceduralOceanMaxY = maxY;
    proceduralOceanMinZ = minZ;
    proceduralOceanMaxZ = maxZ;
}

static void ClearChunks(void)
{
    for (int chunkIndex = 0; chunkIndex < MAX_ACTIVE_CHUNKS; chunkIndex++) {
        for (int sectionIndex = 0;
             sectionIndex < chunks[chunkIndex].sectionCount; sectionIndex++) {
            ChunkSection *section = chunks[chunkIndex].sections[sectionIndex];
            free(section->waterVolumes);
            free(section->fluidQueuedBits);
            free(section->fluidDeferredBits);
            free(section->fluidFlow);
            free(section);
        }
        free(chunks[chunkIndex].sections);
        free(chunks[chunkIndex].resolvedTerrainSectionYs);
    }
    memset(chunks, 0, sizeof(chunks));
}

static Chunk *CreateChunk(int slot, int cx, int cz)
{
    Chunk *chunk = &chunks[slot];
    *chunk = (Chunk){
        .loaded = true,
        .cx = cx,
        .cz = cz,
        .surfaceAddress = SurfaceAddressFromMapCoordinates(
            WorldCurrentSurfaceId(),
            ((float)cx + 0.5f) * (float)CHUNK_SIZE,
            ((float)cz + 0.5f) * (float)CHUNK_SIZE, 0)
    };
    /* Existing scenes use a generated, empty column unless marked pending. */
    for (int sectionY = 0; sectionY < SURFACE_GENERATION_SECTION_COUNT;
         sectionY++) {
        ResolveSection(chunk, sectionY);
    }
    return chunk;
}

static void CreateAllSections(Chunk *chunk)
{
    for (int sectionY = 0;
         sectionY < SURFACE_GENERATION_SECTION_COUNT; sectionY++) {
        assert(ChunkGetSection(chunk, sectionY, true));
    }
}

static void FillFloor(Chunk *chunk)
{
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            assert(ChunkSetLocalBlock(chunk, lx, 0, lz, BLOCK_STONE));
        }
    }
}

static void ResetWorld(void)
{
    FluidReset();
    ClearChunks();
    proceduralOceanEnabled = false;
    surfaceKind = WORLD_SURFACE_PLANET;
    cubeTopology = false;
}

static void TestGravityAndConservation(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    FillFloor(chunk);
    assert(FluidSetVolumeAt(4, 3, 4, FLUID_CAPACITY));
    assert(FluidLoadedVolume() == FLUID_CAPACITY);
    FluidStepTicks(1);
    assert(FluidGetVolumeAt(4, 2, 4) == FLUID_CAPACITY);
    assert(FluidLoadedVolume() == FLUID_CAPACITY);
    FluidSample falling = FluidSampleAt((Vector3){ 4.5f, 2.5f, 4.5f });
    assert(falling.velocity.y < 0.0f);

    FluidStepTicks(10000);
    assert(FluidLoadedVolume() == FLUID_CAPACITY);
    FluidStats stats = FluidGetStats();
    assert(stats.activeCells == 0u);
    assert(stats.lastProcessedCells <= FLUID_MAX_CELLS_PER_TICK);
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int y = 1; y < WORLD_HEIGHT; y++) {
            for (int z = 0; z < CHUNK_SIZE; z++) {
                if (FluidGetVolumeAt(x, y, z) == 0u) continue;
                FluidSample settled = FluidSampleAt((Vector3){
                    (float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f
                });
                assert(settled.velocity.x == 0.0f);
                assert(settled.velocity.y == 0.0f);
                assert(settled.velocity.z == 0.0f);
            }
        }
    }
}

static void TestNegativeSectionCoordinates(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    ResolveSection(chunk, -1);
    assert(FluidSetVolumeAt(4, -1, 4, FLUID_CAPACITY));
    const ChunkSection *section = ChunkGetSectionConst(chunk, -1);
    assert(section != NULL);
    assert(section->blocks[4][SURFACE_SECTION_HEIGHT - 1][4] == BLOCK_AIR);
    assert(section->waterVolumes != NULL);
    assert(section->waterVolumes[
        FixtureWaterCellIndex(4, SURFACE_SECTION_HEIGHT - 1, 4)] ==
        FLUID_CAPACITY);
    assert(FluidGetVolumeAt(4, -1, 4) == FLUID_CAPACITY);
}

static void TestSectionUnloadRehydratesPersistedEdit(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    const int y = -17;
    const int sectionY = SurfaceSectionYFromBlockY(y);
    ResolveSection(chunk, sectionY);
    assert(FluidSetVolumeAt(4, y, 4, 77u));
    assert(FluidGetStats().activeCells == 1u);
    assert(FluidPrepareChunkSectionUnload(chunk, sectionY));
    assert(FluidGetStats().activeCells == 0u);

    ClearChunks();
    chunk = CreateChunk(0, 0, 0);
    FluidApplyEditsToChunk(chunk);
    assert(ChunkGetSectionConst(chunk, sectionY) == NULL);

    assert(ChunkGetSection(chunk, sectionY, true) != NULL);
    FluidOnChunkSectionLoaded(chunk, sectionY);
    assert(FluidGetVolumeAt(4, y, 4) == 77u);
    assert(FluidGetStats().activeCells > 0u);
}

static void TestCrossChunkFlow(void)
{
    ResetWorld();
    Chunk *west = CreateChunk(0, 0, 0);
    Chunk *east = CreateChunk(1, 1, 0);
    FillFloor(west);
    FillFloor(east);
    assert(FluidSetVolumeAt(15, 1, 8, FLUID_CAPACITY));
    FluidStepTicks(8);
    assert(FluidGetVolumeAt(16, 1, 8) > 0u);
    assert(FluidLoadedVolume() == FLUID_CAPACITY);
}

static void TestFlowCrossesNorthPole(void)
{
    ResetWorld();
    const int pole = SURFACE_POLE_TO_POLE_BLOCKS / 2;
    const int poleChunk = pole / CHUNK_SIZE;
    const int halfEquatorChunks =
        SURFACE_EQUATOR_BLOCKS / (2 * CHUNK_SIZE);
    Chunk *nearPole = CreateChunk(0, 0, poleChunk - 1);
    Chunk *overPole = CreateChunk(
        1, -halfEquatorChunks, poleChunk - 1);
    FillFloor(nearPole);
    FillFloor(overPole);

    const int x = 3;
    const int y = 1;
    const int z = pole - 1;
    assert(FluidSetVolumeAt(x, y, z, FLUID_CAPACITY));
    FluidStepTicks(12);

    assert(FluidGetVolumeAt(x, y, z + 1) > 0u);
    assert(FluidGetVolumeAt(
        x + SURFACE_EQUATOR_BLOCKS / 2, y, z) > 0u);
    assert(FluidLoadedVolume() == FLUID_CAPACITY);
}

static void TestOceanReservoirSettlesAfterSeabedEdit(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            assert(ChunkSetLocalBlock(chunk, x, 0, z, BLOCK_STONE));
            assert(ChunkSetLocalBlock(chunk, x, 1, z, BLOCK_STONE));
            assert(ChunkSetLocalBlock(chunk, x, 2, z, BLOCK_WATER));
        }
    }
    SetProceduralOceanBounds(0, CHUNK_SIZE - 1, 2, 2,
                             0, CHUNK_SIZE - 1);
    uint64_t initialVolume = FluidLoadedVolume();

    assert(ChunkSetLocalBlock(chunk, 8, 1, 8, BLOCK_AIR));
    FluidOnBlockChanged(8, 1, 8, BLOCK_STONE, BLOCK_AIR);
    FluidStepTicks(512);

    assert(FluidGetVolumeAt(8, 1, 8) == FLUID_CAPACITY);
    assert(FluidGetVolumeAt(8, 2, 8) == FLUID_CAPACITY);
    assert(FluidLoadedVolume() == initialVolume + FLUID_CAPACITY);
    assert(FluidGetStats().activeCells == 0u);
    FluidSample source = FluidSampleAt((Vector3){ 8.5f, 2.5f, 8.5f });
    FluidSample filled = FluidSampleAt((Vector3){ 8.5f, 1.5f, 8.5f });
    assert(source.velocity.x == 0.0f && source.velocity.y == 0.0f &&
           source.velocity.z == 0.0f);
    assert(filled.velocity.x == 0.0f && filled.velocity.y == 0.0f &&
           filled.velocity.z == 0.0f);
}

static void TestCrossChunkOceanReservoirSettles(void)
{
    ResetWorld();
    Chunk *west = CreateChunk(0, 0, 0);
    Chunk *east = CreateChunk(1, 1, 0);
    assert(ChunkSetLocalBlock(west, 15, 2, 8, BLOCK_WATER));
    assert(ChunkSetLocalBlock(west, 14, 2, 8, BLOCK_STONE));
    assert(ChunkSetLocalBlock(west, 15, 1, 8, BLOCK_STONE));
    assert(ChunkSetLocalBlock(west, 15, 3, 8, BLOCK_STONE));
    assert(ChunkSetLocalBlock(west, 15, 2, 7, BLOCK_STONE));
    assert(ChunkSetLocalBlock(west, 15, 2, 9, BLOCK_STONE));
    assert(ChunkSetLocalBlock(east, 0, 2, 8, BLOCK_STONE));
    assert(ChunkSetLocalBlock(east, 1, 2, 8, BLOCK_STONE));
    assert(ChunkSetLocalBlock(east, 0, 1, 8, BLOCK_STONE));
    assert(ChunkSetLocalBlock(east, 0, 3, 8, BLOCK_STONE));
    assert(ChunkSetLocalBlock(east, 0, 2, 7, BLOCK_STONE));
    assert(ChunkSetLocalBlock(east, 0, 2, 9, BLOCK_STONE));
    SetProceduralOceanBounds(15, 16, 2, 2, 8, 8);

    assert(ChunkSetLocalBlock(east, 0, 2, 8, BLOCK_AIR));
    FluidOnBlockChanged(16, 2, 8, BLOCK_STONE, BLOCK_AIR);
    FluidStepTicks(512);

    assert(FluidGetVolumeAt(15, 2, 8) == FLUID_CAPACITY);
    assert(FluidGetVolumeAt(16, 2, 8) == FLUID_CAPACITY);
    assert(FluidGetStats().activeCells == 0u);
    FluidSample filled = FluidSampleAt((Vector3){ 16.5f, 2.5f, 8.5f });
    assert(filled.velocity.x == 0.0f && filled.velocity.y == 0.0f &&
           filled.velocity.z == 0.0f);
}

static void TestChunkLoadBoundaryActivation(void)
{
    ResetWorld();
    Chunk *west = CreateChunk(0, 0, 0);
    Chunk *east = CreateChunk(1, 1, 0);
    FillFloor(west);
    FillFloor(east);
    assert(ChunkSetLocalBlock(west, 15, 1, 8, BLOCK_WATER));
    FluidOnChunkLoaded(east);
    assert(FluidGetStats().activeCells == 0u);
    assert(FluidGetVolumeAt(16, 1, 8) == 0u);

    ResetWorld();
    west = CreateChunk(0, 0, 0);
    FillFloor(west);
    assert(ChunkSetLocalBlock(west, 14, 1, 8, BLOCK_STONE));
    assert(ChunkSetLocalBlock(west, 15, 1, 7, BLOCK_STONE));
    assert(ChunkSetLocalBlock(west, 15, 1, 9, BLOCK_STONE));
    assert(ChunkSetLocalBlock(west, 15, 2, 8, BLOCK_STONE));
    assert(FluidSetVolumeAt(15, 1, 8, FLUID_CAPACITY));
    FluidStepTicks(1);
    assert(FluidGetStats().activeCells == 0u);

    east = CreateChunk(1, 1, 0);
    FillFloor(east);
    FluidOnChunkLoaded(east);
    assert(FluidGetStats().activeCells == 1u);
    FluidStepTicks(1);
    assert(FluidGetVolumeAt(16, 1, 8) > 0u);
    assert(FluidLoadedVolume() == FLUID_CAPACITY);
}

static void TestContainerTransactions(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    FillFloor(chunk);
    assert(FluidTryDepositUnit(5, 1, 5));
    assert(FluidLoadedVolume() == FLUID_CAPACITY);
    assert(FluidGetStats().editCount == 1u);
    assert(FluidTryCollectUnit(5, 1, 5));
    assert(FluidLoadedVolume() == 0u);
    assert(FluidGetStats().editCount == 0u);
    assert(!FluidTryCollectUnit(5, 1, 5));
}


static void TestPlanetColumnAirRemainsWritable(void)
{
    ResetWorld();
    surfaceKind = WORLD_SURFACE_PLANET;
    Chunk *chunk = CreateChunk(0, 0, 0);
    ForgetSectionBaseline(chunk, 1);
    assert(ChunkGetSectionConst(chunk, 1) == NULL);
    assert(FluidSetVolumeAt(4, SURFACE_SECTION_HEIGHT, 4, 200u));
    FluidStepTicks(1);
    assert(FluidGetVolumeAt(4, SURFACE_SECTION_HEIGHT - 1, 4) == 200u);
    assert(FluidLoadedVolume() == 200u);
}



static void TestProceduralBaselinePrunesEdit(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    assert(ChunkSetLocalBlock(chunk, 4, 3, 4, BLOCK_WATER));
    assert(FluidSetVolumeAt(4, 3, 4, 100u));
    assert(FluidGetStats().editCount == 1u);
    assert(FluidSetVolumeAt(4, 3, 4, FLUID_CAPACITY));
    assert(FluidGetStats().editCount == 0u);
}

static void TestEditDeletionMaintainsChunkIndex(void)
{
    ResetWorld();
    CreateChunk(0, 0, 0);
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            uint8_t volume = (uint8_t)((x * CHUNK_SIZE + z) % 254 + 1);
            assert(FluidSetVolumeAt(x, 1, z, volume));
        }
    }
    assert(FluidGetStats().editCount == CHUNK_SIZE * CHUNK_SIZE);

    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            if (((x * CHUNK_SIZE + z) & 1) == 0) {
                assert(FluidSetVolumeAt(x, 1, z, 0u));
            }
        }
    }
    assert(FluidGetStats().editCount == CHUNK_SIZE * CHUNK_SIZE / 2);

    ClearChunks();
    CreateChunk(0, 0, 0);
    assert(ChunkGetSection(&chunks[0], 0, true) != NULL);
    FluidApplyEditsToChunk(&chunks[0]);
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            int ordinal = x * CHUNK_SIZE + z;
            uint8_t expected = (ordinal & 1) == 0
                ? 0u : (uint8_t)(ordinal % 254 + 1);
            assert(FluidGetVolumeAt(x, 1, z) == expected);
        }
    }
}

static void TestAtomicDisplacement(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    FillFloor(chunk);
    assert(FluidSetVolumeAt(8, 1, 8, 180u));
    assert(FluidTryDisplaceForBlock(8, 1, 8));
    assert(FluidGetVolumeAt(8, 1, 8) == 0u);
    assert(FluidLoadedVolume() == 180u);

    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            if (dx == 0 && dz == 0) continue;
            assert(ChunkSetLocalBlock(chunk, 8 + dx, 1, 8 + dz,
                                      BLOCK_STONE));
        }
    }
    assert(FluidSetVolumeAt(8, 1, 8, 255u));
    assert(ChunkSetLocalBlock(chunk, 8, 2, 8, BLOCK_STONE));
    assert(!FluidTryDisplaceForBlock(8, 1, 8));
    assert(FluidGetVolumeAt(8, 1, 8) == 255u);
}

static void TestDisplacementReplay(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    FillFloor(chunk);
    assert(FluidSetVolumeAt(8, 1, 8, 180u));

    FluidBlockDisplacement displacement = { 0 };
    assert(FluidTryDisplaceForBlockTracked(
        8, 1, 8, &displacement));
    assert(displacement.count == 2u);
    assert(displacement.cells[0].baselineKnown);
    assert(displacement.cells[0].baseline == 0u);
    assert(FluidGetVolumeAt(8, 1, 8) == 0u);
    assert(FluidLoadedVolume() == 180u);

    assert(FluidReplayBlockDisplacement(&displacement, false));
    assert(FluidGetVolumeAt(8, 1, 8) == 180u);
    assert(FluidLoadedVolume() == 180u);

    assert(FluidReplayBlockDisplacement(&displacement, true));
    assert(FluidGetVolumeAt(8, 1, 8) == 0u);
    assert(FluidLoadedVolume() == 180u);
}

static void TestQueueOverflowRetriesDeferredCell(void)
{
    ResetWorld();
    Chunk *first = CreateChunk(0, 0, 0);
    Chunk *second = CreateChunk(1, 1, 0);
    CreateAllSections(first);
    CreateAllSections(second);

    for (int chunkX = 0; chunkX < 2; chunkX++) {
        for (int y = 0; y < WORLD_HEIGHT; y++) {
            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                    FluidWakeCell(chunkX * CHUNK_SIZE + lx, y, lz);
                }
            }
        }
    }
    uint32_t queueCapacity =
        2u * CHUNK_SIZE * WORLD_HEIGHT * CHUNK_SIZE;
    assert(FluidGetStats().activeCells == queueCapacity);

    Chunk *deferred = CreateChunk(2, 2, 0);
    assert(ChunkGetSection(deferred, 0, true));
    FluidWakeCell(2 * CHUNK_SIZE, 0, 0);
    assert(FluidGetStats().queueOverflows == 1u);
    assert(FluidGetStats().activeCells == queueCapacity);

    FluidStepTicks(queueCapacity / FLUID_MAX_CELLS_PER_TICK);
    assert(FluidGetStats().activeCells == 1u);
    FluidStepTicks(1);
    assert(FluidGetStats().activeCells == 0u);
}

static void TestRealtimeUpdatesSpreadCellBudgetAcrossFrames(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    CreateAllSections(chunk);
    for (int y = 0; y < 32; y++) {
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                FluidWakeCell(lx, y, lz);
            }
        }
    }

    uint64_t processedBefore = FluidGetStats().processedCells;
    for (int frame = 0; frame < 6; frame++) {
        uint64_t frameBefore = FluidGetStats().processedCells;
        FluidUpdate(1.0f / 60.0f);
        uint64_t frameProcessed =
            FluidGetStats().processedCells - frameBefore;
        assert(frameProcessed > 0u);
        assert(frameProcessed <= FLUID_MAX_CELLS_PER_UPDATE);
    }
    uint64_t processed = FluidGetStats().processedCells - processedBefore;
    assert(processed >= FLUID_MAX_CELLS_PER_TICK - 1u);
    assert(processed <= FLUID_MAX_CELLS_PER_TICK);
}

static void TestSolidBlockChangePreservesBlock(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    FillFloor(chunk);
    assert(FluidSetVolumeAt(6, 1, 6, 120u));
    assert(ChunkSetLocalBlock(chunk, 6, 1, 6, BLOCK_STONE));
    FluidOnBlockChanged(6, 1, 6, BLOCK_AIR, BLOCK_STONE);
    assert(ChunkGetLocalBlock(chunk, 6, 1, 6) == BLOCK_STONE);
    assert(FluidGetVolumeAt(6, 1, 6) == 0u);
}

static void TestWaterSharesPlantCell(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    FillFloor(chunk);
    assert(ChunkSetLocalBlock(chunk, 4, 1, 4, BLOCK_COMMON_REED));

    assert(FluidSetVolumeAt(4, 1, 4, FLUID_CAPACITY));
    assert(ChunkGetLocalBlock(chunk, 4, 1, 4) == BLOCK_COMMON_REED);
    assert(FixtureWaterVolume(chunk, 4, 1, 4) == FLUID_CAPACITY);
    assert(FluidLoadedVolume() == FLUID_CAPACITY);
}

static void TestWaterFlowsThroughPlantWithoutReplacingIt(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    FillFloor(chunk);
    assert(ChunkSetLocalBlock(chunk, 5, 1, 4, BLOCK_COMMON_REED));
    assert(FluidSetVolumeAt(4, 1, 4, FLUID_CAPACITY));

    FluidStepTicks(8);
    assert(ChunkGetLocalBlock(chunk, 5, 1, 4) == BLOCK_COMMON_REED);
    assert(FluidGetVolumeAt(5, 1, 4) > 0u);
    assert(FluidLoadedVolume() == FLUID_CAPACITY);
}

static void TestSubmergedReedKeepsItsBlock(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    FillFloor(chunk);
    assert(ChunkSetLocalBlock(chunk, 3, 1, 4, BLOCK_STONE));
    assert(ChunkSetLocalBlock(chunk, 5, 1, 4, BLOCK_STONE));
    assert(ChunkSetLocalBlock(chunk, 4, 1, 3, BLOCK_STONE));
    assert(ChunkSetLocalBlock(chunk, 4, 1, 5, BLOCK_STONE));
    assert(ChunkSetLocalBlock(chunk, 4, 1, 4, BLOCK_COMMON_REED));
    assert(FluidSetVolumeAt(4, 1, 4, FLUID_CAPACITY));

    FluidStepTicks(32);
    assert(ChunkGetLocalBlock(chunk, 4, 1, 4) == BLOCK_COMMON_REED);
    assert(FluidGetVolumeAt(4, 1, 4) == FLUID_CAPACITY);
    assert(FluidLoadedVolume() == FLUID_CAPACITY);
}

static void TestLegacyWaterNormalizationKeepsPlant(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    FillFloor(chunk);
    assert(ChunkSetLocalBlock(chunk, 4, 1, 4, BLOCK_WATER));
    assert(ChunkGetLocalBlock(chunk, 4, 1, 4) == BLOCK_AIR);
    assert(FixtureWaterVolume(chunk, 4, 1, 4) == FLUID_CAPACITY);

    assert(ChunkSetLocalBlock(chunk, 4, 1, 4, BLOCK_COMMON_REED));
    assert(ChunkGetLocalBlock(chunk, 4, 1, 4) == BLOCK_COMMON_REED);
    assert(FixtureWaterVolume(chunk, 4, 1, 4) == FLUID_CAPACITY);

    assert(ChunkSetLocalBlock(chunk, 4, 1, 4, BLOCK_STONE));
    assert(ChunkGetLocalBlock(chunk, 4, 1, 4) == BLOCK_STONE);
    assert(FixtureWaterVolume(chunk, 4, 1, 4) == 0u);
}

static void TestSaveLoadReplay(void)
{
    ResetWorld();
    CreateChunk(0, 0, 0);
    assert(FluidSetVolumeAt(3, 6, 9, 77u));
    FILE *file = tmpfile();
    assert(file);
    assert(FluidSaveState(file));
    rewind(file);

    FluidReset();
    ClearChunks();
    CreateChunk(0, 0, 0);
    assert(ChunkGetSection(&chunks[0], 0, true) != NULL);
    assert(FluidLoadState(file));
    FluidApplyEditsToChunk(&chunks[0]);
    assert(FluidGetVolumeAt(3, 6, 9) == 77u);
    assert(FluidSetVolumeAt(3, 6, 9, 0u));
    assert(FluidGetStats().editCount == 0u);
    fclose(file);
}

static void TestLongitudeAliasUsesOneFluidEdit(void)
{
    ResetWorld();
    CreateChunk(0, 0, 0);
    const int x = 3;
    const int y = 6;
    const int z = 9;
    assert(FluidSetVolumeAt(
        x + SURFACE_EQUATOR_BLOCKS, y, z, 77u));
    assert(FluidGetVolumeAt(x, y, z) == 77u);
    assert(FluidGetStats().editCount == 1u);
    assert(FluidSetVolumeAt(x, y, z, 91u));
    assert(FluidGetVolumeAt(
        x + SURFACE_EQUATOR_BLOCKS, y, z) == 91u);
    assert(FluidGetStats().editCount == 1u);
}

static void WriteFluidEdit(FILE *file, int x, int y, int z,
                           uint32_t surfaceId, uint8_t volume)
{
    assert(fwrite(&x, sizeof(x), 1, file) == 1);
    assert(fwrite(&y, sizeof(y), 1, file) == 1);
    assert(fwrite(&z, sizeof(z), 1, file) == 1);
    assert(fwrite(&surfaceId, sizeof(surfaceId), 1, file) == 1);
    assert(fwrite(&volume, sizeof(volume), 1, file) == 1);
}

static void WriteFluidEditV2(FILE *file, int x, int y, int z,
                             uint32_t surfaceId, uint8_t volume,
                             uint8_t baseline)
{
    WriteFluidEdit(file, x, y, z, surfaceId, volume);
    uint8_t flags = 1u;
    assert(fwrite(&baseline, sizeof(baseline), 1, file) == 1);
    assert(fwrite(&flags, sizeof(flags), 1, file) == 1);
}

static void WriteFluidEditV3(FILE *file, SurfaceAddress address,
                             SurfaceChunkHandle chunk, int x, int y, int z,
                             uint8_t volume, uint8_t baseline)
{
    uint32_t bodyId = address.bodyId;
    uint32_t face = (uint32_t)address.face;
    int u = address.u;
    int v = address.v;
    int radial = address.radial;
    uint8_t flags = 1u;
    assert(fwrite(&bodyId, sizeof(bodyId), 1, file) == 1);
    assert(fwrite(&face, sizeof(face), 1, file) == 1);
    assert(fwrite(&u, sizeof(u), 1, file) == 1);
    assert(fwrite(&v, sizeof(v), 1, file) == 1);
    assert(fwrite(&radial, sizeof(radial), 1, file) == 1);
    assert(fwrite(chunk.opaque, sizeof(chunk.opaque), 1, file) == 1);
    assert(fwrite(&x, sizeof(x), 1, file) == 1);
    assert(fwrite(&y, sizeof(y), 1, file) == 1);
    assert(fwrite(&z, sizeof(z), 1, file) == 1);
    assert(fwrite(&volume, sizeof(volume), 1, file) == 1);
    assert(fwrite(&baseline, sizeof(baseline), 1, file) == 1);
    assert(fwrite(&flags, sizeof(flags), 1, file) == 1);
}

static void TestLoadPrunesOceanReservoirDeficits(void)
{
    ResetWorld();
    SetProceduralOceanBounds(4, 5, 2, 2, 4, 4);
    FILE *file = tmpfile();
    assert(file);
    uint32_t count = 2u;
    assert(fwrite("FLD2", 1, 4, file) == 4);
    assert(fwrite(&count, sizeof(count), 1, file) == 1);
    WriteFluidEditV2(file, 4, 2, 4, 0u, 100u, FLUID_CAPACITY);
    WriteFluidEditV2(file, 5, 2, 4, 0u, FLUID_CAPACITY, 0u);
    rewind(file);

    assert(FluidLoadState(file));
    assert(FluidGetStats().editCount == 2u);
    Chunk *chunk = CreateChunk(0, 0, 0);
    assert(ChunkGetSection(chunk, 0, true));
    assert(ChunkSetLocalBlock(chunk, 4, 2, 4, BLOCK_WATER));
    FluidApplyEditsToChunk(chunk);
    assert(FluidGetStats().editCount == 1u);
    assert(FluidGetVolumeAt(4, 2, 4) == FLUID_CAPACITY);
    assert(FluidGetVolumeAt(5, 2, 4) == FLUID_CAPACITY);
    fclose(file);
}

static void TestLoadCanonicalizesLongitudeAlias(void)
{
    ResetWorld();
    FILE *file = tmpfile();
    assert(file);
    uint32_t count = 1u;
    assert(fwrite("FLD2", 1, 4, file) == 4);
    assert(fwrite(&count, sizeof(count), 1, file) == 1);
    WriteFluidEditV2(file, 3 + SURFACE_EQUATOR_BLOCKS, 6, 9,
                     0u, 77u, 0u);
    rewind(file);

    assert(FluidLoadState(file));
    Chunk *chunk = CreateChunk(0, 0, 0);
    assert(ChunkGetSection(chunk, 0, true));
    FluidApplyEditsToChunk(chunk);
    assert(FluidGetVolumeAt(3, 6, 9) == 77u);
    assert(FluidGetStats().editCount == 1u);
    fclose(file);
}

static void TestCorruptLoadPreservesState(void)
{
    ResetWorld();
    CreateChunk(0, 0, 0);
    assert(FluidSetVolumeAt(2, 5, 2, 91u));
    FluidStats before = FluidGetStats();

    FILE *truncated = tmpfile();
    assert(truncated);
    assert(fwrite("FLD1", 1, 4, truncated) == 4);
    rewind(truncated);
    assert(!FluidLoadState(truncated));
    assert(FluidGetVolumeAt(2, 5, 2) == 91u);
    assert(FluidGetStats().editCount == before.editCount);
    fclose(truncated);

    FILE *duplicate = tmpfile();
    assert(duplicate);
    uint32_t count = 2u;
    assert(fwrite("FLD1", 1, 4, duplicate) == 4);
    assert(fwrite(&count, sizeof(count), 1, duplicate) == 1);
    WriteFluidEdit(duplicate, 7, 8, 9, 0u, 10u);
    WriteFluidEdit(duplicate, 7, 8, 9, 0u, 20u);
    rewind(duplicate);
    assert(!FluidLoadState(duplicate));
    assert(FluidGetVolumeAt(2, 5, 2) == 91u);
    assert(FluidGetStats().editCount == before.editCount);
    fclose(duplicate);
}

static void TestEvaporationTransactions(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    assert(FluidSetVolumeAt(2, 2, 2, 100u));
    bool reservoir = true;
    assert(!FluidTryEvaporateAt(2, 2, 2, 99u, 2u, &reservoir));
    assert(!reservoir && FluidGetVolumeAt(2, 2, 2) == 100u);
    assert(!FluidTryEvaporateAt(2, 2, 2, 100u, 101u, &reservoir));
    lavaOverlay = true;
    assert(!FluidTryEvaporateAt(2, 2, 2, 100u, 2u, &reservoir));
    lavaOverlay = false;
    chunk->loaded = false;
    assert(!FluidTryEvaporateAt(2, 2, 2, 100u, 2u, &reservoir));
    chunk->loaded = true;
    assert(FluidTryEvaporateAt(2, 2, 2, 100u, 17u, &reservoir) && !reservoir);
    assert(FluidGetVolumeAt(2, 2, 2) == 83u);
    FILE *file = tmpfile(); assert(file && FluidSaveState(file));
    assert(FluidPrepareChunkSectionUnload(chunk, 0));
    ClearChunks(); chunk = CreateChunk(0, 0, 0);
    assert(ChunkGetSection(chunk, 0, true));
    FluidOnChunkSectionLoaded(chunk, 0);
    assert(FluidGetVolumeAt(2, 2, 2) == 83u);
    ResetWorld(); rewind(file); assert(FluidLoadState(file)); fclose(file);
    chunk = CreateChunk(0, 0, 0); assert(ChunkGetSection(chunk, 0, true));
    FluidApplyEditsToChunk(chunk);
    assert(FluidGetVolumeAt(2, 2, 2) == 83u);
    assert(FluidTryEvaporateAt(2, 2, 2, 83u, 83u, &reservoir));
    assert(FluidGetVolumeAt(2, 2, 2) == 0u && GetBlockAt(2, 2, 2) == BLOCK_AIR);
    assert(FluidSetVolumeAt(2, 2, 2, FLUID_CAPACITY));
    SetProceduralOceanBounds(2, 2, 2, 2, 2, 2);
    assert(!FluidTryEvaporateAt(2, 2, 2, FLUID_CAPACITY, 10u, NULL));
    assert(FluidTryEvaporateAt(2, 2, 2, FLUID_CAPACITY, 10u, &reservoir) && reservoir);
    assert(FluidGetVolumeAt(2, 2, 2) == FLUID_CAPACITY);
}

static void TestCubeFaceChunkIdentityAndCheckpoint(void)
{
    ResetWorld();
    surfaceKind = WORLD_SURFACE_HOME;
    cubeTopology = true;
    (void)WorldCurrentSurfaceContract();
    SurfaceAddress first = {0, SURFACE_FACE_POS_Z, 17, 33, 8};
    SurfaceAddress second = {0, SURFACE_FACE_POS_X, 17, 33, 8};
    SurfaceAddress sameChunk = first;
    sameChunk.u++;
    bool shares = true;
    assert(FluidTestAddressesShareChunk(first, second, &shares));
    assert(!shares);
    assert(FluidTestAddressesShareChunk(first, sameChunk, &shares));
    assert(shares);

    SurfaceChunkHandle firstChunk;
    SurfaceChunkHandle secondChunk;
    assert(TestContractChunkFromAddress(
        &testSurfaceContract, first, &firstChunk));
    assert(TestContractChunkFromAddress(
        &testSurfaceContract, second, &secondChunk));
    FILE *source = tmpfile();
    assert(source);
    uint32_t count = 2u;
    assert(fwrite("FLD3", 1, 4, source) == 4);
    assert(fwrite(&count, sizeof(count), 1, source) == 1);
    WriteFluidEditV3(source, first, firstChunk, 8, 8, 8, 77u, 0u);
    WriteFluidEditV3(source, second, secondChunk, 9, 8, 8, 88u, 0u);
    rewind(source);
    assert(FluidLoadState(source));
    FILE *roundTrip = tmpfile();
    assert(roundTrip && FluidSaveState(roundTrip));

    assert(fseek(source, 0, SEEK_END) == 0);
    long sourceSize = ftell(source);
    assert(fseek(roundTrip, 0, SEEK_END) == 0);
    long roundTripSize = ftell(roundTrip);
    assert(sourceSize > 0 && sourceSize == roundTripSize);
    rewind(source);
    rewind(roundTrip);
    for (long offset = 0; offset < sourceSize; offset++) {
        int expected = fgetc(source);
        int actual = fgetc(roundTrip);
        assert(expected != EOF && actual != EOF && expected == actual);
    }
    fclose(roundTrip);
    fclose(source);
    cubeTopology = false;
}

int main(void)
{
    ResetWorld();
    CreateChunk(0, 0, 0);
    lavaOverlay = true;
    assert(!FluidSetVolumeAt(2, 2, 2, FLUID_CAPACITY));
    assert(FluidSetVolumeAt(2, 3, 2, FLUID_CAPACITY));
    FluidStepTicks(20u);
    assert(FluidGetVolumeAt(2, 2, 2) == 0u);
    lavaOverlay = false;
    TestPlanetColumnAirRemainsWritable();
    TestNegativeSectionCoordinates();
    TestSectionUnloadRehydratesPersistedEdit();
    TestGravityAndConservation();
    TestCrossChunkFlow();
    TestFlowCrossesNorthPole();
    TestOceanReservoirSettlesAfterSeabedEdit();
    TestCrossChunkOceanReservoirSettles();
    TestChunkLoadBoundaryActivation();
    TestContainerTransactions();
    TestProceduralBaselinePrunesEdit();
    TestEditDeletionMaintainsChunkIndex();
    TestAtomicDisplacement();
    TestDisplacementReplay();
    TestQueueOverflowRetriesDeferredCell();
    TestRealtimeUpdatesSpreadCellBudgetAcrossFrames();
    TestSolidBlockChangePreservesBlock();
    TestWaterSharesPlantCell();
    TestWaterFlowsThroughPlantWithoutReplacingIt();
    TestSubmergedReedKeepsItsBlock();
    TestLegacyWaterNormalizationKeepsPlant();
    TestSaveLoadReplay();
    TestLongitudeAliasUsesOneFluidEdit();
    TestLoadPrunesOceanReservoirDeficits();
    TestLoadCanonicalizesLongitudeAlias();
    TestCorruptLoadPreservesState();
    TestEvaporationTransactions();
    TestCubeFaceChunkIdentityAndCheckpoint();
    puts("fluid tests passed");
    return 0;
}
