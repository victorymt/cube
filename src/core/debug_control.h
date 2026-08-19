#ifndef VOXELCRAFT_DEBUG_CONTROL_H
#define VOXELCRAFT_DEBUG_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEBUG_CONTROL_BUFFER_SIZE 2048
#define DEBUG_CONTROL_MARKER_COLOR_SIZE 16
#define DEBUG_CONTROL_MARKER_NAME_SIZE 64
#define DEBUG_CONTROL_WEATHER_NAME_SIZE 32
#define DEBUG_CONTROL_BLOCK_QUERY_SIZE 64
#define DEBUG_CONTROL_FLORA_QUERY_SIZE 64
#define DEBUG_CONTROL_STREAM_WAIT_DEFAULT_FRAMES 300u
#define DEBUG_CONTROL_STREAM_WAIT_MAX_FRAMES 3600u

typedef enum DebugControlCommand {
    DEBUG_CONTROL_COMMAND_NONE = 0,
    DEBUG_CONTROL_COMMAND_START,
    DEBUG_CONTROL_COMMAND_SCREENSHOT,
    DEBUG_CONTROL_COMMAND_STATUS,
    DEBUG_CONTROL_COMMAND_WORLD_TOPOLOGY,
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
    DEBUG_CONTROL_COMMAND_BLOCK_INSPECT,
    DEBUG_CONTROL_COMMAND_BLOCK_SET,
    DEBUG_CONTROL_COMMAND_BLOCK_GALLERY,
    DEBUG_CONTROL_COMMAND_FLORA_INSPECT,
    DEBUG_CONTROL_COMMAND_FLORA_SAMPLE,
    DEBUG_CONTROL_COMMAND_FLORA_GALLERY,
    DEBUG_CONTROL_COMMAND_WEATHER_INSPECT,
    DEBUG_CONTROL_COMMAND_WEATHER_FORCE,
    DEBUG_CONTROL_COMMAND_WEATHER_CLOUD_FORCE,
    DEBUG_CONTROL_COMMAND_WEATHER_CLOUD_CLEAR,
    DEBUG_CONTROL_COMMAND_WEATHER_TORNADO_FORCE,
    DEBUG_CONTROL_COMMAND_WEATHER_TORNADO_CLEAR,
    DEBUG_CONTROL_COMMAND_WEATHER_FIRE_IGNITE,
    DEBUG_CONTROL_COMMAND_WEATHER_FIRE_SUPPRESS,
    DEBUG_CONTROL_COMMAND_WEATHER_FIRE_CLEAR,
    DEBUG_CONTROL_COMMAND_WEATHER_CLEAR,
    DEBUG_CONTROL_COMMAND_WEATHER_DAMAGE,
    DEBUG_CONTROL_COMMAND_WEATHER_STEP,
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
    int weatherFireX;
    int weatherFireY;
    int weatherFireZ;
    int blockGalleryX;
    int blockGalleryY;
    int blockGalleryZ;
    int blockSetX;
    int blockSetY;
    int blockSetZ;
    int floraSampleX;
    int floraSampleZ;
    int floraGalleryX;
    int floraGalleryY;
    int floraGalleryZ;
    int streamAuditX;
    int streamAuditY;
    int streamAuditZ;
    unsigned fluidVolume;
    unsigned fluidTicks;
    unsigned weatherFrames;
    unsigned weatherTicks;
    unsigned weatherCloudFrames;
    unsigned weatherTornadoFrames;
    int streamAuditRadius;
    unsigned streamWaitFrames;
    float lookYaw;
    float lookPitch;
    bool lookRelative;
    bool fluidUsePlayerPosition;
    bool streamAuditUsePlayerPosition;
    bool waterDebugEnabled;
    bool waterDebugThrough;
    bool weatherDamageEnabled;
    bool thirdPerson;
    bool mapLiquidsVisible;
    DebugControlSurfaceStyle surfaceDebugStyle;
    uint32_t surfaceDebugSeed;
    float weatherIntensity;
    float weatherCloudCoverage;
    float weatherTornadoIntensity;
    float weatherTornadoDistance;
    float weatherFireIntensity;
    float weatherFireRadius;
    float weatherFireSuppression;
    char weatherPhenomenon[DEBUG_CONTROL_WEATHER_NAME_SIZE];
    char weatherCloudGenus[DEBUG_CONTROL_WEATHER_NAME_SIZE];
    char blockQuery[DEBUG_CONTROL_BLOCK_QUERY_SIZE];
    char floraQuery[DEBUG_CONTROL_FLORA_QUERY_SIZE];
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
