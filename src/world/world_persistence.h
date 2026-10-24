#ifndef VOXELCRAFT_WORLD_PERSISTENCE_H
#define VOXELCRAFT_WORLD_PERSISTENCE_H

#include "world/block_edit_types.h"
#include "world/surface_types.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

bool WorldPersistenceSaveExtension(FILE *file);
bool WorldPersistenceLoadExtension(FILE *file);
bool WorldPersistenceReserveEdits(int capacity);
bool WorldPersistenceEditsValid(const BlockEdit *edits, int count);
bool WorldPersistenceInstallEdits(const BlockEdit *edits,
                                  const uint32_t *dimensions,
                                  const SurfaceAddress *addresses,
                                  const SurfaceMapCell *mapCells,
                                  int count);

#endif
