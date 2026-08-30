#include "core/plugin.h"

#include <stdint.h>
#include <stdlib.h>

typedef struct TelemetryState {
    uint64_t update_count;
    const VoxelcraftPluginGameService *game_service;
    const VoxelcraftPluginWorldService *world_service;
    VoxelcraftPluginWorldSnapshot world_snapshot;
    const VoxelcraftPluginStreamService *stream_service;
    VoxelcraftPluginStreamSnapshot stream_snapshot;
    const VoxelcraftPluginHydrologyService *hydrology_service;
    VoxelcraftPluginHydrologySnapshot hydrology_snapshot;
    VoxelcraftPluginHydrologyFeedback hydrology_feedback;
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
    const VoxelcraftPluginService *world_service =
        context->lookup_service
            ? context->lookup_service(
                  context, VOXELCRAFT_PLUGIN_WORLD_SERVICE_ID,
                  VOXELCRAFT_PLUGIN_WORLD_SERVICE_ABI_VERSION)
            : NULL;
    if (world_service && world_service->interface) {
        const VoxelcraftPluginWorldService *interface =
            world_service->interface;
        if (interface->abi_version ==
                VOXELCRAFT_PLUGIN_WORLD_SERVICE_ABI_VERSION &&
            interface->struct_size >= sizeof(VoxelcraftPluginWorldService) &&
            interface->snapshot) {
            telemetry->world_service = interface;
            telemetry->world_snapshot.struct_size =
                sizeof(VoxelcraftPluginWorldSnapshot);
            if (context->log) {
                context->log(
                    context->user_data, VOXELCRAFT_PLUGIN_LOG_INFO,
                    "telemetry connected to voxelcraft.world service");
            }
        }
    }
    const VoxelcraftPluginService *stream_service =
        context->lookup_service
            ? context->lookup_service(
                  context, VOXELCRAFT_PLUGIN_STREAM_SERVICE_ID,
                  VOXELCRAFT_PLUGIN_STREAM_SERVICE_ABI_VERSION)
            : NULL;
    if (stream_service && stream_service->interface) {
        const VoxelcraftPluginStreamService *interface =
            stream_service->interface;
        if (interface->abi_version ==
                VOXELCRAFT_PLUGIN_STREAM_SERVICE_ABI_VERSION &&
            interface->struct_size >= sizeof(VoxelcraftPluginStreamService) &&
            interface->snapshot) {
            telemetry->stream_service = interface;
            telemetry->stream_snapshot.struct_size =
                sizeof(VoxelcraftPluginStreamSnapshot);
            if (context->log) {
                context->log(
                    context->user_data, VOXELCRAFT_PLUGIN_LOG_INFO,
                    "telemetry connected to voxelcraft.stream service");
            }
        }
    }
    const VoxelcraftPluginService *hydrology_service =
        context->lookup_service
            ? context->lookup_service(
                  context, VOXELCRAFT_PLUGIN_HYDROLOGY_SERVICE_ID,
                  VOXELCRAFT_PLUGIN_HYDROLOGY_SERVICE_ABI_VERSION)
            : NULL;
    if (hydrology_service && hydrology_service->interface) {
        const VoxelcraftPluginHydrologyService *interface =
            hydrology_service->interface;
        if (interface->abi_version ==
                VOXELCRAFT_PLUGIN_HYDROLOGY_SERVICE_ABI_VERSION &&
            interface->struct_size >=
                sizeof(VoxelcraftPluginHydrologyService) &&
            interface->snapshot && interface->feedback_at) {
            telemetry->hydrology_service = interface;
            telemetry->hydrology_snapshot.struct_size =
                sizeof(VoxelcraftPluginHydrologySnapshot);
            telemetry->hydrology_feedback.struct_size =
                sizeof(VoxelcraftPluginHydrologyFeedback);
            if (context->log) {
                context->log(
                    context->user_data, VOXELCRAFT_PLUGIN_LOG_INFO,
                    "telemetry connected to voxelcraft.hydrology service");
            }
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
        if (telemetry->world_service &&
            telemetry->world_service->snapshot(
                telemetry->world_service->user_data,
                &telemetry->world_snapshot)) {
            telemetry->last_world_seed = telemetry->world_snapshot.seed;
        }
        if (telemetry->stream_service) {
            telemetry->stream_service->snapshot(
                telemetry->stream_service->user_data,
                &telemetry->stream_snapshot);
        }
        if (telemetry->hydrology_service &&
            telemetry->hydrology_service->snapshot(
                telemetry->hydrology_service->user_data,
                &telemetry->hydrology_snapshot)) {
            const VoxelcraftPluginHydrologySnapshot *snapshot =
                &telemetry->hydrology_snapshot;
            telemetry->hydrology_service->feedback_at(
                telemetry->hydrology_service->user_data,
                snapshot->surface_id, snapshot->region_x * 4,
                snapshot->region_z * 4, &telemetry->hydrology_feedback);
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
