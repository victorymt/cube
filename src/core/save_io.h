#ifndef VOXELCRAFT_SAVE_IO_H
#define VOXELCRAFT_SAVE_IO_H

#include <stdbool.h>
#include <stdio.h>

typedef bool (*SaveIoWriter)(FILE *file, void *context);
typedef bool (*SaveIoReader)(FILE *file, void *context);

typedef enum SaveIoTransactionResult {
    SAVE_IO_TRANSACTION_OK = 0,
    SAVE_IO_TRANSACTION_CHECKPOINT_FAILED,
    SAVE_IO_TRANSACTION_READ_FAILED,
    SAVE_IO_TRANSACTION_ROLLBACK_FAILED
} SaveIoTransactionResult;

/* Write a file beside the destination, sync it, preserve the old file as a
 * synced backup, then atomically replace the destination. */
bool SaveIoWriteAtomic(const char *path, const char *backupPath,
                       SaveIoWriter writer, void *context);

/* Snapshot the current state, attempt a read, and restore the snapshot if the
 * reader fails after partially applying data. */
SaveIoTransactionResult SaveIoReadTransactional(
    FILE *source, SaveIoWriter checkpointWriter, SaveIoReader reader,
    void *context);

#endif
