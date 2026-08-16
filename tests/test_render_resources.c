#include "presentation/render_resources.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void TestMeshByteMath(void)
{
    float vertices[9] = { 0 };
    float texcoords[6] = { 0 };
    float texcoords2[6] = { 0 };
    float normals[9] = { 0 };
    unsigned char colors[12] = { 0 };
    unsigned short indices[3] = { 0 };
    Mesh mesh = {
        .vertexCount = 3,
        .triangleCount = 1,
        .vertices = vertices,
        .texcoords = texcoords,
        .texcoords2 = texcoords2,
        .normals = normals,
        .colors = colors,
        .indices = indices
    };
    uint64_t expected = 9u * sizeof(float) + 12u * sizeof(float) +
                        9u * sizeof(float) + 12u + 3u * sizeof(unsigned short);
    assert(RenderMeshEstimatedBytes(&mesh) == expected);
    mesh.texcoords = NULL;
    assert(RenderMeshEstimatedBytes(&mesh) == expected - 6u * sizeof(float));
}

static void TestModelCountsAndMerge(void)
{
    float vertices[18] = { 0 };
    Mesh meshes[2] = {
        { .vertexCount = 3, .vertices = vertices },
        { .vertexCount = 3, .vertices = vertices + 9 }
    };
    Model model = { .meshCount = 2, .meshes = meshes };
    RenderResourceSnapshot snapshot = { 0 };
    RenderResourceSnapshotAddModel(&snapshot, &model, RENDER_RESOURCE_TRANSPARENT);
    assert(snapshot.transparentModels == 1);
    assert(snapshot.meshVertices == 6);
    assert(snapshot.estimatedMeshBytes == 18u * sizeof(float));

    RenderResourceSnapshot other = {
        .solidModels = 2,
        .estimatedMeshBytes = 16,
        .worldLightingTextureBytes = 4096,
        .workerThreadsStarted = 1
    };
    RenderResourceSnapshotMerge(&snapshot, other);
    assert(snapshot.solidModels == 2);
    assert(snapshot.estimatedMeshBytes == 18u * sizeof(float) + 16u);
    assert(snapshot.worldLightingTextureBytes == 4096);
    RenderResourceSnapshotMax(&other, snapshot);
    assert(other.transparentModels == 1);
    assert(other.workerThreadsStarted == 1);
}

static void TestEmptyAndOverflowSafe(void)
{
    Mesh empty = { 0 };
    assert(RenderMeshEstimatedBytes(NULL) == 0);
    assert(RenderMeshEstimatedBytes(&empty) == 0);
    RenderResourceSnapshot snapshot = { .estimatedMeshBytes = UINT64_MAX - 1 };
    RenderResourceSnapshotMerge(&snapshot,
        (RenderResourceSnapshot){ .estimatedMeshBytes = 42 });
    assert(snapshot.estimatedMeshBytes == UINT64_MAX);
}

int main(void)
{
    TestMeshByteMath();
    TestModelCountsAndMerge();
    TestEmptyAndOverflowSafe();
    puts("render resource estimate tests passed");
    return 0;
}
