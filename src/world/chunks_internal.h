#ifndef VOXELCRAFT_CHUNKS_INTERNAL_H
#define VOXELCRAFT_CHUNKS_INTERNAL_H

#include "world/chunks_dependencies.h"

#define SECTION_GEN_SUBMISSIONS_PER_FRAME 8
#define NEGATIVE_TERRAIN_SECTION_RETAIN_RADIUS 6
#define MAX_MESH_JOBS MAX_CHUNK_MESH_JOBS
#define MAX_MESH_SUBMITS_PER_FRAME 4

typedef struct SurfaceWaterBoundarySnapshot {
    unsigned short xBlocks[2][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    unsigned char xVolumes[2][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    unsigned short zBlocks[2][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    unsigned char zVolumes[2][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    unsigned short yBlocks[2][CHUNK_SIZE][CHUNK_SIZE];
    unsigned char yVolumes[2][CHUNK_SIZE][CHUNK_SIZE];
} SurfaceWaterBoundarySnapshot;

typedef struct MeshJob {
    bool inUse;
    bool running;
    bool done;
    int slotIndex;
    int cx;
    int cz;
    int sectionY;
    uint32_t sectionStamp;
    uint32_t chunkGeneration;
    unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    unsigned char waterVolumes[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    SurfaceWaterBoundarySnapshot waterBoundary;
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
extern pthread_t genThread;
extern bool genShutdown;
extern bool genWorkerActive;
extern ChunkStreamingStats streamingStats;
extern MeshJob meshJobs[MAX_MESH_JOBS];

double ChunkNowMs(void);
bool NegativeTerrainSectionOutsideWindow(int sectionY, int playerSectionY);
int ChunkSectionLowerBound(const Chunk *chunk, int sectionY);
int ResolvedTerrainSectionLowerBound(const Chunk *chunk, int sectionY);
void ClearSectionFloraRuntime(ChunkSection *section);
void FreeChunkSectionStorage(ChunkSection *section);
void MarkSectionDirty(ChunkSection *section);
void *ChunkGenWorker(void *arg);
void GenerateChunkJobPayload(ChunkGenJob *job);
int ScheduleNearbyTerrainSections(Vector3 playerPosition);
void UpdateQueuePeaksLocked(void);
void MarkGeneratedSectionAndNeighborsDirty(Chunk *chunk, int sectionY);
int CancelDistantNegativeSectionJobs(int playerSectionY);
int PruneDistantNegativeTerrainSections(int playerSectionY);
bool HasPendingMeshJob(void);
MeshJob *NextPendingMeshJob(void);
void FreeMeshData(Mesh *mesh);
bool MergeMeshData(Mesh *target, Mesh *source);
bool BuildSurfaceWaterMeshDataWithSnapshot(
    const unsigned short (*blocks)[CHUNK_SIZE],
    const unsigned char *waterVolumes, int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount,
    const SurfaceWaterBoundarySnapshot *boundary, Mesh *outMesh);
bool BuildChunkSurfaceSolidMeshData(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, Mesh *outMesh);
bool BuildChunkFloraMeshDataFromSnapshot(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, Mesh *outMesh,
    FloraVisualInstance **outInstances, int *outInstanceCount);
bool BuildMeshDataFiltered(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, bool transparent, bool includePlants,
    bool plantsOnly, bool excludeWater, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh);
void CaptureSurfaceWaterBoundary(
    SurfaceWaterBoundarySnapshot *snapshot, int chunkX, int chunkZ,
    int sectionY);
Color ShadeColor(Color color, float brightness);
void AddMeshFaceLighting(ChunkMeshEmitter *emitter,
                         Vector3 corners[6], Vector3 normal,
                         Vector2 uvs[6], Color color,
                         const float ambientOcclusion[6],
                         float localLight);
void CountMeshFace(ChunkMeshEmitter *emitter);

#endif
