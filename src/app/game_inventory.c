#include "app/game_inventory.h"
#include "app/game_runtime.h"
#include "presentation/starmap.h"
#include "presentation/homeworld_map.h"

bool GameInventorySetOpen(GameRuntime *game, bool open)
{
    if (open && (game->screen != SCREEN_PLAYING || game->paused ||
        game->albumOpen || game->biologyAtlasOpen || game->importDialog.open ||
        game->landingTransition.active || game->ecologyCatchupPending ||
        ShipIsDriving() || StarMapIsOpen() || HomeWorldMapIsOpen())) return false;
    game->inventoryOpen = open;
    game->inventory.drag = -1;
    game->player.velocity = (Vector3){0};
    game->cursorReleased = open;
    if (open) EnableCursor();
    else DisableCursor();
    return true;
}

#include "presentation/inventory_ui.h"
#include <stdio.h>
#include <string.h>

bool GameInventoryDebug(GameRuntime *game, const char *command)
{
    const char *op = command + strlen("inventory ");
    if (strcmp(op,"open") == 0) return GameInventorySetOpen(game,true);
    if (strcmp(op,"close") == 0) return GameInventorySetOpen(game,false);
    if (!game->inventoryOpen) return false;
    if (strcmp(op,"sort") == 0) { InventoryBrowserSort(&game->inventory); return true; }
    if (strcmp(op,"all") == 0 || strcmp(op,"owned") == 0) {
        game->inventory.ownedOnly = strcmp(op,"owned") == 0;
        game->inventory.page = 0; game->inventory.drag = -1; return true;
    }
    int index;
    char extra, action[16], area[16];
    if (sscanf(op,"page %d %c",&index,&extra) == 1) {
        int indices[INVENTORY_BROWSER_CAPACITY];
        int count = InventoryBrowserList(&game->inventory,indices);
        if (index<0 || index>=(count+49)/50) return false;
        game->inventory.page=index; return true;
    }
    if (sscanf(op,"%15s %15s %d %c",action,area,&index,&extra) != 3) return false;
    bool press=strcmp(action,"press")==0;
    if (!press && strcmp(action,"release")!=0) return false;
    bool bar=strcmp(area,"hotbar")==0;
    if (!bar && strcmp(area,"grid")!=0) return false;
    if (index<0 || index>=(bar ? HOTBAR_SIZE : 50)) return false;
    Vector2 pointer=InventoryUiSlotCenter(index,bar);
    SetMousePosition((int)pointer.x,(int)pointer.y);
    InventoryUiUpdate(&game->inventory,game->hotbar,(InventoryUiInput){
        .pointer=pointer,.pressed=press,.released=!press,.hotbarKey=-1});
    return true;
}

bool GameInventoryResolve(GameRuntime *game, const char *name, DebugDslValue *out)
{
    if (strcmp(name,"inventory.open")==0) {
        *out=(DebugDslValue){.type=DEBUG_DSL_VALUE_BOOL,.as.boolean=game->inventoryOpen};
        return true;
    }
    double value;
    int index; char extra;
    if (strcmp(name,"inventory.page")==0) value=game->inventory.page;
    else if (strcmp(name,"inventory.drag")==0) value=game->inventory.drag;
    else if (strcmp(name,"inventory.types")==0) value=game->inventory.count;
    else if (sscanf(name,"inventory.item.%d%c",&index,&extra)==1) {
        if (index<0 || index>=game->inventory.count) return false;
        value=game->inventory.order[index];
    } else if (sscanf(name,"inventory.hotbar.%d%c",&index,&extra)==1) {
        if (index<0 || index>=HOTBAR_SIZE) return false;
        value=game->hotbar[index];
    } else if (sscanf(name,"inventory.count.%d%c",&index,&extra)==1) {
        if (index<0 || index>=INVENTORY_BLOCK_TYPE_COUNT) return false;
        value=InventoryCount((BlockType)index);
    } else return false;
    *out=(DebugDslValue){.type=DEBUG_DSL_VALUE_NUMBER,.as.number=value};
    return true;
}
