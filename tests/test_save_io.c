#define _POSIX_C_SOURCE 200809L
#include "core/save_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool WritePayload(FILE *file, void *context)
{
    const char *payload = context;
    return fwrite(payload, strlen(payload), 1, file) == 1;
}

static bool FailWriter(FILE *file, void *context)
{
    (void)file;
    (void)context;
    return false;
}

typedef struct TransactionFixture {
    int state;
    bool failRead;
    int failedState;
} TransactionFixture;

static bool WriteTransactionState(FILE *file, void *context)
{
    TransactionFixture *fixture = context;
    return fixture &&
           fwrite(&fixture->state, sizeof(fixture->state), 1, file) == 1;
}

static bool ReadTransactionState(FILE *file, void *context)
{
    TransactionFixture *fixture = context;
    int loaded = 0;
    if (!fixture || fread(&loaded, sizeof(loaded), 1, file) != 1) return false;
    fixture->state = loaded;
    if (fixture->failRead && loaded == fixture->failedState) {
        fixture->failRead = false;
        return false;
    }
    return true;
}

static bool ReadTransactionStateAndFail(FILE *file, void *context)
{
    TransactionFixture *fixture = context;
    int loaded = 0;
    if (!fixture || fread(&loaded, sizeof(loaded), 1, file) != 1) return false;
    fixture->state = loaded;
    return false;
}

static char *ReadFile(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length >= 0 && fseek(file, 0, SEEK_SET) == 0);
    char *data = calloc((size_t)length + 1u, 1u);
    assert(data);
    assert(fread(data, (size_t)length, 1, file) == 1);
    fclose(file);
    return data;
}

static void TestAtomicReplacement(void)
{
    char directory[] = "/tmp/voxelcraft-save-XXXXXX";
    assert(mkdtemp(directory));
    char path[256];
    char backup[256];
    snprintf(path, sizeof(path), "%s/save.dat", directory);
    snprintf(backup, sizeof(backup), "%s/save.bak", directory);

    FILE *file = fopen(path, "wb");
    assert(file && fwrite("old", 3, 1, file) == 1);
    fclose(file);
    assert(SaveIoWriteAtomic(path, backup, WritePayload, "new-payload"));

    char *current = ReadFile(path);
    char *previous = ReadFile(backup);
    assert(strcmp(current, "new-payload") == 0);
    assert(strcmp(previous, "old") == 0);
    free(current);
    free(previous);
    unlink(path);
    unlink(backup);
    rmdir(directory);
}

static void TestFailedWritePreservesDestination(void)
{
    char directory[] = "/tmp/voxelcraft-save-fail-XXXXXX";
    assert(mkdtemp(directory));
    char path[256];
    char backup[256];
    snprintf(path, sizeof(path), "%s/save.dat", directory);
    snprintf(backup, sizeof(backup), "%s/save.bak", directory);
    FILE *file = fopen(path, "wb");
    assert(file && fwrite("stable", 6, 1, file) == 1);
    fclose(file);

    assert(!SaveIoWriteAtomic(path, backup, FailWriter, NULL));
    char *current = ReadFile(path);
    assert(strcmp(current, "stable") == 0);
    free(current);
    unlink(path);
    unlink(backup);
    rmdir(directory);
}

static void TestTransactionalReadCommits(void)
{
    TransactionFixture fixture = { .state = 7 };
    FILE *source = tmpfile();
    assert(source);
    int loaded = 19;
    assert(fwrite(&loaded, sizeof(loaded), 1, source) == 1);
    rewind(source);

    assert(SaveIoReadTransactional(
               source, WriteTransactionState, ReadTransactionState, &fixture) ==
           SAVE_IO_TRANSACTION_OK);
    assert(fixture.state == 19);
    fclose(source);
}

static void TestTransactionalReadRollsBackPartialMutation(void)
{
    TransactionFixture fixture = {
        .state = 7,
        .failRead = true,
        .failedState = 19
    };
    FILE *source = tmpfile();
    assert(source);
    int loaded = 19;
    assert(fwrite(&loaded, sizeof(loaded), 1, source) == 1);
    rewind(source);

    assert(SaveIoReadTransactional(
               source, WriteTransactionState, ReadTransactionState, &fixture) ==
           SAVE_IO_TRANSACTION_READ_FAILED);
    assert(fixture.state == 7);
    fclose(source);
}

static void TestTransactionalReadRejectsMissingCallbacks(void)
{
    FILE *source = tmpfile();
    assert(source);
    assert(SaveIoReadTransactional(source, NULL, ReadTransactionState, NULL) ==
           SAVE_IO_TRANSACTION_CHECKPOINT_FAILED);
    fclose(source);
}

static void TestTransactionalReadRequiresCheckpoint(void)
{
    TransactionFixture fixture = { .state = 7 };
    FILE *source = tmpfile();
    assert(source);
    int loaded = 19;
    assert(fwrite(&loaded, sizeof(loaded), 1, source) == 1);
    rewind(source);

    assert(SaveIoReadTransactional(
               source, FailWriter, ReadTransactionState, &fixture) ==
           SAVE_IO_TRANSACTION_CHECKPOINT_FAILED);
    assert(fixture.state == 7);
    fclose(source);
}

static void TestTransactionalReadReportsRollbackFailure(void)
{
    TransactionFixture fixture = { .state = 7 };
    FILE *source = tmpfile();
    assert(source);
    int loaded = 19;
    assert(fwrite(&loaded, sizeof(loaded), 1, source) == 1);
    rewind(source);

    assert(SaveIoReadTransactional(
               source, WriteTransactionState, ReadTransactionStateAndFail,
               &fixture) == SAVE_IO_TRANSACTION_ROLLBACK_FAILED);
    assert(fixture.state == 7);
    fclose(source);
}

int main(void)
{
    TestAtomicReplacement();
    TestFailedWritePreservesDestination();
    TestTransactionalReadCommits();
    TestTransactionalReadRollsBackPartialMutation();
    TestTransactionalReadRejectsMissingCallbacks();
    TestTransactionalReadRequiresCheckpoint();
    TestTransactionalReadReportsRollbackFailure();
    puts("save io tests passed");
    return 0;
}
