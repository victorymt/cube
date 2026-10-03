#ifndef VOXELCRAFT_RENDER_VOLCANO_H
#define VOXELCRAFT_RENDER_VOLCANO_H
#include "world/volcano_model.h"
#include "world/lava_model.h"

typedef struct VolcanoVisual {
    Vector3 position;
    VolcanoState state;
} VolcanoVisual;

/* All drawing reads snapshots and never mutates simulation or shared RNG. */
void DrawVolcanoPreviews(const VolcanoVisual *visuals, unsigned count,
                         float windAngle, float windStrength);
void DrawLavaCell(Matrix sectionTransform, Vector3 sectionPosition,
                  LavaThermalState thermal);
#define LAVA_STEAM_MAX_CELLS 24u
#define LAVA_STEAM_PUFFS_PER_CELL 4u
void DrawLavaSteam(Matrix sectionTransform, Vector3 sectionPosition,
                   double recentWater, uint64_t ticks, float windAngle, float windStrength);
#endif
