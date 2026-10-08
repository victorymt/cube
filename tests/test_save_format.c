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

static void TestHistoricalPayloadCapabilities(void)
{
    const char v18[] = "VOXELCRAFT_SAVE_V18";
    const char v19[] = "VOXELCRAFT_SAVE_V19";
    const char v20[] = "VOXELCRAFT_SAVE_V20";
    const char v21[] = "VOXELCRAFT_SAVE_V21";
    const char v22[] = "VOXELCRAFT_SAVE_V22";
    const char v23[] = "VOXELCRAFT_SAVE_V23";
    const char v24[] = "VOXELCRAFT_SAVE_V24";
    const char v28[] = "VOXELCRAFT_SAVE_V28";
    assert(ReadBytes(v28, sizeof(v28) - 1u) == WORLD_SAVE_FORMAT_UNSUPPORTED);
    assert(WorldSaveFormatHasLava(WORLD_SAVE_FORMAT_V28));
    assert(WorldSaveFormatHasVolcanoes(WORLD_SAVE_FORMAT_V28));
    assert(WorldSaveFormatHasMapMarkers(WORLD_SAVE_FORMAT_V28));
    assert(WorldSaveFormatHasWeatherImpact(WORLD_SAVE_FORMAT_V28));
    assert(WorldSaveFormatHasRadiationRuntime(WORLD_SAVE_FORMAT_V28));
    assert(WorldSaveFormatHasHydrologyRuntime(WORLD_SAVE_FORMAT_V28));
    assert(WorldSaveFormatHasEcologyClock(WORLD_SAVE_FORMAT_V28));
    assert(WorldSaveFormatHasAsh(WORLD_SAVE_FORMAT_V28));
    assert(WorldSaveFormatHasInventoryRemainders(WORLD_SAVE_FORMAT_V28));
    assert(!WorldSaveFormatHasInventoryRemainders(WORLD_SAVE_FORMAT_V27));
    const char v27[] = "VOXELCRAFT_SAVE_V27";
    assert(ReadBytes(v27, sizeof(v27) - 1u) == WORLD_SAVE_FORMAT_UNSUPPORTED);
    assert(WorldSaveFormatHasLava(WORLD_SAVE_FORMAT_V27));
    assert(WorldSaveFormatHasVolcanoes(WORLD_SAVE_FORMAT_V27));
    assert(WorldSaveFormatHasMapMarkers(WORLD_SAVE_FORMAT_V27));
    assert(WorldSaveFormatHasWeatherImpact(WORLD_SAVE_FORMAT_V27));
    assert(WorldSaveFormatHasRadiationRuntime(WORLD_SAVE_FORMAT_V27));
    assert(WorldSaveFormatHasHydrologyRuntime(WORLD_SAVE_FORMAT_V27));
    assert(WorldSaveFormatHasEcologyClock(WORLD_SAVE_FORMAT_V27));
    assert(WorldSaveFormatHasAsh(WORLD_SAVE_FORMAT_V27));
    assert(!WorldSaveFormatHasAsh(WORLD_SAVE_FORMAT_V26));
    const char v26[] = "VOXELCRAFT_SAVE_V26";
    assert(ReadBytes(v26, sizeof(v26) - 1u) == WORLD_SAVE_FORMAT_UNSUPPORTED);
    assert(WorldSaveFormatHasLava(WORLD_SAVE_FORMAT_V26));
    assert(WorldSaveFormatHasVolcanoes(WORLD_SAVE_FORMAT_V26));
    assert(WorldSaveFormatHasMapMarkers(WORLD_SAVE_FORMAT_V26));
    assert(WorldSaveFormatHasWeatherImpact(WORLD_SAVE_FORMAT_V26));
    assert(WorldSaveFormatHasRadiationRuntime(WORLD_SAVE_FORMAT_V26));
    assert(WorldSaveFormatHasHydrologyRuntime(WORLD_SAVE_FORMAT_V26));
    assert(WorldSaveFormatHasEcologyClock(WORLD_SAVE_FORMAT_V26));
    const char v25[] = "VOXELCRAFT_SAVE_V25";
    assert(ReadBytes(v25, sizeof(v25) - 1u) == WORLD_SAVE_FORMAT_UNSUPPORTED);
    assert(WorldSaveFormatHasLava(WORLD_SAVE_FORMAT_V25));
    assert(!WorldSaveFormatHasLava(WORLD_SAVE_FORMAT_V24));
    assert(WorldSaveFormatHasVolcanoes(WORLD_SAVE_FORMAT_V25));
    assert(WorldSaveFormatHasMapMarkers(WORLD_SAVE_FORMAT_V25));
    assert(WorldSaveFormatHasWeatherImpact(WORLD_SAVE_FORMAT_V25));
    assert(WorldSaveFormatHasRadiationRuntime(WORLD_SAVE_FORMAT_V25));
    assert(WorldSaveFormatHasHydrologyRuntime(WORLD_SAVE_FORMAT_V25));
    assert(WorldSaveFormatHasEcologyClock(WORLD_SAVE_FORMAT_V25));
    assert(ReadBytes(v24, sizeof(v24) - 1u) == WORLD_SAVE_FORMAT_UNSUPPORTED);
    assert(WorldSaveFormatHasVolcanoes(WORLD_SAVE_FORMAT_V24));
    assert(!WorldSaveFormatHasVolcanoes(WORLD_SAVE_FORMAT_V23));
    assert(WorldSaveFormatHasMapMarkers(WORLD_SAVE_FORMAT_V24));
    assert(WorldSaveFormatHasWeatherImpact(WORLD_SAVE_FORMAT_V24));
    assert(WorldSaveFormatHasRadiationRuntime(WORLD_SAVE_FORMAT_V24));
    assert(WorldSaveFormatHasHydrologyRuntime(WORLD_SAVE_FORMAT_V24));
    assert(WorldSaveFormatHasEcologyClock(WORLD_SAVE_FORMAT_V24));
    assert(ReadBytes(v18, sizeof(v18) - 1u) == WORLD_SAVE_FORMAT_UNSUPPORTED);
    assert(ReadBytes(v19, sizeof(v19) - 1u) == WORLD_SAVE_FORMAT_UNSUPPORTED);
    assert(ReadBytes(v20, sizeof(v20) - 1u) == WORLD_SAVE_FORMAT_UNSUPPORTED);
    assert(ReadBytes(v21, sizeof(v21) - 1u) == WORLD_SAVE_FORMAT_UNSUPPORTED);
    assert(ReadBytes(v22, sizeof(v22) - 1u) == WORLD_SAVE_FORMAT_UNSUPPORTED);
    assert(ReadBytes(v23, sizeof(v23) - 1u) == WORLD_SAVE_FORMAT_UNSUPPORTED);
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
    assert(WorldSaveFormatRead(file) == WORLD_SAVE_FORMAT_V29);
    fclose(file);
    assert(!WorldSaveFormatWriteCurrent(NULL));
}

static FILE *BytesFile(const unsigned char *bytes, size_t length)
{
    FILE *file = tmpfile();
    assert(file);
    assert(fwrite(bytes, 1, length, file) == length);
    rewind(file);
    return file;
}

static void TestTopologyHeader(void)
{
    // Pinned wire bytes, including LE integers. Keep core payload untouched.
    const unsigned char expected[] =
        "VOXELCRAFT_SAVE_V29" "TOPOL001"
        "\x01\x00\x00\x00" "\x01\x00\x00\x00"
        "\x01\x00\x00\x00" "\x01\x00\x00\x00";
    WorldSurfaceTopologyPolicy policy = WORLD_SURFACE_TOPOLOGY_DEFAULT;
    FILE *file = tmpfile();
    assert(file && WorldSaveWriteHeader(file, policy));
    assert(fputs("TAIL", file) >= 0);
    rewind(file);
    unsigned char written[sizeof(expected) - 1];
    assert(fread(written, 1, sizeof(written), file) == sizeof(written));
    assert(memcmp(written, expected, sizeof(written)) == 0);
    rewind(file);
    WorldSurfaceTopologyPolicy loaded = { 0 };
    assert(WorldSaveReadHeader(file, &loaded) == NULL);
    assert(loaded.home.kind == SURFACE_TOPOLOGY_LEGACY_MAP && loaded.home.version == 1);
    assert(loaded.planets.kind == SURFACE_TOPOLOGY_LEGACY_MAP && loaded.planets.version == 1);
    assert(fgetc(file) == 'T');
    fclose(file);

    for (size_t length = 0; length < sizeof(expected) - 1; ++length) {
        file = BytesFile(expected, length);
        loaded = (WorldSurfaceTopologyPolicy){ 0 };
        assert(WorldSaveReadHeader(file, &loaded));
        assert(loaded.home.kind == SURFACE_TOPOLOGY_NONE && loaded.home.version == 0);
        assert(loaded.planets.kind == SURFACE_TOPOLOGY_NONE && loaded.planets.version == 0);
        fclose(file);
    }
    // Bad marker, NONE/unknown kind, invalid version and high-byte corruption.
    const size_t offsets[] = { 19, 27, 30, 31, 34, 35, 38, 39, 42 };
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        unsigned char corrupt[sizeof(expected) - 1];
        memcpy(corrupt, expected, sizeof(corrupt));
        corrupt[offsets[i]] = 0xff;
        file = BytesFile(corrupt, sizeof(corrupt));
        assert(WorldSaveReadHeader(file, &loaded));
        fclose(file);
    }
    for (size_t offset = 27; offset <= 35; offset += 8) {
        unsigned char cube[sizeof(expected) - 1];
        memcpy(cube, expected, sizeof(cube));
        cube[offset] = SURFACE_TOPOLOGY_CUBE_GRID;
        file = BytesFile(cube, sizeof(cube));
        const char *error = WorldSaveReadHeader(file, &loaded);
        assert(error && strstr(error, "cube-grid"));
        fclose(file);
        cube[offset] = SURFACE_TOPOLOGY_NONE;
        file = BytesFile(cube, sizeof(cube));
        assert(WorldSaveReadHeader(file, &loaded));
        fclose(file);
    }
    for (int version = 1; version <= 30; ++version) {
        if (version == 29) continue;
        char magic[32];
        int length = snprintf(magic, sizeof(magic), "VOXELCRAFT_SAVE_V%d", version);
        file = BytesFile((const unsigned char *)magic, (size_t)length);
        assert(WorldSaveReadHeader(file, &loaded));
        fclose(file);
    }
    policy.planets = (SurfaceTopologyVersion){ SURFACE_TOPOLOGY_CUBE_GRID, 1 };
    file = tmpfile();
    assert(file && !WorldSaveWriteHeader(file, policy) && ftell(file) == 0);
    fclose(file);
    assert(!WorldSaveWriteHeader(NULL, policy));
    assert(WorldSaveReadHeader(NULL, &loaded));
}

int main(void)
{
    TestHistoricalPayloadCapabilities();
    TestUnsupportedFormats();
    TestCurrentFormatWriter();
    TestTopologyHeader();
    puts("save format tests passed");
    return 0;
}
