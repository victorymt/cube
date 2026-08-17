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
    assert(ReadBytes(v18, sizeof(v18) - 1u) == WORLD_SAVE_FORMAT_V18);
    assert(ReadBytes(v19, sizeof(v19) - 1u) == WORLD_SAVE_FORMAT_V19);
    assert(!WorldSaveFormatHasMapMarkers(WORLD_SAVE_FORMAT_V18));
    assert(WorldSaveFormatHasMapMarkers(WORLD_SAVE_FORMAT_V19));
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
    assert(WorldSaveFormatRead(file) == WORLD_SAVE_FORMAT_V19);
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
