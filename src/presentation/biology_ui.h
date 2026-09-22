#ifndef VOXELCRAFT_BIOLOGY_UI_H
#define VOXELCRAFT_BIOLOGY_UI_H

#include "ecology/entity.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct BiologyAtlasUiInput {
    bool open;
    int selectedSlot;
    bool evolutionScanLocked;
    uint32_t evolutionLockedOrganismId;
} BiologyAtlasUiInput;

typedef struct BiologyAtlasUiOutput {
    int selectedSlot;
    bool closeRequested;
} BiologyAtlasUiOutput;

void DrawEvolutionScanPanel(const EntityEvolutionDebugInfo *info,
                            bool locked);
void BiologyAtlasDraw(const BiologyAtlasUiInput *input,
                      BiologyAtlasUiOutput *output);

#endif
