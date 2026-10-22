#ifndef VOXELCRAFT_CUBE_ADDRESS_SAVE_H
#define VOXELCRAFT_CUBE_ADDRESS_SAVE_H

#include "world/world_location.h"
#include "world/world_types.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Versioned, explicitly little-endian encoding of cube block identities.
 * This is the V30 foundation: it never writes struct padding, host byte order
 * or a map-space coordinate. Reading validates every field and rejects an
 * unknown version instead of guessing.
 */
#define CUBE_ADDRESS_SAVE_MAGIC "CUBADDR1"
#define CUBE_ADDRESS_SAVE_VERSION 1u
#define CUBE_ADDRESS_SAVE_HEADER_BYTES 16u

typedef struct CubeAddressBlock {
    WorldBlockAddress *addresses;
    BlockType *types;
    uint32_t count;
} CubeAddressBlock;

/* Writes magic, version and record count, then each record. */
bool CubeAddressSaveWrite(FILE *file, const CubeAddressBlock *block);
/* Allocates the record arrays; the caller owns them. Rejects a wrong magic,
 * an unknown version, an over-large count or any invalid address. */
bool CubeAddressSaveRead(FILE *file, CubeAddressBlock *outBlock);
/* Rejects a file whose header names a different format or version. */
bool CubeAddressSavePeekVersion(FILE *file, uint32_t *outVersion);
void CubeAddressBlockRelease(CubeAddressBlock *block);

#endif
