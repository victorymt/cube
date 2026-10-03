#include "core/plugin.h"

#include <assert.h>
#include <stdio.h>

static const VoxelcraftPluginStreamService *streamV1;
static const VoxelcraftPluginStreamServiceV2 *streamV2;
static const VoxelcraftPluginGameService *gameService;
static const VoxelcraftPluginWorldService *worldService;
static uint64_t lastCapture;
static uint64_t lastGeneration;
static bool sawHome;
static bool sawPlanet;
static bool sawReturn;

static bool Load(const VoxelcraftPluginContext *context, void **state)
{
    const VoxelcraftPluginService *v1 = context->lookup_service(
        context, VOXELCRAFT_PLUGIN_STREAM_SERVICE_ID,
        VOXELCRAFT_PLUGIN_STREAM_SERVICE_ABI_VERSION);
    const VoxelcraftPluginService *v2 = context->lookup_service(
        context, VOXELCRAFT_PLUGIN_STREAM_V2_SERVICE_ID,
        VOXELCRAFT_PLUGIN_STREAM_V2_SERVICE_ABI_VERSION);
    const VoxelcraftPluginService *game = context->lookup_service(
        context, VOXELCRAFT_PLUGIN_GAME_SERVICE_ID,
        VOXELCRAFT_PLUGIN_GAME_SERVICE_ABI_VERSION);
    const VoxelcraftPluginService *world = context->lookup_service(
        context, VOXELCRAFT_PLUGIN_WORLD_SERVICE_ID,
        VOXELCRAFT_PLUGIN_WORLD_SERVICE_ABI_VERSION);
    assert(v1 && v2 && game && world);
    streamV1 = v1->interface;
    streamV2 = v2->interface;
    gameService = game->interface;
    worldService = world->interface;
    assert(streamV1->abi_version == 1u && streamV2->abi_version == 2u);
    assert(streamV1->struct_size == sizeof(*streamV1));
    assert(streamV2->struct_size == sizeof(*streamV2));
    *state = NULL;
    return true;
}

static void Update(void *state, const VoxelcraftPluginFrame *frame)
{
    (void)state;
    if (!gameService->is_playing(gameService->user_data)) return;
    struct {
        VoxelcraftPluginStreamSnapshot snapshot;
        uint64_t guard;
    } v1 = { .snapshot.struct_size = sizeof(v1.snapshot), .guard = UINT64_MAX };
    struct {
        VoxelcraftPluginStreamSnapshotV2 snapshot;
        uint64_t guard;
    } v2 = { .snapshot.struct_size = sizeof(v2.snapshot), .guard = UINT64_MAX };
    assert(streamV1->snapshot(streamV1->user_data, &v1.snapshot));
    assert(streamV2->snapshot(streamV2->user_data, &v2.snapshot));
    assert(v1.guard == UINT64_MAX && v2.guard == UINT64_MAX);
    assert(v2.snapshot.capture_id > lastCapture);
    assert(v2.snapshot.frame_id == frame->frame_index);
    assert(v2.snapshot.world_generation >= lastGeneration);
    assert(v2.snapshot.counters.render_distance == v1.snapshot.render_distance);
    /* Worker completion may change between the two calls; queue counts need
     * not be equal, even when both observations carry the same frame. */
    lastCapture = v2.snapshot.capture_id;
    lastGeneration = v2.snapshot.world_generation;
    VoxelcraftPluginWorldSnapshot world = { .struct_size = sizeof(world) };
    assert(worldService->snapshot(worldService->user_data, &world));
    if (world.dimension == VOXELCRAFT_PLUGIN_WORLD_DIMENSION_HOME) {
        sawHome = true;
        if (sawPlanet) sawReturn = true;
    }
    if (world.dimension == VOXELCRAFT_PLUGIN_WORLD_DIMENSION_PLANET && sawHome)
        sawPlanet = true;
}

static void Unload(void *state)
{
    (void)state;
    assert(sawHome && sawPlanet && sawReturn);
    puts("DIAGNOSTICS_PLUGIN v1_v2_identity_passed");
}

const VoxelcraftPluginDescriptor *VoxelcraftPluginGetDescriptor(void)
{
    static const VoxelcraftPluginDescriptor descriptor = {
        .abi_version = VOXELCRAFT_PLUGIN_ABI_VERSION,
        .struct_size = sizeof(VoxelcraftPluginDescriptor),
        .id = "test.diagnostics", .name = "Diagnostics regression probe",
        .version_major = 1u, .on_load = Load, .on_update = Update, .on_unload = Unload
    };
    return &descriptor;
}
