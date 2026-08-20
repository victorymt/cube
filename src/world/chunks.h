#ifndef VOXELCRAFT_CHUNKS_H
#define VOXELCRAFT_CHUNKS_H

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
void ProcessFinishedChunkJobs(void);
void ProcessFinishedMeshJobs(double uploadBudgetMs);
int GetActiveChunkCount(void);
ChunkCanonicalIdentityStats ChunksGetCanonicalIdentityStats(void);
int GetPendingGenJobCount(void);
int GetPendingMeshJobCount(void);
void ChunksResetStreamingStats(void);
ChunkStreamingStats ChunksGetStreamingStats(void);
bool ChunksGetWaterRenderDebugInfo(Vector3 position,
                                   ChunkWaterRenderDebugInfo *outInfo);
bool ChunksGetSectionPipelineInfo(int cx, int sectionY, int cz,
                                  ChunkSectionPipelineInfo *outInfo);
const char *ChunkPipelineStageName(ChunkPipelineStage stage);
RenderResourceSnapshot ChunksGetRenderResourceSnapshot(void);
void DrainChunkGen(void);
void UpdateChunks(Vector3 playerPosition, int effectiveRenderDistance);
bool RequestChunkTerrainSection(int cx, int sectionY, int cz);
void ChunksUpdateEcologyVisuals(float dt, float daylight);
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
Chunk *ChunksMutableForTesting(void);
bool ChunksTestSphereInFrustum(const Camera3D *camera, Vector3 center,
                               float radius, float aspect);
void ChunksTestResetScheduler(void);
void ChunksTestConfigureChunk(int slotIndex, int cx, int cz, bool loaded, bool dirty);
bool ChunksTestChunkDirty(int slotIndex);
int ChunksTestMeshJobSlot(int jobIndex);
int ChunksTestMeshJobSectionY(int jobIndex);
bool ChunksTestMeshJobPriority(int jobIndex);
int ChunksTestBuildWaterMeshJob(int jobIndex);
void ChunksTestSeedMeshJob(int jobIndex, int slotIndex, int cx, int cz,
                           int sectionY, bool done);
void ChunksTestCompleteMeshJob(int jobIndex);
void ChunksTestFillGenerationQueue(void);
int ChunksTestGenerationJobSectionY(int jobIndex);
int ChunksTestGenerationJobSlot(int jobIndex);
bool ChunksTestGenerationJobSurfaceAddress(
    int jobIndex, SurfaceAddress *outAddress);
void ChunksTestRunGenerationJob(int jobIndex);
int ChunksTestScheduleTerrainSections(Vector3 playerPosition,
                                      int effectiveRenderDistance);
int ChunksTestPruneTerrainSections(Vector3 playerPosition);
int ChunksTestCancelDistantSectionJobs(Vector3 playerPosition);
void ChunksTestSetGenerationJobRunning(int jobIndex, bool running);
void ChunksTestSetMeshJobRunning(int jobIndex, bool running);
void ChunksTestSetMeshJobPriority(int jobIndex, bool priority);
void ChunksTestSeedGenerationJob(int jobIndex, int cx, int cz,
                                 int sectionY, bool done);
int ChunksTestNextGenerationJobIndex(void);
int ChunksTestNextMeshJobIndex(void);
bool ChunksTestEnsureChunk(int cx, int cz);
bool ChunksTestFindPendingGenerationJob(int cx, int cz);

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
#endif

#endif
