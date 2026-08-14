#include "debug_control.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

void DebugControlInitFds(DebugControl *control, bool enabled,
                         int inputFd, int outputFd)
{
    if (!control) return;
    *control = (DebugControl){
        .enabled = enabled,
        .inputFd = inputFd,
        .outputFd = outputFd
    };
}

void DebugControlInit(DebugControl *control, bool enabled)
{
    DebugControlInitFds(control, enabled, STDIN_FILENO, STDOUT_FILENO);
}

static DebugControlCommand DebugControlParseLine(DebugControl *control,
                                                 char *line)
{
    while (*line != '\0' && isspace((unsigned char)*line)) line++;
    char *end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    for (char *cursor = line; *cursor != '\0'; cursor++) {
        *cursor = (char)tolower((unsigned char)*cursor);
    }

    if (line[0] == '\0') return DEBUG_CONTROL_COMMAND_NONE;
    if (strcmp(line, "start") == 0) return DEBUG_CONTROL_COMMAND_START;
    if (strcmp(line, "screenshot") == 0) {
        return DEBUG_CONTROL_COMMAND_SCREENSHOT;
    }
    if (strcmp(line, "status") == 0) return DEBUG_CONTROL_COMMAND_STATUS;
    if (strcmp(line, "fluid inspect") == 0) {
        control->fluidUsePlayerPosition = true;
        return DEBUG_CONTROL_COMMAND_FLUID_INSPECT;
    }
    int fluidX = 0;
    int fluidY = 0;
    int fluidZ = 0;
    unsigned fluidValue = 0u;
    char fluidTrailing = '\0';
    if (sscanf(line, "fluid inspect %d %d %d %c", &fluidX, &fluidY,
               &fluidZ, &fluidTrailing) == 3) {
        control->fluidX = fluidX;
        control->fluidY = fluidY;
        control->fluidZ = fluidZ;
        control->fluidUsePlayerPosition = false;
        return DEBUG_CONTROL_COMMAND_FLUID_INSPECT;
    }
    if (sscanf(line, "fluid set %d %d %d %u %c", &fluidX, &fluidY,
               &fluidZ, &fluidValue, &fluidTrailing) == 4 &&
        fluidValue <= 255u) {
        control->fluidX = fluidX;
        control->fluidY = fluidY;
        control->fluidZ = fluidZ;
        control->fluidVolume = fluidValue;
        return DEBUG_CONTROL_COMMAND_FLUID_SET;
    }
    if (sscanf(line, "fluid step %u %c", &fluidValue, &fluidTrailing) == 1 &&
        fluidValue >= 1u && fluidValue <= 1000000u) {
        control->fluidTicks = fluidValue;
        return DEBUG_CONTROL_COMMAND_FLUID_STEP;
    }
    if (strcmp(line, "evolution region") == 0) {
        return DEBUG_CONTROL_COMMAND_EVOLUTION_REGION;
    }
    if (strcmp(line, "evolution atlas") == 0) {
        return DEBUG_CONTROL_COMMAND_EVOLUTION_ATLAS;
    }
    if (strcmp(line, "evolution catalog") == 0) {
        return DEBUG_CONTROL_COMMAND_EVOLUTION_CATALOG;
    }
    if (strcmp(line, "evolution bootstrap status") == 0) {
        return DEBUG_CONTROL_COMMAND_EVOLUTION_BOOTSTRAP;
    }
    float evolutionValue = 0.0f;
    char evolutionTrailing = '\0';
    if (strcmp(line, "evolution inspect") == 0) {
        control->evolutionRadius = 24.0f;
        return DEBUG_CONTROL_COMMAND_EVOLUTION_INSPECT;
    }
    if (sscanf(line, "evolution inspect %f %c", &evolutionValue,
               &evolutionTrailing) == 1 && isfinite(evolutionValue) &&
        evolutionValue >= 1.0f && evolutionValue <= 256.0f) {
        control->evolutionRadius = evolutionValue;
        return DEBUG_CONTROL_COMMAND_EVOLUTION_INSPECT;
    }
    if (sscanf(line, "evolution focus %f %c", &evolutionValue,
               &evolutionTrailing) == 1 && isfinite(evolutionValue) &&
        evolutionValue >= 1.0f && evolutionValue <= 256.0f) {
        control->evolutionRadius = evolutionValue;
        return DEBUG_CONTROL_COMMAND_EVOLUTION_FOCUS;
    }
    if (strcmp(line, "evolution focus") == 0) {
        control->evolutionRadius = 24.0f;
        return DEBUG_CONTROL_COMMAND_EVOLUTION_FOCUS;
    }
    if (sscanf(line, "evolution advance %f %c", &evolutionValue,
               &evolutionTrailing) == 1 && isfinite(evolutionValue) &&
        evolutionValue >= 0.25f && evolutionValue <= 4096.0f) {
        control->evolutionAdvanceDays = evolutionValue;
        return DEBUG_CONTROL_COMMAND_EVOLUTION_ADVANCE;
    }
    if (strcmp(line, "quit") == 0) return DEBUG_CONTROL_COMMAND_QUIT;
    DebugControlTeleport teleport = { 0 };
    char trailing = '\0';
    if (sscanf(line, "teleport %f %f %f %f %f %c",
               &teleport.x, &teleport.y, &teleport.z,
               &teleport.yaw, &teleport.pitch, &trailing) == 5 &&
        isfinite(teleport.x) && isfinite(teleport.y) &&
        isfinite(teleport.z) && isfinite(teleport.yaw) &&
        isfinite(teleport.pitch) && fabsf(teleport.x) <= 1000000.0f &&
        fabsf(teleport.y) <= 1000000.0f &&
        fabsf(teleport.z) <= 1000000.0f && fabsf(teleport.yaw) <= 1000.0f &&
        teleport.pitch >= -1.45f && teleport.pitch <= 1.45f) {
        control->teleport = teleport;
        return DEBUG_CONTROL_COMMAND_TELEPORT;
    }
    DebugControlInput input = { 0 };
    int sprint = 0;
    unsigned frames = 0;
    if (sscanf(line, "input %f %f %f %d %u %c",
               &input.forward, &input.strafe, &input.vertical, &sprint,
               &frames, &trailing) == 5 && isfinite(input.forward) &&
        isfinite(input.strafe) && isfinite(input.vertical) &&
        input.forward >= -1.0f && input.forward <= 1.0f &&
        input.strafe >= -1.0f && input.strafe <= 1.0f &&
        input.vertical >= -1.0f && input.vertical <= 1.0f &&
        (sprint == 0 || sprint == 1) && frames >= 1u && frames <= 600u) {
        input.sprint = sprint != 0;
        input.frames = frames;
        control->playerInput = input;
        return DEBUG_CONTROL_COMMAND_INPUT;
    }
    return DEBUG_CONTROL_COMMAND_INVALID;
}

static bool DebugControlTakeLine(DebugControl *control, char *line,
                                 size_t lineSize)
{
    for (size_t index = 0; index < control->inputLength; index++) {
        if (control->input[index] != '\n') continue;
        size_t lineLength = index;
        if (lineLength >= lineSize) lineLength = lineSize - 1;
        memcpy(line, control->input, lineLength);
        line[lineLength] = '\0';

        size_t consumed = index + 1;
        memmove(control->input, control->input + consumed,
                control->inputLength - consumed);
        control->inputLength -= consumed;
        return true;
    }
    return false;
}

DebugControlCommand DebugControlPoll(DebugControl *control)
{
    if (!control || !control->enabled) return DEBUG_CONTROL_COMMAND_NONE;

    char line[DEBUG_CONTROL_BUFFER_SIZE];
    while (DebugControlTakeLine(control, line, sizeof(line))) {
        DebugControlCommand command = DebugControlParseLine(control, line);
        if (command != DEBUG_CONTROL_COMMAND_NONE) return command;
    }
    if (control->inputClosed) return DEBUG_CONTROL_COMMAND_NONE;

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(control->inputFd, &readSet);
    struct timeval timeout = { 0 };
    int selected = select(control->inputFd + 1, &readSet, NULL, NULL, &timeout);
    if (selected <= 0 || !FD_ISSET(control->inputFd, &readSet)) {
        return DEBUG_CONTROL_COMMAND_NONE;
    }

    if (control->inputLength == sizeof(control->input)) {
        control->inputLength = 0;
        return DEBUG_CONTROL_COMMAND_INVALID;
    }
    ssize_t count = read(control->inputFd,
                         control->input + control->inputLength,
                         sizeof(control->input) - control->inputLength);
    if (count < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return DEBUG_CONTROL_COMMAND_NONE;
        }
        control->inputClosed = true;
        return DEBUG_CONTROL_COMMAND_INVALID;
    }
    if (count == 0) {
        control->inputClosed = true;
        if (control->inputLength == 0) return DEBUG_CONTROL_COMMAND_NONE;
        if (control->inputLength == sizeof(control->input)) {
            control->inputLength = 0;
            return DEBUG_CONTROL_COMMAND_INVALID;
        }
        control->input[control->inputLength++] = '\n';
    } else {
        control->inputLength += (size_t)count;
    }

    while (DebugControlTakeLine(control, line, sizeof(line))) {
        DebugControlCommand command = DebugControlParseLine(control, line);
        if (command != DEBUG_CONTROL_COMMAND_NONE) return command;
    }
    if (control->inputLength == sizeof(control->input)) {
        control->inputLength = 0;
        return DEBUG_CONTROL_COMMAND_INVALID;
    }
    return DEBUG_CONTROL_COMMAND_NONE;
}

bool DebugControlReply(DebugControl *control, const char *format, ...)
{
    if (!control || !control->enabled || !format) return false;

    char message[1024];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (length < 0 || (size_t)length >= sizeof(message)) return false;

    size_t written = 0;
    while (written < (size_t)length) {
        ssize_t count = write(control->outputFd, message + written,
                              (size_t)length - written);
        if (count < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (count == 0) return false;
        written += (size_t)count;
    }
    return true;
}
