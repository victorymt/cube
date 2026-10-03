#ifndef VOXELCRAFT_CHUNKS_H
#define VOXELCRAFT_CHUNKS_H

#include "core/render_resources.h"
#include "core/perf_metrics.h"
#include "world/world_types.h"

enum {
    CHUNK_WATER_NEIGHBOR_WEST = 1u << 0,
    CHUNK_WATER_NEIGHBOR_EAST = 1u << 1,
    CHUNK_WATER_NEIGHBOR_NORTH = 1u << 2,
    CHUNK_WATER_NEIGHBOR_SOUTH = 1u << 3
};

typedef struct ChunkWaterRenderDebugInfo {
    int cx;
    int cz;
    int sectionY;
    bool chunkLoaded;
    unsigned int neighborLoadedMask;
    int triangleCount;
    int sectionTriangleCount;
} ChunkWaterRenderDebugInfo;

typedef enum ChunkPipelineStage {
    CHUNK_PIPELINE_MISSING_CHUNK = 0,
    CHUNK_PIPELINE_GENERATION_WAIT,
    CHUNK_PIPELINE_GENERATION_QUEUED,
    CHUNK_PIPELINE_GENERATION_RUNNING,
    CHUNK_PIPELINE_GENERATION_DONE,
    CHUNK_PIPELINE_IMPLICIT,
    CHUNK_PIPELINE_DIRTY_WAIT,
    CHUNK_PIPELINE_MESH_QUEUED,
    CHUNK_PIPELINE_MESH_RUNNING,
    CHUNK_PIPELINE_MESH_DONE,
    CHUNK_PIPELINE_READY
} ChunkPipelineStage;

typedef struct ChunkSectionPipelineInfo {
    ChunkPipelineStage stage;
    double stageAgeMs;
    uint32_t currentStamp;
    uint32_t snapshotStamp;
    int solidVertices;
    int waterVertices;
    int floraVertices;
    bool chunkLoaded;
    bool resolved;
    bool materialized;
    bool dirty;
} ChunkSectionPipelineInfo;

typedef struct ChunkCanonicalIdentityStats {
    int loaded;
    int unique;
    int duplicates;
} ChunkCanonicalIdentityStats;

enum {
    CHUNK_LOD_EXACT_MAX_DISTANCE = 4,
    CHUNK_LOD_HALF_MAX_DISTANCE = 8,
    CHUNK_LOD_HYSTERESIS = 1,
    CHUNK_PRELOAD_WALK_MAX_DISTANCE = 2,
    CHUNK_PRELOAD_SHIP_MAX_DISTANCE = 4
};

typedef struct ChunkLodStats {
    uint64_t targetChunks[CHUNK_LOD_COUNT];
    uint64_t activeChunks[CHUNK_LOD_COUNT];
    uint64_t readySections[CHUNK_LOD_COUNT];
    uint64_t proxyFloraSections[CHUNK_LOD_COUNT];
    uint64_t pendingJobs[CHUNK_LOD_COUNT];
    uint64_t targetChanges;
    bool coarseAllowed;
} ChunkLodStats;

typedef struct ChunkStreamingRequest {
    Vector3 position;
    Vector3 velocity;
    int effectiveRenderDistance;
    int maxPreloadChunks;
} ChunkStreamingRequest;

typedef struct ChunkPreloadInfo {
    bool active;
    int offsetX;
    int offsetZ;
    int targetChunks;
    int loadedChunks;
    int pendingGenerationJobs;
    int pendingMeshJobs;
} ChunkPreloadInfo;

/*
 * A copied, read-only view of the chunk scheduler.  The snapshot owns all of
 * its scalar data; callers may keep it after the scheduler changes or the
 * current world is unloaded.  `streaming` remains the legacy cumulative
 * counter set used by performance reporting, while the other fields describe
 * the state observed at this capture point.
 */
#define CHUNK_DIAGNOSTICS_REASON_CAPACITY 64

typedef enum ChunkDiagnosticsStatus {
    CHUNK_DIAGNOSTICS_UNAVAILABLE = 0,
    CHUNK_DIAGNOSTICS_VALID
} ChunkDiagnosticsStatus;

typedef struct ChunkDiagnostics {
    ChunkDiagnosticsStatus status;
    uint64_t captureSequence;
    ChunkStreamingStats streaming;
    ChunkPreloadInfo preload;
    ChunkLodStats lod;
    int activeChunks;
    int pendingGenerationJobs;
    int pendingMeshJobs;
    int renderDistance;
    int focusChunkX;
    int focusChunkZ;
    int predictedChunkX;
    int predictedChunkZ;
    bool focusChunkLoaded;
    unsigned int workerThreadsConfigured;
    unsigned int workerThreadsStarted;
    unsigned int workerThreadsActive;
    char blockedReason[CHUNK_DIAGNOSTICS_REASON_CAPACITY];
} ChunkDiagnostics;

bool InHeight(int y);
int FloorDivInt(int value, int divisor);
int PositiveMod(int value, int divisor);
bool SurfaceSectionInBounds(int sectionY);
int SurfaceSectionYFromBlockY(int y);
int SurfaceSectionLocalYFromBlockY(int y);
void WorldToChunkLocal(int x, int z, int *cx, int *cz, int *lx, int *lz);
void CanonicalizeSurfaceChunkCoordinates(int *cx, int *cz);
const Chunk *ChunksView(void);
Texture2D ChunksBlockAtlas(void);
void ChunksSetBlockAtlas(Texture2D atlas);
int ChunksRenderDistance(void);
void ChunksSetRenderDistance(int distance);

Chunk *FindChunk(int cx, int cz);
Chunk *FindHorizontalChunkNeighbor(int cx, int cz, int deltaCx, int deltaCz);
SurfaceAddress SurfaceAddressAtWorld(float x, float z, int radial);
SurfaceAddress ChunkSurfaceAddressAt(int cx, int cz);
SurfaceChunkKey ChunkSurfaceKeyAt(int cx, int cz);
Chunk *FindSurfaceChunk(SurfaceChunkKey key);
int ChunkGridDistanceFrom(const Chunk *chunk, int cx, int cz);
ChunkSection *ChunkGetSection(Chunk *chunk, int sectionY, bool create);
const ChunkSection *ChunkGetSectionConst(const Chunk *chunk, int sectionY);
bool ChunkTerrainSectionIsResolved(const Chunk *chunk, int sectionY);
bool ChunkMarkTerrainSectionResolved(Chunk *chunk, int sectionY);
bool ChunkTryGetLocalBlock(const Chunk *chunk, int lx, int y, int lz,
                           BlockType *outBlock);
BlockType ChunkGetLocalBlock(const Chunk *chunk, int lx, int y, int lz);
bool ChunkSetLocalBlock(Chunk *chunk, int lx, int y, int lz, BlockType type);
void ChunkClearBlockStorage(Chunk *chunk);
void MarkChunkDirty(int cx, int cz);
void MarkChunkDirtyAtBlock(int x, int y, int z);
void MarkChunkAndHorizontalNeighborsDirty(int cx, int cz);

unsigned int Hash3D(int x, int y, int z);
bool BuildMeshData(const unsigned short (*blocks)[CHUNK_SIZE],
                   int height, int layerY, int chunkX, int chunkZ,
                   bool transparent, const int faces[6][3],
                   const int *nearbyTorchIndices, int nearbyTorchCount,
                   Mesh *outMesh);
bool BuildSurfaceSolidMeshData(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh);
bool BuildSurfaceWaterMeshData(
    const unsigned short (*blocks)[CHUNK_SIZE],
    const unsigned char *waterVolumes, int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh);
bool BuildFloraMeshData(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh);
bool BuildChunkFloraMeshData(
    const Chunk *chunk, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh,
    FloraVisualInstance **outInstances, int *outInstanceCount);
bool ChunkFloraStructureOwnsBlock(
    const Chunk *chunk, int worldX, int y, int worldZ, BlockType block);
bool ApplyFloraMeshPresenceColors(
    unsigned char *colors, const unsigned char *baseColors, int vertexCount,
    const float *targetPresence, int targetCount, float blend);
bool ApplyFloraMeshInstancePresenceColors(
    unsigned char *colors, const unsigned char *baseColors, int vertexCount,
    const float *targetPresence, const FloraVisualInstance *instances,
    int instanceCount, float blend);
bool DeformFloraMeshInstance(
    float *vertices, const float *baseVertices, int vertexCount,
    const FloraVisualInstance *instance, float targetScale, float blend,
    float sway, float windAngle, float *outScale, bool *outChanged);
void AddBlockFace(Mesh *mesh, int *vertexIndex, int x, int y, int z, int face, BlockType type, Color baseColor, float extraLight);

bool ChunksStartGenThread(void);
void ChunksShutdownGenThread(void);
bool ChunksRestartGenThreads(int requestedWorkers);
bool ChunksConfigureWorkerCount(int requestedWorkers);
int ChunksConfiguredWorkerCount(void);
int ChunksStartedWorkerCount(void);
int ChunksActiveWorkerCount(void);
void ProcessFinishedChunkJobs(void);
void ProcessFinishedMeshJobs(double uploadBudgetMs);
int GetActiveChunkCount(void);
ChunkCanonicalIdentityStats ChunksGetCanonicalIdentityStats(void);
ChunkLodStats ChunksGetLodStats(void);
ChunkLodLevel ChunkLodSelect(ChunkLodLevel current, bool initialized,
                             int distance, bool coarseAllowed);
const Model *ChunksSectionSolidModel(const Chunk *chunk,
                                     const ChunkSection *section);
const Model *ChunksSectionWaterModel(const Chunk *chunk,
                                     const ChunkSection *section);
int GetPendingGenJobCount(void);
int GetPendingMeshJobCount(void);
void ChunksResetStreamingStats(void);
ChunkStreamingStats ChunksGetStreamingStats(void);
void ChunksCaptureDiagnostics(ChunkDiagnostics *outDiagnostics);
const char *ChunkDiagnosticsStatusName(ChunkDiagnosticsStatus status);
/* Streaming diagnostics are main-thread-only snapshots. */
ChunkPreloadInfo ChunksGetPreloadInfo(void);
bool ChunksGetWaterRenderDebugInfo(Vector3 position,
                                   ChunkWaterRenderDebugInfo *outInfo);
bool ChunksGetSectionPipelineInfo(int cx, int sectionY, int cz,
                                  ChunkSectionPipelineInfo *outInfo);
const char *ChunkPipelineStageName(ChunkPipelineStage stage);
RenderResourceSnapshot ChunksGetRenderResourceSnapshot(void);
void DrainChunkGen(void);
void UpdateChunks(const ChunkStreamingRequest *request);
bool RequestChunkTerrainSection(int cx, int sectionY, int cz);
void ChunksUpdateEcologyVisuals(float dt, float daylight,
                                double simulationTime);
BlockType GetBlock(int x, int y, int z);
void RebuildDirtyChunkMeshes(Vector3 focusPosition);
bool RebuildDirtyChunkMeshAt(int x, int y, int z);
void UnloadAllChunks(void);
bool ChunkWithinDrawDistance(const Chunk *chunk, Vector3 cameraPosition, int effectiveRenderDistance);
bool ChunkIntersectsCameraView(const Chunk *chunk, const Camera3D *camera);
bool ChunkSectionIntersectsCameraView(const Chunk *chunk,
                                      const ChunkSection *section,
                                      const Camera3D *camera);
bool SphereInFrustum(const Camera3D *camera, Vector3 center, float radius);

#ifdef CHUNKS_TESTING
int ChunksTestWorkerCountForCpu(int onlineCpus, int requestedWorkers);
void ChunksTestReleaseScheduler(void);
void ChunksTestFailNextColumnAllocation(void);
Chunk *ChunksMutableForTesting(void);
bool ChunksTestSphereInFrustum(const Camera3D *camera, Vector3 center,
                               float radius, float aspect);
void ChunksTestResetScheduler(void);
void ChunksTestConfigureChunk(int slotIndex, int cx, int cz, bool loaded, bool dirty);
bool ChunksTestChunkDirty(int slotIndex);
int ChunksTestMeshJobSlot(int jobIndex);
int ChunksTestMeshJobSectionY(int jobIndex);
ChunkLodLevel ChunksTestMeshJobLod(int jobIndex);
bool ChunksTestMeshJobPriority(int jobIndex);
bool ChunksTestMeshJobPreload(int jobIndex);
int ChunksTestBuildWaterMeshJob(int jobIndex);
void ChunksTestSeedMeshJob(int jobIndex, int slotIndex, int cx, int cz,
                           int sectionY, bool done);
void ChunksTestCompleteMeshJob(int jobIndex);
void ChunksTestFillGenerationQueue(void);
int ChunksTestGenerationJobSectionY(int jobIndex);
int ChunksTestGenerationJobSlot(int jobIndex);
bool ChunksTestGenerationJobPreload(int jobIndex);
bool ChunksTestGenerationJobSurfaceAddress(
    int jobIndex, SurfaceAddress *outAddress);
void ChunksTestRunGenerationJob(int jobIndex);
int ChunksTestScheduleTerrainSections(Vector3 playerPosition,
                                      int effectiveRenderDistance);
int ChunksTestPruneTerrainSections(Vector3 playerPosition);
int ChunksTestCancelDistantSectionJobs(Vector3 playerPosition);
void ChunksTestSetGenerationJobRunning(int jobIndex, bool running);
void ChunksTestSetGenerationJobPreload(int jobIndex, bool preload);
void ChunksTestSetMeshJobRunning(int jobIndex, bool running);
void ChunksTestSetMeshJobPriority(int jobIndex, bool priority);
void ChunksTestSetMeshJobPreload(int jobIndex, bool preload);
void ChunksTestSeedGenerationJob(int jobIndex, int cx, int cz,
                                 int sectionY, bool done);
int ChunksTestNextGenerationJobIndex(void);
int ChunksTestNextMeshJobIndex(void);
bool ChunksTestEnsureChunk(int cx, int cz);
bool ChunksTestFindPendingGenerationJob(int cx, int cz);
void ChunksTestUpdateLodTargets(Vector3 focusPosition, bool coarseAllowed);

typedef struct ChunkTestBoundarySnapshot {
    unsigned short blocks[CHUNK_SIZE + 2]
                         [SURFACE_SECTION_HEIGHT + 2]
                         [CHUNK_SIZE + 2];
    unsigned char volumes[CHUNK_SIZE + 2]
                         [SURFACE_SECTION_HEIGHT + 2]
                         [CHUNK_SIZE + 2];
    unsigned char known[CHUNK_SIZE + 2]
                       [SURFACE_SECTION_HEIGHT + 2]
                       [CHUNK_SIZE + 2];
} ChunkTestBoundarySnapshot;

typedef struct ChunkTestMeshPayloadInfo {
    int solidVertices;
    int waterVertices;
    int floraVertices;
    int floraInstances;
    int lodFloraVertices;
} ChunkTestMeshPayloadInfo;

bool ChunksTestBuildMeshJobPayload(
    int jobIndex, ChunkTestMeshPayloadInfo *outInfo);

bool ChunksTestBuildSurfaceWaterMeshDataWithSnapshot(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const unsigned char *waterVolumes, int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, const ChunkTestBoundarySnapshot *boundary,
    Mesh *outMesh);
bool ChunksTestBuildMeshDataFilteredWithSnapshot(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, bool transparent, bool includePlants,
    bool plantsOnly, bool excludeWater, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount,
    const ChunkTestBoundarySnapshot *boundary, Mesh *outMesh);
bool ChunksTestSurfaceBoundaryCellAt(
    const ChunkTestBoundarySnapshot *boundary, int lx, int y, int lz,
    BlockType *outBlock, unsigned char *outVolume);
bool ChunksTestBuildLodHeightfieldMeshData(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int chunkX, int chunkZ, ChunkLodLevel lod,
    const ChunkTestBoundarySnapshot *boundary, Mesh *outMesh);
bool ChunksTestBuildLodWaterHeightfieldMeshData(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const unsigned char waterVolumes[CHUNK_SIZE]
                                    [SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int chunkX, int chunkZ, ChunkLodLevel lod,
    const ChunkTestBoundarySnapshot *boundary, Mesh *outMesh);
#endif

#endif
