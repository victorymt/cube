#ifndef VOXELCRAFT_PERF_METRICS_H
#define VOXELCRAFT_PERF_METRICS_H

#include <stdint.h>

typedef struct PerfStreamingStats {
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
} PerfStreamingStats;

typedef struct PerfResourceSnapshot {
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
} PerfResourceSnapshot;

typedef PerfStreamingStats ChunkStreamingStats;
typedef PerfResourceSnapshot RenderResourceSnapshot;

#endif
