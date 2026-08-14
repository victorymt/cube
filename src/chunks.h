#ifndef VOXELCRAFT_CHUNKS_H
#define VOXELCRAFT_CHUNKS_H

#include "types.h"
#include "render_resources.h"

#define MAX_CHUNK_MESH_JOBS 64

extern Chunk chunks[MAX_ACTIVE_CHUNKS];
extern Texture2D blockAtlas;
extern int renderDistanceChunks;
extern TerrainMode terrainMode;

typedef struct ChunkStreamingStats {
    uint64_t generationSubmitted;
    uint64_t generationCompleted;
    uint64_t generationCanceled;
    uint64_t meshSubmitted;
    uint64_t meshCompleted;
    uint64_t meshCanceled;
    uint64_t meshSnapshotBytes;
    uint64_t syncRebuilds;
    uint64_t uploadedMeshes;
    uint64_t uploadBudgetDeferrals;
    uint64_t generationQueuePeak;
    uint64_t meshQueuePeak;
    uint64_t pendingMeshSnapshotBytes;
    uint64_t pendingMeshSnapshotBytesPeak;
    double generationCpuMs;
    double meshCpuMs;
    double uploadCpuMs;
    double maxUploadCpuMs;
} ChunkStreamingStats;

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

bool InHeight(int y);
int FloorDivInt(int value, int divisor);
int PositiveMod(int value, int divisor);
void WorldToChunkLocal(int x, int z, int *cx, int *cz, int *lx, int *lz);

Chunk *FindChunk(int cx, int cz);
ChunkSection *ChunkGetSection(Chunk *chunk, int sectionY, bool create);
const ChunkSection *ChunkGetSectionConst(const Chunk *chunk, int sectionY);
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
int GetPendingGenJobCount(void);
int GetPendingMeshJobCount(void);
void ChunksResetStreamingStats(void);
ChunkStreamingStats ChunksGetStreamingStats(void);
bool ChunksGetWaterRenderDebugInfo(Vector3 position,
                                   ChunkWaterRenderDebugInfo *outInfo);
RenderResourceSnapshot ChunksGetRenderResourceSnapshot(void);
void DrainChunkGen(void);
void UpdateChunks(Vector3 playerPosition, int effectiveRenderDistance);
void ChunksUpdateEcologyVisuals(float dt, float daylight);
BlockType GetBlock(int x, int y, int z);
void RebuildDirtyChunkMeshes(Vector3 focusPosition);
void UnloadAllChunks(void);
bool ChunkWithinDrawDistance(const Chunk *chunk, Vector3 cameraPosition, int effectiveRenderDistance);
bool ChunkIntersectsCameraView(const Chunk *chunk, const Camera3D *camera);
bool ChunkSectionIntersectsCameraView(const Chunk *chunk,
                                      const ChunkSection *section,
                                      const Camera3D *camera);
bool SphereInFrustum(const Camera3D *camera, Vector3 center, float radius);

#ifdef CHUNKS_TESTING
void ChunksTestResetScheduler(void);
void ChunksTestConfigureChunk(int slotIndex, int cx, int cz, bool loaded, bool dirty);
bool ChunksTestChunkDirty(int slotIndex);
int ChunksTestMeshJobSlot(int jobIndex);
int ChunksTestMeshJobSectionY(int jobIndex);
int ChunksTestBuildWaterMeshJob(int jobIndex);
void ChunksTestSeedMeshJob(int jobIndex, int slotIndex, int cx, int cz, bool done);
void ChunksTestCompleteMeshJob(int jobIndex);
void ChunksTestFillGenerationQueue(void);
#endif

#endif
