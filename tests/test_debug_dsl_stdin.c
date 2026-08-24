#include "core/debug_dsl_stdin.h"

#include <assert.h>
#include <string.h>

static void TestBlockAssembly(void)
{
    DebugDslStdinState state = {0};
    DebugDslStdinReset(&state);
    assert(DebugDslStdinAppendLine(&state, "repeat 2 {") ==
           DEBUG_DSL_STDIN_APPEND_PENDING);
    assert(state.braceDepth == 1);
    assert(DebugDslStdinAppendLine(&state, "  status") ==
           DEBUG_DSL_STDIN_APPEND_PENDING);
    assert(DebugDslStdinAppendLine(&state, "}") ==
           DEBUG_DSL_STDIN_APPEND_READY);
    assert(!strcmp(DebugDslStdinSource(&state),
                   "repeat 2 {\n  status\n}\n"));
    assert(DebugDslStdinHasPending(&state));
    DebugDslStdinReset(&state);
    assert(!DebugDslStdinHasPending(&state));
    assert(state.braceDepth == 0);
    assert(!strcmp(DebugDslStdinSource(&state), ""));
}

static void TestStringsAndCommentsDoNotChangeDepth(void)
{
    DebugDslStdinState state = {0};
    DebugDslStdinReset(&state);
    assert(DebugDslStdinAppendLine(&state, "repeat 1 {") ==
           DEBUG_DSL_STDIN_APPEND_PENDING);
    assert(DebugDslStdinAppendLine(&state, "emit \"}\\\" {\" # }") ==
           DEBUG_DSL_STDIN_APPEND_PENDING);
    assert(state.braceDepth == 1);
    assert(DebugDslStdinAppendLine(&state, "}") ==
           DEBUG_DSL_STDIN_APPEND_READY);
    assert(state.braceDepth == 0);
}

static void TestOverflowAndRecovery(void)
{
    DebugDslStdinState state = {0};
    char line[DEBUG_DSL_STDIN_BUFFER_SIZE];
    memset(line, 'x', sizeof(line) - 1u);
    line[sizeof(line) - 1u] = '\0';
    DebugDslStdinReset(&state);
    assert(DebugDslStdinAppendLine(&state, line) ==
           DEBUG_DSL_STDIN_APPEND_LIMIT);
    assert(!DebugDslStdinHasPending(&state));
    DebugDslStdinReset(&state);
    assert(DebugDslStdinAppendLine(&state, "status") ==
           DEBUG_DSL_STDIN_APPEND_READY);
    assert(!strcmp(DebugDslStdinSource(&state), "status\n"));
}

static void TestInvalidArguments(void)
{
    DebugDslStdinState state = {0};
    assert(DebugDslStdinAppendLine(NULL, "status") ==
           DEBUG_DSL_STDIN_APPEND_INVALID);
    assert(DebugDslStdinAppendLine(&state, NULL) ==
           DEBUG_DSL_STDIN_APPEND_INVALID);
    assert(!DebugDslStdinHasPending(NULL));
    assert(!strcmp(DebugDslStdinSource(NULL), ""));
}

int main(void)
{
    TestBlockAssembly();
    TestStringsAndCommentsDoNotChangeDepth();
    TestOverflowAndRecovery();
    TestInvalidArguments();
    return 0;
}
