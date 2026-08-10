#ifndef VOXELCRAFT_CHUNKS_H
#define VOXELCRAFT_CHUNKS_H

#include "types.h"

extern Chunk chunks[MAX_ACTIVE_CHUNKS];
extern Texture2D blockAtlas;
extern int renderDistanceChunks;
extern TerrainMode terrainMode;

bool InHeight(int y);
int FloorDivInt(int value, int divisor);
int PositiveMod(int value, int divisor);
void WorldToChunkLocal(int x, int z, int *cx, int *cz, int *lx, int *lz);

Chunk *FindChunk(int cx, int cz);
void MarkChunkDirty(int cx, int cz);
void MarkChunkDirtyAtBlock(int x, int z);
void MarkChunkAndHorizontalNeighborsDirty(int cx, int cz);

unsigned int Hash3D(int x, int y, int z);
Color ColorWithNoise(Color base, int amount, unsigned int hash);
void DrawAtlasTile(Image *image, BlockTexture texture);
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
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
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
void AddBlockFace(Mesh *mesh, int *vertexIndex, int x, int y, int z, int face, BlockType type, Color baseColor, float extraLight);
BlockTexture TextureForBlockFace(BlockType type, int face);
void AtlasUVs(BlockTexture texture, Vector2 uvs[6]);
Rectangle AtlasSourceRect(BlockTexture texture);
Texture2D LoadBlockAtlas(void);

bool ChunksStartGenThread(void);
void ChunksShutdownGenThread(void);
void ProcessFinishedChunkJobs(void);
void ProcessFinishedMeshJobs(void);
int GetActiveChunkCount(void);
int GetPendingGenJobCount(void);
int GetPendingMeshJobCount(void);
void DrainChunkGen(void);
void UpdateChunks(Vector3 playerPosition, int effectiveRenderDistance);
void ChunksUpdateEcologyVisuals(float dt, float daylight);
BlockType GetBlock(int x, int y, int z);
void RebuildDirtyChunkMeshes(void);
void UnloadAllChunks(void);
bool ChunkWithinDrawDistance(const Chunk *chunk, Vector3 cameraPosition, int effectiveRenderDistance);
bool ChunkIntersectsCameraView(const Chunk *chunk, const Camera3D *camera);
bool SphereInFrustum(const Camera3D *camera, Vector3 center, float radius);

#endif
