#include "world/save_format_internal.h"

#include <string.h>

#define SAVE_MAGIC_V19 "VOXELCRAFT_SAVE_V19"
#define SAVE_MAGIC_V18 "VOXELCRAFT_SAVE_V18"
#define SAVE_MAGIC_LENGTH (sizeof(SAVE_MAGIC_V19) - 1u)

WorldSaveFormat WorldSaveFormatRead(FILE *file)
{
    char magic[SAVE_MAGIC_LENGTH];
    if (!file || fread(magic, 1, sizeof(magic), file) != sizeof(magic)) {
        return WORLD_SAVE_FORMAT_UNSUPPORTED;
    }
    if (memcmp(magic, SAVE_MAGIC_V19, sizeof(magic)) == 0) {
        return WORLD_SAVE_FORMAT_V19;
    }
    if (memcmp(magic, SAVE_MAGIC_V18, sizeof(magic)) == 0) {
        return WORLD_SAVE_FORMAT_V18;
    }
    return WORLD_SAVE_FORMAT_UNSUPPORTED;
}

bool WorldSaveFormatWriteCurrent(FILE *file)
{
    return file &&
           fwrite(SAVE_MAGIC_V19, 1, SAVE_MAGIC_LENGTH, file) ==
               SAVE_MAGIC_LENGTH;
}

bool WorldSaveFormatHasMapMarkers(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V19;
}
