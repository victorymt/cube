#ifndef VOXELCRAFT_RENDER_RESOURCES_H
#define VOXELCRAFT_RENDER_RESOURCES_H

#include "core/perf_metrics.h"

#include "raylib.h"

#include <stdint.h>

typedef enum RenderResourceModelKind {
    RENDER_RESOURCE_SOLID = 0,
    RENDER_RESOURCE_FLORA,
    RENDER_RESOURCE_TRANSPARENT
} RenderResourceModelKind;

uint64_t RenderMeshEstimatedBytes(const Mesh *mesh);
void RenderResourceSnapshotAddModel(RenderResourceSnapshot *snapshot,
                                    const Model *model,
                                    RenderResourceModelKind kind);
void RenderResourceSnapshotMerge(RenderResourceSnapshot *target,
                                 RenderResourceSnapshot source);
void RenderResourceSnapshotMax(RenderResourceSnapshot *target,
                               RenderResourceSnapshot value);

#endif
