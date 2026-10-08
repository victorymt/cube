#include "world/save_format.h"

#include <string.h>

#define SAVE_MAGIC_V29 "VOXELCRAFT_SAVE_V29"
#define SAVE_MAGIC_LENGTH (sizeof(SAVE_MAGIC_V29) - 1u)

WorldSaveFormat WorldSaveFormatRead(FILE *file)
{
    char magic[SAVE_MAGIC_LENGTH];
    if (!file || fread(magic, 1, sizeof(magic), file) != sizeof(magic)) {
        return WORLD_SAVE_FORMAT_UNSUPPORTED;
    }
    return memcmp(magic, SAVE_MAGIC_V29, sizeof(magic)) == 0
        ? WORLD_SAVE_FORMAT_V29 : WORLD_SAVE_FORMAT_UNSUPPORTED;
}

bool WorldSaveFormatWriteCurrent(FILE *file)
{
    return file &&
           fwrite(SAVE_MAGIC_V29, 1, SAVE_MAGIC_LENGTH, file) ==
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
           format == WORLD_SAVE_FORMAT_V28 ||
           format == WORLD_SAVE_FORMAT_V29;
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
           format == WORLD_SAVE_FORMAT_V28 ||
           format == WORLD_SAVE_FORMAT_V29;
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
           format == WORLD_SAVE_FORMAT_V28 ||
           format == WORLD_SAVE_FORMAT_V29;
}

bool WorldSaveFormatHasHydrologyRuntime(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V22 ||
           format == WORLD_SAVE_FORMAT_V23 ||
           format == WORLD_SAVE_FORMAT_V24 ||
           format == WORLD_SAVE_FORMAT_V25 ||
           format == WORLD_SAVE_FORMAT_V26 ||
           format == WORLD_SAVE_FORMAT_V27 ||
           format == WORLD_SAVE_FORMAT_V28 ||
           format == WORLD_SAVE_FORMAT_V29;
}

bool WorldSaveFormatHasEcologyClock(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V23 ||
           format == WORLD_SAVE_FORMAT_V24 ||
           format == WORLD_SAVE_FORMAT_V25 ||
           format == WORLD_SAVE_FORMAT_V26 ||
           format == WORLD_SAVE_FORMAT_V27 ||
           format == WORLD_SAVE_FORMAT_V28 ||
           format == WORLD_SAVE_FORMAT_V29;
}

bool WorldSaveFormatHasVolcanoes(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V24 ||
           format == WORLD_SAVE_FORMAT_V25 ||
           format == WORLD_SAVE_FORMAT_V26 ||
           format == WORLD_SAVE_FORMAT_V27 ||
           format == WORLD_SAVE_FORMAT_V28 ||
           format == WORLD_SAVE_FORMAT_V29;
}

bool WorldSaveFormatHasLava(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V25 ||
           format == WORLD_SAVE_FORMAT_V26 ||
           format == WORLD_SAVE_FORMAT_V27 ||
           format == WORLD_SAVE_FORMAT_V28 ||
           format == WORLD_SAVE_FORMAT_V29;
}

bool WorldSaveFormatHasAsh(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V27 ||
           format == WORLD_SAVE_FORMAT_V28 ||
           format == WORLD_SAVE_FORMAT_V29;
}

bool WorldSaveFormatHasInventoryRemainders(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V28 ||
           format == WORLD_SAVE_FORMAT_V29;
}

static void WriteU32LE(unsigned char *bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (unsigned char)(value >> (8u * i));
}

static uint32_t ReadU32LE(const unsigned char *bytes)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= (uint32_t)bytes[i] << (8u * i);
    return value;
}

bool WorldSaveWriteHeader(FILE *file, WorldSurfaceTopologyPolicy policy)
{
    if (!file || !WorldSurfaceTopologyPolicyIsSupported(policy)) return false;
    unsigned char bytes[24] = { 0 };
    memcpy(bytes, "TOPOL001", 8);
    WriteU32LE(bytes + 8, (uint32_t)policy.home.kind);
    WriteU32LE(bytes + 12, policy.home.version);
    WriteU32LE(bytes + 16, (uint32_t)policy.planets.kind);
    WriteU32LE(bytes + 20, policy.planets.version);
    return WorldSaveFormatWriteCurrent(file) &&
           fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
}

const char *WorldSaveReadHeader(FILE *file, WorldSurfaceTopologyPolicy *outPolicy)
{
    if (!file || !outPolicy) return "Load failed: save header is unavailable.";
    if (WorldSaveFormatRead(file) != WORLD_SAVE_FORMAT_V29) {
        return "Load failed: unsupported save format. V28 and older are retired; start a new world.";
    }
    unsigned char bytes[24];
    if (fread(bytes, 1, sizeof(bytes), file) != sizeof(bytes) ||
        memcmp(bytes, "TOPOL001", 8) != 0) {
        return "Load failed: topology metadata is missing or corrupted.";
    }
    // Validate the wire integers before converting to enums.
    uint32_t homeKind = ReadU32LE(bytes + 8);
    uint32_t planetKind = ReadU32LE(bytes + 16);
    if (homeKind < SURFACE_TOPOLOGY_LEGACY_MAP || homeKind > SURFACE_TOPOLOGY_CUBE_GRID ||
        planetKind < SURFACE_TOPOLOGY_LEGACY_MAP || planetKind > SURFACE_TOPOLOGY_CUBE_GRID) {
        return "Load failed: unknown surface topology.";
    }
    WorldSurfaceTopologyPolicy policy = {
        .home = { (SurfaceTopologyKind)homeKind, ReadU32LE(bytes + 12) },
        .planets = { (SurfaceTopologyKind)planetKind, ReadU32LE(bytes + 20) }
    };
    if (!SurfaceTopologyIsKnown(policy.home) || !SurfaceTopologyIsKnown(policy.planets)) {
        return "Load failed: unknown surface topology version.";
    }
    if (!WorldSurfaceTopologyPolicyIsSupported(policy)) {
        return "Load failed: cube-grid topology is not supported by this runtime.";
    }
    *outPolicy = policy;
    return NULL;
}
