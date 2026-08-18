#ifndef VOXELCRAFT_DEBUG_CONTROL_H
#define VOXELCRAFT_DEBUG_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEBUG_CONTROL_BUFFER_SIZE 2048
#define DEBUG_CONTROL_MARKER_COLOR_SIZE 16
#define DEBUG_CONTROL_MARKER_NAME_SIZE 64
#define DEBUG_CONTROL_STREAM_WAIT_DEFAULT_FRAMES 300u
#define DEBUG_CONTROL_STREAM_WAIT_MAX_FRAMES 3600u

typedef enum DebugControlCommand {
    DEBUG_CONTROL_COMMAND_NONE = 0,
    DEBUG_CONTROL_COMMAND_START,
    DEBUG_CONTROL_COMMAND_SCREENSHOT,
    DEBUG_CONTROL_COMMAND_STATUS,
    DEBUG_CONTROL_COMMAND_WATER_DEBUG,
    DEBUG_CONTROL_COMMAND_WATER_DEBUG_THROUGH,
    DEBUG_CONTROL_COMMAND_STREAM_AUDIT,
    DEBUG_CONTROL_COMMAND_STREAM_WAIT,
    DEBUG_CONTROL_COMMAND_SAVE,
    DEBUG_CONTROL_COMMAND_LOAD,
    DEBUG_CONTROL_COMMAND_MAP,
    DEBUG_CONTROL_COMMAND_MAP_LAYER_LIQUIDS,
    DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_HOME,
    DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_PLANET,
    DEBUG_CONTROL_COMMAND_MARKER_ADD,
    DEBUG_CONTROL_COMMAND_MARKER_LIST,
    DEBUG_CONTROL_COMMAND_MARKER_TARGET,
    DEBUG_CONTROL_COMMAND_MARKER_REMOVE,
    DEBUG_CONTROL_COMMAND_FLUID_INSPECT,
    DEBUG_CONTROL_COMMAND_FLUID_SET,
    DEBUG_CONTROL_COMMAND_FLUID_STEP,
    DEBUG_CONTROL_COMMAND_TELEPORT,
    DEBUG_CONTROL_COMMAND_LOOK,
    DEBUG_CONTROL_COMMAND_INPUT,
    DEBUG_CONTROL_COMMAND_SHIP_BEGIN,
    DEBUG_CONTROL_COMMAND_SHIP_ENTER,
    DEBUG_CONTROL_COMMAND_SHIP_INPUT,
    DEBUG_CONTROL_COMMAND_SHIP_EXHAUST,
    DEBUG_CONTROL_COMMAND_SHIP_DUST,
    DEBUG_CONTROL_COMMAND_VIEW,
    DEBUG_CONTROL_COMMAND_EVOLUTION_INSPECT,
    DEBUG_CONTROL_COMMAND_EVOLUTION_FOCUS,
    DEBUG_CONTROL_COMMAND_EVOLUTION_REGION,
    DEBUG_CONTROL_COMMAND_EVOLUTION_ADVANCE,
    DEBUG_CONTROL_COMMAND_EVOLUTION_BOOTSTRAP,
    DEBUG_CONTROL_COMMAND_EVOLUTION_ATLAS,
    DEBUG_CONTROL_COMMAND_EVOLUTION_CATALOG,
    DEBUG_CONTROL_COMMAND_QUIT,
    DEBUG_CONTROL_COMMAND_INVALID
} DebugControlCommand;

typedef enum DebugControlReadResult {
    DEBUG_CONTROL_READ_NONE = 0,
    DEBUG_CONTROL_READ_LINE,
    DEBUG_CONTROL_READ_EOF,
    DEBUG_CONTROL_READ_ERROR
} DebugControlReadResult;

typedef enum DebugControlSurfaceStyle {
    DEBUG_CONTROL_SURFACE_TEMPERATE = 0,
    DEBUG_CONTROL_SURFACE_DESERT,
    DEBUG_CONTROL_SURFACE_ICE,
    DEBUG_CONTROL_SURFACE_LAVA,
    DEBUG_CONTROL_SURFACE_CRATER
} DebugControlSurfaceStyle;

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

typedef struct DebugControlMarker {
    float x;
    float z;
    unsigned id;
    char color[DEBUG_CONTROL_MARKER_COLOR_SIZE];
    char name[DEBUG_CONTROL_MARKER_NAME_SIZE];
} DebugControlMarker;

typedef struct DebugControl {
    bool enabled;
    bool inputClosed;
    int inputFd;
    int outputFd;
    char input[DEBUG_CONTROL_BUFFER_SIZE];
    size_t inputLength;
    DebugControlTeleport teleport;
    DebugControlInput playerInput;
    DebugControlInput shipInput;
    DebugControlMarker marker;
    float evolutionRadius;
    float evolutionAdvanceDays;
    float shipExhaustDemand;
    int fluidX;
    int fluidY;
    int fluidZ;
    int streamAuditX;
    int streamAuditY;
    int streamAuditZ;
    unsigned fluidVolume;
    unsigned fluidTicks;
    int streamAuditRadius;
    unsigned streamWaitFrames;
    float lookYaw;
    float lookPitch;
    bool lookRelative;
    bool fluidUsePlayerPosition;
    bool streamAuditUsePlayerPosition;
    bool waterDebugEnabled;
    bool waterDebugThrough;
    bool thirdPerson;
    bool mapLiquidsVisible;
    DebugControlSurfaceStyle surfaceDebugStyle;
    uint32_t surfaceDebugSeed;
} DebugControl;

void DebugControlInit(DebugControl *control, bool enabled);
void DebugControlInitFds(DebugControl *control, bool enabled,
                         int inputFd, int outputFd);
DebugControlCommand DebugControlParseText(DebugControl *control,
                                           const char *text);
DebugControlReadResult DebugControlReadLine(DebugControl *control,
                                             char *line, size_t lineSize);
DebugControlCommand DebugControlPoll(DebugControl *control);
bool DebugControlReply(DebugControl *control, const char *format, ...);

#endif
