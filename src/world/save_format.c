#include "world/save_format.h"

#include <string.h>

#define SAVE_MAGIC_V23 "VOXELCRAFT_SAVE_V23"
#define SAVE_MAGIC_V24 "VOXELCRAFT_SAVE_V24"
#define SAVE_MAGIC_V28 "VOXELCRAFT_SAVE_V28"
#define SAVE_MAGIC_V27 "VOXELCRAFT_SAVE_V27"
#define SAVE_MAGIC_V26 "VOXELCRAFT_SAVE_V26"
#define SAVE_MAGIC_V25 "VOXELCRAFT_SAVE_V25"
#define SAVE_MAGIC_V22 "VOXELCRAFT_SAVE_V22"
#define SAVE_MAGIC_V21 "VOXELCRAFT_SAVE_V21"
#define SAVE_MAGIC_V20 "VOXELCRAFT_SAVE_V20"
#define SAVE_MAGIC_V19 "VOXELCRAFT_SAVE_V19"
#define SAVE_MAGIC_V18 "VOXELCRAFT_SAVE_V18"
#define SAVE_MAGIC_LENGTH (sizeof(SAVE_MAGIC_V22) - 1u)

WorldSaveFormat WorldSaveFormatRead(FILE *file)
{
    char magic[SAVE_MAGIC_LENGTH];
    if (!file || fread(magic, 1, sizeof(magic), file) != sizeof(magic)) {
        return WORLD_SAVE_FORMAT_UNSUPPORTED;
    }
    if (memcmp(magic, SAVE_MAGIC_V28, sizeof(magic)) == 0) {
        return WORLD_SAVE_FORMAT_V28;
    }
    if (memcmp(magic, SAVE_MAGIC_V27, sizeof(magic)) == 0) {
        return WORLD_SAVE_FORMAT_V27;
    }
    if (memcmp(magic, SAVE_MAGIC_V26, sizeof(magic)) == 0) {
        return WORLD_SAVE_FORMAT_V26;
    }
    if (memcmp(magic, SAVE_MAGIC_V25, sizeof(magic)) == 0) {
        return WORLD_SAVE_FORMAT_V25;
    }
    if (memcmp(magic, SAVE_MAGIC_V24, sizeof(magic)) == 0) {
        return WORLD_SAVE_FORMAT_V24;
    }
    if (memcmp(magic, SAVE_MAGIC_V23, sizeof(magic)) == 0) {
        return WORLD_SAVE_FORMAT_V23;
    }
    if (memcmp(magic, SAVE_MAGIC_V22, sizeof(magic)) == 0) {
        return WORLD_SAVE_FORMAT_V22;
    }
    if (memcmp(magic, SAVE_MAGIC_V21, sizeof(magic)) == 0) {
        return WORLD_SAVE_FORMAT_V21;
    }
    if (memcmp(magic, SAVE_MAGIC_V20, sizeof(magic)) == 0) {
        return WORLD_SAVE_FORMAT_V20;
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
           fwrite(SAVE_MAGIC_V28, 1, SAVE_MAGIC_LENGTH, file) ==
               SAVE_MAGIC_LENGTH;
}

bool WorldSaveFormatHasMapMarkers(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V19 ||
           format == WORLD_SAVE_FORMAT_V20 ||
           format == WORLD_SAVE_FORMAT_V21 ||
           format == WORLD_SAVE_FORMAT_V22 ||
           format == WORLD_SAVE_FORMAT_V23 ||
           format == WORLD_SAVE_FORMAT_V24 ||
           format == WORLD_SAVE_FORMAT_V25 ||
           format == WORLD_SAVE_FORMAT_V26 ||
           format == WORLD_SAVE_FORMAT_V27 ||
           format == WORLD_SAVE_FORMAT_V28;
}

bool WorldSaveFormatHasWeatherImpact(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V20 ||
           format == WORLD_SAVE_FORMAT_V21 ||
           format == WORLD_SAVE_FORMAT_V22 ||
           format == WORLD_SAVE_FORMAT_V23 ||
           format == WORLD_SAVE_FORMAT_V24 ||
           format == WORLD_SAVE_FORMAT_V25 ||
           format == WORLD_SAVE_FORMAT_V26 ||
           format == WORLD_SAVE_FORMAT_V27 ||
           format == WORLD_SAVE_FORMAT_V28;
}

bool WorldSaveFormatHasRadiationRuntime(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V21 ||
           format == WORLD_SAVE_FORMAT_V22 ||
           format == WORLD_SAVE_FORMAT_V23 ||
           format == WORLD_SAVE_FORMAT_V24 ||
           format == WORLD_SAVE_FORMAT_V25 ||
           format == WORLD_SAVE_FORMAT_V26 ||
           format == WORLD_SAVE_FORMAT_V27 ||
           format == WORLD_SAVE_FORMAT_V28;
}

bool WorldSaveFormatHasHydrologyRuntime(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V22 ||
           format == WORLD_SAVE_FORMAT_V23 ||
           format == WORLD_SAVE_FORMAT_V24 ||
           format == WORLD_SAVE_FORMAT_V25 ||
           format == WORLD_SAVE_FORMAT_V26 ||
           format == WORLD_SAVE_FORMAT_V27 ||
           format == WORLD_SAVE_FORMAT_V28;
}

bool WorldSaveFormatHasEcologyClock(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V23 ||
           format == WORLD_SAVE_FORMAT_V24 ||
           format == WORLD_SAVE_FORMAT_V25 ||
           format == WORLD_SAVE_FORMAT_V26 ||
           format == WORLD_SAVE_FORMAT_V27 ||
           format == WORLD_SAVE_FORMAT_V28;
}

bool WorldSaveFormatHasVolcanoes(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V24 ||
           format == WORLD_SAVE_FORMAT_V25 ||
           format == WORLD_SAVE_FORMAT_V26 ||
           format == WORLD_SAVE_FORMAT_V27 ||
           format == WORLD_SAVE_FORMAT_V28;
}

bool WorldSaveFormatHasLava(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V25 ||
           format == WORLD_SAVE_FORMAT_V26 ||
           format == WORLD_SAVE_FORMAT_V27 ||
           format == WORLD_SAVE_FORMAT_V28;
}

bool WorldSaveFormatHasAsh(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V27 ||
           format == WORLD_SAVE_FORMAT_V28;
}

bool WorldSaveFormatHasInventoryRemainders(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V28;
}
