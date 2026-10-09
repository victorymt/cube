#include "presentation/inventory_ui.h"
#include "presentation/render_ui.h"
#include "presentation/render_ui.h"
#include "world/block_atlas.h"
#include "world/chunks.h"
#include "world/world.h"
#include <math.h>

#define COLS 10
#define ROWS 5
#define PAGE_SIZE (COLS * ROWS)
static const Color ink = { 234, 229, 216, 255 };
static const Color muted = { 153, 160, 154, 255 };
static const Color accent = { 188, 216, 146, 255 };

static float Scale(void)
{
    return fminf(1.3f, fminf(GetScreenWidth() / 800.0f, GetScreenHeight() / 650.0f));
}
static Rectangle Rect(float x, float y, float w, float h)
{
    float s = Scale();
    return (Rectangle){ (GetScreenWidth() - 780*s)/2 + x*s,
        (GetScreenHeight() - 620*s)/2 + y*s, w*s, h*s };
}
static void Text(const char *text, float x, float y, int size, Color color)
{
    Rectangle r = Rect(x,y,0,0);
    UiDrawText(text, (int)r.x, (int)r.y, (int)fmaxf(8, size*Scale()), color);
}
static Rectangle Slot(int index, bool hotbar)
{
    return Rect(30 + (index % COLS)*72, hotbar ? 508 : 132 + (index/COLS)*68, 66, 60);
}
Vector2 InventoryUiSlotCenter(int index, bool hotbar)
{
    Rectangle r = Slot(index, hotbar);
    return (Vector2){r.x+r.width/2, r.y+r.height/2};
}
static int List(const InventoryBrowser *b, int *indices, int *page, int *pages)
{
    int n = InventoryBrowserList(b, indices);
    *pages = (n + PAGE_SIZE - 1)/PAGE_SIZE;
    if (*pages < 1) *pages = 1;
    *page = b->page < *pages ? b->page : *pages-1;
    if (*page < 0) *page = 0;
    return n;
}
static int Hit(const InventoryBrowser *b, Vector2 p)
{
    int indices[INVENTORY_BROWSER_CAPACITY], page, pages;
    int n = List(b, indices, &page, &pages);
    for (int i = 0; i < PAGE_SIZE && page*PAGE_SIZE+i < n; i++)
        if (CheckCollisionPointRec(p, Slot(i,false))) return indices[page*PAGE_SIZE+i];
    for (int i = 0; i < HOTBAR_SIZE; i++)
        if (CheckCollisionPointRec(p, Slot(i,true))) return b->count+i;
    return -1;
}
void InventoryUiUpdate(InventoryBrowser *b, BlockType *hotbar, InventoryUiInput in)
{
    int indices[INVENTORY_BROWSER_CAPACITY], page, pages;
    List(b, indices, &page, &pages);
    b->page = page;
    int target = Hit(b, in.pointer);
    if (in.hotbarKey >= 0 && in.hotbarKey < HOTBAR_SIZE && target >= 0)
        InventoryBrowserDrop(b, hotbar, target, b->count+in.hotbarKey);
    if (in.pressed) {
        b->drag = target;
        if (CheckCollisionPointRec(in.pointer, Rect(30,80,150,32))) {
            b->ownedOnly = false; b->page = 0;
        } else if (CheckCollisionPointRec(in.pointer, Rect(188,80,150,32))) {
            b->ownedOnly = true; b->page = 0;
        } else if (CheckCollisionPointRec(in.pointer, Rect(600,80,144,32))) {
            InventoryBrowserSort(b);
        } else if (CheckCollisionPointRec(in.pointer, Rect(548,470,60,28))) {
            b->page--;
        } else if (CheckCollisionPointRec(in.pointer, Rect(684,470,60,28))) {
            b->page++;
        }
    }
    if (in.released) {
        InventoryBrowserDrop(b, hotbar, b->drag, target);
        b->drag = -1;
    }
    if (in.wheel) {
        b->page += in.wheel > 0 ? -1 : 1;
        /* Absolute source index is retained when paging during a drag. */
    }
    List(b, indices, &page, &pages);
    b->page = page;
}
static void Button(const char *label, float x, float y, float w, bool active)
{
    Rectangle r = Rect(x,y,w,32);
    bool hover = CheckCollisionPointRec(GetMousePosition(),r);
    DrawRectangleRec(r, active ? (Color){68,86,62,255} :
        hover ? (Color){62,68,61,255} : (Color){44,49,44,255});
    Text(label,x+12,y+8,16,active ? accent : ink);
}
static void Icon(BlockType type, Rectangle r, bool enabled)
{
    if (type == BLOCK_AIR) return;
    Rectangle dest = {r.x+r.width*.22f, r.y+r.height*.08f,r.width*.56f,r.height*.62f};
    DrawTexturePro(ChunksBlockAtlas(), AtlasSourceRect(TextureForBlockFace(type,2)),
        dest,(Vector2){0},0, enabled ? WHITE : Fade(WHITE,.4f));
}
void InventoryUiDraw(const InventoryBrowser *b, const BlockType *hotbar)
{
    int indices[INVENTORY_BROWSER_CAPACITY], page, pages;
    int n = List(b,indices,&page,&pages);
    Vector2 mouse = GetMousePosition();
    int hover = Hit(b,mouse);
    DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),Fade(BLACK,.65f));
    DrawRectangleRec(Rect(5,7,780,620),Fade(BLACK,.4f));
    DrawRectangleRec(Rect(0,0,780,620),(Color){27,32,29,255});
    DrawRectangleLinesEx(Rect(0,0,780,620),Scale(),(Color){93,105,85,255});
    Text("INVENTORY",30,24,28,ink);
    Text("Browse materials / arrange your hotbar",30,56,14,muted);
    Text("E / Esc  Close",602,32,16,muted);
    Button("All items",30,80,150,!b->ownedOnly);
    Button("Owned",188,80,150,b->ownedOnly);
    Button("Sort by count",600,80,144,false);
    for (int i=0; i<PAGE_SIZE+HOTBAR_SIZE; i++) {
        bool bar = i>=PAGE_SIZE;
        int local = bar ? i-PAGE_SIZE : i;
        int entry = bar ? b->count+local : page*PAGE_SIZE+i<n ? indices[page*PAGE_SIZE+i] : -1;
        BlockType type = entry<0 ? BLOCK_AIR : bar ? hotbar[local] : b->order[entry];
        Rectangle r = Slot(local,bar);
        DrawRectangleRec(r, (Color){39,45,40,255});
        DrawRectangleLinesEx(r,Scale(),entry>=0 && entry==hover ? accent : (Color){68,76,65,255});
        Icon(type,r,InventoryCount(type)>0);
        if (type!=BLOCK_AIR) {
            const char *count = InventoryRemainder(type) ? TextFormat("%d+",InventoryCount(type)) : TextFormat("%d",InventoryCount(type));
            int size = (int)fmaxf(8,13*Scale());
            UiDrawText(count,(int)(r.x+r.width-5*Scale()-UiMeasureText(count,size)),
                (int)(r.y+r.height-18*Scale()),size,InventoryCount(type)>0 ? ink : muted);
        }
        if (bar) Text(local==9 ? "0" : TextFormat("%d",local+1),34+local*72,551,12,muted);
    }
    if (!n) Text("No items collected yet",274,280,20,muted);
    Text(TextFormat("%d item types",n),30,474,14,muted);
    Button("<",548,466,60,false);
    Text(TextFormat("%d / %d",page+1,pages),619,474,14,ink);
    Button(">",684,466,60,false);
    Text("Drag to swap / drag to hotbar / hover + 1-0 to assign",30,587,14,muted);
    if (b->drag >= 0) {
        BlockType type = b->drag < b->count ? b->order[b->drag] : hotbar[b->drag-b->count];
        Rectangle r = {mouse.x-28*Scale(),mouse.y-28*Scale(),56*Scale(),56*Scale()};
        Icon(type,r,true);
    } else if (hover>=0) {
        BlockType type = hover<b->count ? b->order[hover] : hotbar[hover-b->count];
        if (type!=BLOCK_AIR) {
            const char *label=TextFormat("%s  |  %d owned%s",BlockName(type),InventoryCount(type),InventoryRemainder(type) ? " + partial" : "");
            int size=(int)fmaxf(8,16*Scale());
            float width=UiMeasureText(label,size)+20*Scale();
            float x=fminf(mouse.x+16,GetScreenWidth()-width-4);
            float y=fminf(mouse.y+22,GetScreenHeight()-32*Scale());
            DrawRectangleRec((Rectangle){x,y,width,28*Scale()},(Color){12,17,14,250});
            UiDrawText(label,(int)(x+10*Scale()),(int)(y+6*Scale()),size,ink);
        }
    }
}
