#include "fluid.h"

#include "chunks.h"
#include "world_environment.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Chunk chunks[MAX_ACTIVE_CHUNKS];

static bool proceduralOceanEnabled = false;
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

BlockType ChunkGetLocalBlock(const Chunk *chunk, int lx, int y, int lz)
{
    const ChunkSection *section = ChunkGetSectionConst(
        chunk, SurfaceSectionYFromBlockY(y));
    return section
        ? (BlockType)section->blocks[
              lx][SurfaceSectionLocalYFromBlockY(y)][lz]
        : BLOCK_AIR;
}

bool ChunkSetLocalBlock(Chunk *chunk, int lx, int y, int lz, BlockType type)
{
    ChunkSection *section = ChunkGetSection(
        chunk, SurfaceSectionYFromBlockY(y), type != BLOCK_AIR);
    if (!section) return type == BLOCK_AIR;
    section->blocks[lx][SurfaceSectionLocalYFromBlockY(y)][lz] = type;
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
    }
    memset(chunks, 0, sizeof(chunks));
}

static Chunk *CreateChunk(int slot, int cx, int cz)
{
    Chunk *chunk = &chunks[slot];
    *chunk = (Chunk){ .loaded = true, .cx = cx, .cz = cz };
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
    assert(FluidSetVolumeAt(4, -1, 4, FLUID_CAPACITY));
    const ChunkSection *section = ChunkGetSectionConst(chunk, -1);
    assert(section != NULL);
    assert(section->blocks[4][SURFACE_SECTION_HEIGHT - 1][4] == BLOCK_WATER);
    assert(FluidGetVolumeAt(4, -1, 4) == FLUID_CAPACITY);
}

static void TestSectionUnloadRehydratesPersistedEdit(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    const int y = -17;
    const int sectionY = SurfaceSectionYFromBlockY(y);
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

static void TestSolidBlockChangePreservesBlock(void)
{
    ResetWorld();
    Chunk *chunk = CreateChunk(0, 0, 0);
    FillFloor(chunk);
    assert(FluidSetVolumeAt(6, 1, 6, 120u));
    assert(ChunkSetLocalBlock(chunk, 6, 1, 6, BLOCK_STONE));
    FluidOnBlockChanged(6, 1, 6, BLOCK_WATER, BLOCK_STONE);
    assert(ChunkGetLocalBlock(chunk, 6, 1, 6) == BLOCK_STONE);
    assert(FluidGetVolumeAt(6, 1, 6) == 0u);
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

int main(void)
{
    TestNegativeSectionCoordinates();
    TestSectionUnloadRehydratesPersistedEdit();
    TestGravityAndConservation();
    TestCrossChunkFlow();
    TestOceanReservoirSettlesAfterSeabedEdit();
    TestCrossChunkOceanReservoirSettles();
    TestChunkLoadBoundaryActivation();
    TestContainerTransactions();
    TestProceduralBaselinePrunesEdit();
    TestEditDeletionMaintainsChunkIndex();
    TestAtomicDisplacement();
    TestDisplacementReplay();
    TestQueueOverflowRetriesDeferredCell();
    TestSolidBlockChangePreservesBlock();
    TestSaveLoadReplay();
    TestLoadPrunesOceanReservoirDeficits();
    TestCorruptLoadPreservesState();
    ResetWorld();
    puts("fluid tests passed");
    return 0;
}
