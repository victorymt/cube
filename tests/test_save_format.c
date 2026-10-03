#include "world/save_format.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static WorldSaveFormat ReadBytes(const char *bytes, size_t length)
{
    FILE *file = tmpfile();
    assert(file != NULL);
    assert(fwrite(bytes, 1, length, file) == length);
    rewind(file);
    WorldSaveFormat format = WorldSaveFormatRead(file);
    fclose(file);
    return format;
}

static void TestSupportedFormats(void)
{
    const char v18[] = "VOXELCRAFT_SAVE_V18";
    const char v19[] = "VOXELCRAFT_SAVE_V19";
    const char v20[] = "VOXELCRAFT_SAVE_V20";
    const char v21[] = "VOXELCRAFT_SAVE_V21";
    const char v22[] = "VOXELCRAFT_SAVE_V22";
    const char v23[] = "VOXELCRAFT_SAVE_V23";
    const char v24[] = "VOXELCRAFT_SAVE_V24";
    assert(ReadBytes(v24, sizeof(v24) - 1u) == WORLD_SAVE_FORMAT_V24);
    assert(WorldSaveFormatHasVolcanoes(WORLD_SAVE_FORMAT_V24));
    assert(!WorldSaveFormatHasVolcanoes(WORLD_SAVE_FORMAT_V23));
    assert(WorldSaveFormatHasMapMarkers(WORLD_SAVE_FORMAT_V24));
    assert(WorldSaveFormatHasWeatherImpact(WORLD_SAVE_FORMAT_V24));
    assert(WorldSaveFormatHasRadiationRuntime(WORLD_SAVE_FORMAT_V24));
    assert(WorldSaveFormatHasHydrologyRuntime(WORLD_SAVE_FORMAT_V24));
    assert(WorldSaveFormatHasEcologyClock(WORLD_SAVE_FORMAT_V24));
    assert(ReadBytes(v18, sizeof(v18) - 1u) == WORLD_SAVE_FORMAT_V18);
    assert(ReadBytes(v19, sizeof(v19) - 1u) == WORLD_SAVE_FORMAT_V19);
    assert(ReadBytes(v20, sizeof(v20) - 1u) == WORLD_SAVE_FORMAT_V20);
    assert(ReadBytes(v21, sizeof(v21) - 1u) == WORLD_SAVE_FORMAT_V21);
    assert(ReadBytes(v22, sizeof(v22) - 1u) == WORLD_SAVE_FORMAT_V22);
    assert(ReadBytes(v23, sizeof(v23) - 1u) == WORLD_SAVE_FORMAT_V23);
    assert(WorldSaveFormatHasMapMarkers(WORLD_SAVE_FORMAT_V23));
    assert(WorldSaveFormatHasWeatherImpact(WORLD_SAVE_FORMAT_V23));
    assert(WorldSaveFormatHasRadiationRuntime(WORLD_SAVE_FORMAT_V23));
    assert(WorldSaveFormatHasHydrologyRuntime(WORLD_SAVE_FORMAT_V23));
    assert(WorldSaveFormatHasEcologyClock(WORLD_SAVE_FORMAT_V23));
    assert(!WorldSaveFormatHasEcologyClock(WORLD_SAVE_FORMAT_V22));
    assert(!WorldSaveFormatHasMapMarkers(WORLD_SAVE_FORMAT_V18));
    assert(WorldSaveFormatHasMapMarkers(WORLD_SAVE_FORMAT_V19));
    assert(WorldSaveFormatHasMapMarkers(WORLD_SAVE_FORMAT_V20));
    assert(WorldSaveFormatHasMapMarkers(WORLD_SAVE_FORMAT_V21));
    assert(WorldSaveFormatHasMapMarkers(WORLD_SAVE_FORMAT_V22));
    assert(!WorldSaveFormatHasWeatherImpact(WORLD_SAVE_FORMAT_V19));
    assert(WorldSaveFormatHasWeatherImpact(WORLD_SAVE_FORMAT_V20));
    assert(WorldSaveFormatHasWeatherImpact(WORLD_SAVE_FORMAT_V21));
    assert(WorldSaveFormatHasWeatherImpact(WORLD_SAVE_FORMAT_V22));
    assert(!WorldSaveFormatHasRadiationRuntime(WORLD_SAVE_FORMAT_V20));
    assert(WorldSaveFormatHasRadiationRuntime(WORLD_SAVE_FORMAT_V21));
    assert(WorldSaveFormatHasRadiationRuntime(WORLD_SAVE_FORMAT_V22));
    assert(!WorldSaveFormatHasHydrologyRuntime(WORLD_SAVE_FORMAT_V21));
    assert(WorldSaveFormatHasHydrologyRuntime(WORLD_SAVE_FORMAT_V22));
}

static void TestUnsupportedFormats(void)
{
    const char v17[] = "VOXELCRAFT_SAVE_V17";
    const char truncated[] = "VOXELCRAFT_SAVE_V1";
    assert(ReadBytes(v17, sizeof(v17) - 1u) ==
           WORLD_SAVE_FORMAT_UNSUPPORTED);
    assert(ReadBytes(truncated, sizeof(truncated) - 1u) ==
           WORLD_SAVE_FORMAT_UNSUPPORTED);
    assert(WorldSaveFormatRead(NULL) == WORLD_SAVE_FORMAT_UNSUPPORTED);
}

static void TestCurrentFormatWriter(void)
{
    FILE *file = tmpfile();
    assert(file != NULL);
    assert(WorldSaveFormatWriteCurrent(file));
    rewind(file);
    assert(WorldSaveFormatRead(file) == WORLD_SAVE_FORMAT_V24);
    fclose(file);
    assert(!WorldSaveFormatWriteCurrent(NULL));
}

int main(void)
{
    TestSupportedFormats();
    TestUnsupportedFormats();
    TestCurrentFormatWriter();
    puts("save format tests passed");
    return 0;
}
