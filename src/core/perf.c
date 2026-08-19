#include "core/perf.h"

#include "core/config.h"

#include "raylib.h"
#include "rlgl.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PERF_VERSION 3
#ifdef PERF_TESTING
#define PERF_WARMUP_FRAMES 2
#define PERF_SAMPLE_FRAMES 4
#define PERF_STABILITY_FRAMES 2
#else
#define PERF_WARMUP_FRAMES 120
#define PERF_SAMPLE_FRAMES 600
#define PERF_STABILITY_FRAMES 30
#endif
#define PERF_MAX_PATH 512
#define PERF_GPU_RING 4
#define GL_TIME_ELAPSED_VALUE 0x88BFu
#define GL_QUERY_RESULT_AVAILABLE_VALUE 0x8867u
#define GL_QUERY_RESULT_VALUE 0x8866u

typedef void (*PerfGenQueries)(int count, unsigned int *ids);
typedef void (*PerfDeleteQueries)(int count, const unsigned int *ids);
typedef void (*PerfBeginQuery)(unsigned int target, unsigned int id);
typedef void (*PerfEndQuery)(unsigned int target);
typedef void (*PerfGetQueryObjectiv)(unsigned int id, unsigned int pname, int *value);
typedef void (*PerfGetQueryObjectui64v)(unsigned int id, unsigned int pname,
                                        unsigned long long *value);

typedef struct PerfGpuState {
    bool supported;
    bool active;
    unsigned int ids[PERF_GPU_RING];
    int frameNumbers[PERF_GPU_RING];
    bool pending[PERF_GPU_RING];
    int next;
    PerfGenQueries genQueries;
    PerfDeleteQueries deleteQueries;
    PerfBeginQuery beginQuery;
    PerfEndQuery endQuery;
    PerfGetQueryObjectiv getQueryObjectiv;
    PerfGetQueryObjectui64v getQueryObjectui64v;
} PerfGpuState;

typedef struct PerfCounters {
    uint64_t candidates;
    uint64_t distanceCulled;
    uint64_t frustumCulled;
    uint64_t visibleChunks;
    uint64_t drawCalls;
    uint64_t draws[PERF_DRAW_KIND_COUNT];
} PerfCounters;

typedef enum PerfBaselineStatus {
    PERF_BASELINE_OK = 0,
    PERF_BASELINE_UNREADABLE,
    PERF_BASELINE_SCHEMA_MISMATCH,
    PERF_BASELINE_INCOMPLETE
} PerfBaselineStatus;

typedef struct PerfBaseline {
    int version;
    double cpuP95;
    double gpuP95;
    double uploadP95;
    uint64_t estimatedMeshBytesFinal;
    bool hasGpu;
} PerfBaseline;

typedef struct PerfCollector {
    bool enabled;
    bool reportWritten;
    bool reportPassed;
    bool updateMarked;
    int frame;
    int sampleCount;
    double frameStartedMs;
    double updateCompletedMs;
    double *frameMs;
    double *updateMs;
    double *drawMs;
    double *uploadMs;
    double *gpuMs;
    bool *gpuValid;
    PerfCounters *counters;
    ChunkStreamingStats previousStreaming;
    RenderResourceSnapshot resourcePeak;
    RenderResourceSnapshot resourceRepeatCheckpoint;
    RenderResourceSnapshot resourceFinal;
    bool haveResourceRepeatCheckpoint;
    int stableFrames;
    PerfCounters currentCounters;
    uint32_t seed;
    int effectiveRenderDistance;
    const char *reportStatus;
    char reportPath[PERF_MAX_PATH];
    char baselinePath[PERF_MAX_PATH];
    PerfGpuState gpu;
} PerfCollector;

static PerfCollector collector;

static double PerfNowMs(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0.0;
    return (double)value.tv_sec * 1000.0 + (double)value.tv_nsec / 1000000.0;
}

static bool PerfResolveGpu(void)
{
#ifdef PERF_TESTING
    return false;
#else
    PerfGpuState *gpu = &collector.gpu;
    gpu->genQueries = (PerfGenQueries)rlGetProcAddress("glGenQueries");
    gpu->deleteQueries = (PerfDeleteQueries)rlGetProcAddress("glDeleteQueries");
    gpu->beginQuery = (PerfBeginQuery)rlGetProcAddress("glBeginQuery");
    gpu->endQuery = (PerfEndQuery)rlGetProcAddress("glEndQuery");
    gpu->getQueryObjectiv = (PerfGetQueryObjectiv)rlGetProcAddress("glGetQueryObjectiv");
    gpu->getQueryObjectui64v = (PerfGetQueryObjectui64v)rlGetProcAddress("glGetQueryObjectui64v");
    if (!gpu->getQueryObjectui64v) {
        gpu->getQueryObjectui64v = (PerfGetQueryObjectui64v)rlGetProcAddress("glGetQueryObjectui64vEXT");
    }
    if (!gpu->genQueries || !gpu->deleteQueries || !gpu->beginQuery || !gpu->endQuery ||
        !gpu->getQueryObjectiv || !gpu->getQueryObjectui64v) {
        return false;
    }
    gpu->genQueries(PERF_GPU_RING, gpu->ids);
    return true;
#endif
}

static void PerfPollGpu(void)
{
    PerfGpuState *gpu = &collector.gpu;
    if (!gpu->supported) return;
    for (int i = 0; i < PERF_GPU_RING; i++) {
        if (!gpu->pending[i]) continue;
        int available = 0;
        gpu->getQueryObjectiv(gpu->ids[i], GL_QUERY_RESULT_AVAILABLE_VALUE, &available);
        if (!available) continue;
        unsigned long long nanoseconds = 0;
        gpu->getQueryObjectui64v(gpu->ids[i], GL_QUERY_RESULT_VALUE, &nanoseconds);
        int frame = gpu->frameNumbers[i];
        if (frame >= PERF_WARMUP_FRAMES && frame < PERF_WARMUP_FRAMES + PERF_SAMPLE_FRAMES) {
            int sample = frame - PERF_WARMUP_FRAMES;
            collector.gpuMs[sample] = (double)nanoseconds / 1000000.0;
            collector.gpuValid[sample] = true;
        }
        gpu->pending[i] = false;
    }
}

static void PerfDrainGpu(void)
{
    PerfGpuState *gpu = &collector.gpu;
    if (!gpu->supported) return;
    for (int i = 0; i < PERF_GPU_RING; i++) {
        if (!gpu->pending[i]) continue;
        unsigned long long nanoseconds = 0;
        gpu->getQueryObjectui64v(gpu->ids[i], GL_QUERY_RESULT_VALUE, &nanoseconds);
        int frame = gpu->frameNumbers[i];
        if (frame >= PERF_WARMUP_FRAMES && frame < PERF_WARMUP_FRAMES + PERF_SAMPLE_FRAMES) {
            int sample = frame - PERF_WARMUP_FRAMES;
            collector.gpuMs[sample] = (double)nanoseconds / 1000000.0;
            collector.gpuValid[sample] = true;
        }
        gpu->pending[i] = false;
    }
}

static void PerfFreeArrays(void)
{
    free(collector.frameMs);
    free(collector.updateMs);
    free(collector.drawMs);
    free(collector.uploadMs);
    free(collector.gpuMs);
    free(collector.gpuValid);
    free(collector.counters);
    collector.frameMs = NULL;
    collector.updateMs = NULL;
    collector.drawMs = NULL;
    collector.uploadMs = NULL;
    collector.gpuMs = NULL;
    collector.gpuValid = NULL;
    collector.counters = NULL;
}

static int PerfCompareDouble(const void *left, const void *right)
{
    double a = *(const double *)left;
    double b = *(const double *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static double PerfPercentile(const double *values, int count, double percentile)
{
    if (!values || count <= 0) return 0.0;
    double *sorted = malloc((size_t)count * sizeof(double));
    if (!sorted) return 0.0;
    memcpy(sorted, values, (size_t)count * sizeof(double));
    qsort(sorted, (size_t)count, sizeof(double), PerfCompareDouble);
    int index = (int)ceil(percentile * (double)count) - 1;
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    double result = sorted[index];
    free(sorted);
    return result;
}

static double PerfMean(const double *values, int count)
{
    double total = 0.0;
    for (int i = 0; i < count; i++) total += values[i];
    return count > 0 ? total / (double)count : 0.0;
}

static uint64_t PerfSumCounter(int kind)
{
    uint64_t total = 0;
    for (int i = 0; i < collector.sampleCount; i++) {
        if (kind < 0) total += collector.counters[i].drawCalls;
        else if (kind < PERF_DRAW_KIND_COUNT) total += collector.counters[i].draws[kind];
    }
    return total;
}

static uint64_t PerfSumCandidates(int field)
{
    uint64_t total = 0;
    for (int i = 0; i < collector.sampleCount; i++) {
        if (field == 0) total += collector.counters[i].candidates;
        else if (field == 1) total += collector.counters[i].distanceCulled;
        else if (field == 2) total += collector.counters[i].frustumCulled;
        else total += collector.counters[i].visibleChunks;
    }
    return total;
}

static PerfBaselineStatus PerfReadBaseline(const char *path,
                                           PerfBaseline *baseline)
{
    FILE *file = fopen(path, "r");
    if (!file) return PERF_BASELINE_UNREADABLE;
    *baseline = (PerfBaseline){ 0 };
    char line[192];
    bool haveVersion = false;
    bool haveCpu = false;
    bool haveUpload = false;
    bool haveMeshBytes = false;
    while (fgets(line, sizeof(line), file)) {
        char key[96];
        double value = 0.0;
        if (sscanf(line, " %95[^=]=%lf", key, &value) != 2) continue;
        if (strcmp(key, "perf_version") == 0) {
            baseline->version = (int)value;
            haveVersion = true;
        } else if (strcmp(key, "cpu_frame_p95_ms") == 0) {
            baseline->cpuP95 = value;
            haveCpu = true;
        } else if (strcmp(key, "gpu_frame_p95_ms") == 0) {
            baseline->gpuP95 = value;
            baseline->hasGpu = value > 0.0;
        } else if (strcmp(key, "upload_p95_ms") == 0) {
            baseline->uploadP95 = value;
            haveUpload = true;
        } else if (strcmp(key, "estimated_mesh_bytes_final") == 0) {
            baseline->estimatedMeshBytesFinal = (uint64_t)value;
            haveMeshBytes = true;
        }
    }
    fclose(file);
    if (!haveVersion || baseline->version != PERF_VERSION) {
        return PERF_BASELINE_SCHEMA_MISMATCH;
    }
    return haveCpu && haveUpload && haveMeshBytes ? PERF_BASELINE_OK
                                                  : PERF_BASELINE_INCOMPLETE;
}

static const char *PerfBaselineStatusName(PerfBaselineStatus status)
{
    switch (status) {
    case PERF_BASELINE_OK: return "ok";
    case PERF_BASELINE_UNREADABLE: return "unreadable";
    case PERF_BASELINE_SCHEMA_MISMATCH: return "schema_mismatch";
    case PERF_BASELINE_INCOMPLETE: return "incomplete";
    default: return "unknown";
    }
}

static bool PerfWithinFivePercent(uint64_t value, uint64_t baseline)
{
    uint64_t allowance = baseline / 20u;
    uint64_t limit = UINT64_MAX - baseline < allowance ?
                     UINT64_MAX : baseline + allowance;
    return value <= limit;
}

static void PerfResourceSnapshotMax(PerfResourceSnapshot *target,
                                    PerfResourceSnapshot value)
{
    if (!target) return;
#define PERF_MAX_FIELD(field) \
    do { if (value.field > target->field) target->field = value.field; } while (0)
    PERF_MAX_FIELD(solidModels);
    PERF_MAX_FIELD(floraModels);
    PERF_MAX_FIELD(transparentModels);
    PERF_MAX_FIELD(meshVertices);
    PERF_MAX_FIELD(meshIndices);
    PERF_MAX_FIELD(estimatedMeshBytes);
    PERF_MAX_FIELD(worldLightingTextureBytes);
    PERF_MAX_FIELD(pendingMeshSnapshotBytes);
    PERF_MAX_FIELD(workerThreadsConfigured);
    PERF_MAX_FIELD(workerThreadsStarted);
    PERF_MAX_FIELD(workerThreadsActive);
#undef PERF_MAX_FIELD
}

static bool PerfStreamingSettled(PerfStreamingStats streaming,
                                 PerfResourceSnapshot resources)
{
    uint64_t generationFinished = streaming.generationCompleted +
                                  streaming.generationCanceled;
    uint64_t meshBuilt = streaming.meshCompleted + streaming.meshCanceled;
    uint64_t meshUploaded = streaming.uploadedMeshes + streaming.meshCanceled;
    return generationFinished >= streaming.generationSubmitted &&
           meshBuilt >= streaming.meshSubmitted &&
           meshUploaded >= streaming.meshCompleted &&
           resources.pendingMeshSnapshotBytes == 0 &&
           resources.workerThreadsActive == 0;
}

static void PerfWriteReport(PerfStreamingStats streaming)
{
    if (!collector.enabled || collector.reportWritten) return;
    PerfDrainGpu();
    double cpuP95 = PerfPercentile(collector.frameMs, collector.sampleCount, 0.95);
    double updateP95 = PerfPercentile(collector.updateMs, collector.sampleCount, 0.95);
    double drawP95 = PerfPercentile(collector.drawMs, collector.sampleCount, 0.95);
    double uploadP95 = PerfPercentile(collector.uploadMs, collector.sampleCount, 0.95);
    double gpuTotal = 0.0;
    int gpuCount = 0;
    double gpuValues[PERF_SAMPLE_FRAMES];
    for (int i = 0; i < collector.sampleCount; i++) {
        if (collector.gpuValid[i]) gpuValues[gpuCount++] = collector.gpuMs[i];
    }
    double gpuP95 = PerfPercentile(gpuValues, gpuCount, 0.95);
    for (int i = 0; i < gpuCount; i++) gpuTotal += gpuValues[i];
    uint64_t drawCalls = PerfSumCounter(-1);
    bool cpuPass = true;
    bool gpuPass = true;
    bool uploadPass = true;
    bool syncPass = streaming.syncRebuilds == 0;
    bool queuePass = streaming.generationQueuePeak <= MAX_CHUNK_GEN_JOBS &&
                     streaming.meshQueuePeak <= MAX_CHUNK_MESH_JOBS;
    bool threadPass = collector.resourcePeak.workerThreadsConfigured <= 2 &&
                      collector.resourcePeak.workerThreadsStarted <= 2 &&
                      collector.resourcePeak.workerThreadsActive <= 2;
    bool resourceStablePass = collector.resourceFinal.estimatedMeshBytes <=
                              collector.resourcePeak.estimatedMeshBytes;
    if (collector.haveResourceRepeatCheckpoint &&
        collector.resourceRepeatCheckpoint.estimatedMeshBytes > 0) {
        resourceStablePass = resourceStablePass &&
            PerfWithinFivePercent(
                collector.resourceFinal.estimatedMeshBytes,
                collector.resourceRepeatCheckpoint.estimatedMeshBytes);
    }
    bool resourceBaselinePass = true;
    bool baselineConfigured = collector.baselinePath[0] != '\0';
    PerfBaselineStatus baselineStatus = PERF_BASELINE_OK;
    if (baselineConfigured) {
        PerfBaseline baseline = { 0 };
        baselineStatus = PerfReadBaseline(collector.baselinePath, &baseline);
        bool valid = baselineStatus == PERF_BASELINE_OK;
        cpuPass = valid && cpuP95 <= baseline.cpuP95 * 1.05;
        gpuPass = valid && (!collector.gpu.supported || !baseline.hasGpu ||
                  (gpuCount > 0 && gpuP95 <= baseline.gpuP95 * 1.05));
        uploadPass = valid && uploadP95 <= baseline.uploadP95 * 1.05;
        resourceBaselinePass = valid && PerfWithinFivePercent(
            collector.resourceFinal.estimatedMeshBytes,
            baseline.estimatedMeshBytesFinal);
    }
    collector.reportPassed = cpuPass && gpuPass && uploadPass && syncPass &&
                             queuePass && threadPass && resourceStablePass &&
                             resourceBaselinePass;
    if (baselineConfigured && baselineStatus == PERF_BASELINE_SCHEMA_MISMATCH) {
        collector.reportStatus = "baseline_schema_mismatch";
    } else if (baselineConfigured && baselineStatus != PERF_BASELINE_OK) {
        collector.reportStatus = "baseline_invalid";
    } else {
        collector.reportStatus = collector.reportPassed ? "pass" : "fail";
    }

    FILE *file = fopen(collector.reportPath, "w");
    if (!file) {
        fprintf(stderr, "perf_report_error=%s\n", strerror(errno));
        collector.reportStatus = "write_error";
        collector.reportWritten = true;
        collector.reportPassed = false;
        return;
    }
    fprintf(file, "perf_version=%d\n", PERF_VERSION);
    fprintf(file, "seed=%u\n", collector.seed);
    fprintf(file, "route_frames=%d\n", PERF_WARMUP_FRAMES + PERF_SAMPLE_FRAMES);
    fprintf(file, "effective_render_distance=%d\n", collector.effectiveRenderDistance);
    fprintf(file, "report_status=%s\n", collector.reportStatus ? collector.reportStatus : "pending");
    fprintf(file, "warmup_frames=%d\n", PERF_WARMUP_FRAMES);
    fprintf(file, "sample_frames=%d\n", collector.sampleCount);
    fprintf(file, "stability_frames=%d\n", PERF_STABILITY_FRAMES);
    fprintf(file, "cpu_frame_mean_ms=%.3f\n", PerfMean(collector.frameMs, collector.sampleCount));
    fprintf(file, "cpu_frame_p95_ms=%.3f\n", cpuP95);
    fprintf(file, "cpu_update_p95_ms=%.3f\n", updateP95);
    fprintf(file, "cpu_draw_p95_ms=%.3f\n", drawP95);
    fprintf(file, "upload_p95_ms=%.3f\n", uploadP95);
    fprintf(file, "upload_peak_ms=%.3f\n", streaming.maxUploadCpuMs);
    fprintf(file, "gpu_timer_supported=%d\n", collector.gpu.supported ? 1 : 0);
    fprintf(file, "gpu_samples=%d\n", gpuCount);
    fprintf(file, "gpu_frame_mean_ms=%.3f\n", gpuCount > 0 ? gpuTotal / (double)gpuCount : 0.0);
    fprintf(file, "gpu_frame_p95_ms=%.3f\n", gpuP95);
    fprintf(file, "instrumented_draw_calls=%llu\n", (unsigned long long)drawCalls);
    fprintf(file, "solid_draws=%llu\n", (unsigned long long)PerfSumCounter(PERF_DRAW_SOLID));
    fprintf(file, "water_draws=%llu\n", (unsigned long long)PerfSumCounter(PERF_DRAW_WATER));
    fprintf(file, "flora_draws=%llu\n", (unsigned long long)PerfSumCounter(PERF_DRAW_FLORA));
    fprintf(file, "space_draws=%llu\n", (unsigned long long)PerfSumCounter(PERF_DRAW_SPACE));
    fprintf(file, "nether_draws=%llu\n", (unsigned long long)PerfSumCounter(PERF_DRAW_NETHER));
    fprintf(file, "cloud_draws=%llu\n", (unsigned long long)PerfSumCounter(PERF_DRAW_CLOUD));
    fprintf(file, "fire_draws=%llu\n", (unsigned long long)PerfSumCounter(PERF_DRAW_FIRE));
    fprintf(file, "world_candidates=%llu\n", (unsigned long long)PerfSumCandidates(0));
    fprintf(file, "distance_culled=%llu\n", (unsigned long long)PerfSumCandidates(1));
    fprintf(file, "frustum_culled=%llu\n", (unsigned long long)PerfSumCandidates(2));
    fprintf(file, "visible_chunks=%llu\n", (unsigned long long)PerfSumCandidates(3));
    fprintf(file, "sync_rebuilds=%llu\n", (unsigned long long)streaming.syncRebuilds);
    fprintf(file, "mesh_snapshot_bytes=%llu\n", (unsigned long long)streaming.meshSnapshotBytes);
    fprintf(file, "generation_submitted=%llu\n", (unsigned long long)streaming.generationSubmitted);
    fprintf(file, "generation_completed=%llu\n", (unsigned long long)streaming.generationCompleted);
    fprintf(file, "mesh_submitted=%llu\n", (unsigned long long)streaming.meshSubmitted);
    fprintf(file, "mesh_completed=%llu\n", (unsigned long long)streaming.meshCompleted);
    fprintf(file, "uploaded_meshes=%llu\n", (unsigned long long)streaming.uploadedMeshes);
    fprintf(file, "upload_budget_deferrals=%llu\n", (unsigned long long)streaming.uploadBudgetDeferrals);
    fprintf(file, "generation_queue_peak=%llu\n", (unsigned long long)streaming.generationQueuePeak);
    fprintf(file, "mesh_queue_peak=%llu\n", (unsigned long long)streaming.meshQueuePeak);
    fprintf(file, "pending_mesh_snapshot_bytes_final=%llu\n",
            (unsigned long long)collector.resourceFinal.pendingMeshSnapshotBytes);
    fprintf(file, "pending_mesh_snapshot_bytes_peak=%llu\n",
            (unsigned long long)streaming.pendingMeshSnapshotBytesPeak);
    fprintf(file, "solid_models_final=%llu\n",
            (unsigned long long)collector.resourceFinal.solidModels);
    fprintf(file, "flora_models_final=%llu\n",
            (unsigned long long)collector.resourceFinal.floraModels);
    fprintf(file, "transparent_models_final=%llu\n",
            (unsigned long long)collector.resourceFinal.transparentModels);
    fprintf(file, "mesh_vertices_final=%llu\n",
            (unsigned long long)collector.resourceFinal.meshVertices);
    fprintf(file, "mesh_indices_final=%llu\n",
            (unsigned long long)collector.resourceFinal.meshIndices);
    fprintf(file, "estimated_mesh_bytes_final=%llu\n",
            (unsigned long long)collector.resourceFinal.estimatedMeshBytes);
    fprintf(file, "estimated_mesh_bytes_peak=%llu\n",
            (unsigned long long)collector.resourcePeak.estimatedMeshBytes);
    fprintf(file, "world_lighting_texture_bytes=%llu\n",
            (unsigned long long)collector.resourcePeak.worldLightingTextureBytes);
    fprintf(file, "estimated_mesh_bytes_repeat_checkpoint=%llu\n",
            (unsigned long long)collector.resourceRepeatCheckpoint.estimatedMeshBytes);
    fprintf(file, "worker_threads_configured=%llu\n",
            (unsigned long long)collector.resourcePeak.workerThreadsConfigured);
    fprintf(file, "worker_threads_started_peak=%llu\n",
            (unsigned long long)collector.resourcePeak.workerThreadsStarted);
    fprintf(file, "worker_threads_active_peak=%llu\n",
            (unsigned long long)collector.resourcePeak.workerThreadsActive);
    fprintf(file, "resource_bytes_kind=estimated_public_mesh_buffers\n");
    fprintf(file, "queue_capacity_target=%s\n", queuePass ? "pass" : "fail");
    fprintf(file, "worker_thread_target=%s\n", threadPass ? "pass" : "fail");
    fprintf(file, "resource_stability_target=%s\n",
            resourceStablePass ? "pass" : "fail");
    fprintf(file, "sync_rebuild_target=%s\n", syncPass ? "pass" : "fail");
    if (baselineConfigured) {
        fprintf(file, "baseline_status=%s\n", PerfBaselineStatusName(baselineStatus));
        fprintf(file, "baseline_cpu_target=%s\n", cpuPass ? "pass" : "fail");
        fprintf(file, "baseline_gpu_target=%s\n", gpuPass ? "pass" : "fail");
        fprintf(file, "baseline_upload_target=%s\n", uploadPass ? "pass" : "fail");
        fprintf(file, "baseline_sync_target=%s\n", syncPass ? "pass" : "fail");
        fprintf(file, "baseline_resource_target=%s\n",
                resourceBaselinePass ? "pass" : "fail");
    }
    if (fclose(file) != 0) {
        fprintf(stderr, "perf_report_error=%s\n", strerror(errno));
        collector.reportStatus = "write_error";
        collector.reportPassed = false;
    }
    collector.reportWritten = true;
}

void PerfConfigure(bool enabled, const char *reportPath, const char *baselinePath)
{
    PerfShutdown();
    collector.enabled = enabled;
    if (!enabled) return;
    snprintf(collector.reportPath, sizeof(collector.reportPath), "%s",
             reportPath && reportPath[0] ? reportPath : "/tmp/voxelcraft_perf.keyvalue");
    snprintf(collector.baselinePath, sizeof(collector.baselinePath), "%s",
             baselinePath ? baselinePath : "");
    collector.frameMs = calloc(PERF_SAMPLE_FRAMES, sizeof(double));
    collector.updateMs = calloc(PERF_SAMPLE_FRAMES, sizeof(double));
    collector.drawMs = calloc(PERF_SAMPLE_FRAMES, sizeof(double));
    collector.uploadMs = calloc(PERF_SAMPLE_FRAMES, sizeof(double));
    collector.gpuMs = calloc(PERF_SAMPLE_FRAMES, sizeof(double));
    collector.gpuValid = calloc(PERF_SAMPLE_FRAMES, sizeof(bool));
    collector.counters = calloc(PERF_SAMPLE_FRAMES, sizeof(PerfCounters));
    if (!collector.frameMs || !collector.updateMs || !collector.drawMs || !collector.uploadMs ||
        !collector.gpuMs || !collector.gpuValid || !collector.counters) {
        collector.enabled = false;
        PerfFreeArrays();
        collector.reportWritten = true;
        collector.reportPassed = false;
        collector.reportStatus = "allocation_error";
        return;
    }
    collector.gpu.supported = PerfResolveGpu();
    collector.reportStatus = "pending";
}

void PerfSetMetadata(uint32_t seed, int effectiveRenderDistance)
{
    if (!collector.enabled) return;
    collector.seed = seed;
    collector.effectiveRenderDistance = effectiveRenderDistance;
}

bool PerfIsEnabled(void) { return collector.enabled; }
int PerfFrameIndex(void) { return collector.frame; }
bool PerfRouteComplete(void) { return collector.frame >= PERF_WARMUP_FRAMES + PERF_SAMPLE_FRAMES; }

void PerfBeginFrame(void)
{
    if (!collector.enabled) return;
    PerfPollGpu();
    collector.currentCounters = (PerfCounters){ 0 };
    collector.updateMarked = false;
    collector.frameStartedMs = PerfNowMs();
}

void PerfMarkUpdateComplete(void)
{
    if (!collector.enabled) return;
    collector.updateCompletedMs = PerfNowMs();
    collector.updateMarked = true;
}

void PerfBeginGpuFrame(void)
{
    PerfGpuState *gpu = &collector.gpu;
    if (!collector.enabled || !gpu->supported || gpu->active ||
        collector.frame >= PERF_WARMUP_FRAMES + PERF_SAMPLE_FRAMES) return;
    int slot = gpu->next;
    if (gpu->pending[slot]) return;
    gpu->beginQuery(GL_TIME_ELAPSED_VALUE, gpu->ids[slot]);
    gpu->frameNumbers[slot] = collector.frame;
    gpu->pending[slot] = true;
    gpu->active = true;
}

void PerfEndGpuFrame(void)
{
    if (!collector.enabled || !collector.gpu.supported || !collector.gpu.active) return;
    collector.gpu.endQuery(GL_TIME_ELAPSED_VALUE);
    collector.gpu.next = (collector.gpu.next + 1) % PERF_GPU_RING;
    collector.gpu.active = false;
}

void PerfRecordWorldCandidate(bool distanceVisible, bool frustumVisible)
{
    if (!collector.enabled) return;
    collector.currentCounters.candidates++;
    if (!distanceVisible) collector.currentCounters.distanceCulled++;
    else if (!frustumVisible) collector.currentCounters.frustumCulled++;
    else collector.currentCounters.visibleChunks++;
}

void PerfRecordDrawCall(PerfDrawKind kind)
{
    if (!collector.enabled || kind < 0 || kind >= PERF_DRAW_KIND_COUNT) return;
    collector.currentCounters.drawCalls++;
    collector.currentCounters.draws[kind]++;
}

void PerfEndFrame(PerfStreamingStats streaming, PerfResourceSnapshot resources)
{
    if (!collector.enabled) return;
    double now = PerfNowMs();
    PerfResourceSnapshotMax(&collector.resourcePeak, resources);
    if (!collector.updateMarked) collector.updateCompletedMs = now;
    if (collector.frame >= PERF_WARMUP_FRAMES && collector.frame < PERF_WARMUP_FRAMES + PERF_SAMPLE_FRAMES) {
        int sample = collector.frame - PERF_WARMUP_FRAMES;
        collector.frameMs[sample] = now - collector.frameStartedMs;
        collector.updateMs[sample] = collector.updateCompletedMs - collector.frameStartedMs;
        collector.drawMs[sample] = now - collector.updateCompletedMs;
        collector.uploadMs[sample] = fmax(streaming.uploadCpuMs - collector.previousStreaming.uploadCpuMs, 0.0);
        collector.counters[sample] = collector.currentCounters;
        collector.sampleCount++;
        collector.resourceFinal = resources;
    } else if (collector.frame >= PERF_WARMUP_FRAMES + PERF_SAMPLE_FRAMES) {
        collector.resourceFinal = resources;
        if (PerfStreamingSettled(streaming, resources)) {
            if (!collector.haveResourceRepeatCheckpoint) {
                collector.resourceRepeatCheckpoint = resources;
                collector.haveResourceRepeatCheckpoint = true;
                collector.stableFrames = 1;
            } else {
                collector.stableFrames++;
            }
        } else {
            collector.haveResourceRepeatCheckpoint = false;
            collector.stableFrames = 0;
        }
    }
    collector.previousStreaming = streaming;
    collector.frame++;
    if (!collector.reportWritten &&
        collector.stableFrames >= PERF_STABILITY_FRAMES) {
        bool drainComplete = !collector.gpu.supported || collector.frame >=
                             PERF_WARMUP_FRAMES + PERF_SAMPLE_FRAMES + PERF_GPU_RING;
        if (drainComplete) PerfWriteReport(streaming);
    }
}

bool PerfReportWritten(void) { return collector.reportWritten; }
bool PerfReportPassed(void) { return collector.reportPassed; }

void PerfShutdown(void)
{
    if (collector.gpu.supported && collector.gpu.deleteQueries) {
        collector.gpu.deleteQueries(PERF_GPU_RING, collector.gpu.ids);
    }
    PerfFreeArrays();
    memset(&collector, 0, sizeof(collector));
}
