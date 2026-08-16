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

int main(void)
{
    TestAtomicReplacement();
    TestFailedWritePreservesDestination();
    puts("save io tests passed");
    return 0;
}
