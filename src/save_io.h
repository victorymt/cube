#ifndef VOXELCRAFT_SAVE_IO_H
#define VOXELCRAFT_SAVE_IO_H

#include <stdbool.h>
#include <stdio.h>

typedef bool (*SaveIoWriter)(FILE *file, void *context);

/* Write a file beside the destination, sync it, preserve the old file as a
 * synced backup, then atomically replace the destination. */
bool SaveIoWriteAtomic(const char *path, const char *backupPath,
                       SaveIoWriter writer, void *context);

#endif
