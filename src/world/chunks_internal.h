#ifndef VOXELCRAFT_CHUNKS_INTERNAL_H
#define VOXELCRAFT_CHUNKS_INTERNAL_H

#include "world/chunks_dependencies.h"

#define SECTION_GEN_SUBMISSIONS_PER_FRAME 8
#define NEGATIVE_TERRAIN_SECTION_RETAIN_RADIUS 6
#define MAX_MESH_JOBS MAX_CHUNK_MESH_JOBS
#define MAX_MESH_SUBMITS_PER_FRAME 4
#define MESH_PENDING_LOOKUP_CAPACITY (MAX_MESH_JOBS * 2)

typedef struct SurfaceBoundarySnapshot {
    unsigned short blocks[CHUNK_SIZE + 2]
                         [SURFACE_SECTION_HEIGHT + 2]
                         [CHUNK_SIZE + 2];
    unsigned char volumes[CHUNK_SIZE + 2]
                         [SURFACE_SECTION_HEIGHT + 2]
                         [CHUNK_SIZE + 2];
    unsigned char known[CHUNK_SIZE + 2]
                       [SURFACE_SECTION_HEIGHT + 2]
                       [CHUNK_SIZE + 2];
} SurfaceBoundarySnapshot;

typedef struct MeshJob {
    bool inUse;
    bool running;
    bool done;
    bool priority;
    int slotIndex;
    int cx;
    int cz;
    int sectionY;
    ChunkLodLevel lod;
    uint32_t sectionStamp;
    uint32_t chunkGeneration;
    uint64_t queueSequence;
    double submittedAtMs;
    double startedAtMs;
    double completedAtMs;
    bool spherical;
    SurfaceAddress surfaceAddress;
    SurfaceChunkKey surfaceKey;
    int surfaceMapOriginX;
    int surfaceMapOriginZ;
    unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    unsigned char waterVolumes[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    SurfaceBoundarySnapshot boundary;
    FloraStructureInstance floraStructures[MAX_CHUNK_FLORA_STRUCTURES];
    int floraStructureCount;
    int nearbyIndices[MAX_TORCH_LIGHTS];
    int nearbyCount;
    Mesh mesh;
    Mesh waterMesh;
    Mesh floraMesh;
    FloraVisualInstance *floraInstances;
    int floraInstanceCount;
    bool hasMesh;
    bool hasWaterMesh;
    bool hasFloraMesh;
} MeshJob;

typedef struct MeshPendingLookup {
    int slots[MESH_PENDING_LOOKUP_CAPACITY];
    int ys[MESH_PENDING_LOOKUP_CAPACITY];
    ChunkLodLevel lods[MESH_PENDING_LOOKUP_CAPACITY];
    bool used[MESH_PENDING_LOOKUP_CAPACITY];
} MeshPendingLookup;

typedef struct ChunkMeshEmitter {
    Mesh *mesh;
    int vertexIndex;
    int vertexCapacity;
    bool dynamicCapacity;
    bool failed;
} ChunkMeshEmitter;

extern ChunkGenJob chunkGenJobs[MAX_CHUNK_GEN_JOBS];
extern Chunk chunks[MAX_ACTIVE_CHUNKS];
extern Texture2D blockAtlas;
extern int renderDistanceChunks;
extern pthread_mutex_t genMutex;
extern pthread_cond_t genCond;
extern pthread_cond_t genCompletionCond;
extern pthread_t genThreads[MAX_CHUNK_WORKER_THREADS];
extern bool genShutdown;
extern unsigned int genWorkerThreadsConfigured;
extern unsigned int genWorkerThreadsStarted;
extern unsigned int genWorkerThreadsActive;
extern ChunkStreamingStats streamingStats;
extern MeshJob meshJobs[MAX_MESH_JOBS];

double ChunkNowMs(void);
double ChunkThreadCpuNowMs(void);
bool NegativeTerrainSectionOutsideWindow(int sectionY, int playerSectionY);
int ChunkSectionLowerBound(const Chunk *chunk, int sectionY);
int ResolvedTerrainSectionLowerBound(const Chunk *chunk, int sectionY);
void ClearSectionFloraRuntime(ChunkSection *section);
void InitializeFloraTargets(
    ChunkSection *section, const FloraVisualInstance *sourceInstances,
    int sourceInstanceCount);
void FreeChunkSectionStorage(ChunkSection *section);
void ReplaceChunkModel(Model *model, bool *hasModel,
                       Mesh *mesh, bool hasMesh, bool dynamic);
void MarkSectionDirty(ChunkSection *section);
void *ChunkGenWorker(void *arg);
void GenerateChunkJobPayload(ChunkGenJob *job);
void FreeChunkGenJobResult(ChunkGenJob *job);
int ScheduleNearbyTerrainSections(Vector3 playerPosition,
                                  int effectiveRenderDistance);
void UpdateQueuePeaksLocked(void);
void MarkGeneratedSectionAndNeighborsDirty(Chunk *chunk, int sectionY);
ChunkLodLevel ChunkLodSanitize(ChunkLodLevel lod);
bool ChunkSectionLodReady(const ChunkSection *section, ChunkLodLevel lod);
void ChunkRefreshActiveLod(Chunk *chunk);
bool MeshPendingLookupVisit(MeshPendingLookup *lookup, int slot, int y,
                            ChunkLodLevel lod, bool insert);
void ChunksUpdateLodTargets(Vector3 focusPosition, bool coarseAllowed);
void ChunksResetLodState(void);
void RebuildChunkSectionMeshSync(Chunk *chunk, ChunkSection *section);
int CancelDistantNegativeSectionJobs(int playerSectionY);
int PruneDistantNegativeTerrainSections(int playerSectionY);
bool HasPendingMeshJob(void);
bool HasPendingPriorityMeshJob(void);
MeshJob *NextPendingMeshJob(void);
void FreeMeshData(Mesh *mesh);
void LocalizeChunkMeshData(Mesh *mesh, int chunkX, int chunkZ);
void LocalizeChunkFloraInstances(FloraVisualInstance *instances, int count,
                                 int chunkX, int chunkZ);
void CurveChunkMeshData(Mesh *mesh, int chunkX, int chunkZ, int sectionY,
                        uint32_t bodyId, int mapOriginX, int mapOriginZ);
void CurveChunkFloraInstances(
    FloraVisualInstance *instances, int count, int chunkX, int chunkZ,
    int sectionY, uint32_t bodyId, int mapOriginX, int mapOriginZ);
bool MergeMeshData(Mesh *target, Mesh *source);
bool BuildSurfaceWaterMeshDataWithSnapshot(
    const unsigned short (*blocks)[CHUNK_SIZE],
    const unsigned char *waterVolumes, int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount,
    const SurfaceBoundarySnapshot *boundary, Mesh *outMesh);
bool BuildChunkSurfaceSolidMeshData(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, int greedyMaxSpan,
    const SurfaceBoundarySnapshot *boundary,
    Mesh *outMesh);
bool BuildChunkLodHeightfieldMeshData(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int chunkX, int chunkZ, ChunkLodLevel lod,
    const SurfaceBoundarySnapshot *boundary, Mesh *outMesh);
bool BuildChunkLodWaterHeightfieldMeshData(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const unsigned char waterVolumes[CHUNK_SIZE]
                                    [SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int chunkX, int chunkZ, ChunkLodLevel lod,
    const SurfaceBoundarySnapshot *boundary, Mesh *outMesh);
void BuildMeshJobPayload(MeshJob *job);
bool BuildChunkSurfaceWaterMeshDataWithSnapshot(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const unsigned char *waterVolumes, int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, const SurfaceBoundarySnapshot *boundary,
    Mesh *outMesh);
bool BuildChunkFloraMeshDataFromSnapshot(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, const SurfaceBoundarySnapshot *boundary,
    Mesh *outMesh,
    FloraVisualInstance **outInstances, int *outInstanceCount);
bool BuildMeshDataFilteredWithSnapshot(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, bool transparent, bool includePlants,
    bool plantsOnly, bool excludeWater, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount,
    const SurfaceBoundarySnapshot *boundary, Mesh *outMesh);
bool BuildMeshDataFilteredWithSnapshotSpan(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, bool transparent, bool includePlants,
    bool plantsOnly, bool excludeWater, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount,
    int greedyMaxSpan, const SurfaceBoundarySnapshot *boundary,
    Mesh *outMesh);
bool BuildMeshDataFiltered(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, bool transparent, bool includePlants,
    bool plantsOnly, bool excludeWater, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh);
void CaptureSurfaceBoundary(SurfaceBoundarySnapshot *snapshot,
                            int chunkX, int chunkZ, int sectionY);
bool SurfaceBoundaryBlockAt(const SurfaceBoundarySnapshot *snapshot,
                            int lx, int y, int lz, BlockType *outBlock);
bool SurfaceBoundaryCellAt(const SurfaceBoundarySnapshot *snapshot,
                           int lx, int y, int lz, BlockType *outBlock,
                           unsigned char *outVolume);
Color ShadeColor(Color color, float brightness);
void AddMeshFaceLighting(ChunkMeshEmitter *emitter,
                         Vector3 corners[6], Vector3 normal,
                         Vector2 uvs[6], Color color,
                         const float ambientOcclusion[6],
                         float localLight);
void AddPlantMesh(ChunkMeshEmitter *emitter, int x, int y, int z,
                  BlockType type, float extraLight);
void CountMeshFace(ChunkMeshEmitter *emitter);
bool ChunkFaceIsVisibleWithSnapshot(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const SurfaceBoundarySnapshot *boundary,
    int lx, int y, int lz, int nx, int ny, int nz);
float BlockCornerAmbientOcclusion(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const SurfaceBoundarySnapshot *boundary,
    int x, int y, int z, Vector3 normal, Vector3 corner);
bool BlockUsesGreedyCubeMesh(BlockType type);
void EmitGreedyCubeFaces(
    ChunkMeshEmitter *emitter,
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const SurfaceBoundarySnapshot *boundary,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, int maximumSpan);

#endif
