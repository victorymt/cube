#ifndef VOXELCRAFT_DEBUG_CONTROL_H
#define VOXELCRAFT_DEBUG_CONTROL_H

#include <stdbool.h>
#include <stddef.h>

#define DEBUG_CONTROL_BUFFER_SIZE 512

typedef enum DebugControlCommand {
    DEBUG_CONTROL_COMMAND_NONE = 0,
    DEBUG_CONTROL_COMMAND_START,
    DEBUG_CONTROL_COMMAND_SCREENSHOT,
    DEBUG_CONTROL_COMMAND_STATUS,
    DEBUG_CONTROL_COMMAND_TELEPORT,
    DEBUG_CONTROL_COMMAND_INPUT,
    DEBUG_CONTROL_COMMAND_EVOLUTION_INSPECT,
    DEBUG_CONTROL_COMMAND_EVOLUTION_REGION,
    DEBUG_CONTROL_COMMAND_EVOLUTION_ADVANCE,
    DEBUG_CONTROL_COMMAND_EVOLUTION_BOOTSTRAP,
    DEBUG_CONTROL_COMMAND_QUIT,
    DEBUG_CONTROL_COMMAND_INVALID
} DebugControlCommand;

typedef struct DebugControlTeleport {
    float x;
    float y;
    float z;
    float yaw;
    float pitch;
} DebugControlTeleport;

typedef struct DebugControlInput {
    float forward;
    float strafe;
    float vertical;
    bool sprint;
    unsigned frames;
} DebugControlInput;

typedef struct DebugControl {
    bool enabled;
    bool inputClosed;
    int inputFd;
    int outputFd;
    char input[DEBUG_CONTROL_BUFFER_SIZE];
    size_t inputLength;
    DebugControlTeleport teleport;
    DebugControlInput playerInput;
    float evolutionRadius;
    float evolutionAdvanceDays;
} DebugControl;

void DebugControlInit(DebugControl *control, bool enabled);
void DebugControlInitFds(DebugControl *control, bool enabled,
                         int inputFd, int outputFd);
DebugControlCommand DebugControlPoll(DebugControl *control);
bool DebugControlReply(DebugControl *control, const char *format, ...);

#endif
