#ifndef VOXELCRAFT_RENDER_SORT_H
#define VOXELCRAFT_RENDER_SORT_H

#include "raylib.h"

#include <stdbool.h>

typedef enum TransparentRenderDimension {
    TRANSPARENT_RENDER_SURFACE = 0,
    TRANSPARENT_RENDER_SPACE,
    TRANSPARENT_RENDER_NETHER
} TransparentRenderDimension;

typedef struct TransparentRenderItem {
    const Model *model;
    Vector3 translation;
    float distanceSquared;
    TransparentRenderDimension dimension;
    int cx;
    int cz;
    int slot;
} TransparentRenderItem;

bool TransparentRenderItemAppend(
    TransparentRenderItem *items, int capacity, int *count,
    const Model *model, Vector3 translation, Vector3 center,
    Vector3 cameraPosition, TransparentRenderDimension dimension,
    int cx, int cz, int slot);
void SortTransparentRenderItems(TransparentRenderItem *items, int count);

#endif
