#include "presentation/render_sort.h"

#include <stdlib.h>

static int CompareTransparentRenderItems(const void *left, const void *right)
{
    const TransparentRenderItem *a = left;
    const TransparentRenderItem *b = right;
    if (a->distanceSquared > b->distanceSquared) return -1;
    if (a->distanceSquared < b->distanceSquared) return 1;
    if (a->dimension != b->dimension) return a->dimension < b->dimension ? -1 : 1;
    if (a->cx != b->cx) return a->cx < b->cx ? -1 : 1;
    if (a->cz != b->cz) return a->cz < b->cz ? -1 : 1;
    if (a->slot != b->slot) return a->slot < b->slot ? -1 : 1;
    return 0;
}

bool TransparentRenderItemAppend(
    TransparentRenderItem *items, int capacity, int *count,
    const Model *model, Vector3 translation, Vector3 center,
    Vector3 cameraPosition, TransparentRenderDimension dimension,
    int cx, int cz, int slot)
{
    if (!items || !count || !model || *count < 0 || *count >= capacity) return false;
    float dx = center.x - cameraPosition.x;
    float dy = center.y - cameraPosition.y;
    float dz = center.z - cameraPosition.z;
    items[*count] = (TransparentRenderItem){
        .model = model,
        .translation = translation,
        .distanceSquared = dx * dx + dy * dy + dz * dz,
        .dimension = dimension,
        .cx = cx,
        .cz = cz,
        .slot = slot
    };
    (*count)++;
    return true;
}

void SortTransparentRenderItems(TransparentRenderItem *items, int count)
{
    if (!items || count <= 1) return;
    qsort(items, (size_t)count, sizeof(*items), CompareTransparentRenderItems);
}
