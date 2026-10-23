#include "world/cube_address_save.h"
#ifndef CUBE_ADDRESS_SAVE_NO_EDIT_STORE
#include "world/world_cube.h"
#endif

#include <stdlib.h>
#include <string.h>

#define CUBE_ADDRESS_SAVE_MAX_RECORDS (1u << 20)

static bool WriteU32(FILE *file, uint32_t value)
{
    unsigned char bytes[4];
    for (unsigned i = 0; i < 4; i++) bytes[i] = (unsigned char)(value >> (i * 8));
    return fwrite(bytes, 1, 4, file) == 4;
}

static bool ReadU32(FILE *file, uint32_t *value)
{
    unsigned char bytes[4];
    if (!value || fread(bytes, 1, 4, file) != 4) return false;
    *value = 0;
    for (unsigned i = 0; i < 4; i++) *value |= (uint32_t)bytes[i] << (i * 8);
    return true;
}

static bool WriteI32(FILE *file, int32_t value)
{
    return WriteU32(file, (uint32_t)value);
}

static bool ReadI32(FILE *file, int32_t *value)
{
    uint32_t raw = 0;
    if (!value || !ReadU32(file, &raw)) return false;
    *value = (int32_t)raw;
    return true;
}

void CubeAddressBlockRelease(CubeAddressBlock *block)
{
    if (!block) return;
    free(block->addresses);
    free(block->types);
    block->addresses = NULL;
    block->types = NULL;
    block->count = 0;
}

bool CubeAddressSaveWrite(FILE *file, const CubeAddressBlock *block)
{
    if (!file || !block || block->count > CUBE_ADDRESS_SAVE_MAX_RECORDS ||
        (block->count > 0 && (!block->addresses || !block->types)))
        return false;
    if (fwrite(CUBE_ADDRESS_SAVE_MAGIC, 1, 8, file) != 8 ||
        !WriteU32(file, CUBE_ADDRESS_SAVE_VERSION) ||
        !WriteU32(file, block->count)) return false;
    for (uint32_t i = 0; i < block->count; i++) {
        WorldBlockAddress address = block->addresses[i];
        if (!WorldBlockAddressIsValid(address)) return false;
        if (!WriteU32(file, (uint32_t)address.dimension)) return false;
        if (address.dimension == WORLD_DIMENSION_HOME ||
            address.dimension == WORLD_DIMENSION_PLANET) {
            if (!WriteU32(file, address.at.surface.bodyId) ||
                !WriteU32(file, (uint32_t)address.at.surface.face) ||
                !WriteI32(file, address.at.surface.u) ||
                !WriteI32(file, address.at.surface.v) ||
                !WriteI32(file, address.at.surface.radial)) return false;
        } else {
            if (!WriteI32(file, address.at.cartesian.x) ||
                !WriteI32(file, address.at.cartesian.y) ||
                !WriteI32(file, address.at.cartesian.z)) return false;
        }
        if (!WriteU32(file, (uint32_t)block->types[i])) return false;
    }
    return !ferror(file);
}

bool CubeAddressSavePeekVersion(FILE *file, uint32_t *outVersion)
{
    if (!file || !outVersion) return false;
    long start = ftell(file);
    if (start < 0) return false;
    char magic[8];
    uint32_t version = 0;
    bool ok = fread(magic, 1, 8, file) == 8 &&
              memcmp(magic, CUBE_ADDRESS_SAVE_MAGIC, 8) == 0 &&
              ReadU32(file, &version) && version == CUBE_ADDRESS_SAVE_VERSION;
    if (fseek(file, start, SEEK_SET) != 0) return false;
    if (ok) *outVersion = version;
    return ok;
}

bool CubeAddressSaveRead(FILE *file, CubeAddressBlock *outBlock)
{
    if (!file || !outBlock) return false;
    *outBlock = (CubeAddressBlock){0};
    char magic[8];
    uint32_t version = 0, count = 0;
    if (fread(magic, 1, 8, file) != 8 ||
        memcmp(magic, CUBE_ADDRESS_SAVE_MAGIC, 8) != 0 ||
        !ReadU32(file, &version) || version != CUBE_ADDRESS_SAVE_VERSION ||
        !ReadU32(file, &count) || count > CUBE_ADDRESS_SAVE_MAX_RECORDS)
        return false;
    if (count == 0) return true;
    /* A header may declare far more records than the file holds. Check the
     * remaining length before allocating so a 16-byte file cannot trigger a
     * multi-megabyte allocation. A non-seekable stream skips this check and
     * relies on the per-record read failing later. */
    long payload = ftell(file);
    if (payload >= 0 && fseek(file, 0, SEEK_END) == 0) {
        long end = ftell(file);
        if (end >= 0 && fseek(file, payload, SEEK_SET) == 0) {
            long remaining = end - payload;
            /* 20 bytes is the smallest record (cartesian); a surface record is
             * 28. Using the minimum keeps this a conservative lower bound so a
             * legitimate file with cartesian records is never rejected. */
            if (remaining < 0 ||
                (unsigned long)remaining < (unsigned long)count * 20ul)
                return false;
        }
    }
    WorldBlockAddress *addresses = calloc(count, sizeof(*addresses));
    BlockType *types = calloc(count, sizeof(*types));
    if (!addresses || !types) {
        free(addresses);
        free(types);
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t dimension = 0, bodyId = 0, face = 0, type = 0;
        int32_t a = 0, b = 0, c = 0;
        WorldBlockAddress address = {0};
        if (!ReadU32(file, &dimension) ||
            dimension > (uint32_t)WORLD_DIMENSION_NETHER) goto fail;
        address.dimension = (WorldDimension)dimension;
        if (address.dimension == WORLD_DIMENSION_HOME ||
            address.dimension == WORLD_DIMENSION_PLANET) {
            if (!ReadU32(file, &bodyId) || !ReadU32(file, &face) ||
                face >= (uint32_t)SURFACE_FACE_COUNT ||
                !ReadI32(file, &a) || !ReadI32(file, &b) || !ReadI32(file, &c))
                goto fail;
            address.at.surface.bodyId = bodyId;
            address.at.surface.face = (SurfaceFace)face;
            address.at.surface.u = a;
            address.at.surface.v = b;
            address.at.surface.radial = c;
        } else {
            if (!ReadI32(file, &a) || !ReadI32(file, &b) || !ReadI32(file, &c))
                goto fail;
            address.at.cartesian.x = a;
            address.at.cartesian.y = b;
            address.at.cartesian.z = c;
        }
        /* Bounds-check the enum here; full block validity belongs to the
         * world layer, which this codec deliberately does not import. */
        /* Accept exactly what the world layer stores. Colour blocks live above
         * BLOCK_BUILTIN_END, so a narrower check here would reject saves that
         * the game itself wrote. */
        if (!ReadU32(file, &type) || type > (uint32_t)BLOCK_COLOR_END ||
            !WorldBlockAddressIsValid(address)) goto fail;
        addresses[i] = address;
        types[i] = (BlockType)type;
    }
    outBlock->addresses = addresses;
    outBlock->types = types;
    outBlock->count = count;
    return true;
fail:
    free(addresses);
    free(types);
    return false;
}

#ifndef CUBE_ADDRESS_SAVE_NO_EDIT_STORE

bool CubeAddressSaveWriteEdits(FILE *file)
{
    unsigned count = WorldCubeEditCount();
    if (!file || count > CUBE_ADDRESS_SAVE_MAX_RECORDS) return false;
    CubeAddressBlock block = {0};
    block.count = count;
    if (count > 0) {
        block.addresses = calloc(count, sizeof(*block.addresses));
        block.types = calloc(count, sizeof(*block.types));
        if (!block.addresses || !block.types) {
            CubeAddressBlockRelease(&block);
            return false;
        }
        for (unsigned i = 0; i < count; i++) {
            if (!WorldCubeEditAt(i, &block.addresses[i], &block.types[i])) {
                CubeAddressBlockRelease(&block);
                return false;
            }
        }
    }
    bool ok = CubeAddressSaveWrite(file, &block);
    CubeAddressBlockRelease(&block);
    return ok;
}

bool CubeAddressSaveReadEdits(FILE *file, uint32_t *outCount)
{
    if (outCount) *outCount = 0;
    WorldCubeClearEdits();
    CubeAddressBlock block;
    if (!CubeAddressSaveRead(file, &block)) return false;
    /* Apply in order. A record the store rejects is dropped on its own: the
     * codec's address check is intentionally looser than the store's, so a
     * single foreign record must not discard the whole edit set. The count
     * reported is what was actually applied, so callers can see the loss. */
    uint32_t applied = 0;
    for (uint32_t i = 0; i < block.count; i++) {
        if (!WorldCubeSetBlock(block.addresses[i], block.types[i])) continue;
        applied++;
    }
    if (outCount) *outCount = applied;
    CubeAddressBlockRelease(&block);
    return true;
}

#endif /* CUBE_ADDRESS_SAVE_NO_EDIT_STORE */
