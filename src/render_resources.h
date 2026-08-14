#ifndef VOXELCRAFT_RENDER_RESOURCES_H
#define VOXELCRAFT_RENDER_RESOURCES_H

#include "raylib.h"

#include <stdint.h>

typedef enum RenderResourceModelKind {
    RENDER_RESOURCE_SOLID = 0,
    RENDER_RESOURCE_FLORA,
    RENDER_RESOURCE_TRANSPARENT
} RenderResourceModelKind;

typedef struct RenderResourceSnapshot {
    uint64_t solidModels;
    uint64_t floraModels;
    uint64_t transparentModels;
    uint64_t meshVertices;
    uint64_t meshIndices;
    uint64_t estimatedMeshBytes;
    uint64_t worldLightingTextureBytes;
    uint64_t pendingMeshSnapshotBytes;
    uint64_t workerThreadsConfigured;
    uint64_t workerThreadsStarted;
    uint64_t workerThreadsActive;
} RenderResourceSnapshot;

uint64_t RenderMeshEstimatedBytes(const Mesh *mesh);
void RenderResourceSnapshotAddModel(RenderResourceSnapshot *snapshot,
                                    const Model *model,
                                    RenderResourceModelKind kind);
void RenderResourceSnapshotMerge(RenderResourceSnapshot *target,
                                 RenderResourceSnapshot source);
void RenderResourceSnapshotMax(RenderResourceSnapshot *target,
                               RenderResourceSnapshot value);

#endif
