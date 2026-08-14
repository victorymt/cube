#include "debug_control.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void TestDisabledControl(void)
{
    DebugControl control;
    DebugControlInitFds(&control, false, -1, -1);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_NONE);
    assert(!DebugControlReply(&control, "unused\n"));
}

static void TestCommandStream(void)
{
    int inputPipe[2];
    int outputPipe[2];
    assert(pipe(inputPipe) == 0);
    assert(pipe(outputPipe) == 0);

    DebugControl control;
    DebugControlInitFds(&control, true, inputPipe[0], outputPipe[1]);
    const char *commands =
        "\n START \r\nscreenshot\nstatus\n"
        "teleport 1.5 72.0 -4.25 3.14 -0.4\n"
        "input 1 -0.5 1 1 120\nunknown\nquit\n";
    assert(write(inputPipe[1], commands, strlen(commands)) ==
           (ssize_t)strlen(commands));

    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_START);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_SCREENSHOT);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_STATUS);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_TELEPORT);
    assert(control.teleport.x == 1.5f);
    assert(control.teleport.y == 72.0f);
    assert(control.teleport.z == -4.25f);
    assert(control.teleport.pitch == -0.4f);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_INPUT);
    assert(control.playerInput.forward == 1.0f);
    assert(control.playerInput.strafe == -0.5f);
    assert(control.playerInput.vertical == 1.0f);
    assert(control.playerInput.sprint);
    assert(control.playerInput.frames == 120u);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_QUIT);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_NONE);

    assert(DebugControlReply(&control,
                             "DEBUG_CONTROL capture ok png=%s\n",
                             "screenshots/test.png"));
    close(outputPipe[1]);
    char response[256];
    ssize_t responseLength = read(outputPipe[0], response, sizeof(response) - 1);
    assert(responseLength > 0);
    response[responseLength] = '\0';
    assert(strcmp(response,
                  "DEBUG_CONTROL capture ok png=screenshots/test.png\n") == 0);

    close(inputPipe[0]);
    close(inputPipe[1]);
    close(outputPipe[0]);
}

static void TestInvalidParameterizedCommands(void)
{
    int inputPipe[2];
    assert(pipe(inputPipe) == 0);
    DebugControl control;
    DebugControlInitFds(&control, true, inputPipe[0], STDOUT_FILENO);
    const char *commands =
        "teleport nan 2 3 0 0\n"
        "teleport 1 2 3 0 2\n"
        "input 2 0 0 0 1\n"
        "input 0 0 0 0 601\n";
    assert(write(inputPipe[1], commands, strlen(commands)) ==
           (ssize_t)strlen(commands));
    for (int index = 0; index < 4; index++) {
        assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_INVALID);
    }
    close(inputPipe[0]);
    close(inputPipe[1]);
}

static void TestFinalCommandWithoutNewline(void)
{
    int inputPipe[2];
    assert(pipe(inputPipe) == 0);
    DebugControl control;
    DebugControlInitFds(&control, true, inputPipe[0], STDOUT_FILENO);
    assert(write(inputPipe[1], "quit", 4) == 4);
    close(inputPipe[1]);

    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_NONE);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_QUIT);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_NONE);
    close(inputPipe[0]);
}

int main(void)
{
    TestDisabledControl();
    TestCommandStream();
    TestFinalCommandWithoutNewline();
    TestInvalidParameterizedCommands();
    puts("debug control tests passed");
    return 0;
}
