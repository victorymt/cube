#include "world/surface_save.h"

#include <stdlib.h>

static bool SurfaceSaveWriteAddress(FILE *file, SurfaceAddress address)
{
    uint32_t face = (uint32_t)address.face;
    int32_t u = (int32_t)address.u;
    int32_t v = (int32_t)address.v;
    int32_t radial = (int32_t)address.radial;
    return file && SurfaceAddressIsValid(address) &&
           fwrite(&address.bodyId, sizeof(address.bodyId), 1, file) == 1 &&
           fwrite(&face, sizeof(face), 1, file) == 1 &&
           fwrite(&u, sizeof(u), 1, file) == 1 &&
           fwrite(&v, sizeof(v), 1, file) == 1 &&
           fwrite(&radial, sizeof(radial), 1, file) == 1;
}

static bool SurfaceSaveReadAddress(FILE *file, SurfaceAddress *outAddress)
{
    if (!file || !outAddress) return false;
    uint32_t bodyId = 0u;
    uint32_t face = 0u;
    int32_t u = 0;
    int32_t v = 0;
    int32_t radial = 0;
    if (fread(&bodyId, sizeof(bodyId), 1, file) != 1 ||
        fread(&face, sizeof(face), 1, file) != 1 ||
        fread(&u, sizeof(u), 1, file) != 1 ||
        fread(&v, sizeof(v), 1, file) != 1 ||
        fread(&radial, sizeof(radial), 1, file) != 1 ||
        face >= (uint32_t)SURFACE_FACE_COUNT) {
        return false;
    }
    SurfaceAddress address = {
        .bodyId = bodyId,
        .face = (SurfaceFace)face,
        .u = (int)u,
        .v = (int)v,
        .radial = (int)radial
    };
    if (!SurfaceAddressIsValid(address)) return false;
    *outAddress = address;
    return true;
}

static bool SurfaceSaveMapCellIsCanonical(SurfaceMapCell cell)
{
    SurfaceMapCell canonical = SurfaceCanonicalMapCell(
        (float)cell.x, (float)cell.z);
    return canonical.x == cell.x && canonical.z == cell.z;
}

static bool SurfaceSaveAddressMatchesMapCell(
    SurfaceAddress address, SurfaceMapCell cell)
{
    SurfaceAddress expected = SurfaceAddressFromMapCoordinates(
        address.bodyId, (float)cell.x, (float)cell.z, address.radial);
    return SurfaceAddressEqual(address, expected);
}

bool SurfaceSaveWriteTrailer(FILE *file, bool playerHasAddress,
                             SurfaceAddress playerAddress,
                             const SurfaceAddress *editAddresses,
                             const SurfaceMapCell *editMapCells,
                             uint32_t editCount)
{
    if (!file || (editCount > 0u && (!editAddresses || !editMapCells))) return false;
    uint32_t schemaVersion = SURFACE_SAVE_SCHEMA_VERSION;
    uint8_t hasPlayer = playerHasAddress ? 1u : 0u;
    bool ok = fwrite(&schemaVersion, sizeof(schemaVersion), 1, file) == 1 &&
              fwrite(&hasPlayer, sizeof(hasPlayer), 1, file) == 1 &&
              SurfaceSaveWriteAddress(file, playerAddress) &&
              fwrite(&editCount, sizeof(editCount), 1, file) == 1;
    for (uint32_t index = 0u; ok && index < editCount; index++) {
        ok = SurfaceSaveWriteAddress(file, editAddresses[index]);
        if (ok) ok = fwrite(&editMapCells[index].x,
                           sizeof(editMapCells[index].x), 1, file) == 1 &&
                    fwrite(&editMapCells[index].z,
                           sizeof(editMapCells[index].z), 1, file) == 1;
    }
    return ok && !ferror(file);
}

bool SurfaceSaveReadTrailer(FILE *file, uint32_t expectedEditCount,
                            uint32_t *outSchemaVersion,
                            bool *outPlayerHasAddress,
                            SurfaceAddress *outPlayerAddress,
                            SurfaceAddress **outEditAddresses,
                            SurfaceMapCell **outEditMapCells)
{
    if (!file || !outSchemaVersion || !outPlayerHasAddress || !outPlayerAddress ||
        !outEditAddresses || !outEditMapCells) {
        return false;
    }
    *outSchemaVersion = 0u;
    *outEditAddresses = NULL;
    *outEditMapCells = NULL;

    uint32_t schemaVersion = 0u;
    uint8_t playerHasAddress = 0u;
    SurfaceAddress playerAddress = { 0 };
    uint32_t editCount = 0u;
    if (fread(&schemaVersion, sizeof(schemaVersion), 1, file) != 1 ||
        schemaVersion < SURFACE_SAVE_MIN_SCHEMA_VERSION ||
        schemaVersion > SURFACE_SAVE_SCHEMA_VERSION ||
        fread(&playerHasAddress, sizeof(playerHasAddress), 1, file) != 1 ||
        playerHasAddress > 1u ||
        !SurfaceSaveReadAddress(file, &playerAddress) ||
        fread(&editCount, sizeof(editCount), 1, file) != 1 ||
        editCount != expectedEditCount) {
        return false;
    }

    SurfaceAddress *addresses = editCount > 0u
        ? malloc((size_t)editCount * sizeof(*addresses)) : NULL;
    SurfaceMapCell *mapCells = editCount > 0u
        ? malloc((size_t)editCount * sizeof(*mapCells)) : NULL;
    if (editCount > 0u && (!addresses || !mapCells)) {
        free(addresses);
        free(mapCells);
        return false;
    }
    for (uint32_t index = 0u; index < editCount; index++) {
        if (!SurfaceSaveReadAddress(file, &addresses[index])) {
            free(addresses);
            free(mapCells);
            return false;
        }
        bool cellValid = false;
        if (schemaVersion >= 2u) {
            cellValid =
                fread(&mapCells[index].x,
                      sizeof(mapCells[index].x), 1, file) == 1 &&
                fread(&mapCells[index].z,
                      sizeof(mapCells[index].z), 1, file) == 1 &&
                SurfaceSaveMapCellIsCanonical(mapCells[index]) &&
                SurfaceSaveAddressMatchesMapCell(
                    addresses[index], mapCells[index]);
        } else {
            mapCells[index] = (SurfaceMapCell){ 0 };
            cellValid = true;
        }
        if (!cellValid) {
            free(addresses);
            free(mapCells);
            return false;
        }
    }
    if (fgetc(file) != EOF || ferror(file)) {
        free(addresses);
        free(mapCells);
        return false;
    }

    *outSchemaVersion = schemaVersion;
    *outPlayerHasAddress = playerHasAddress != 0u;
    *outPlayerAddress = playerAddress;
    *outEditAddresses = addresses;
    *outEditMapCells = mapCells;
    return true;
}
