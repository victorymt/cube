#include "world/surface_save.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static SurfaceAddress Address(uint32_t bodyId, float longitude, float latitude,
                              int radial)
{
    return SurfaceAddressFromLatLon(bodyId, longitude, latitude, radial);
}

static FILE *WriteValidTrailer(void)
{
    FILE *file = tmpfile();
    assert(file != NULL);
    SurfaceAddress edits[] = {
        Address(7u, 0.25f, -0.5f, 12),
        Address(19u, -2.5f, 0.75f, -8)
    };
    assert(SurfaceSaveWriteTrailer(
        file, true, Address(19u, 1.0f, 0.2f, 91), edits, 2u));
    rewind(file);
    return file;
}

static void TestRoundTrip(void)
{
    FILE *file = WriteValidTrailer();
    bool playerHasAddress = false;
    SurfaceAddress playerAddress = { 0 };
    SurfaceAddress *edits = NULL;
    assert(SurfaceSaveReadTrailer(
        file, 2u, &playerHasAddress, &playerAddress, &edits));
    assert(playerHasAddress);
    assert(SurfaceAddressEqual(
        playerAddress, Address(19u, 1.0f, 0.2f, 91)));
    assert(SurfaceAddressEqual(edits[0], Address(7u, 0.25f, -0.5f, 12)));
    assert(SurfaceAddressEqual(edits[1], Address(19u, -2.5f, 0.75f, -8)));
    free(edits);
    fclose(file);

    file = tmpfile();
    assert(file != NULL);
    SurfaceAddress placeholder = Address(0u, 0.0f, 0.0f, 0);
    assert(SurfaceSaveWriteTrailer(file, false, placeholder, NULL, 0u));
    rewind(file);
    edits = NULL;
    playerHasAddress = true;
    assert(SurfaceSaveReadTrailer(
        file, 0u, &playerHasAddress, &playerAddress, &edits));
    assert(!playerHasAddress);
    assert(edits == NULL);
    fclose(file);
}

static void TestRejectsInvalidMetadata(void)
{
    FILE *file = WriteValidTrailer();
    uint32_t invalidSchema = SURFACE_SAVE_SCHEMA_VERSION + 1u;
    assert(fwrite(&invalidSchema, sizeof(invalidSchema), 1, file) == 1);
    rewind(file);
    bool hasPlayer = false;
    SurfaceAddress player = { 0 };
    SurfaceAddress *edits = NULL;
    assert(!SurfaceSaveReadTrailer(file, 2u, &hasPlayer, &player, &edits));
    fclose(file);

    file = WriteValidTrailer();
    assert(!SurfaceSaveReadTrailer(file, 3u, &hasPlayer, &player, &edits));
    fclose(file);

    file = WriteValidTrailer();
    assert(fseek(file, (long)(sizeof(uint32_t) + sizeof(uint8_t) +
                             sizeof(uint32_t)), SEEK_SET) == 0);
    uint32_t invalidFace = (uint32_t)SURFACE_FACE_COUNT;
    assert(fwrite(&invalidFace, sizeof(invalidFace), 1, file) == 1);
    rewind(file);
    assert(!SurfaceSaveReadTrailer(file, 2u, &hasPlayer, &player, &edits));
    fclose(file);
}

static void TestRejectsTruncationAndTrailingData(void)
{
    FILE *valid = WriteValidTrailer();
    assert(fseek(valid, 0, SEEK_END) == 0);
    long size = ftell(valid);
    assert(size > 1);

    FILE *truncated = tmpfile();
    assert(truncated != NULL);
    rewind(valid);
    for (long offset = 0; offset < size - 1; offset++) {
        int byte = fgetc(valid);
        assert(byte != EOF);
        assert(fputc(byte, truncated) != EOF);
    }
    rewind(truncated);
    bool hasPlayer = false;
    SurfaceAddress player = { 0 };
    SurfaceAddress *edits = NULL;
    assert(!SurfaceSaveReadTrailer(
        truncated, 2u, &hasPlayer, &player, &edits));
    fclose(truncated);

    assert(fseek(valid, 0, SEEK_END) == 0);
    assert(fputc(0x5a, valid) != EOF);
    rewind(valid);
    assert(!SurfaceSaveReadTrailer(valid, 2u, &hasPlayer, &player, &edits));
    fclose(valid);
}

int main(void)
{
    TestRoundTrip();
    TestRejectsInvalidMetadata();
    TestRejectsTruncationAndTrailingData();
    puts("surface save tests passed");
    return 0;
}
