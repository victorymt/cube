#include "core/perf.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool FileContains(const char *path, const char *needle)
{
    FILE *file = fopen(path, "r");
    if (!file) return false;
    char line[256];
    bool found = false;
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, needle)) {
            found = true;
            break;
        }
    }
    fclose(file);
    return found;
}

static RenderResourceSnapshot TestResources(uint64_t bytes)
{
    return (RenderResourceSnapshot){
        .solidModels = 2,
        .floraModels = 1,
        .transparentModels = 1,
        .meshVertices = 120,
        .meshIndices = 36,
        .estimatedMeshBytes = bytes,
        .pendingMeshSnapshotBytes = 0,
        .workerThreadsConfigured = 2,
        .workerThreadsStarted = 2,
        .workerThreadsActive = 1
    };
}

static void RunFrames(RenderResourceSnapshot resources, ChunkStreamingStats streaming)
{
    for (int frame = 0; frame < 8; frame++) {
        streaming.uploadCpuMs = (double)frame * 0.25;
        RenderResourceSnapshot frameResources = resources;
        if (frame >= 6) frameResources.workerThreadsActive = 0;
        PerfBeginFrame();
        PerfMarkUpdateComplete();
        PerfRecordWorldCandidate(true, true);
        PerfRecordWorldCandidate(false, false);
        PerfRecordDrawCall(PERF_DRAW_SOLID);
        PerfRecordDrawCall(PERF_DRAW_WATER);
        PerfEndFrame(streaming, frameResources);
    }
}

static void RunGrowingResourceFrames(void)
{
    for (int frame = 0; frame < 8; frame++) {
        RenderResourceSnapshot resources = TestResources(
            frame < 6 ? 1000u : (uint64_t)(1000 + (frame - 5) * 500));
        if (frame >= 6) resources.workerThreadsActive = 0;
        PerfBeginFrame();
        PerfMarkUpdateComplete();
        PerfEndFrame((ChunkStreamingStats){ 0 }, resources);
    }
}

int main(void)
{
    const char *report = "/tmp/voxelcraft_test_perf.keyvalue";
    const char *baseline = "/tmp/voxelcraft_test_perf_baseline.keyvalue";
    FILE *file = fopen(baseline, "w");
    assert(file != NULL);
    fputs("perf_version=3\n", file);
    fputs("cpu_frame_p95_ms=1000\n", file);
    fputs("gpu_frame_p95_ms=0\n", file);
    fputs("upload_p95_ms=1000\n", file);
    fputs("sync_rebuilds=0\n", file);
    fputs("estimated_mesh_bytes_final=10000\n", file);
    assert(fclose(file) == 0);

    PerfConfigure(true, report, baseline);
    PerfSetMetadata(12345u, 7);
    ChunkStreamingStats streaming = { .meshSnapshotBytes = 4096,
                                      .meshQueuePeak = 3,
                                      .pendingMeshSnapshotBytesPeak = 8192 };
    RunFrames(TestResources(9000), streaming);

    assert(PerfReportWritten());
    assert(PerfReportPassed());
    assert(FileContains(report, "seed=12345"));
    assert(FileContains(report, "route_frames=6"));
    assert(FileContains(report, "sample_frames=4"));
    assert(FileContains(report, "report_status=pass"));
    assert(FileContains(report, "instrumented_draw_calls=8"));
    assert(FileContains(report, "world_candidates=8"));
    assert(FileContains(report, "baseline_cpu_target=pass"));
    assert(FileContains(report, "mesh_queue_peak=3"));
    assert(FileContains(report, "perf_version=3"));
    assert(FileContains(report, "stability_frames=2"));
    assert(FileContains(report, "estimated_mesh_bytes_final=9000"));
    assert(FileContains(report, "worker_threads_started_peak=2"));
    assert(FileContains(report, "resource_bytes_kind=estimated_public_mesh_buffers"));
    assert(FileContains(report, "baseline_resource_target=pass"));
    PerfShutdown();

    file = fopen(baseline, "w");
    assert(file != NULL);
    fputs("perf_version=2\n", file);
    fputs("cpu_frame_p95_ms=1000\n", file);
    fputs("upload_p95_ms=1000\n", file);
    fputs("estimated_mesh_bytes_final=10000\n", file);
    assert(fclose(file) == 0);
    PerfConfigure(true, report, baseline);
    RunFrames(TestResources(9000), (ChunkStreamingStats){ 0 });
    assert(PerfReportWritten());
    assert(!PerfReportPassed());
    assert(FileContains(report, "report_status=baseline_schema_mismatch"));
    assert(FileContains(report, "baseline_status=schema_mismatch"));
    PerfShutdown();

    PerfConfigure(true, report, NULL);
    streaming = (ChunkStreamingStats){
        .generationQueuePeak = MAX_CHUNK_GEN_JOBS + 1,
        .meshQueuePeak = MAX_CHUNK_MESH_JOBS + 1
    };
    RenderResourceSnapshot excessiveThreads = TestResources(9000);
    excessiveThreads.workerThreadsStarted = 3;
    excessiveThreads.workerThreadsActive = 3;
    RunFrames(excessiveThreads, streaming);
    assert(PerfReportWritten());
    assert(!PerfReportPassed());
    assert(FileContains(report, "queue_capacity_target=fail"));
    assert(FileContains(report, "worker_thread_target=fail"));
    PerfShutdown();

    PerfConfigure(true, report, NULL);
    RunGrowingResourceFrames();
    assert(PerfReportWritten());
    assert(!PerfReportPassed());
    assert(FileContains(report, "resource_stability_target=fail"));
    PerfShutdown();

    PerfConfigure(true, report, NULL);
    RunFrames(TestResources(9000),
              (ChunkStreamingStats){ .syncRebuilds = 1 });
    assert(PerfReportWritten());
    assert(!PerfReportPassed());
    assert(FileContains(report, "sync_rebuild_target=fail"));
    PerfShutdown();

    PerfConfigure(true, "/tmp/voxelcraft_missing/report.keyvalue", NULL);
    for (int frame = 0; frame < 8; frame++) {
        PerfBeginFrame();
        PerfMarkUpdateComplete();
        PerfEndFrame((ChunkStreamingStats){ 0 }, (RenderResourceSnapshot){ 0 });
    }
    assert(PerfReportWritten());
    assert(!PerfReportPassed());
    PerfShutdown();

    remove(report);
    remove(baseline);
    puts("perf collector tests passed");
    return 0;
}
