#ifndef VOXELCRAFT_RENDER_VOLCANO_H
#define VOXELCRAFT_RENDER_VOLCANO_H
#include "world/volcano_model.h"

typedef struct VolcanoVisual {
    Vector3 position;
    VolcanoState state;
} VolcanoVisual;

/* G1 preview geometry only; no terrain edits, particles or simulation writes. */
void DrawVolcanoPreviews(const VolcanoVisual *visuals, unsigned count,
                         float windAngle, float windStrength);
#endif
