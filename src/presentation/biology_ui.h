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

void BiologyAtlasPreviewConfigure(uint32_t worldSeed, EvolutionArchetype type,
                                  uint32_t seed, int length, int width,
                                  int appendages, bool silhouette);

typedef struct BiologyPreviewPoseInfo {
    int action;
    float speed;
    float phase;
    float turn;
    bool frozen;
} BiologyPreviewPoseInfo;

bool BiologyAtlasPreviewPoseConfigure(int action, float speed, float phase, float turn);
BiologyPreviewPoseInfo BiologyAtlasPreviewPoseInfo(void);

typedef struct BiologyPreviewScenarioInfo {
    bool valid;
    FaunaBehaviorInput input;
    FaunaBehaviorDecision decision;
} BiologyPreviewScenarioInfo;

bool BiologyAtlasPreviewScenarioConfigure(const FaunaBehaviorInput *input);
BiologyPreviewScenarioInfo BiologyAtlasPreviewScenarioInfo(void);

void DrawEvolutionScanPanel(const EntityEvolutionDebugInfo *info,
                            bool locked);
void BiologyAtlasDraw(const BiologyAtlasUiInput *input,
                      BiologyAtlasUiOutput *output);

#endif
