#include "core/plugin.h"

#include <stdint.h>
#include <stdlib.h>

typedef struct TelemetryState {
    uint64_t update_count;
    const VoxelcraftPluginGameService *game_service;
    uint32_t last_world_seed;
} TelemetryState;

static bool TelemetryLoad(const VoxelcraftPluginContext *context, void **state)
{
    if (!context || !state) return false;
    TelemetryState *telemetry = calloc(1, sizeof(*telemetry));
    if (!telemetry) return false;
    const VoxelcraftPluginService *game_service =
        context->lookup_service
            ? context->lookup_service(
                  context, VOXELCRAFT_PLUGIN_GAME_SERVICE_ID,
                  VOXELCRAFT_PLUGIN_GAME_SERVICE_ABI_VERSION)
            : NULL;
    if (game_service && game_service->interface) {
        telemetry->game_service = game_service->interface;
        if (context->log) {
            context->log(context->user_data, VOXELCRAFT_PLUGIN_LOG_INFO,
                         "telemetry connected to voxelcraft.game service");
        }
    }
    *state = telemetry;
    if (context->log) {
        context->log(context->user_data, VOXELCRAFT_PLUGIN_LOG_INFO,
                     "telemetry plugin loaded");
    }
    return true;
}

static void TelemetryUnload(void *state)
{
    free(state);
}

static void TelemetryReset(void *state)
{
    TelemetryState *telemetry = state;
    if (telemetry) telemetry->update_count = 0;
}

static void TelemetryUpdate(void *state, const VoxelcraftPluginFrame *frame)
{
    TelemetryState *telemetry = state;
    if (telemetry && frame) {
        telemetry->update_count++;
        if (telemetry->game_service &&
            telemetry->game_service->world_seed) {
            telemetry->last_world_seed =
                telemetry->game_service->world_seed(
                    telemetry->game_service->user_data);
        }
    }
}

static const VoxelcraftPluginDescriptor TELEMETRY_DESCRIPTOR = {
    .abi_version = VOXELCRAFT_PLUGIN_ABI_VERSION,
    .struct_size = sizeof(VoxelcraftPluginDescriptor),
    .id = "telemetry",
    .name = "Frame telemetry sample",
    .version_major = 1,
    .version_minor = 0,
    .version_patch = 0,
    .priority = 100,
    .on_load = TelemetryLoad,
    .on_unload = TelemetryUnload,
    .on_reset = TelemetryReset,
    .on_update = TelemetryUpdate,
    .on_render = NULL
};

const VoxelcraftPluginDescriptor *VoxelcraftPluginGetDescriptor(void)
{
    return &TELEMETRY_DESCRIPTOR;
}
