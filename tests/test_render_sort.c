#include "render_sort.h"

#include <assert.h>
#include <stdio.h>

static void TestFarToNearAcrossDimensions(void)
{
    TransparentRenderItem items[4] = { 0 };
    Model models[4] = { 0 };
    int count = 0;
    assert(TransparentRenderItemAppend(items, 4, &count, &models[0], (Vector3){ 0 },
        (Vector3){ 2, 0, 0 }, (Vector3){ 0 }, TRANSPARENT_RENDER_SURFACE, 0, 0, 0));
    assert(TransparentRenderItemAppend(items, 4, &count, &models[1], (Vector3){ 0 },
        (Vector3){ 9, 0, 0 }, (Vector3){ 0 }, TRANSPARENT_RENDER_SPACE, 0, 0, 1));
    assert(TransparentRenderItemAppend(items, 4, &count, &models[2], (Vector3){ 0 },
        (Vector3){ 5, 0, 0 }, (Vector3){ 0 }, TRANSPARENT_RENDER_NETHER, 0, 0, 2));
    SortTransparentRenderItems(items, count);
    assert(items[0].model == &models[1]);
    assert(items[1].model == &models[2]);
    assert(items[2].model == &models[0]);
}

static void TestStableTieBreakOrder(void)
{
    TransparentRenderItem items[6] = { 0 };
    Model models[6] = { 0 };
    int count = 0;
    TransparentRenderItemAppend(items, 6, &count, &models[0], (Vector3){ 0 },
        (Vector3){ 3, 4, 0 }, (Vector3){ 0 }, TRANSPARENT_RENDER_SPACE, 0, 0, 4);
    TransparentRenderItemAppend(items, 6, &count, &models[1], (Vector3){ 0 },
        (Vector3){ 0, 0, 5 }, (Vector3){ 0 }, TRANSPARENT_RENDER_SURFACE, 2, 0, 3);
    TransparentRenderItemAppend(items, 6, &count, &models[2], (Vector3){ 0 },
        (Vector3){ -5, 0, 0 }, (Vector3){ 0 }, TRANSPARENT_RENDER_SURFACE, 1, 1, 2);
    TransparentRenderItemAppend(items, 6, &count, &models[3], (Vector3){ 0 },
        (Vector3){ 0, -5, 0 }, (Vector3){ 0 }, TRANSPARENT_RENDER_SURFACE, 1, 0, 8);
    TransparentRenderItemAppend(items, 6, &count, &models[4], (Vector3){ 0 },
        (Vector3){ 0, 5, 0 }, (Vector3){ 0 }, TRANSPARENT_RENDER_SURFACE, 1, 0, 7);
    SortTransparentRenderItems(items, count);
    assert(items[0].model == &models[4]);
    assert(items[1].model == &models[3]);
    assert(items[2].model == &models[2]);
    assert(items[3].model == &models[1]);
    assert(items[4].model == &models[0]);
}

static void TestCapacityAndCameraMovement(void)
{
    TransparentRenderItem items[2] = { 0 };
    Model models[3] = { 0 };
    int count = 0;
    assert(TransparentRenderItemAppend(items, 2, &count, &models[0], (Vector3){ 0 },
        (Vector3){ 8, 0, 0 }, (Vector3){ 17, 0, 0 }, TRANSPARENT_RENDER_SURFACE, 0, 0, 0));
    assert(TransparentRenderItemAppend(items, 2, &count, &models[1], (Vector3){ 0 },
        (Vector3){ 24, 0, 0 }, (Vector3){ 17, 0, 0 }, TRANSPARENT_RENDER_SURFACE, 1, 0, 1));
    assert(!TransparentRenderItemAppend(items, 2, &count, &models[2], (Vector3){ 0 },
        (Vector3){ 40, 0, 0 }, (Vector3){ 17, 0, 0 }, TRANSPARENT_RENDER_SURFACE, 2, 0, 2));
    SortTransparentRenderItems(items, count);
    assert(items[0].model == &models[0]);
    assert(items[1].model == &models[1]);
    SortTransparentRenderItems(NULL, 0);
    SortTransparentRenderItems(items, 1);
}

int main(void)
{
    TestFarToNearAcrossDimensions();
    TestStableTieBreakOrder();
    TestCapacityAndCameraMovement();
    puts("transparent render sorting tests passed");
    return 0;
}
