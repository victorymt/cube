#include "core/plugin.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PluginFileName {
    char *name;
} PluginFileName;

static char *PluginStringDuplicate(const char *text)
{
    if (!text) return NULL;
    size_t length = strlen(text) + 1u;
    char *copy = malloc(length);
    if (!copy) return NULL;
    memcpy(copy, text, length);
    return copy;
}

static void PluginSetError(VoxelcraftPluginHost *host, const char *format, ...)
{
    if (!host) return;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(host->last_error, sizeof(host->last_error), format, arguments);
    va_end(arguments);
}

static bool PluginIdentifierIsValid(const char *id, size_t maxLength)
{
    if (!id || id[0] == '\0') return false;
    size_t length = strlen(id);
    if (length >= maxLength) return false;
    for (size_t index = 0; index < length; index++) {
        char character = id[index];
        bool valid = (character >= 'a' && character <= 'z') ||
                     (character >= 'A' && character <= 'Z') ||
                     (character >= '0' && character <= '9') ||
                     character == '_' || character == '-' || character == '.';
        if (!valid) return false;
    }
    return true;
}

static bool PluginIdIsValid(const char *id)
{
    return PluginIdentifierIsValid(id, VOXELCRAFT_PLUGIN_MAX_ID);
}

static bool PluginServiceIdIsValid(const char *id)
{
    return PluginIdentifierIsValid(id, VOXELCRAFT_PLUGIN_MAX_SERVICE_ID);
}

static bool PluginServiceIsValid(VoxelcraftPluginHost *host,
                                 const VoxelcraftPluginService *service)
{
    if (!service) {
        PluginSetError(host, "plugin service is null");
        return false;
    }
    if (service->abi_version == 0u) {
        PluginSetError(host, "plugin service '%s' has no ABI version",
                       service->id ? service->id : "<unknown>");
        return false;
    }
    if (service->struct_size < sizeof(VoxelcraftPluginService)) {
        PluginSetError(host, "plugin service '%s' descriptor is too small",
                       service->id ? service->id : "<unknown>");
        return false;
    }
    if (!PluginServiceIdIsValid(service->id)) {
        PluginSetError(host,
                       "plugin service id is missing or contains invalid "
                       "characters");
        return false;
    }
    if (!service->interface) {
        PluginSetError(host, "plugin service '%s' interface is null",
                       service->id);
        return false;
    }
    return true;
}

static bool PluginDescriptorIsValid(VoxelcraftPluginHost *host,
                                    const VoxelcraftPluginDescriptor *descriptor)
{
    if (!descriptor) {
        PluginSetError(host, "plugin descriptor is null");
        return false;
    }
    if (descriptor->abi_version != VOXELCRAFT_PLUGIN_ABI_VERSION) {
        PluginSetError(host, "plugin '%s' requires ABI %u (host ABI %u)",
                       descriptor->id ? descriptor->id : "<unknown>",
                       descriptor->abi_version,
                       VOXELCRAFT_PLUGIN_ABI_VERSION);
        return false;
    }
    if (descriptor->struct_size < sizeof(VoxelcraftPluginDescriptor)) {
        PluginSetError(host, "plugin '%s' descriptor is too small",
                       descriptor->id ? descriptor->id : "<unknown>");
        return false;
    }
    if (!PluginIdIsValid(descriptor->id)) {
        PluginSetError(host, "plugin id is missing or contains invalid characters");
        return false;
    }
    return true;
}

static bool PluginIdAlreadyRegistered(const VoxelcraftPluginHost *host,
                                      const char *id)
{
    for (size_t index = 0; index < host->count; index++) {
        const VoxelcraftPluginDescriptor *registered =
            host->instances[index].descriptor;
        if (registered && strcmp(registered->id, id) == 0) return true;
    }
    return false;
}

static bool PluginHostAdd(VoxelcraftPluginHost *host,
                          const VoxelcraftPluginDescriptor *descriptor,
                          void *libraryHandle)
{
    if (!host || host->active) {
        if (host) PluginSetError(host, "plugins cannot be registered after activation");
        return false;
    }
    if (!PluginDescriptorIsValid(host, descriptor)) return false;
    if (PluginIdAlreadyRegistered(host, descriptor->id)) {
        PluginSetError(host, "duplicate plugin id '%s'", descriptor->id);
        return false;
    }
    if (host->count >= VOXELCRAFT_PLUGIN_MAX_COUNT) {
        PluginSetError(host, "plugin limit (%u) exceeded",
                       VOXELCRAFT_PLUGIN_MAX_COUNT);
        return false;
    }
    host->instances[host->count++] = (VoxelcraftPluginInstance){
        .descriptor = descriptor,
        .library_handle = libraryHandle,
        .state = NULL,
        .active = false
    };
    return true;
}

static int PluginInstanceCompare(const void *left, const void *right)
{
    const VoxelcraftPluginInstance *a = left;
    const VoxelcraftPluginInstance *b = right;
    int priorityA = a->descriptor ? a->descriptor->priority : 0;
    int priorityB = b->descriptor ? b->descriptor->priority : 0;
    if (priorityA != priorityB) return priorityA < priorityB ? -1 : 1;
    return strcmp(a->descriptor->id, b->descriptor->id);
}

static void PluginHostClear(VoxelcraftPluginHost *host)
{
    if (!host) return;
    for (size_t index = host->count; index > 0; index--) {
        VoxelcraftPluginInstance *instance = &host->instances[index - 1];
        if (instance->active && instance->descriptor &&
            instance->descriptor->on_unload) {
            instance->descriptor->on_unload(instance->state);
        }
        if (instance->library_handle) dlclose(instance->library_handle);
        *instance = (VoxelcraftPluginInstance){ 0 };
    }
    host->count = 0;
    memset(host->services, 0, sizeof(host->services));
    host->service_count = 0;
    host->active = false;
    host->user_data = NULL;
}

static void PluginHostDefaultLog(void *userData,
                                 VoxelcraftPluginLogLevel level,
                                 const char *message)
{
    (void)userData;
    const char *label = level == VOXELCRAFT_PLUGIN_LOG_ERROR
                            ? "error"
                            : level == VOXELCRAFT_PLUGIN_LOG_WARNING
                                  ? "warning"
                                  : "info";
    fprintf(stderr, "[voxelcraft plugin %s] %s\n", label,
            message ? message : "");
}

static bool PluginContextProvideService(
    const VoxelcraftPluginContext *context,
    const VoxelcraftPluginService *service)
{
    return VoxelcraftPluginHostProvide(context, service);
}

static const VoxelcraftPluginService *PluginContextLookupService(
    const VoxelcraftPluginContext *context, const char *serviceId,
    uint32_t abiVersion)
{
    return VoxelcraftPluginHostLookup(context, serviceId, abiVersion);
}

static int PluginFileNameCompare(const void *left, const void *right)
{
    const PluginFileName *a = left;
    const PluginFileName *b = right;
    return strcmp(a->name, b->name);
}

static bool PluginFileNameHasSharedObjectSuffix(const char *name)
{
    if (!name) return false;
    size_t length = strlen(name);
    return length > 3 && strcmp(name + length - 3, ".so") == 0;
}

void VoxelcraftPluginHostInit(VoxelcraftPluginHost *host)
{
    if (!host) return;
    *host = (VoxelcraftPluginHost){ 0 };
}

bool VoxelcraftPluginHostRegister(
    VoxelcraftPluginHost *host, const VoxelcraftPluginDescriptor *descriptor)
{
    return PluginHostAdd(host, descriptor, NULL);
}

bool VoxelcraftPluginHostLoadDirectory(VoxelcraftPluginHost *host,
                                       const char *directory)
{
    if (!host || !directory || directory[0] == '\0') {
        if (host) PluginSetError(host, "plugin directory is empty");
        return false;
    }
    if (host->active) {
        PluginSetError(host, "plugins cannot be loaded after activation");
        return false;
    }

    DIR *directoryHandle = opendir(directory);
    if (!directoryHandle) {
        PluginSetError(host, "cannot open plugin directory '%s': %s", directory,
                       strerror(errno));
        return false;
    }

    PluginFileName *files = NULL;
    size_t fileCount = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(directoryHandle)) != NULL) {
        if (entry->d_name[0] == '.' ||
            !PluginFileNameHasSharedObjectSuffix(entry->d_name)) {
            continue;
        }
        char *copy = PluginStringDuplicate(entry->d_name);
        if (!copy) {
            PluginSetError(host, "out of memory while listing '%s'", directory);
            closedir(directoryHandle);
            for (size_t index = 0; index < fileCount; index++)
                free(files[index].name);
            free(files);
            PluginHostClear(host);
            return false;
        }
        PluginFileName *grown = realloc(files, (fileCount + 1) * sizeof(*files));
        if (!grown) {
            PluginSetError(host, "out of memory while listing '%s'", directory);
            free(copy);
            closedir(directoryHandle);
            for (size_t index = 0; index < fileCount; index++) free(files[index].name);
            free(files);
            PluginHostClear(host);
            return false;
        }
        files = grown;
        files[fileCount++].name = copy;
    }
    closedir(directoryHandle);
    if (fileCount > 1u)
        qsort(files, fileCount, sizeof(*files), PluginFileNameCompare);

    for (size_t index = 0; index < fileCount; index++) {
        size_t directoryLength = strlen(directory);
        size_t fileLength = strlen(files[index].name);
        bool needsSeparator = directoryLength > 0 &&
                              directory[directoryLength - 1] != '/';
        if (directoryLength + (needsSeparator ? 1u : 0u) + fileLength >=
            VOXELCRAFT_PLUGIN_MAX_PATH) {
            PluginSetError(host, "plugin path is too long: '%s'", files[index].name);
            PluginHostClear(host);
            goto fail_files;
        }
        char path[VOXELCRAFT_PLUGIN_MAX_PATH];
        size_t pathLength = directoryLength + (needsSeparator ? 1u : 0u) +
                            fileLength;
        memcpy(path, directory, directoryLength);
        size_t pathOffset = directoryLength;
        if (needsSeparator) path[pathOffset++] = '/';
        memcpy(path + pathOffset, files[index].name, fileLength);
        path[pathLength] = '\0';
        dlerror();
        void *libraryHandle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!libraryHandle) {
            const char *error = dlerror();
            PluginSetError(host, "cannot load '%s': %s", files[index].name,
                           error ? error : "unknown loader error");
            PluginHostClear(host);
            goto fail_files;
        }
        dlerror();
        VoxelcraftPluginEntryPoint entryPoint =
            (VoxelcraftPluginEntryPoint)dlsym(libraryHandle,
                                              "VoxelcraftPluginGetDescriptor");
        const char *symbolError = dlerror();
        if (symbolError || !entryPoint) {
            PluginSetError(host, "'%s' does not export VoxelcraftPluginGetDescriptor",
                           files[index].name);
            dlclose(libraryHandle);
            PluginHostClear(host);
            goto fail_files;
        }
        const VoxelcraftPluginDescriptor *descriptor = entryPoint();
        if (!PluginHostAdd(host, descriptor, libraryHandle)) {
            dlclose(libraryHandle);
            PluginHostClear(host);
            goto fail_files;
        }
    }

    for (size_t index = 0; index < fileCount; index++) free(files[index].name);
    free(files);
    return true;

fail_files:
    for (size_t index = 0; index < fileCount; index++) free(files[index].name);
    free(files);
    return false;
}

bool VoxelcraftPluginHostActivate(VoxelcraftPluginHost *host, void *userData)
{
    if (!host) return false;
    if (host->active) return true;
    if (host->count > 1u) {
        qsort(host->instances, host->count, sizeof(host->instances[0]),
              PluginInstanceCompare);
    }
    host->user_data = userData;
    for (size_t index = 0; index < host->count; index++) {
        VoxelcraftPluginInstance *instance = &host->instances[index];
        const VoxelcraftPluginDescriptor *descriptor = instance->descriptor;
        if (descriptor->on_load) {
            VoxelcraftPluginContext context = {
                .abi_version = VOXELCRAFT_PLUGIN_ABI_VERSION,
                .host = host,
                .plugin_id = descriptor->id,
                .user_data = userData,
                .log = PluginHostDefaultLog,
                .provide_service = PluginContextProvideService,
                .lookup_service = PluginContextLookupService
            };
            if (!descriptor->on_load(&context, &instance->state)) {
                PluginSetError(host, "plugin '%s' rejected activation",
                               descriptor->id);
                PluginHostClear(host);
                return false;
            }
        }
        instance->active = true;
    }
    host->active = true;
    host->last_error[0] = '\0';
    return true;
}

static bool PluginHostProvideForOwner(VoxelcraftPluginHost *host,
                                      const char *ownerId,
                                      const VoxelcraftPluginService *service)
{
    if (!host || !PluginIdIsValid(ownerId)) {
        if (host) PluginSetError(host, "plugin service owner id is invalid");
        return false;
    }
    if (!PluginServiceIsValid(host, service)) return false;
    for (size_t index = 0; index < host->service_count; index++) {
        const VoxelcraftPluginServiceRegistration *registered =
            &host->services[index];
        if (strcmp(registered->service.id, service->id) == 0) {
            PluginSetError(host, "duplicate plugin service id '%s'",
                           service->id);
            return false;
        }
    }
    if (host->service_count >= VOXELCRAFT_PLUGIN_MAX_SERVICE_COUNT) {
        PluginSetError(host, "plugin service limit (%u) exceeded",
                       VOXELCRAFT_PLUGIN_MAX_SERVICE_COUNT);
        return false;
    }
    host->services[host->service_count++] =
        (VoxelcraftPluginServiceRegistration){
            .service = *service, .owner_plugin_id = ownerId};
    return true;
}

bool VoxelcraftPluginHostProvideBuiltin(
    VoxelcraftPluginHost *host, const char *ownerId,
    const VoxelcraftPluginService *service)
{
    return PluginHostProvideForOwner(host, ownerId, service);
}

bool VoxelcraftPluginHostProvide(const VoxelcraftPluginContext *context,
                                 const VoxelcraftPluginService *service)
{
    if (!context || !context->host || !context->plugin_id) return false;
    return PluginHostProvideForOwner((VoxelcraftPluginHost *)context->host,
                                     context->plugin_id, service);
}

const VoxelcraftPluginService *VoxelcraftPluginHostLookup(
    const VoxelcraftPluginContext *context, const char *serviceId,
    uint32_t abiVersion)
{
    if (!context || !context->host || !PluginServiceIdIsValid(serviceId) ||
        abiVersion == 0u) {
        return NULL;
    }
    const VoxelcraftPluginHost *host = context->host;
    for (size_t index = 0; index < host->service_count; index++) {
        const VoxelcraftPluginService *service =
            &host->services[index].service;
        if (service->abi_version == abiVersion &&
            strcmp(service->id, serviceId) == 0) {
            return service;
        }
    }
    return NULL;
}

size_t VoxelcraftPluginHostServiceCount(const VoxelcraftPluginHost *host)
{
    return host ? host->service_count : 0u;
}

void VoxelcraftPluginHostReset(VoxelcraftPluginHost *host)
{
    if (!host || !host->active) return;
    for (size_t index = 0; index < host->count; index++) {
        VoxelcraftPluginInstance *instance = &host->instances[index];
        if (instance->active && instance->descriptor->on_reset)
            instance->descriptor->on_reset(instance->state);
    }
}

void VoxelcraftPluginHostUpdate(VoxelcraftPluginHost *host,
                                const VoxelcraftPluginFrame *frame)
{
    if (!host || !host->active || !frame) return;
    for (size_t index = 0; index < host->count; index++) {
        VoxelcraftPluginInstance *instance = &host->instances[index];
        if (instance->active && instance->descriptor->on_update)
            instance->descriptor->on_update(instance->state, frame);
    }
}

void VoxelcraftPluginHostRender(VoxelcraftPluginHost *host,
                                const VoxelcraftPluginFrame *frame)
{
    if (!host || !host->active || !frame) return;
    for (size_t index = 0; index < host->count; index++) {
        VoxelcraftPluginInstance *instance = &host->instances[index];
        if (instance->active && instance->descriptor->on_render)
            instance->descriptor->on_render(instance->state, frame);
    }
}

void VoxelcraftPluginHostShutdown(VoxelcraftPluginHost *host)
{
    PluginHostClear(host);
    if (host) host->last_error[0] = '\0';
}

size_t VoxelcraftPluginHostCount(const VoxelcraftPluginHost *host)
{
    return host ? host->count : 0;
}

bool VoxelcraftPluginHostIsActive(const VoxelcraftPluginHost *host)
{
    return host && host->active;
}

const char *VoxelcraftPluginHostLastError(const VoxelcraftPluginHost *host)
{
    return host ? host->last_error : "";
}
