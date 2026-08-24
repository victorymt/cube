#ifndef VOXELCRAFT_DEBUG_DSL_STDIN_H
#define VOXELCRAFT_DEBUG_DSL_STDIN_H

#include <stdbool.h>
#include <stddef.h>

#define DEBUG_DSL_STDIN_BUFFER_SIZE 16384u

typedef enum DebugDslStdinAppendResult {
    DEBUG_DSL_STDIN_APPEND_PENDING = 0,
    DEBUG_DSL_STDIN_APPEND_READY,
    DEBUG_DSL_STDIN_APPEND_LIMIT,
    DEBUG_DSL_STDIN_APPEND_INVALID
} DebugDslStdinAppendResult;

typedef struct DebugDslStdinState {
    size_t length;
    int braceDepth;
    char input[DEBUG_DSL_STDIN_BUFFER_SIZE];
} DebugDslStdinState;

void DebugDslStdinReset(DebugDslStdinState *state);
DebugDslStdinAppendResult DebugDslStdinAppendLine(
    DebugDslStdinState *state, const char *line);
bool DebugDslStdinHasPending(const DebugDslStdinState *state);
const char *DebugDslStdinSource(const DebugDslStdinState *state);

#endif
