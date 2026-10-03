#ifndef VOXELCRAFT_PLUGIN_H
#define VOXELCRAFT_PLUGIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The plugin ABI is deliberately small and independent from raylib and the
 * game's private state.  A plugin receives lifecycle callbacks and an opaque
 * host user-data pointer; new game services can therefore be added without
 * exposing mutable globals from the domain modules.
 */
/* ABI v2 adds injected service hooks to VoxelcraftPluginContext. */
#define VOXELCRAFT_PLUGIN_ABI_VERSION 2u
#define VOXELCRAFT_PLUGIN_MAX_COUNT 64u
#define VOXELCRAFT_PLUGIN_MAX_ID 64u
#define VOXELCRAFT_PLUGIN_MAX_SERVICE_COUNT 64u
#define VOXELCRAFT_PLUGIN_MAX_SERVICE_ID 64u
#define VOXELCRAFT_PLUGIN_MAX_PATH 512u
#define VOXELCRAFT_PLUGIN_MAX_ERROR 256u
#define VOXELCRAFT_PLUGIN_GAME_SERVICE_ID "voxelcraft.game"
#define VOXELCRAFT_PLUGIN_GAME_SERVICE_ABI_VERSION 1u
#define VOXELCRAFT_PLUGIN_WORLD_SERVICE_ID "voxelcraft.world"
#define VOXELCRAFT_PLUGIN_WORLD_SERVICE_ABI_VERSION 1u
#define VOXELCRAFT_PLUGIN_STREAM_SERVICE_ID "voxelcraft.stream"
#define VOXELCRAFT_PLUGIN_STREAM_SERVICE_ABI_VERSION 1u
#define VOXELCRAFT_PLUGIN_STREAM_V2_SERVICE_ID "voxelcraft.stream.v2"
#define VOXELCRAFT_PLUGIN_STREAM_V2_SERVICE_ABI_VERSION 2u
#define VOXELCRAFT_PLUGIN_HYDROLOGY_SERVICE_ID "voxelcraft.hydrology"
#define VOXELCRAFT_PLUGIN_HYDROLOGY_SERVICE_ABI_VERSION 1u

typedef struct VoxelcraftPluginHost VoxelcraftPluginHost;
typedef struct VoxelcraftPluginContext VoxelcraftPluginContext;
typedef struct VoxelcraftPluginService VoxelcraftPluginService;

typedef enum VoxelcraftPluginLogLevel {
    VOXELCRAFT_PLUGIN_LOG_INFO = 0,
    VOXELCRAFT_PLUGIN_LOG_WARNING,
    VOXELCRAFT_PLUGIN_LOG_ERROR
} VoxelcraftPluginLogLevel;

typedef void (*VoxelcraftPluginLogFn)(
    void *user_data, VoxelcraftPluginLogLevel level, const char *message);

typedef bool (*VoxelcraftPluginProvideServiceFn)(
    const VoxelcraftPluginContext *context,
    const VoxelcraftPluginService *service);
typedef const VoxelcraftPluginService *(*VoxelcraftPluginLookupServiceFn)(
    const VoxelcraftPluginContext *context, const char *service_id,
    uint32_t abi_version);

struct VoxelcraftPluginContext {
    uint32_t abi_version;
    const VoxelcraftPluginHost *host;
    const char *plugin_id;
    void *user_data;
    VoxelcraftPluginLogFn log;
    /* Service hooks are appended after the original v1 context fields. */
    VoxelcraftPluginProvideServiceFn provide_service;
    VoxelcraftPluginLookupServiceFn lookup_service;
};

typedef struct VoxelcraftPluginFrame {
    uint64_t frame_index;
    float dt;
    bool debug_mode;
} VoxelcraftPluginFrame;

/*
 * A service is a versioned, read-only interface published by one plugin for
 * another.  The interface points at plugin-owned data and is valid until the
 * host shuts down; consumers must not retain it across that boundary.
 */
struct VoxelcraftPluginService {
    uint32_t abi_version;
    uint32_t struct_size;
    const char *id;
    const void *interface;
};

/* Neutral read-only game hooks exposed by the app through the service table. */
typedef struct VoxelcraftPluginGameService {
    uint32_t abi_version;
    uint32_t struct_size;
    void *user_data;
    bool (*is_playing)(void *user_data);
    bool (*is_paused)(void *user_data);
    uint64_t (*frame_index)(void *user_data);
    uint32_t (*world_seed)(void *user_data);
    void (*post_notice)(void *user_data, const char *message);
} VoxelcraftPluginGameService;

/* Stable, read-only world coordinates exposed without world-module types. */
typedef enum VoxelcraftPluginWorldDimension {
    VOXELCRAFT_PLUGIN_WORLD_DIMENSION_HOME = 0,
    VOXELCRAFT_PLUGIN_WORLD_DIMENSION_PLANET,
    VOXELCRAFT_PLUGIN_WORLD_DIMENSION_SPACE,
    VOXELCRAFT_PLUGIN_WORLD_DIMENSION_NETHER
} VoxelcraftPluginWorldDimension;

typedef enum VoxelcraftPluginWorldSurfaceKind {
    VOXELCRAFT_PLUGIN_WORLD_SURFACE_NONE = 0,
    VOXELCRAFT_PLUGIN_WORLD_SURFACE_HOME,
    VOXELCRAFT_PLUGIN_WORLD_SURFACE_PLANET
} VoxelcraftPluginWorldSurfaceKind;

typedef struct VoxelcraftPluginWorldSnapshot {
    uint32_t struct_size;
    bool surface_active;
    /* These use fixed-width fields; compare with the enum constants above. */
    uint32_t dimension;
    uint32_t surface_kind;
    uint32_t surface_id;
    uint32_t seed;
    int32_t map_origin_x;
    int32_t map_origin_z;
} VoxelcraftPluginWorldSnapshot;

typedef struct VoxelcraftPluginWorldService {
    uint32_t abi_version;
    uint32_t struct_size;
    void *user_data;
    bool (*snapshot)(void *user_data,
                     VoxelcraftPluginWorldSnapshot *out_snapshot);
} VoxelcraftPluginWorldService;

/* Bounded streaming/worker diagnostics for tooling and telemetry plugins. */
typedef struct VoxelcraftPluginStreamSnapshot {
    uint32_t struct_size;
    bool surface_active;
    int32_t render_distance;
    int32_t loaded_chunks;
    int32_t pending_generation_jobs;
    int32_t pending_mesh_jobs;
    uint32_t worker_threads_configured;
    uint32_t worker_threads_started;
    uint32_t worker_threads_active;
    uint64_t generation_submitted;
    uint64_t generation_completed;
    uint64_t mesh_submitted;
    uint64_t mesh_completed;
    uint64_t generation_queue_peak;
    uint64_t mesh_queue_peak;
    bool preload_active;
    int32_t preload_offset_x;
    int32_t preload_offset_z;
    int32_t preload_target_chunks;
    int32_t preload_loaded_chunks;
    int32_t preload_pending_generation_jobs;
    int32_t preload_pending_mesh_jobs;
} VoxelcraftPluginStreamSnapshot;

typedef struct VoxelcraftPluginStreamService {
    uint32_t abi_version;
    uint32_t struct_size;
    void *user_data;
    bool (*snapshot)(void *user_data,
                     VoxelcraftPluginStreamSnapshot *out_snapshot);
} VoxelcraftPluginStreamService;

/* Separate service ID preserves the v1 registry and binary layout. All
 * counters and identity fields come from one observation. Main thread only.
 * IDs are process-local; frame/tick/world generation correlate observations
 * from different consumers, whose capture IDs will normally differ. */
typedef struct VoxelcraftPluginStreamSnapshotV2 {
    uint32_t struct_size;
    uint64_t capture_id;
    uint64_t frame_id;
    uint64_t simulation_tick;
    uint64_t world_generation;
    uint64_t capture_sequence;
    VoxelcraftPluginStreamSnapshot counters;
} VoxelcraftPluginStreamSnapshotV2;

typedef struct VoxelcraftPluginStreamServiceV2 {
    uint32_t abi_version;
    uint32_t struct_size;
    void *user_data;
    bool (*snapshot)(void *user_data,
                     VoxelcraftPluginStreamSnapshotV2 *out_snapshot);
} VoxelcraftPluginStreamServiceV2;

/* Read-only hydrology runtime state and coarse-cell feedback. */
typedef struct VoxelcraftPluginHydrologySnapshot {
    uint32_t struct_size;
    bool active;
    uint32_t surface_id;
    int32_t region_x;
    int32_t region_z;
    uint64_t step_count;
    double simulation_time_seconds;
    double accumulator_seconds;
    double total_water_volume_m3;
    double total_sediment_mass_kg;
} VoxelcraftPluginHydrologySnapshot;

typedef struct VoxelcraftPluginHydrologyFeedback {
    uint32_t struct_size;
    bool active;
    float surface_water_m;
    float soil_moisture;
    float groundwater_m;
    float runoff_m3_per_s;
    float evaporation_m_per_s;
    float sediment_load_kg_m2;
} VoxelcraftPluginHydrologyFeedback;

typedef struct VoxelcraftPluginHydrologyService {
    uint32_t abi_version;
    uint32_t struct_size;
    void *user_data;
    bool (*snapshot)(void *user_data,
                     VoxelcraftPluginHydrologySnapshot *out_snapshot);
    bool (*feedback_at)(void *user_data, uint32_t surface_id,
                        int32_t map_x, int32_t map_z,
                        VoxelcraftPluginHydrologyFeedback *out_feedback);
} VoxelcraftPluginHydrologyService;

typedef struct VoxelcraftPluginDescriptor {
    uint32_t abi_version;
    uint32_t struct_size;
    const char *id;
    const char *name;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
    int priority;

    bool (*on_load)(const VoxelcraftPluginContext *context, void **state);
    void (*on_unload)(void *state);
    void (*on_reset)(void *state);
    void (*on_update)(void *state, const VoxelcraftPluginFrame *frame);
    void (*on_render)(void *state, const VoxelcraftPluginFrame *frame);
} VoxelcraftPluginDescriptor;

/* Every shared object must export this exact symbol. */
typedef const VoxelcraftPluginDescriptor *(*VoxelcraftPluginEntryPoint)(void);

typedef struct VoxelcraftPluginInstance {
    const VoxelcraftPluginDescriptor *descriptor;
    void *library_handle;
    void *state;
    bool active;
} VoxelcraftPluginInstance;

typedef struct VoxelcraftPluginServiceRegistration {
    VoxelcraftPluginService service;
    const char *owner_plugin_id;
} VoxelcraftPluginServiceRegistration;

struct VoxelcraftPluginHost {
    VoxelcraftPluginInstance instances[VOXELCRAFT_PLUGIN_MAX_COUNT];
    size_t count;
    VoxelcraftPluginServiceRegistration
        services[VOXELCRAFT_PLUGIN_MAX_SERVICE_COUNT];
    size_t service_count;
    bool active;
    void *user_data;
    char last_error[VOXELCRAFT_PLUGIN_MAX_ERROR];
};

void VoxelcraftPluginHostInit(VoxelcraftPluginHost *host);

/* Register an in-process plugin before activation. */
bool VoxelcraftPluginHostRegister(
    VoxelcraftPluginHost *host, const VoxelcraftPluginDescriptor *descriptor);

/* Load every *.so in a directory. Files are visited in sorted order. */
bool VoxelcraftPluginHostLoadDirectory(
    VoxelcraftPluginHost *host, const char *directory);

/* Activate registered plugins in priority/id order. */
bool VoxelcraftPluginHostActivate(VoxelcraftPluginHost *host, void *user_data);

/* Publish or find a versioned service through the current plugin context. */
bool VoxelcraftPluginHostProvideBuiltin(
    VoxelcraftPluginHost *host, const char *owner_id,
    const VoxelcraftPluginService *service);
bool VoxelcraftPluginHostProvide(const VoxelcraftPluginContext *context,
                                 const VoxelcraftPluginService *service);
const VoxelcraftPluginService *VoxelcraftPluginHostLookup(
    const VoxelcraftPluginContext *context, const char *service_id,
    uint32_t abi_version);
size_t VoxelcraftPluginHostServiceCount(const VoxelcraftPluginHost *host);

void VoxelcraftPluginHostReset(VoxelcraftPluginHost *host);
void VoxelcraftPluginHostUpdate(
    VoxelcraftPluginHost *host, const VoxelcraftPluginFrame *frame);
void VoxelcraftPluginHostRender(
    VoxelcraftPluginHost *host, const VoxelcraftPluginFrame *frame);
void VoxelcraftPluginHostShutdown(VoxelcraftPluginHost *host);

size_t VoxelcraftPluginHostCount(const VoxelcraftPluginHost *host);
bool VoxelcraftPluginHostIsActive(const VoxelcraftPluginHost *host);
const char *VoxelcraftPluginHostLastError(const VoxelcraftPluginHost *host);

#endif
