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

static void WriteAddress(FILE *file, SurfaceAddress address)
{
    uint32_t face = (uint32_t)address.face;
    int32_t u = (int32_t)address.u;
    int32_t v = (int32_t)address.v;
    int32_t radial = (int32_t)address.radial;
    assert(fwrite(&address.bodyId, sizeof(address.bodyId), 1, file) == 1);
    assert(fwrite(&face, sizeof(face), 1, file) == 1);
    assert(fwrite(&u, sizeof(u), 1, file) == 1);
    assert(fwrite(&v, sizeof(v), 1, file) == 1);
    assert(fwrite(&radial, sizeof(radial), 1, file) == 1);
}

static FILE *WriteValidTrailer(void)
{
    FILE *file = tmpfile();
    assert(file != NULL);
    SurfaceMapCell cells[] = { { 12, -24 }, { -88, 61 } };
    SurfaceAddress edits[] = {
        SurfaceAddressFromMapCoordinates(7u, (float)cells[0].x,
                                         (float)cells[0].z, 12),
        SurfaceAddressFromMapCoordinates(19u, (float)cells[1].x,
                                         (float)cells[1].z, -8)
    };
    assert(SurfaceSaveWriteTrailer(
        file, true, Address(19u, 1.0f, 0.2f, 91), edits, cells, 2u));
    rewind(file);
    return file;
}

static void TestRoundTrip(void)
{
    FILE *file = WriteValidTrailer();
    bool playerHasAddress = false;
    uint32_t schemaVersion = 0u;
    SurfaceAddress playerAddress = { 0 };
    SurfaceAddress *edits = NULL;
    SurfaceMapCell *cells = NULL;
    assert(SurfaceSaveReadTrailer(
        file, 2u, &schemaVersion, &playerHasAddress, &playerAddress,
        &edits, &cells));
    assert(schemaVersion == 2u);
    assert(playerHasAddress);
    assert(SurfaceAddressEqual(
        playerAddress, Address(19u, 1.0f, 0.2f, 91)));
    assert(SurfaceAddressEqual(edits[0],
                               SurfaceAddressFromMapCoordinates(
                                   7u, 12.0f, -24.0f, 12)));
    assert(SurfaceAddressEqual(edits[1],
                               SurfaceAddressFromMapCoordinates(
                                   19u, -88.0f, 61.0f, -8)));
    assert(cells[0].x == 12 && cells[0].z == -24);
    assert(cells[1].x == -88 && cells[1].z == 61);
    free(edits);
    free(cells);
    fclose(file);

    file = tmpfile();
    assert(file != NULL);
    SurfaceAddress placeholder = Address(0u, 0.0f, 0.0f, 0);
    assert(SurfaceSaveWriteTrailer(file, false, placeholder, NULL, NULL, 0u));
    rewind(file);
    edits = NULL;
    playerHasAddress = true;
    assert(SurfaceSaveReadTrailer(
        file, 0u, &schemaVersion, &playerHasAddress, &playerAddress,
        &edits, &cells));
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
    uint32_t schemaVersion = 0u;
    SurfaceAddress player = { 0 };
    SurfaceAddress *edits = NULL;
    SurfaceMapCell *cells = NULL;
    assert(!SurfaceSaveReadTrailer(file, 2u, &schemaVersion, &hasPlayer,
                                   &player, &edits, &cells));
    fclose(file);

    file = WriteValidTrailer();
    assert(!SurfaceSaveReadTrailer(file, 3u, &schemaVersion, &hasPlayer,
                                   &player, &edits, &cells));
    fclose(file);

    file = WriteValidTrailer();
    assert(fseek(file, (long)(sizeof(uint32_t) + sizeof(uint8_t) +
                             sizeof(uint32_t)), SEEK_SET) == 0);
    uint32_t invalidFace = (uint32_t)SURFACE_FACE_COUNT;
    assert(fwrite(&invalidFace, sizeof(invalidFace), 1, file) == 1);
    rewind(file);
    assert(!SurfaceSaveReadTrailer(file, 2u, &schemaVersion, &hasPlayer,
                                   &player, &edits, &cells));
    fclose(file);
}

static void TestReadsSchemaOne(void)
{
    FILE *file = tmpfile();
    assert(file != NULL);
    uint32_t schemaVersion = 1u;
    uint8_t hasPlayer = 1u;
    uint32_t editCount = 1u;
    SurfaceAddress player = SurfaceAddressFromMapCoordinates(
        0u, 90.0f, -42.0f, 74);
    SurfaceAddress edit = SurfaceAddressFromMapCoordinates(
        7u, 411.0f, -209.0f, 13);
    assert(fwrite(&schemaVersion, sizeof(schemaVersion), 1, file) == 1);
    assert(fwrite(&hasPlayer, sizeof(hasPlayer), 1, file) == 1);
    WriteAddress(file, player);
    assert(fwrite(&editCount, sizeof(editCount), 1, file) == 1);
    WriteAddress(file, edit);
    rewind(file);

    bool loadedHasPlayer = false;
    uint32_t loadedSchema = 0u;
    SurfaceAddress loadedPlayer = { 0 };
    SurfaceAddress *loadedEdits = NULL;
    SurfaceMapCell *loadedCells = NULL;
    assert(SurfaceSaveReadTrailer(
        file, 1u, &loadedSchema, &loadedHasPlayer, &loadedPlayer,
        &loadedEdits, &loadedCells));
    assert(loadedSchema == 1u);
    assert(loadedHasPlayer);
    assert(SurfaceAddressEqual(loadedPlayer, player));
    assert(SurfaceAddressEqual(loadedEdits[0], edit));
    free(loadedEdits);
    free(loadedCells);
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
    uint32_t schemaVersion = 0u;
    SurfaceAddress player = { 0 };
    SurfaceAddress *edits = NULL;
    SurfaceMapCell *cells = NULL;
    assert(!SurfaceSaveReadTrailer(
        truncated, 2u, &schemaVersion, &hasPlayer, &player, &edits, &cells));
    fclose(truncated);

    assert(fseek(valid, 0, SEEK_END) == 0);
    assert(fputc(0x5a, valid) != EOF);
    rewind(valid);
    assert(!SurfaceSaveReadTrailer(valid, 2u, &schemaVersion, &hasPlayer,
                                   &player, &edits, &cells));
    fclose(valid);
}

int main(void)
{
    TestRoundTrip();
    TestReadsSchemaOne();
    TestRejectsInvalidMetadata();
    TestRejectsTruncationAndTrailingData();
    puts("surface save tests passed");
    return 0;
}
