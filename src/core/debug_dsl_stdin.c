#include "core/debug_dsl_stdin.h"

#include <string.h>

void DebugDslStdinReset(DebugDslStdinState *state)
{
    if (!state) return;
    state->length = 0u;
    state->braceDepth = 0;
    state->input[0] = '\0';
}

static void CountBraces(DebugDslStdinState *state, const char *line)
{
    bool string = false;
    bool escaped = false;
    for (const char *cursor = line; *cursor != '\0'; cursor++) {
        if (!string && *cursor == '#') break;
        if (string && escaped) {
            escaped = false;
            continue;
        }
        if (string && *cursor == '\\') {
            escaped = true;
            continue;
        }
        if (*cursor == '"') {
            string = !string;
            continue;
        }
        if (string) continue;
        if (*cursor == '{') state->braceDepth++;
        else if (*cursor == '}') state->braceDepth--;
    }
}

DebugDslStdinAppendResult DebugDslStdinAppendLine(
    DebugDslStdinState *state, const char *line)
{
    if (!state || !line) return DEBUG_DSL_STDIN_APPEND_INVALID;
    if (state->length >= DEBUG_DSL_STDIN_BUFFER_SIZE)
        return DEBUG_DSL_STDIN_APPEND_LIMIT;
    size_t length = strlen(line);
    if (length + 2u > DEBUG_DSL_STDIN_BUFFER_SIZE - state->length)
        return DEBUG_DSL_STDIN_APPEND_LIMIT;

    memcpy(state->input + state->length, line, length);
    state->length += length;
    state->input[state->length++] = '\n';
    state->input[state->length] = '\0';
    CountBraces(state, line);
    return state->braceDepth > 0 ? DEBUG_DSL_STDIN_APPEND_PENDING
                                 : DEBUG_DSL_STDIN_APPEND_READY;
}

bool DebugDslStdinHasPending(const DebugDslStdinState *state)
{
    return state && state->length != 0u;
}

const char *DebugDslStdinSource(const DebugDslStdinState *state)
{
    return state ? state->input : "";
}
