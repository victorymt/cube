/* V30 foundation: cube block addresses must survive a save/load round trip
 * byte-for-byte, and a damaged or version-mismatched file must be rejected. */
#define _POSIX_C_SOURCE 200809L
#include "world/cube_address_save.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static FILE *TempFile(void)
{
    FILE *file = tmpfile();
    assert(file);
    return file;
}

static WorldBlockAddress SurfaceCell(uint32_t body, SurfaceFace face, int u,
                                     int v, int radial)
{
    WorldBlockAddress address;
    address.dimension = body == 0 ? WORLD_DIMENSION_HOME : WORLD_DIMENSION_PLANET;
    address.at.surface.bodyId = body;
    address.at.surface.face = face;
    address.at.surface.u = u;
    address.at.surface.v = v;
    address.at.surface.radial = radial;
    return address;
}

static void TestRoundTrip(void)
{
    WorldBlockAddress addresses[5];
    WorldCell cells[5];
    addresses[0] = SurfaceCell(0, SURFACE_FACE_POS_X, 12, 34, 56);
    addresses[1] = SurfaceCell(0, SURFACE_FACE_NEG_Y, 4095, 4095, -200);
    addresses[2] = SurfaceCell(991, SURFACE_FACE_POS_Y, 0, 4095, 7);
    addresses[3].dimension = WORLD_DIMENSION_SPACE;
    addresses[3].at.cartesian.x = -5;
    addresses[3].at.cartesian.y = 120;
    addresses[3].at.cartesian.z = 42;
    addresses[4].dimension = WORLD_DIMENSION_NETHER;
    addresses[4].at.cartesian.x = 1;
    addresses[4].at.cartesian.y = -60;
    addresses[4].at.cartesian.z = 3;
    for (int i = 0; i < 5; i++) {
        cells[i] = (WorldCell){ (BlockType)(BLOCK_STONE + i), 0u };
    }
    cells[1].block = BLOCK_AIR;
    cells[1].waterVolume = 127u;
    cells[2].block = BLOCK_REED;
    cells[2].waterVolume = WORLD_CELL_WATER_FULL;

    FILE *file = TempFile();
    CubeAddressBlock block = {.addresses = addresses, .cells = cells, .count = 5};
    assert(CubeAddressSaveWrite(file, &block));
    rewind(file);

    uint32_t version = 0;
    assert(CubeAddressSavePeekVersion(file, &version));
    assert(version == CUBE_ADDRESS_SAVE_VERSION);

    /* PeekVersion consumes the header, so the stream must be rewound before a
     * full read starts at the magic. */
    rewind(file);
    CubeAddressBlock loaded;
    assert(CubeAddressSaveRead(file, &loaded));
    assert(loaded.count == 5);
    for (uint32_t i = 0; i < 5; i++) {
        assert(WorldBlockAddressEqual(loaded.addresses[i], addresses[i]));
        assert(WorldCellEqual(loaded.cells[i], cells[i]));
    }
    CubeAddressBlockRelease(&loaded);
    fclose(file);
}

static void TestWrongMagicRejected(void)
{
    FILE *file = TempFile();
    fwrite("NOTCUBE!", 1, 8, file);
    fflush(file);
    rewind(file);
    CubeAddressBlock block;
    assert(!CubeAddressSaveRead(file, &block));
    uint32_t version = 0;
    assert(!CubeAddressSavePeekVersion(file, &version));
    fclose(file);
}

static void TestWrongVersionRejected(void)
{
    FILE *file = TempFile();
    fwrite(CUBE_ADDRESS_SAVE_MAGIC, 1, 8, file);
    unsigned char version[4] = {99, 0, 0, 0};
    assert(fwrite(version, 1, 4, file) == 4);
    unsigned char count[4] = {0, 0, 0, 0};
    assert(fwrite(count, 1, 4, file) == 4);
    rewind(file);
    CubeAddressBlock block;
    assert(!CubeAddressSaveRead(file, &block));
    fclose(file);
}

static void TestTruncatedRejected(void)
{
    WorldBlockAddress address = SurfaceCell(0, SURFACE_FACE_POS_Z, 1, 2, 3);
    WorldCell cell = { BLOCK_STONE, 0u };
    FILE *file = TempFile();
    CubeAddressBlock block = {.addresses = &address, .cells = &cell, .count = 1};
    assert(CubeAddressSaveWrite(file, &block));
    fflush(file);
    /* Chop the final byte; a partial record must never be accepted. */
    long size = ftell(file);
    assert(size > 1);
    assert(ftruncate(fileno(file), size - 1) == 0);
    rewind(file);
    CubeAddressBlock loaded;
    assert(!CubeAddressSaveRead(file, &loaded));
    fclose(file);
}

static void TestInvalidAddressRejectedOnWrite(void)
{
    WorldBlockAddress bad = SurfaceCell(0, SURFACE_FACE_POS_X, -1, 0, 0);
    WorldCell cell = { BLOCK_STONE, 0u };
    FILE *file = TempFile();
    CubeAddressBlock block = {.addresses = &bad, .cells = &cell, .count = 1};
    assert(!CubeAddressSaveWrite(file, &block));
    fclose(file);
}

static void TestInvalidCellRejectedOnWrite(void)
{
    WorldBlockAddress address = SurfaceCell(0, SURFACE_FACE_POS_X, 1, 2, 3);
    WorldCell solidWater = { BLOCK_STONE, WORLD_CELL_WATER_FULL };
    FILE *file = TempFile();
    CubeAddressBlock block = {
        .addresses = &address, .cells = &solidWater, .count = 1
    };
    assert(!CubeAddressSaveWrite(file, &block));
    assert(ftell(file) == CUBE_ADDRESS_SAVE_HEADER_BYTES);
    fclose(file);
}

static void TestInvalidVolumeRejectedOnRead(void)
{
    WorldBlockAddress address = SurfaceCell(0, SURFACE_FACE_POS_X, 1, 2, 3);
    WorldCell cell = { BLOCK_STONE, 0u };
    FILE *file = TempFile();
    CubeAddressBlock block = { .addresses = &address, .cells = &cell, .count = 1 };
    assert(CubeAddressSaveWrite(file, &block));
    fflush(file);
    long volumeOffset = CUBE_ADDRESS_SAVE_HEADER_BYTES + 24;
    unsigned char invalid[4] = { 0xff, 0, 0, 0 };
    assert(fseek(file, volumeOffset, SEEK_SET) == 0);
    assert(fwrite(invalid, 1, sizeof(invalid), file) == sizeof(invalid));
    rewind(file);
    CubeAddressBlock loaded;
    assert(!CubeAddressSaveRead(file, &loaded));
    fclose(file);
}

static void TestEmptyBlock(void)
{
    FILE *file = TempFile();
    CubeAddressBlock empty = {0};
    assert(CubeAddressSaveWrite(file, &empty));
    rewind(file);
    CubeAddressBlock loaded;
    assert(CubeAddressSaveRead(file, &loaded));
    assert(loaded.count == 0 && loaded.addresses == NULL);
    CubeAddressBlockRelease(&loaded);
    fclose(file);
}

/* Colour blocks sit above BLOCK_BUILTIN_END. The codec must accept exactly
 * what the store accepts, otherwise a save it wrote cannot be read back. */
static void TestColourBlockRoundTrip(void)
{
    WorldBlockAddress address = { .dimension = WORLD_DIMENSION_HOME };
    address.at.surface = (SurfaceAddress){ 0, SURFACE_FACE_POS_X, 10, 20, 5 };
    WorldCell colour = { (BlockType)BLOCK_COLOR_START, 0u };
    CubeAddressBlock block = { .count = 1u, .addresses = &address,
                               .cells = &colour };

    FILE *file = tmpfile();
    assert(file);
    assert(CubeAddressSaveWrite(file, &block));
    rewind(file);

    CubeAddressBlock read = { 0 };
    assert(CubeAddressSaveRead(file, &read));
    assert(read.count == 1u);
    assert(WorldCellEqual(read.cells[0], colour));
    CubeAddressBlockRelease(&read);
    fclose(file);
}

int main(void)
{
    TestRoundTrip();
    TestColourBlockRoundTrip();
    TestWrongMagicRejected();
    TestWrongVersionRejected();
    TestTruncatedRejected();
    TestInvalidAddressRejectedOnWrite();
    TestInvalidCellRejectedOnWrite();
    TestInvalidVolumeRejectedOnRead();
    TestEmptyBlock();
    puts("cube address save tests passed");
    return 0;
}
