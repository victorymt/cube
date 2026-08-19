#ifndef VOXELCRAFT_PERF_H
#define VOXELCRAFT_PERF_H

#include "core/config.h"
#include "core/perf_metrics.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum PerfDrawKind {
    PERF_DRAW_SOLID = 0,
    PERF_DRAW_WATER,
    PERF_DRAW_FLORA,
    PERF_DRAW_SPACE,
    PERF_DRAW_NETHER,
    PERF_DRAW_CLOUD,
    PERF_DRAW_FIRE,
    PERF_DRAW_KIND_COUNT
} PerfDrawKind;

void PerfConfigure(bool enabled, const char *reportPath, const char *baselinePath);
void PerfSetMetadata(uint32_t seed, int effectiveRenderDistance);
bool PerfIsEnabled(void);
int PerfFrameIndex(void);
bool PerfRouteComplete(void);
void PerfBeginFrame(void);
void PerfMarkUpdateComplete(void);
void PerfBeginGpuFrame(void);
void PerfEndGpuFrame(void);
void PerfRecordWorldCandidate(bool distanceVisible, bool frustumVisible);
void PerfRecordDrawCall(PerfDrawKind kind);
void PerfEndFrame(PerfStreamingStats streaming, PerfResourceSnapshot resources);
bool PerfReportWritten(void);
bool PerfReportPassed(void);
void PerfShutdown(void);

#endif
