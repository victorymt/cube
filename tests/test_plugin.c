#include "core/plugin.h"

#include <assert.h>
#include <stddef.h>

static int load_count;
static int unload_count;
static int reset_count;
static int update_count;
static int render_count;
static int load_order[4];
static int load_order_count;

typedef struct TestPluginServiceInterface {
    int value;
} TestPluginServiceInterface;

static const TestPluginServiceInterface TEST_SERVICE_INTERFACE = {.value = 42};
static const VoxelcraftPluginService TEST_SERVICE = {
    .abi_version = 1u,
    .struct_size = sizeof(VoxelcraftPluginService),
    .id = "test.counter",
    .interface = &TEST_SERVICE_INTERFACE};
static const VoxelcraftPluginService INVALID_SERVICE = {
    .abi_version = 1u,
    .struct_size = sizeof(VoxelcraftPluginService),
    .id = "bad service",
    .interface = &TEST_SERVICE_INTERFACE};

static bool TestWorldSnapshot(
    void *user_data, VoxelcraftPluginWorldSnapshot *out_snapshot)
{
    (void)user_data;
    if (!out_snapshot || out_snapshot->struct_size <
                            sizeof(VoxelcraftPluginWorldSnapshot)) {
        return false;
    }
    out_snapshot->surface_active = true;
    out_snapshot->dimension = VOXELCRAFT_PLUGIN_WORLD_DIMENSION_PLANET;
    out_snapshot->surface_kind = VOXELCRAFT_PLUGIN_WORLD_SURFACE_PLANET;
    out_snapshot->surface_id = 17u;
    out_snapshot->seed = 23u;
    out_snapshot->map_origin_x = -128;
    out_snapshot->map_origin_z = 256;
    return true;
}

static const VoxelcraftPluginWorldService TEST_WORLD_INTERFACE = {
    .abi_version = VOXELCRAFT_PLUGIN_WORLD_SERVICE_ABI_VERSION,
    .struct_size = sizeof(VoxelcraftPluginWorldService),
    .snapshot = TestWorldSnapshot};

static const VoxelcraftPluginService TEST_WORLD_SERVICE = {
    .abi_version = VOXELCRAFT_PLUGIN_WORLD_SERVICE_ABI_VERSION,
    .struct_size = sizeof(VoxelcraftPluginService),
    .id = VOXELCRAFT_PLUGIN_WORLD_SERVICE_ID,
    .interface = &TEST_WORLD_INTERFACE};

static bool TestStreamSnapshot(
    void *user_data, VoxelcraftPluginStreamSnapshot *out_snapshot)
{
    (void)user_data;
    if (!out_snapshot || out_snapshot->struct_size <
                            sizeof(VoxelcraftPluginStreamSnapshot)) {
        return false;
    }
    out_snapshot->surface_active = true;
    out_snapshot->render_distance = 12;
    out_snapshot->loaded_chunks = 81;
    out_snapshot->pending_generation_jobs = 2;
    out_snapshot->pending_mesh_jobs = 3;
    out_snapshot->worker_threads_configured = 4u;
    out_snapshot->worker_threads_started = 4u;
    out_snapshot->worker_threads_active = 1u;
    out_snapshot->generation_submitted = 100u;
    out_snapshot->generation_completed = 98u;
    out_snapshot->mesh_submitted = 90u;
    out_snapshot->mesh_completed = 87u;
    out_snapshot->generation_queue_peak = 8u;
    out_snapshot->mesh_queue_peak = 6u;
    out_snapshot->preload_active = true;
    out_snapshot->preload_offset_x = 2;
    out_snapshot->preload_offset_z = -1;
    out_snapshot->preload_target_chunks = 16;
    out_snapshot->preload_loaded_chunks = 11;
    out_snapshot->preload_pending_generation_jobs = 1;
    out_snapshot->preload_pending_mesh_jobs = 2;
    return true;
}

static const VoxelcraftPluginStreamService TEST_STREAM_INTERFACE = {
    .abi_version = VOXELCRAFT_PLUGIN_STREAM_SERVICE_ABI_VERSION,
    .struct_size = sizeof(VoxelcraftPluginStreamService),
    .snapshot = TestStreamSnapshot};

static const VoxelcraftPluginService TEST_STREAM_SERVICE = {
    .abi_version = VOXELCRAFT_PLUGIN_STREAM_SERVICE_ABI_VERSION,
    .struct_size = sizeof(VoxelcraftPluginService),
    .id = VOXELCRAFT_PLUGIN_STREAM_SERVICE_ID,
    .interface = &TEST_STREAM_INTERFACE};

static bool LoadPlugin(const VoxelcraftPluginContext *context, void **state)
{
    assert(context);
    assert(context->abi_version == VOXELCRAFT_PLUGIN_ABI_VERSION);
    assert(context->host);
    assert(context->plugin_id);
    assert(context->log);
    assert(context->provide_service);
    assert(context->lookup_service);
    assert(state);
    load_order[load_order_count++] = context->plugin_id[0] == 'a' ? 1 : 2;
    load_count++;
    if (context->plugin_id[0] == 'b') {
        assert(!context->provide_service(context, &INVALID_SERVICE));
        assert(context->provide_service(context, &TEST_SERVICE));
        assert(!context->provide_service(context, &TEST_SERVICE));
    } else {
        const VoxelcraftPluginService *service = context->lookup_service(
            context, "test.counter", 1u);
        assert(service);
        assert(service->interface == &TEST_SERVICE_INTERFACE);
    }
    *state = (void *)(uintptr_t)load_count;
    return true;
}

static void UnloadPlugin(void *state)
{
    assert(state);
    unload_count++;
}

static void ResetPlugin(void *state)
{
    assert(state);
    reset_count++;
}

static void UpdatePlugin(void *state, const VoxelcraftPluginFrame *frame)
{
    assert(state);
    assert(frame && frame->frame_index == 42u);
    update_count++;
}

static void RenderPlugin(void *state, const VoxelcraftPluginFrame *frame)
{
    assert(state);
    assert(frame && frame->dt == 0.25f);
    render_count++;
}

static const VoxelcraftPluginDescriptor ALPHA = {
    .abi_version = VOXELCRAFT_PLUGIN_ABI_VERSION,
    .struct_size = sizeof(VoxelcraftPluginDescriptor),
    .id = "alpha",
    .name = "alpha",
    .priority = 20,
    .on_load = LoadPlugin,
    .on_unload = UnloadPlugin,
    .on_reset = ResetPlugin,
    .on_update = UpdatePlugin,
    .on_render = RenderPlugin
};

static const VoxelcraftPluginDescriptor BETA = {
    .abi_version = VOXELCRAFT_PLUGIN_ABI_VERSION,
    .struct_size = sizeof(VoxelcraftPluginDescriptor),
    .id = "beta",
    .name = "beta",
    .priority = 10,
    .on_load = LoadPlugin,
    .on_unload = UnloadPlugin,
    .on_reset = ResetPlugin,
    .on_update = UpdatePlugin,
    .on_render = RenderPlugin
};

static const VoxelcraftPluginDescriptor DUPLICATE = {
    .abi_version = VOXELCRAFT_PLUGIN_ABI_VERSION,
    .struct_size = sizeof(VoxelcraftPluginDescriptor),
    .id = "alpha",
    .name = "duplicate",
    .priority = 30
};

static const VoxelcraftPluginDescriptor BAD_ABI = {
    .abi_version = VOXELCRAFT_PLUGIN_ABI_VERSION + 1u,
    .struct_size = sizeof(VoxelcraftPluginDescriptor),
    .id = "bad-abi"
};

int main(int argc, char **argv)
{
    VoxelcraftPluginHost host;
    VoxelcraftPluginHostInit(&host);
    assert(VoxelcraftPluginHostCount(&host) == 0u);
    assert(!VoxelcraftPluginHostIsActive(&host));
    assert(VoxelcraftPluginHostRegister(&host, &ALPHA));
    assert(VoxelcraftPluginHostRegister(&host, &BETA));
    assert(!VoxelcraftPluginHostRegister(&host, &DUPLICATE));
    assert(VoxelcraftPluginHostLastError(&host)[0] != '\0');
    assert(!VoxelcraftPluginHostRegister(&host, &BAD_ABI));
    assert(VoxelcraftPluginHostCount(&host) == 2u);

    int user_data = 7;
    assert(VoxelcraftPluginHostActivate(&host, &user_data));
    assert(VoxelcraftPluginHostIsActive(&host));
    assert(load_count == 2);
    assert(load_order_count == 2);
    assert(load_order[0] == 2);
    assert(load_order[1] == 1);
    assert(VoxelcraftPluginHostServiceCount(&host) == 1u);

    VoxelcraftPluginFrame frame = {
        .frame_index = 42u, .dt = 0.25f, .debug_mode = true};
    VoxelcraftPluginHostReset(&host);
    VoxelcraftPluginHostUpdate(&host, &frame);
    VoxelcraftPluginHostRender(&host, &frame);
    assert(reset_count == 2);
    assert(update_count == 2);
    assert(render_count == 2);

    VoxelcraftPluginHostShutdown(&host);
    assert(unload_count == 2);
    assert(VoxelcraftPluginHostCount(&host) == 0u);
    assert(VoxelcraftPluginHostServiceCount(&host) == 0u);
    assert(!VoxelcraftPluginHostIsActive(&host));

    VoxelcraftPluginHost builtin_host;
    VoxelcraftPluginHostInit(&builtin_host);
    assert(VoxelcraftPluginHostProvideBuiltin(&builtin_host, "host",
                                              &TEST_SERVICE));
    assert(VoxelcraftPluginHostProvideBuiltin(&builtin_host, "host",
                                              &TEST_WORLD_SERVICE));
    assert(VoxelcraftPluginHostProvideBuiltin(&builtin_host, "host",
                                              &TEST_STREAM_SERVICE));
    assert(VoxelcraftPluginHostServiceCount(&builtin_host) == 3u);
    VoxelcraftPluginContext consumer_context = {
        .abi_version = VOXELCRAFT_PLUGIN_ABI_VERSION,
        .host = &builtin_host,
        .plugin_id = "consumer"};
    assert(VoxelcraftPluginHostLookup(&consumer_context, "test.counter", 1u) ==
           &builtin_host.services[0].service);
    const VoxelcraftPluginService *world_service =
        VoxelcraftPluginHostLookup(
            &consumer_context, VOXELCRAFT_PLUGIN_WORLD_SERVICE_ID,
            VOXELCRAFT_PLUGIN_WORLD_SERVICE_ABI_VERSION);
    assert(world_service);
    const VoxelcraftPluginWorldService *world_interface =
        world_service->interface;
    VoxelcraftPluginWorldSnapshot world_snapshot = {
        .struct_size = sizeof(VoxelcraftPluginWorldSnapshot)};
    assert(world_interface->snapshot(NULL, &world_snapshot));
    assert(world_snapshot.surface_active);
    assert(world_snapshot.dimension == VOXELCRAFT_PLUGIN_WORLD_DIMENSION_PLANET);
    assert(world_snapshot.surface_id == 17u);
    assert(world_snapshot.seed == 23u);
    assert(world_snapshot.map_origin_x == -128);
    assert(world_snapshot.map_origin_z == 256);
    const VoxelcraftPluginService *stream_service =
        VoxelcraftPluginHostLookup(
            &consumer_context, VOXELCRAFT_PLUGIN_STREAM_SERVICE_ID,
            VOXELCRAFT_PLUGIN_STREAM_SERVICE_ABI_VERSION);
    assert(stream_service);
    const VoxelcraftPluginStreamService *stream_interface =
        stream_service->interface;
    VoxelcraftPluginStreamSnapshot stream_snapshot = {
        .struct_size = sizeof(VoxelcraftPluginStreamSnapshot)};
    assert(stream_interface->snapshot(NULL, &stream_snapshot));
    assert(stream_snapshot.surface_active);
    assert(stream_snapshot.render_distance == 12);
    assert(stream_snapshot.loaded_chunks == 81);
    assert(stream_snapshot.pending_generation_jobs == 2);
    assert(stream_snapshot.worker_threads_active == 1u);
    assert(stream_snapshot.generation_submitted == 100u);
    assert(stream_snapshot.mesh_completed == 87u);
    assert(stream_snapshot.preload_active);
    assert(stream_snapshot.preload_offset_z == -1);
    assert(stream_snapshot.preload_pending_mesh_jobs == 2);
    VoxelcraftPluginHostShutdown(&builtin_host);

    if (argc > 1) {
        VoxelcraftPluginHost dynamic_host;
        VoxelcraftPluginHostInit(&dynamic_host);
        assert(VoxelcraftPluginHostLoadDirectory(&dynamic_host, argv[1]));
        assert(VoxelcraftPluginHostCount(&dynamic_host) > 0u);
        assert(VoxelcraftPluginHostActivate(&dynamic_host, NULL));
        VoxelcraftPluginHostShutdown(&dynamic_host);
    }
    return 0;
}
