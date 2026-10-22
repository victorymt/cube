#include "world/world_cube.h"
#include "world/chunks_internal.h"
#include "world/terrain_cube.h"

typedef struct CubeEdit {
    WorldBlockAddress address;
    BlockType type;
    unsigned nextInChunk;
} CubeEdit;

static CubeEdit *edits;
static unsigned *cellIndex, *chunkIndex;
static unsigned count, capacity;
#ifdef CHUNKS_TESTING
static bool failNextReserve;
void WorldCubeTestFailNextReserve(void) { failNextReserve = true; }
#endif

static bool ValidAddress(WorldBlockAddress address)
{
    return WorldBlockAddressIsValid(address) &&
        ((address.dimension == WORLD_DIMENSION_HOME && address.at.surface.bodyId == 0) ||
         (address.dimension == WORLD_DIMENSION_PLANET && address.at.surface.bodyId != 0)) &&
        address.at.surface.radial >= TERRAIN_CUBE_MIN_RADIAL &&
        address.at.surface.radial < SURFACE_MAX_Y_EXCLUSIVE;
}

static SurfaceCubeChunkKey Key(WorldBlockAddress address)
{
    SurfaceAddress cell = address.at.surface;
    return (SurfaceCubeChunkKey){cell.bodyId, cell.face,
        cell.u / CHUNK_SIZE, cell.v / CHUNK_SIZE};
}

static unsigned CellSlot(WorldBlockAddress address, unsigned *index,
                          CubeEdit *entries, unsigned size)
{
    unsigned slot = WorldBlockAddressHash(address) & (size - 1);
    while (index[slot] && !WorldBlockAddressEqual(entries[index[slot] - 1].address, address))
        slot = (slot + 1) & (size - 1);
    return slot;
}

static unsigned ChunkSlot(SurfaceCubeChunkKey key, unsigned *index,
                           CubeEdit *entries, unsigned size)
{
    unsigned slot = SurfaceCubeChunkKeyHash(key) & (size - 1);
    while (index[slot] && !SurfaceCubeChunkKeyEqual(Key(entries[index[slot] - 1].address), key))
        slot = (slot + 1) & (size - 1);
    return slot;
}

static bool Reserve(void)
{
    if (count < capacity) return true;
    if (capacity == WORLD_CUBE_MAX_EDITS) return false;
#ifdef CHUNKS_TESTING
    if (failNextReserve) { failNextReserve = false; return false; }
#endif
    unsigned next = capacity ? capacity * 2 : 64;
    CubeEdit *nextEdits = malloc(next * sizeof(*nextEdits));
    unsigned *cells = calloc(next * 2, sizeof(*cells));
    unsigned *chunksByKey = calloc(next * 2, sizeof(*chunksByKey));
    if (!nextEdits || !cells || !chunksByKey) {
        free(nextEdits); free(cells); free(chunksByKey);
        return false;
    }
    if (count) memcpy(nextEdits, edits, count * sizeof(*edits));
    for (unsigned i = 0; i < count; i++) {
        cells[CellSlot(nextEdits[i].address, cells, nextEdits, next * 2)] = i + 1;
        unsigned slot = ChunkSlot(Key(nextEdits[i].address), chunksByKey, nextEdits, next * 2);
        nextEdits[i].nextInChunk = chunksByKey[slot];
        chunksByKey[slot] = i + 1;
    }
    free(edits); free(cellIndex); free(chunkIndex);
    edits = nextEdits; cellIndex = cells; chunkIndex = chunksByKey; capacity = next;
    return true;
}

static unsigned Head(SurfaceCubeChunkKey key)
{
    return capacity ? chunkIndex[ChunkSlot(key, chunkIndex, edits, capacity * 2)] : 0;
}

bool WorldCubeGetEdit(WorldBlockAddress address, BlockType *outType)
{
    if (!outType || !ValidAddress(address) || !capacity) return false;
    unsigned index = cellIndex[CellSlot(address, cellIndex, edits, capacity * 2)];
    if (!index) return false;
    *outType = edits[index - 1].type;
    return true;
}

bool WorldCubeBlockAt(WorldBlockAddress address, BlockType *outType)
{
    if (!outType || !ValidAddress(address)) return false;
    if (WorldCubeGetEdit(address, outType)) return true;
    Chunk *chunk = FindCubeChunk(Key(address));
    int y = address.at.surface.radial;
    if (!chunk || chunk->cubeWorldGeneration != WorldCurrentGeneration() ||
        (!ChunkGetSectionConst(chunk, SurfaceSectionYFromBlockY(y)) &&
         !ChunkTerrainSectionIsResolved(chunk, SurfaceSectionYFromBlockY(y)))) return false;
    *outType = ChunkGetLocalBlock(chunk, address.at.surface.u % CHUNK_SIZE,
                                 y, address.at.surface.v % CHUNK_SIZE);
    return true;
}

bool WorldCubeSetBlock(WorldBlockAddress address, BlockType type)
{
    if (!ValidAddress(address) || !IsValidBlockType(type)) return false;
    unsigned slot = capacity ? CellSlot(address, cellIndex, edits, capacity * 2) : 0;
    bool exists = capacity && cellIndex[slot];
    if (!exists && !Reserve()) return false;
    slot = CellSlot(address, cellIndex, edits, capacity * 2);
    Chunk *chunk = FindCubeChunk(Key(address));
    int sectionY = SurfaceSectionYFromBlockY(address.at.surface.radial);
    bool resident = chunk && chunk->cubeWorldGeneration == WorldCurrentGeneration() &&
        (ChunkTerrainSectionIsResolved(chunk, sectionY) || ChunkGetSectionConst(chunk, sectionY));
    if (resident && !ChunkSetLocalBlock(chunk, address.at.surface.u % CHUNK_SIZE,
        address.at.surface.radial, address.at.surface.v % CHUNK_SIZE, type)) return false;
    if (cellIndex[slot]) edits[cellIndex[slot] - 1].type = type;
    else {
        unsigned bucket = ChunkSlot(Key(address), chunkIndex, edits, capacity * 2);
        edits[count] = (CubeEdit){address, type, chunkIndex[bucket]};
        cellIndex[slot] = chunkIndex[bucket] = ++count;
    }
    if (resident) MarkGeneratedSectionAndNeighborsDirty(chunk, sectionY);
    return true;
}

void WorldCubeApplySectionEdits(SurfaceCubeChunkKey key, int sectionY,
    unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE])
{
    if (!blocks || !SurfaceCubeChunkKeyIsValid(key) ||
        sectionY < TERRAIN_CUBE_MIN_RADIAL / SURFACE_SECTION_HEIGHT ||
        sectionY >= SURFACE_MAX_Y_EXCLUSIVE / SURFACE_SECTION_HEIGHT) return;
    for (unsigned at = Head(key); at; at = edits[at - 1].nextInChunk) {
        const CubeEdit *edit = &edits[at - 1];
        SurfaceAddress cell = edit->address.at.surface;
        if (SurfaceSectionYFromBlockY(cell.radial) == sectionY)
            blocks[cell.u % CHUNK_SIZE][SurfaceSectionLocalYFromBlockY(cell.radial)]
                  [cell.v % CHUNK_SIZE] = (unsigned short)edit->type;
    }
}

bool WorldCubeBlockAtPoint(uint32_t bodyId, SurfaceVector point,
                            BlockType *outType)
{
    if (!outType || !SurfaceVectorIsFinite(point)) return false;
    SurfaceAddress cell;
    if (!SurfaceCellFromPoint(bodyId, point, &cell)) return false;
    WorldBlockAddress address;
    address.dimension = bodyId == 0 ? WORLD_DIMENSION_HOME
                                    : WORLD_DIMENSION_PLANET;
    address.at.surface = cell;
    return WorldCubeBlockAt(address, outType);
}

bool WorldCubeApplyEdits(Chunk *staging)
{
    if (!staging || !staging->cubeGrid || staging->loaded) return false;
    for (unsigned at = Head(staging->cubeKey); at; at = edits[at - 1].nextInChunk) {
        const CubeEdit *edit = &edits[at - 1];
        SurfaceAddress cell = edit->address.at.surface;
        int sectionY = SurfaceSectionYFromBlockY(cell.radial);
        if (!ChunkTerrainSectionIsResolved(staging, sectionY) &&
            !ChunkGetSectionConst(staging, sectionY)) continue;
        if (!ChunkSetLocalBlock(staging, cell.u % CHUNK_SIZE, cell.radial,
                               cell.v % CHUNK_SIZE, edit->type)) return false;
    }
    return true;
}

unsigned WorldCubeEditCount(void) { return count; }

bool WorldCubeEditAt(unsigned index, WorldBlockAddress *outAddress, BlockType *outType)
{
    if (index >= count || !outAddress || !outType) return false;
    *outAddress = edits[index].address;
    *outType = edits[index].type;
    return true;
}

void WorldCubeClearEdits(void)
{
    free(edits); free(cellIndex); free(chunkIndex);
    edits = NULL; cellIndex = chunkIndex = NULL; count = capacity = 0;
#ifdef CHUNKS_TESTING
    failNextReserve = false;
#endif
}
