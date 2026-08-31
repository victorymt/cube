#ifndef VOXELCRAFT_PERF_METRICS_H
#define VOXELCRAFT_PERF_METRICS_H

#include "core/render_resources.h"

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

typedef RenderResourceSnapshot PerfResourceSnapshot;
typedef PerfStreamingStats ChunkStreamingStats;

#endif
