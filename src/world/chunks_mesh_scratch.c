#include "world/chunks_internal.h"

#define MESH_SCRATCH_POOL_SLOTS 12

typedef struct MeshScratchBuffer {
    Mesh mesh;
    int vertexCapacity;
} MeshScratchBuffer;

static _Thread_local bool meshScratchActive;
static _Thread_local MeshScratchBuffer meshScratchPool[MESH_SCRATCH_POOL_SLOTS];

static void FreeMeshArrays(Mesh *mesh)
{
    if (!mesh) return;
    free(mesh->vertices);
    free(mesh->texcoords);
    free(mesh->texcoords2);
    free(mesh->normals);
    free(mesh->colors);
    *mesh = (Mesh){ 0 };
}

static bool MeshHasAllCpuBuffers(const Mesh *mesh)
{
    return mesh && mesh->vertices && mesh->texcoords && mesh->texcoords2 &&
           mesh->normals && mesh->colors;
}

void FreeMeshData(Mesh *mesh)
{
    if (!mesh) return;
    ChunkMeshWorkerScratchReleaseMesh(mesh, mesh->vertexCount);
}

void ChunkMeshWorkerScratchBegin(void)
{
    meshScratchActive = true;
}

void ChunkMeshWorkerScratchEnd(void)
{
    meshScratchActive = false;
}

bool ChunkMeshWorkerScratchIsActive(void)
{
    return meshScratchActive;
}

bool ChunkMeshWorkerScratchAcquireMesh(Mesh *mesh, int vertexCapacity)
{
    if (!mesh || vertexCapacity <= 0) return false;
    *mesh = (Mesh){ 0 };
    if (meshScratchActive) {
        for (int index = 0; index < MESH_SCRATCH_POOL_SLOTS; index++) {
            MeshScratchBuffer *slot = &meshScratchPool[index];
            if (slot->vertexCapacity < vertexCapacity ||
                !MeshHasAllCpuBuffers(&slot->mesh)) continue;
            *mesh = slot->mesh;
            mesh->vertexCount = vertexCapacity;
            mesh->triangleCount = vertexCapacity / 3;
            *slot = (MeshScratchBuffer){ 0 };
            return true;
        }
    }

    mesh->vertices = malloc((size_t)vertexCapacity * 3u * sizeof(float));
    mesh->texcoords = malloc((size_t)vertexCapacity * 2u * sizeof(float));
    mesh->texcoords2 = malloc((size_t)vertexCapacity * 2u * sizeof(float));
    mesh->normals = malloc((size_t)vertexCapacity * 3u * sizeof(float));
    mesh->colors = malloc((size_t)vertexCapacity * 4u * sizeof(unsigned char));
    if (!MeshHasAllCpuBuffers(mesh)) {
        FreeMeshArrays(mesh);
        return false;
    }
    mesh->vertexCount = vertexCapacity;
    mesh->triangleCount = vertexCapacity / 3;
    return true;
}

void ChunkMeshWorkerScratchReleaseMesh(Mesh *mesh, int vertexCapacity)
{
    if (!mesh) return;
    if (!meshScratchActive || !MeshHasAllCpuBuffers(mesh)) {
        FreeMeshArrays(mesh);
        return;
    }

    int capacity = vertexCapacity > 0 ? vertexCapacity : mesh->vertexCount;
    if (capacity <= 0) {
        FreeMeshArrays(mesh);
        return;
    }
    int replacement = -1;
    for (int index = 0; index < MESH_SCRATCH_POOL_SLOTS; index++) {
        MeshScratchBuffer *slot = &meshScratchPool[index];
        if (!MeshHasAllCpuBuffers(&slot->mesh)) {
            replacement = index;
            break;
        }
        if (slot->vertexCapacity >= capacity) {
            FreeMeshArrays(mesh);
            return;
        }
        if (replacement < 0) replacement = index;
    }
    if (replacement < 0) {
        FreeMeshArrays(mesh);
        return;
    }
    MeshScratchBuffer *slot = &meshScratchPool[replacement];
    FreeMeshArrays(&slot->mesh);
    slot->mesh = *mesh;
    slot->vertexCapacity = capacity;
    *mesh = (Mesh){ 0 };
}

void ChunkMeshWorkerScratchShutdown(void)
{
    for (int index = 0; index < MESH_SCRATCH_POOL_SLOTS; index++) {
        FreeMeshArrays(&meshScratchPool[index].mesh);
        meshScratchPool[index].vertexCapacity = 0;
    }
    ChunkGreedyMeshWorkerScratchShutdown();
    ChunkFloraWorkerScratchShutdown();
    meshScratchActive = false;
}
