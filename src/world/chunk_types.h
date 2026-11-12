#ifndef VOXELCRAFT_CHUNK_TYPES_H
#define VOXELCRAFT_CHUNK_TYPES_H

#include "core/config.h"
#include "world/flora_types.h"
#include "world/terrain_types.h"
#include "world/surface_types.h"
#include "world/surface_cube.h"

#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

/* Chunk and job storage. This is the expensive header in the module: it carries
 * GPU `Model` handles and a full staging buffer per job, so a consumer that only
 * classifies blocks must include `block_type.h` instead of this one. */
typedef enum ChunkGenScope {
    CHUNK_GEN_SCOPE_COLUMN = 0,
    CHUNK_GEN_SCOPE_SECTION
} ChunkGenScope;

typedef enum ChunkLodLevel {
    CHUNK_LOD_EXACT = 0,
    CHUNK_LOD_HALF,
    CHUNK_LOD_QUARTER,
    CHUNK_LOD_COUNT
} ChunkLodLevel;

typedef struct ChunkSection {
    bool dirty;
    // Monotonic content revision bumped by every MarkChunkDirty* call.
    // Mesh jobs record it at snapshot time so stale uploads do not clear
    // dirty flags that were re-set after the snapshot (lost-edit race).
    uint32_t dirtyStamp;
    double dirtySinceMs;
    bool hasModel;
    bool exactModelReady;
    uint32_t exactModelStamp;
    bool hasLodModel;
    bool lodModelReady;
    uint32_t lodModelStamp;
    ChunkLodLevel lodModelLevel;
    bool hasWaterModel;
    bool hasLodWaterModel;
    bool hasFloraModel;
    int sectionY;
    Model model;
    Model lodModel;
    Model waterModel;
    Model lodWaterModel;
    Model floraModel;
    Model lodFloraModel;
    bool hasLodFloraModel;
    bool lodFloraModelReady;
    uint32_t lodFloraModelStamp;
    ChunkLodLevel lodFloraModelLevel;
    float floraVisualScale;
    float *floraTargetScales;
    float *floraTargetWind;
    float *floraTargetWindAngle;
    float *floraTargetPresence;
    float *floraBaseVertices;
    unsigned char *floraBaseColors;
    FloraVisualInstance *floraVisualInstances;
    int floraTargetScaleCount;
    unsigned char *waterVolumes;
    unsigned char *fluidQueuedBits;
    unsigned char *fluidDeferredBits;
    signed char *fluidFlow;
    bool fluidDirty;
    unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
} ChunkSection;

typedef struct Chunk {
    bool loaded;
    bool generating;
    bool preloaded;
    int cx;
    int cz;
    bool spherical;
    bool cubeGrid;
    SurfaceCubeChunkKey cubeKey;
    uint64_t cubeWorldGeneration;
    SurfaceAddress surfaceAddress;
    SurfaceChunkKey surfaceKey;
    // Incarnation counter bumped on every slot reuse (EnsureChunk).
    // In-flight mesh jobs from a previous incarnation are discarded on
    // upload so stale terrain never overwrites a freshly generated chunk.
    uint32_t generation;
    ChunkLodLevel targetLod;
    ChunkLodLevel activeLod;
    bool lodInitialized;
    float floraActivity;
    float floraCapacity;
    float floraSampleTimer;
    float floraWindAngle;
    FloraStructureInstance floraStructures[MAX_CHUNK_FLORA_STRUCTURES];
    int floraStructureCount;
    // Materialized vertical sections, sorted by signed sectionY. The logical
    // world height must not determine the size of every resident XZ column.
    ChunkSection **sections;
    int sectionCount;
    int sectionCapacity;
    // Sorted Section Y values whose procedural baseline has been evaluated.
    // This records all-air results without allocating a full ChunkSection.
    int *resolvedTerrainSectionYs;
    int resolvedTerrainSectionCount;
    int resolvedTerrainSectionCapacity;
} Chunk;

typedef struct ChunkGenJob {
    bool inUse;
    bool running;
    bool done;
    bool queued;
    bool completionQueued;
    bool preload;
    bool succeeded;
    bool hasSectionBlocks;
    ChunkGenScope scope;
    int cx;
    int cz;
    int sectionY;
    int slotIndex;
    uint32_t chunkGeneration;
    bool spherical;
    bool cubeGrid;
    SurfaceCubeChunkKey cubeKey;
    uint64_t worldGeneration;
    // Owned immutable input for a cube job. The backend callbacks keep the
    // scheduler independent of terrain construction and its atlas ownership.
    struct TerrainCubeContext *cubeContext;
    bool (*generateCube)(const struct TerrainCubeContext *,
                         SurfaceCubeChunkKey, struct Chunk *, ChunkGenScope, int);
    void (*releaseCube)(struct TerrainCubeContext *);
    SurfaceAddress surfaceAddress;
    SurfaceChunkKey surfaceKey;
    TerrainMode terrainMode;
    uint64_t queueSequence;
    double submittedAtMs;
    double startedAtMs;
    double completedAtMs;
    // Column generation owns this private result until the main thread
    // validates the destination chunk incarnation and transfers its storage.
    struct Chunk *columnResult;
    // Section jobs generate into staging storage because their target chunk
    // is already visible to the main/render thread. Completion copies this
    // snapshot only if the chunk incarnation still matches.
    unsigned short sectionBlocks
        [CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    int sectionFloraStructureCount;
    FloraStructureInstance sectionFloraStructures[MAX_CHUNK_FLORA_STRUCTURES];
} ChunkGenJob;

#endif
