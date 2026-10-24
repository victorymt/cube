#ifndef VOXELCRAFT_UI_TYPES_H
#define VOXELCRAFT_UI_TYPES_H

#include <stdbool.h>

typedef struct ImportDialog {
    bool open;
    bool relief;
    int maxBlocks;
    char path[1024];
} ImportDialog;

#endif
