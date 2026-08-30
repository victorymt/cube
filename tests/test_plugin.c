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

static bool LoadPlugin(const VoxelcraftPluginContext *context, void **state)
{
    assert(context);
    assert(context->abi_version == VOXELCRAFT_PLUGIN_ABI_VERSION);
    assert(context->host);
    assert(context->plugin_id);
    assert(context->log);
    assert(state);
    load_order[load_order_count++] = context->plugin_id[0] == 'a' ? 1 : 2;
    load_count++;
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
    assert(!VoxelcraftPluginHostIsActive(&host));

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
