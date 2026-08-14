#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "save_io.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern int mkstemp(char *template);

static bool SyncParentDirectory(const char *path)
{
    char directory[512];
    const char *slash = strrchr(path, '/');
    if (!slash) {
        snprintf(directory, sizeof(directory), ".");
    } else {
        size_t length = (size_t)(slash - path);
        if (length == 0) length = 1;
        if (length >= sizeof(directory)) return false;
        memcpy(directory, path, length);
        directory[length] = '\0';
    }
    int fd = open(directory, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return false;
    bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
}

static bool CopyFileAtomic(const char *sourcePath, const char *destinationPath)
{
    FILE *source = fopen(sourcePath, "rb");
    if (!source) return errno == ENOENT;

    char temporaryPath[512];
    int pathLength = snprintf(temporaryPath, sizeof(temporaryPath),
                              "%s.tmp.XXXXXX", destinationPath);
    if (pathLength < 0 || (size_t)pathLength >= sizeof(temporaryPath)) {
        fclose(source);
        return false;
    }
    int fd = mkstemp(temporaryPath);
    if (fd < 0) {
        fclose(source);
        return false;
    }
    FILE *destination = fdopen(fd, "wb");
    if (!destination) {
        close(fd);
        unlink(temporaryPath);
        fclose(source);
        return false;
    }

    bool ok = true;
    char buffer[8192];
    size_t read = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        if (fwrite(buffer, 1, read, destination) != read) {
            ok = false;
            break;
        }
    }
    if (ferror(source) || fflush(destination) != 0 || fsync(fileno(destination)) != 0) {
        ok = false;
    }
    if (fclose(destination) != 0) ok = false;
    fclose(source);
    if (!ok || rename(temporaryPath, destinationPath) != 0) {
        unlink(temporaryPath);
        return false;
    }
    (void)SyncParentDirectory(destinationPath);
    return true;
}

bool SaveIoWriteAtomic(const char *path, const char *backupPath,
                       SaveIoWriter writer, void *context)
{
    if (!path || !backupPath || !writer) return false;

    char temporaryPath[512];
    int pathLength = snprintf(temporaryPath, sizeof(temporaryPath),
                              "%s.tmp.XXXXXX", path);
    if (pathLength < 0 || (size_t)pathLength >= sizeof(temporaryPath)) return false;

    int fd = mkstemp(temporaryPath);
    if (fd < 0) return false;
    FILE *file = fdopen(fd, "wb");
    if (!file) {
        close(fd);
        unlink(temporaryPath);
        return false;
    }

    bool ok = writer(file, context) && !ferror(file);
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        unlink(temporaryPath);
        return false;
    }
    if (!CopyFileAtomic(path, backupPath)) {
        unlink(temporaryPath);
        return false;
    }
    if (rename(temporaryPath, path) != 0) {
        unlink(temporaryPath);
        return false;
    }
    (void)SyncParentDirectory(path);
    return true;
}
