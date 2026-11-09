/*
 * Cross-process cube persistence: phase 1 writes the edit store through the
 * codec, phase 2 (a separate process) reads it back and verifies every cell.
 * Run as: test_cube_save_reload write PATH | read PATH
 */
#define _POSIX_C_SOURCE 200809L
#include "world/cube_address_save.h"
#include "world/world_cube.h"
#include "world/surface_contract.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The codec only needs identity and validation; the world layer proper is not
 * linked into this focused persistence test. */
bool IsValidBlockType(BlockType type)
{
    return (int)type > 0 && type <= BLOCK_BUILTIN_END;
}
uint64_t WorldCurrentGeneration(void) { return 1u; }
const SurfaceContract *WorldCurrentSurfaceContract(void) { return NULL; }
void MarkGeneratedSectionAndNeighborsDirty(void *chunk, int sectionY)
{
    (void)chunk;
    (void)sectionY;
}

#define RECORDS 40u

static WorldBlockAddress Cell(uint32_t body, SurfaceFace face, int u, int v,
                              int radial)
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

static WorldBlockAddress Expected(unsigned i)
{
    return Cell(i % 2 ? 991 : 0, (SurfaceFace)(i % SURFACE_FACE_COUNT),
                100 + (int)i, 4095 - (int)i, 20 - (int)i);
}

static int WritePhase(const char *path)
{
    WorldCubeClearEdits();
    for (unsigned i = 0; i < RECORDS; i++) {
        if (i == RECORDS - 1u) {
            assert(WorldCubeSetCell(
                Expected(i), (WorldCell){ BLOCK_AIR, 127u }));
        } else {
            assert(WorldCubeSetBlock(
                Expected(i), (BlockType)(BLOCK_STONE + i % 5)));
        }
    }
    assert(WorldCubeEditCount() == RECORDS);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(CubeAddressSaveWriteEdits(file));
    assert(fclose(file) == 0);
    printf("wrote %u cube edits to %s\n", RECORDS, path);
    return 0;
}

static int ReadPhase(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    uint32_t count = 0;
    /* A damaged payload is a normal, reported outcome, not a crash. */
    if (!CubeAddressSaveReadEdits(file, &count)) {
        fclose(file);
        fprintf(stderr, "rejected damaged payload: %s\n", path);
        return 1;
    }
    if (fclose(file) != 0 || count != RECORDS ||
        WorldCubeEditCount() != RECORDS) {
        fprintf(stderr, "unexpected record count in %s\n", path);
        return 1;
    }
    for (unsigned i = 0; i < RECORDS; i++) {
        WorldCell cell;
        if (!WorldCubeGetEditCell(Expected(i), &cell)) {
            fprintf(stderr, "record %u did not survive the reload\n", i);
            return 1;
        }
        WorldCell expected = i == RECORDS - 1u
            ? (WorldCell){ BLOCK_AIR, 127u }
            : (WorldCell){ (BlockType)(BLOCK_STONE + i % 5), 0u };
        if (!WorldCellEqual(cell, expected)) {
            fprintf(stderr, "record %u cell did not survive the reload\n", i);
            return 1;
        }
    }
    printf("reloaded %u cube edits from %s\n", count, path);
    return 0;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    if (strcmp(argv[1], "write") == 0) return WritePhase(argv[2]);
    if (strcmp(argv[1], "read") == 0) return ReadPhase(argv[2]);
    fprintf(stderr, "usage: %s write|read PATH\n", argv[0]);
    return 2;
}
