#include "app/game_debug_trace.h"
#include "app/game_runtime.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void TestJsonEscaping(void)
{
    char escaped[128];
    assert(GameDebugTraceEscapeForTest(
        "quote\" slash\\ line\n tab\t \x01 utf8:\xe8\x8d\x89",
        escaped, sizeof(escaped)));
    assert(strcmp(
        escaped,
        "\"quote\\\" slash\\\\ line\\n tab\\t \\u0001 "
        "utf8:\xe8\x8d\x89\"") == 0);

    char tooSmall[4];
    assert(!GameDebugTraceEscapeForTest("long", tooSmall,
                                        sizeof(tooSmall)));
}

static void TestPathValidation(void)
{
    char path[8] = "keep";
    assert(GameDebugTraceSetPath(path, sizeof(path), "trace"));
    assert(strcmp(path, "trace") == 0);
    assert(!GameDebugTraceSetPath(path, sizeof(path), ""));
    assert(strcmp(path, "trace") == 0);
    assert(!GameDebugTraceSetPath(path, sizeof(path), "12345678"));
    assert(strcmp(path, "trace") == 0);
}

static void TestFixedDeadline(void)
{
    double deadline = GameDebugTraceAdvanceDeadlineForTest(0.0, 0.0);
    assert(fabs(deadline - 0.1) < 0.000001);
    deadline = GameDebugTraceAdvanceDeadlineForTest(deadline, 0.35);
    assert(fabs(deadline - 0.4) < 0.000001);
    deadline = GameDebugTraceAdvanceDeadlineForTest(deadline, 0.4);
    assert(fabs(deadline - 0.5) < 0.000001);
}

static void TestWriteFailureDisablesTrace(void)
{
    FILE *file = fopen("/dev/full", "wb");
    if (!file) return;
    GameRuntime game = {
        .screen = SCREEN_PLAYING,
        .debugTraceEnabled = true,
        .debugTrace = {
            .file = file,
            .frame = 7u,
            .elapsed = 1.25
        }
    };
    snprintf(game.debugTracePath, sizeof(game.debugTracePath),
             "/dev/full");
    assert(!GameDebugTraceWriteEventForTest(&game, "write_failure"));
    assert(game.debugTrace.file == NULL);
    assert(!game.debugTraceEnabled);
    assert(game.debugTrace.ioErrorReported);
    assert(!GameDebugTraceWriteEventForTest(&game, "ignored"));
}

int main(void)
{
    TestJsonEscaping();
    TestPathValidation();
    TestFixedDeadline();
    TestWriteFailureDisablesTrace();
    puts("game debug trace tests passed");
    return 0;
}
