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

bool SurfaceSaveWriteTrailer(FILE *file, bool playerHasAddress,
                             SurfaceAddress playerAddress,
                             const SurfaceAddress *editAddresses,
                             uint32_t editCount)
{
    if (!file || (editCount > 0u && !editAddresses)) return false;
    uint32_t schemaVersion = SURFACE_SAVE_SCHEMA_VERSION;
    uint8_t hasPlayer = playerHasAddress ? 1u : 0u;
    bool ok = fwrite(&schemaVersion, sizeof(schemaVersion), 1, file) == 1 &&
              fwrite(&hasPlayer, sizeof(hasPlayer), 1, file) == 1 &&
              SurfaceSaveWriteAddress(file, playerAddress) &&
              fwrite(&editCount, sizeof(editCount), 1, file) == 1;
    for (uint32_t index = 0u; ok && index < editCount; index++) {
        ok = SurfaceSaveWriteAddress(file, editAddresses[index]);
    }
    return ok && !ferror(file);
}

bool SurfaceSaveReadTrailer(FILE *file, uint32_t expectedEditCount,
                            bool *outPlayerHasAddress,
                            SurfaceAddress *outPlayerAddress,
                            SurfaceAddress **outEditAddresses)
{
    if (!file || !outPlayerHasAddress || !outPlayerAddress ||
        !outEditAddresses) {
        return false;
    }
    *outEditAddresses = NULL;

    uint32_t schemaVersion = 0u;
    uint8_t playerHasAddress = 0u;
    SurfaceAddress playerAddress = { 0 };
    uint32_t editCount = 0u;
    if (fread(&schemaVersion, sizeof(schemaVersion), 1, file) != 1 ||
        schemaVersion != SURFACE_SAVE_SCHEMA_VERSION ||
        fread(&playerHasAddress, sizeof(playerHasAddress), 1, file) != 1 ||
        playerHasAddress > 1u ||
        !SurfaceSaveReadAddress(file, &playerAddress) ||
        fread(&editCount, sizeof(editCount), 1, file) != 1 ||
        editCount != expectedEditCount) {
        return false;
    }

    SurfaceAddress *addresses = editCount > 0u
        ? malloc((size_t)editCount * sizeof(*addresses)) : NULL;
    if (editCount > 0u && !addresses) return false;
    for (uint32_t index = 0u; index < editCount; index++) {
        if (!SurfaceSaveReadAddress(file, &addresses[index])) {
            free(addresses);
            return false;
        }
    }
    if (fgetc(file) != EOF || ferror(file)) {
        free(addresses);
        return false;
    }

    *outPlayerHasAddress = playerHasAddress != 0u;
    *outPlayerAddress = playerAddress;
    *outEditAddresses = addresses;
    return true;
}
