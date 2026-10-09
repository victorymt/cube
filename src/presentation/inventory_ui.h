#ifndef VOXELCRAFT_INVENTORY_UI_H
#define VOXELCRAFT_INVENTORY_UI_H
#include "gameplay/inventory.h"

typedef struct InventoryUiInput {
    Vector2 pointer;
    bool pressed;
    bool released;
    float wheel;
    int hotbarKey; /* -1 when no number key was pressed */
} InventoryUiInput;
void InventoryUiUpdate(InventoryBrowser *browser, BlockType *hotbar,
                       InventoryUiInput input);
void InventoryUiDraw(const InventoryBrowser *browser, const BlockType *hotbar);
/* Shared hit-test layout for deterministic DSL pointer replay. */
Vector2 InventoryUiSlotCenter(int visibleSlot, bool hotbar);
#endif
