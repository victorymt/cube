#include "core/render_resources.h"

#include <limits.h>
#include <stddef.h>

/* raylib 6.x splits skinning data: Mesh.boneIds became Mesh.boneIndices and
 * Mesh.boneMatrices moved to Model.boneMatrices. The build is not pinned to one
 * raylib, so name the fields through compat aliases instead of hard-coding
 * either layout. RenderMeshEstimatedBytes only sees a Mesh, so the 6.x bone
 * matrices are accounted for by the caller, which has the Model. */
#if defined(RAYLIB_VERSION_MAJOR) && RAYLIB_VERSION_MAJOR >= 6
#define VOXELCRAFT_MESH_BONE_IDS(mesh) ((mesh)->boneIndices)
#define VOXELCRAFT_MESH_BONE_MATRICES(mesh) ((const Matrix *)NULL)
#define VOXELCRAFT_MODEL_BONE_MATRICES(model) ((model)->boneMatrices)
#define VOXELCRAFT_MODEL_BONE_COUNT(model) ((model)->skeleton.boneCount)
#define VOXELCRAFT_MESH_HAS_BONE_MATRICES 0
#else
#define VOXELCRAFT_MESH_BONE_IDS(mesh) ((mesh)->boneIds)
#define VOXELCRAFT_MESH_BONE_MATRICES(mesh) ((mesh)->boneMatrices)
#define VOXELCRAFT_MODEL_BONE_MATRICES(model) ((const Matrix *)NULL)
#define VOXELCRAFT_MODEL_BONE_COUNT(model) 0u
#define VOXELCRAFT_MESH_HAS_BONE_MATRICES 1
#endif

static uint64_t SaturatingAdd(uint64_t left, uint64_t right)
{
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static uint64_t SaturatingMultiply(uint64_t left, uint64_t right)
{
    if (left == 0 || right == 0) return 0;
    return left > UINT64_MAX / right ? UINT64_MAX : left * right;
}

static void AddBufferBytes(uint64_t *total, const void *buffer,
                           uint64_t count, uint64_t elementSize)
{
    if (!buffer) return;
    *total = SaturatingAdd(*total, SaturatingMultiply(count, elementSize));
}

uint64_t RenderMeshEstimatedBytes(const Mesh *mesh)
{
    if (!mesh || mesh->vertexCount <= 0) return 0;
    uint64_t vertices = (uint64_t)mesh->vertexCount;
    uint64_t bytes = 0;
    AddBufferBytes(&bytes, mesh->vertices, vertices * 3u, sizeof(float));
    AddBufferBytes(&bytes, mesh->texcoords, vertices * 2u, sizeof(float));
    AddBufferBytes(&bytes, mesh->texcoords2, vertices * 2u, sizeof(float));
    AddBufferBytes(&bytes, mesh->normals, vertices * 3u, sizeof(float));
    AddBufferBytes(&bytes, mesh->tangents, vertices * 4u, sizeof(float));
    AddBufferBytes(&bytes, mesh->colors, vertices * 4u, sizeof(unsigned char));
    if (mesh->triangleCount > 0) {
        AddBufferBytes(&bytes, mesh->indices,
                       (uint64_t)mesh->triangleCount * 3u,
                       sizeof(unsigned short));
    }
    AddBufferBytes(&bytes, mesh->animVertices, vertices * 3u, sizeof(float));
    AddBufferBytes(&bytes, mesh->animNormals, vertices * 3u, sizeof(float));
    AddBufferBytes(&bytes, VOXELCRAFT_MESH_BONE_IDS(mesh), vertices * 4u,
                   sizeof(unsigned char));
    AddBufferBytes(&bytes, mesh->boneWeights, vertices * 4u, sizeof(float));
    if (mesh->boneCount > 0) {
        AddBufferBytes(&bytes, VOXELCRAFT_MESH_BONE_MATRICES(mesh),
                       (uint64_t)mesh->boneCount, sizeof(Matrix));
    }
    return bytes;
}

void RenderResourceSnapshotAddModel(RenderResourceSnapshot *snapshot,
                                    const Model *model,
                                    RenderResourceModelKind kind)
{
    if (!snapshot || !model) return;
    if (kind == RENDER_RESOURCE_SOLID) snapshot->solidModels++;
    else if (kind == RENDER_RESOURCE_FLORA) snapshot->floraModels++;
    else snapshot->transparentModels++;

    if (!model->meshes || model->meshCount <= 0) return;
    for (int i = 0; i < model->meshCount; i++) {
        const Mesh *mesh = &model->meshes[i];
        if (mesh->vertexCount > 0) {
            snapshot->meshVertices = SaturatingAdd(
                snapshot->meshVertices, (uint64_t)mesh->vertexCount);
        }
        if (mesh->indices && mesh->triangleCount > 0) {
            snapshot->meshIndices = SaturatingAdd(
                snapshot->meshIndices, (uint64_t)mesh->triangleCount * 3u);
        }
        snapshot->estimatedMeshBytes = SaturatingAdd(
            snapshot->estimatedMeshBytes, RenderMeshEstimatedBytes(mesh));
    }
#if !VOXELCRAFT_MESH_HAS_BONE_MATRICES
    /* raylib 6.x keeps the bone matrices on the Model, not on each Mesh, so
     * account for them once here to keep the estimate comparable to 5.x. */
    if (VOXELCRAFT_MODEL_BONE_COUNT(model) > 0) {
        AddBufferBytes(&snapshot->estimatedMeshBytes,
                       VOXELCRAFT_MODEL_BONE_MATRICES(model),
                       (uint64_t)VOXELCRAFT_MODEL_BONE_COUNT(model),
                       sizeof(Matrix));
    }
#endif
}

void RenderResourceSnapshotMerge(RenderResourceSnapshot *target,
                                 RenderResourceSnapshot source)
{
    if (!target) return;
#define MERGE_FIELD(field) target->field = SaturatingAdd(target->field, source.field)
    MERGE_FIELD(solidModels);
    MERGE_FIELD(floraModels);
    MERGE_FIELD(transparentModels);
    MERGE_FIELD(meshVertices);
    MERGE_FIELD(meshIndices);
    MERGE_FIELD(estimatedMeshBytes);
    MERGE_FIELD(worldLightingTextureBytes);
    MERGE_FIELD(pendingMeshSnapshotBytes);
    MERGE_FIELD(workerThreadsConfigured);
    MERGE_FIELD(workerThreadsStarted);
    MERGE_FIELD(workerThreadsActive);
#undef MERGE_FIELD
}

void RenderResourceSnapshotMax(RenderResourceSnapshot *target,
                               RenderResourceSnapshot value)
{
    if (!target) return;
#define MAX_FIELD(field) if (value.field > target->field) target->field = value.field
    MAX_FIELD(solidModels);
    MAX_FIELD(floraModels);
    MAX_FIELD(transparentModels);
    MAX_FIELD(meshVertices);
    MAX_FIELD(meshIndices);
    MAX_FIELD(estimatedMeshBytes);
    MAX_FIELD(worldLightingTextureBytes);
    MAX_FIELD(pendingMeshSnapshotBytes);
    MAX_FIELD(workerThreadsConfigured);
    MAX_FIELD(workerThreadsStarted);
    MAX_FIELD(workerThreadsActive);
#undef MAX_FIELD
}
