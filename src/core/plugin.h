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
#define VOXELCRAFT_PLUGIN_ABI_VERSION 1u
#define VOXELCRAFT_PLUGIN_MAX_COUNT 64u
#define VOXELCRAFT_PLUGIN_MAX_ID 64u
#define VOXELCRAFT_PLUGIN_MAX_PATH 512u
#define VOXELCRAFT_PLUGIN_MAX_ERROR 256u

typedef struct VoxelcraftPluginHost VoxelcraftPluginHost;

typedef enum VoxelcraftPluginLogLevel {
    VOXELCRAFT_PLUGIN_LOG_INFO = 0,
    VOXELCRAFT_PLUGIN_LOG_WARNING,
    VOXELCRAFT_PLUGIN_LOG_ERROR
} VoxelcraftPluginLogLevel;

typedef void (*VoxelcraftPluginLogFn)(
    void *user_data, VoxelcraftPluginLogLevel level, const char *message);

typedef struct VoxelcraftPluginContext {
    uint32_t abi_version;
    const VoxelcraftPluginHost *host;
    const char *plugin_id;
    void *user_data;
    VoxelcraftPluginLogFn log;
} VoxelcraftPluginContext;

typedef struct VoxelcraftPluginFrame {
    uint64_t frame_index;
    float dt;
    bool debug_mode;
} VoxelcraftPluginFrame;

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

struct VoxelcraftPluginHost {
    VoxelcraftPluginInstance instances[VOXELCRAFT_PLUGIN_MAX_COUNT];
    size_t count;
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
