#ifndef VOXELCRAFT_SPACE_PERSISTENCE_H
#define VOXELCRAFT_SPACE_PERSISTENCE_H

#include "space/planet_profile.h"

#include <stdbool.h>
#include <stdio.h>

typedef enum SpaceLoadError {
    SPACE_LOAD_ERROR_NONE = 0,
    SPACE_LOAD_ERROR_INVALID,
    SPACE_LOAD_ERROR_INCOMPATIBLE_SCALE
} SpaceLoadError;

void SpaceSaveOrigin(FILE *file);
bool SpaceLoadOrigin(FILE *file);
bool SpaceSaveState(FILE *file);
bool SpaceLoadState(FILE *file);
SpaceLoadError SpaceLastLoadError(void);
bool SpaceLoadLegacyState(FILE *file);
bool SpaceSaveEdits(FILE *file);
bool SpaceLoadEdits(FILE *file, int storedLayerY);
bool PlanetProfileSaveState(FILE *file, const PlanetProfile *profile);
bool PlanetProfileLoadState(FILE *file, PlanetProfile *outProfile);
bool HomeWorldSaveState(FILE *file);
bool HomeWorldLoadState(FILE *file);
bool PlanetWorldSaveState(FILE *file);
bool PlanetWorldLoadState(FILE *file);

#endif
