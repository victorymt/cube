#include "app/game_plugin_stream.h"
#include "app/game_runtime.h"
#include "world/world_environment.h"
#include "world/world_surface_snapshot.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned captures;
static uint64_t generation = 7u;
static bool available = true;

uint64_t WorldCurrentGeneration(void) { return generation; }
bool WorldIsSurfaceActive(void) { return true; }

void ChunksCaptureDiagnosticsAt(ChunkDiagnostics *out,
                                const DiagnosticsCaptureIdentity *identity)
{
    captures++;
    *out = (ChunkDiagnostics){
        .status = available ? CHUNK_DIAGNOSTICS_VALID : CHUNK_DIAGNOSTICS_UNAVAILABLE,
        .identity = *identity, .captureSequence = 100u + captures,
        .renderDistance = 8, .activeChunks = 81, .pendingGenerationJobs = 3,
        .pendingMeshJobs = 2, .workerThreadsConfigured = 4,
        .streaming = { .generationSubmitted = 37u, .meshCompleted = 29u },
        .preload = { .active = true, .offsetX = -2, .targetChunks = 10 }
    };
}

int main(void)
{
    static GameRuntime game;
    game.debugFrame = 12u;
    game.simulationTick = 34u;
    struct {
        VoxelcraftPluginStreamSnapshot snapshot;
        uint64_t canary;
    } v1 = { .snapshot.struct_size = sizeof(v1.snapshot), .canary = UINT64_MAX };
    assert(GamePluginStreamSnapshot(&game, &v1.snapshot));
    assert(v1.canary == UINT64_MAX && captures == 1u);

    struct {
        VoxelcraftPluginStreamSnapshotV2 snapshot;
        uint64_t canary;
    } v2 = { .snapshot.struct_size = sizeof(v2.snapshot), .canary = UINT64_MAX };
    assert(GamePluginStreamSnapshotV2(&game, &v2.snapshot));
    assert(v2.canary == UINT64_MAX && captures == 2u);
    assert(v2.snapshot.capture_id == 2u);
    assert(v2.snapshot.capture_sequence == 102u);
    assert(v2.snapshot.frame_id == 12u && v2.snapshot.simulation_tick == 34u);
    assert(v2.snapshot.world_generation == 7u);
    assert(v2.snapshot.counters.struct_size == sizeof(v1.snapshot));
    assert(v2.snapshot.counters.render_distance == 8);
    assert(v2.snapshot.counters.pending_generation_jobs == v1.snapshot.pending_generation_jobs);
    assert(v2.snapshot.counters.generation_submitted == 37u);
    assert(v2.snapshot.counters.mesh_completed == 29u);
    assert(v2.snapshot.counters.preload_offset_x == -2);

    v2.snapshot.struct_size = sizeof(v2.snapshot) - 1;
    VoxelcraftPluginStreamSnapshotV2 before = v2.snapshot;
    assert(!GamePluginStreamSnapshotV2(&game, &v2.snapshot));
    assert(memcmp(&before, &v2.snapshot, sizeof(before)) == 0);
    assert(!GamePluginStreamSnapshotV2(&game, NULL));
    assert(!GamePluginStreamSnapshot(NULL, &v1.snapshot));
    assert(captures == 2u);
    v2.snapshot.struct_size = sizeof(v2); /* Forward-compatible larger buffer. */
    generation++;
    assert(GamePluginStreamSnapshotV2(&game, &v2.snapshot));
    assert(v2.snapshot.world_generation == 8u && v2.snapshot.capture_id == 3u);
    assert(v2.canary == UINT64_MAX && v2.snapshot.struct_size == sizeof(v2));
    available = false;
    assert(!GamePluginStreamSnapshotV2(&game, &v2.snapshot));
    puts("game plugin stream tests passed");
    return 0;
}
