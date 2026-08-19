#include "core/debug_control.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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

static DebugControlCommand DebugControlParseMarker(
    DebugControl *control, const char *original, const char *normalized)
{
    if (strcmp(normalized, "marker list") == 0) {
        return DEBUG_CONTROL_COMMAND_MARKER_LIST;
    }
    if (strcmp(normalized, "marker target none") == 0) {
        control->marker.id = 0u;
        return DEBUG_CONTROL_COMMAND_MARKER_TARGET;
    }

    unsigned id = 0u;
    char trailing = '\0';
    if (sscanf(normalized, "marker target %u %c", &id, &trailing) == 1 &&
        id != 0u) {
        control->marker.id = id;
        return DEBUG_CONTROL_COMMAND_MARKER_TARGET;
    }
    if (sscanf(normalized, "marker remove %u %c", &id, &trailing) == 1 &&
        id != 0u) {
        control->marker.id = id;
        return DEBUG_CONTROL_COMMAND_MARKER_REMOVE;
    }

    float x = 0.0f;
    float z = 0.0f;
    char color[DEBUG_CONTROL_MARKER_COLOR_SIZE] = { 0 };
    int nameOffset = -1;
    if (sscanf(normalized, "marker add %f %f %15s %n",
               &x, &z, color, &nameOffset) == 3 &&
        nameOffset >= 0 && original[nameOffset] != '\0' &&
        isfinite(x) && isfinite(z) && fabsf(x) <= 1000000.0f &&
        fabsf(z) <= 1000000.0f &&
        strlen(original + nameOffset) < sizeof(control->marker.name)) {
        control->marker.x = x;
        control->marker.z = z;
        snprintf(control->marker.color, sizeof(control->marker.color),
                 "%s", color);
        snprintf(control->marker.name, sizeof(control->marker.name),
                 "%s", original + nameOffset);
        return DEBUG_CONTROL_COMMAND_MARKER_ADD;
    }
    return DEBUG_CONTROL_COMMAND_INVALID;
}

static DebugControlCommand DebugControlParseStreamAudit(
    DebugControl *control, const char *line)
{
    if (strcmp(line, "stream wait") == 0) {
        control->streamWaitFrames = DEBUG_CONTROL_STREAM_WAIT_DEFAULT_FRAMES;
        return DEBUG_CONTROL_COMMAND_STREAM_WAIT;
    }

    unsigned waitFrames = 0u;
    char waitTrailing = '\0';
    if (sscanf(line, "stream wait %u %c", &waitFrames, &waitTrailing) == 1 &&
        waitFrames >= 1u &&
        waitFrames <= DEBUG_CONTROL_STREAM_WAIT_MAX_FRAMES) {
        control->streamWaitFrames = waitFrames;
        return DEBUG_CONTROL_COMMAND_STREAM_WAIT;
    }

    if (strcmp(line, "stream audit") == 0) {
        control->streamAuditRadius = 2;
        control->streamAuditUsePlayerPosition = true;
        return DEBUG_CONTROL_COMMAND_STREAM_AUDIT;
    }

    int x = 0;
    int y = 0;
    int z = 0;
    int radius = 0;
    int offset = -1;
    int fields = sscanf(line, "stream audit at %d %d %d %d %n",
                        &x, &y, &z, &radius, &offset);
    bool atValid = fields == 4 && offset >= 0 && line[offset] == '\0' &&
        radius >= 1 && radius <= 4;
    if (!atValid) {
        offset = -1;
        fields = sscanf(line, "stream audit at %d %d %d %n",
                        &x, &y, &z, &offset);
        atValid = fields == 3 && offset >= 0 && line[offset] == '\0';
        if (atValid) radius = 2;
    }
    if (atValid) {
        control->streamAuditX = x;
        control->streamAuditY = y;
        control->streamAuditZ = z;
        control->streamAuditRadius = radius;
        control->streamAuditUsePlayerPosition = false;
        return DEBUG_CONTROL_COMMAND_STREAM_AUDIT;
    }

    char trailing = '\0';
    if (sscanf(line, "stream audit %d %c", &radius, &trailing) == 1 &&
        radius >= 1 && radius <= 4) {
        control->streamAuditRadius = radius;
        control->streamAuditUsePlayerPosition = true;
        return DEBUG_CONTROL_COMMAND_STREAM_AUDIT;
    }
    return DEBUG_CONTROL_COMMAND_NONE;
}

static DebugControlCommand DebugControlParseShip(DebugControl *control,
                                                 const char *line)
{
    if (strcmp(line, "ship begin") == 0) {
        return DEBUG_CONTROL_COMMAND_SHIP_BEGIN;
    }
    if (strcmp(line, "ship enter") == 0) {
        return DEBUG_CONTROL_COMMAND_SHIP_ENTER;
    }
    if (strcmp(line, "ship dust") == 0) {
        return DEBUG_CONTROL_COMMAND_SHIP_DUST;
    }

    float exhaustDemand = 0.0f;
    char trailing = '\0';
    if (sscanf(line, "ship exhaust %f %c", &exhaustDemand, &trailing) == 1 &&
        isfinite(exhaustDemand) && exhaustDemand >= 0.0f &&
        exhaustDemand <= 1.0f) {
        control->shipExhaustDemand = exhaustDemand;
        return DEBUG_CONTROL_COMMAND_SHIP_EXHAUST;
    }

    DebugControlInput input = { 0 };
    unsigned frames = 0u;
    if (sscanf(line, "ship input %f %f %f %u %c",
               &input.forward, &input.strafe, &input.vertical,
               &frames, &trailing) == 4 && isfinite(input.forward) &&
        isfinite(input.strafe) && isfinite(input.vertical) &&
        input.forward >= -1.0f && input.forward <= 1.0f &&
        input.strafe >= -1.0f && input.strafe <= 1.0f &&
        input.vertical >= -1.0f && input.vertical <= 1.0f &&
        frames >= 1u && frames <= 600u) {
        input.frames = frames;
        control->shipInput = input;
        return DEBUG_CONTROL_COMMAND_SHIP_INPUT;
    }
    return DEBUG_CONTROL_COMMAND_NONE;
}

static DebugControlCommand DebugControlParseFluid(DebugControl *control,
                                                  const char *line)
{
    if (strcmp(line, "fluid inspect") == 0) {
        control->fluidUsePlayerPosition = true;
        return DEBUG_CONTROL_COMMAND_FLUID_INSPECT;
    }

    int x = 0;
    int y = 0;
    int z = 0;
    unsigned value = 0u;
    char trailing = '\0';
    if (sscanf(line, "fluid inspect %d %d %d %c", &x, &y, &z,
               &trailing) == 3) {
        control->fluidX = x;
        control->fluidY = y;
        control->fluidZ = z;
        control->fluidUsePlayerPosition = false;
        return DEBUG_CONTROL_COMMAND_FLUID_INSPECT;
    }
    if (sscanf(line, "fluid set %d %d %d %u %c", &x, &y, &z,
               &value, &trailing) == 4 && value <= 255u) {
        control->fluidX = x;
        control->fluidY = y;
        control->fluidZ = z;
        control->fluidVolume = value;
        return DEBUG_CONTROL_COMMAND_FLUID_SET;
    }
    if (sscanf(line, "fluid step %u %c", &value, &trailing) == 1 &&
        value >= 1u && value <= 1000000u) {
        control->fluidTicks = value;
        return DEBUG_CONTROL_COMMAND_FLUID_STEP;
    }
    return DEBUG_CONTROL_COMMAND_NONE;
}

static DebugControlCommand DebugControlParseBlock(DebugControl *control,
                                                  const char *line)
{
    int x = 0;
    int y = 0;
    int z = 0;
    char trailing = '\0';
    int typeOffset = -1;
    if (sscanf(line, "block set %d %d %d %n",
               &x, &y, &z, &typeOffset) == 3 && typeOffset >= 0 &&
        line[typeOffset] != '\0' &&
        strlen(line + typeOffset) < sizeof(control->blockQuery) &&
        x >= -1000000 && x <= 1000000 &&
        y >= -1000000 && y <= 1000000 &&
        z >= -1000000 && z <= 1000000) {
        control->blockSetX = x;
        control->blockSetY = y;
        control->blockSetZ = z;
        snprintf(control->blockQuery, sizeof(control->blockQuery), "%s",
                 line + typeOffset);
        return DEBUG_CONTROL_COMMAND_BLOCK_SET;
    }
    if (sscanf(line, "block gallery %d %d %d %c", &x, &y, &z,
               &trailing) == 3 && x >= -1000000 && x <= 1000000 &&
        y >= -1000000 && y <= 1000000 &&
        z >= -1000000 && z <= 1000000) {
        control->blockGalleryX = x;
        control->blockGalleryY = y;
        control->blockGalleryZ = z;
        return DEBUG_CONTROL_COMMAND_BLOCK_GALLERY;
    }

    int queryOffset = -1;
    if (sscanf(line, "block inspect %n", &queryOffset) == 0 &&
        queryOffset >= 0 && line[queryOffset] != '\0' &&
        strlen(line + queryOffset) < sizeof(control->blockQuery)) {
        snprintf(control->blockQuery, sizeof(control->blockQuery), "%s",
                 line + queryOffset);
        return DEBUG_CONTROL_COMMAND_BLOCK_INSPECT;
    }
    return DEBUG_CONTROL_COMMAND_NONE;
}

static DebugControlCommand DebugControlParseFlora(DebugControl *control,
                                                  const char *line)
{
    int x = 0;
    int y = 0;
    int z = 0;
    char trailing = '\0';
    if (sscanf(line, "flora sample %d %d %c", &x, &z, &trailing) == 2 &&
        x >= -1000000 && x <= 1000000 && z >= -1000000 && z <= 1000000) {
        control->floraSampleX = x;
        control->floraSampleZ = z;
        return DEBUG_CONTROL_COMMAND_FLORA_SAMPLE;
    }
    if (sscanf(line, "flora gallery %d %d %d %c", &x, &y, &z,
               &trailing) == 3 && x >= -1000000 && x <= 1000000 &&
        y >= -1000000 && y <= 1000000 && z >= -1000000 && z <= 1000000) {
        control->floraGalleryX = x;
        control->floraGalleryY = y;
        control->floraGalleryZ = z;
        return DEBUG_CONTROL_COMMAND_FLORA_GALLERY;
    }
    int queryOffset = -1;
    if (sscanf(line, "flora inspect %n", &queryOffset) == 0 &&
        queryOffset >= 0 && line[queryOffset] != '\0' &&
        strlen(line + queryOffset) < sizeof(control->floraQuery)) {
        snprintf(control->floraQuery, sizeof(control->floraQuery), "%s",
                 line + queryOffset);
        return DEBUG_CONTROL_COMMAND_FLORA_INSPECT;
    }
    return DEBUG_CONTROL_COMMAND_NONE;
}

static DebugControlCommand DebugControlParseWeather(DebugControl *control,
                                                    const char *line)
{
    if (strcmp(line, "weather inspect") == 0) {
        return DEBUG_CONTROL_COMMAND_WEATHER_INSPECT;
    }
    if (strcmp(line, "weather clear") == 0) {
        return DEBUG_CONTROL_COMMAND_WEATHER_CLEAR;
    }
    if (strcmp(line, "weather cloud clear") == 0) {
        return DEBUG_CONTROL_COMMAND_WEATHER_CLOUD_CLEAR;
    }
    if (strcmp(line, "weather tornado clear") == 0) {
        return DEBUG_CONTROL_COMMAND_WEATHER_TORNADO_CLEAR;
    }
    if (strcmp(line, "weather fire clear") == 0) {
        return DEBUG_CONTROL_COMMAND_WEATHER_FIRE_CLEAR;
    }
    if (strcmp(line, "weather damage on") == 0 ||
        strcmp(line, "weather damage off") == 0) {
        control->weatherDamageEnabled = strcmp(line, "weather damage on") == 0;
        return DEBUG_CONTROL_COMMAND_WEATHER_DAMAGE;
    }

    unsigned value = 0u;
    char trailing = '\0';
    if (sscanf(line, "weather step %u %c", &value, &trailing) == 1 &&
        value >= 1u && value <= 100000u) {
        control->weatherTicks = value;
        return DEBUG_CONTROL_COMMAND_WEATHER_STEP;
    }

    int fireX = 0;
    int fireY = 0;
    int fireZ = 0;
    float fireIntensity = 0.0f;
    if (sscanf(line, "weather fire ignite %d %d %d %f %c", &fireX,
               &fireY, &fireZ, &fireIntensity, &trailing) == 4 &&
        fireX >= -1000000 && fireX <= 1000000 &&
        fireY >= -1000000 && fireY <= 1000000 &&
        fireZ >= -1000000 && fireZ <= 1000000 &&
        isfinite(fireIntensity) && fireIntensity > 0.0f &&
        fireIntensity <= 1.0f) {
        control->weatherFireX = fireX;
        control->weatherFireY = fireY;
        control->weatherFireZ = fireZ;
        control->weatherFireIntensity = fireIntensity;
        return DEBUG_CONTROL_COMMAND_WEATHER_FIRE_IGNITE;
    }
    float fireRadius = 0.0f;
    float suppression = 1.0f;
    int fireFields = sscanf(
        line, "weather fire suppress %d %d %d %f %f %c", &fireX,
        &fireY, &fireZ, &fireRadius, &suppression, &trailing);
    if ((fireFields == 4 || fireFields == 5) &&
        fireX >= -1000000 && fireX <= 1000000 &&
        fireY >= -1000000 && fireY <= 1000000 &&
        fireZ >= -1000000 && fireZ <= 1000000 &&
        isfinite(fireRadius) && fireRadius >= 0.0f && fireRadius <= 64.0f &&
        isfinite(suppression) && suppression > 0.0f && suppression <= 1.0f) {
        control->weatherFireX = fireX;
        control->weatherFireY = fireY;
        control->weatherFireZ = fireZ;
        control->weatherFireRadius = fireRadius;
        control->weatherFireSuppression = suppression;
        return DEBUG_CONTROL_COMMAND_WEATHER_FIRE_SUPPRESS;
    }

    char phenomenon[DEBUG_CONTROL_WEATHER_NAME_SIZE] = { 0 };
    float intensity = 0.0f;
    unsigned frames = 0u;
    if (sscanf(line, "weather force %31s %f %u %c", phenomenon,
               &intensity, &frames, &trailing) == 3 &&
        isfinite(intensity) && intensity >= 0.0f && intensity <= 1.0f &&
        frames >= 1u && frames <= 36000u) {
        snprintf(control->weatherPhenomenon,
                 sizeof(control->weatherPhenomenon), "%s", phenomenon);
        control->weatherIntensity = intensity;
        control->weatherFrames = frames;
        return DEBUG_CONTROL_COMMAND_WEATHER_FORCE;
    }
    char genus[DEBUG_CONTROL_WEATHER_NAME_SIZE] = { 0 };
    float coverage = 0.0f;
    if (sscanf(line, "weather cloud %31s %f %u %c", genus,
               &coverage, &frames, &trailing) == 3 &&
        isfinite(coverage) && coverage >= 0.0f && coverage <= 1.0f &&
        frames >= 1u && frames <= 36000u) {
        snprintf(control->weatherCloudGenus,
                 sizeof(control->weatherCloudGenus), "%s", genus);
        control->weatherCloudCoverage = coverage;
        control->weatherCloudFrames = frames;
        return DEBUG_CONTROL_COMMAND_WEATHER_CLOUD_FORCE;
    }
    float tornadoDistance = 48.0f;
    int tornadoFields = sscanf(
        line, "weather tornado force %f %u %f %c", &intensity, &frames,
        &tornadoDistance, &trailing);
    if ((tornadoFields == 2 || tornadoFields == 3) &&
        isfinite(intensity) && intensity >= 0.0f && intensity <= 1.0f &&
        frames >= 1u && frames <= 36000u && isfinite(tornadoDistance) &&
        tornadoDistance >= 8.0f && tornadoDistance <= 160.0f) {
        control->weatherTornadoIntensity = intensity;
        control->weatherTornadoFrames = frames;
        control->weatherTornadoDistance = tornadoDistance;
        return DEBUG_CONTROL_COMMAND_WEATHER_TORNADO_FORCE;
    }
    return DEBUG_CONTROL_COMMAND_NONE;
}

static DebugControlCommand DebugControlParseEvolution(
    DebugControl *control, const char *line)
{
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
    if (strcmp(line, "evolution inspect") == 0) {
        control->evolutionRadius = 24.0f;
        return DEBUG_CONTROL_COMMAND_EVOLUTION_INSPECT;
    }
    if (strcmp(line, "evolution focus") == 0) {
        control->evolutionRadius = 24.0f;
        return DEBUG_CONTROL_COMMAND_EVOLUTION_FOCUS;
    }

    float value = 0.0f;
    char trailing = '\0';
    if (sscanf(line, "evolution inspect %f %c", &value, &trailing) == 1 &&
        isfinite(value) && value >= 1.0f && value <= 256.0f) {
        control->evolutionRadius = value;
        return DEBUG_CONTROL_COMMAND_EVOLUTION_INSPECT;
    }
    if (sscanf(line, "evolution focus %f %c", &value, &trailing) == 1 &&
        isfinite(value) && value >= 1.0f && value <= 256.0f) {
        control->evolutionRadius = value;
        return DEBUG_CONTROL_COMMAND_EVOLUTION_FOCUS;
    }
    if (sscanf(line, "evolution advance %f %c", &value, &trailing) == 1 &&
        isfinite(value) && value >= 0.25f && value <= 4096.0f) {
        control->evolutionAdvanceDays = value;
        return DEBUG_CONTROL_COMMAND_EVOLUTION_ADVANCE;
    }
    return DEBUG_CONTROL_COMMAND_NONE;
}

static DebugControlCommand DebugControlParseLook(DebugControl *control,
                                                 const char *line)
{
    float yaw = 0.0f;
    float pitch = 0.0f;
    char trailing = '\0';
    if (sscanf(line, "look delta %f %f %c", &yaw, &pitch, &trailing) == 2 &&
        isfinite(yaw) && isfinite(pitch) && fabsf(yaw) <= 1000.0f &&
        fabsf(pitch) <= 1000.0f) {
        control->lookYaw = yaw;
        control->lookPitch = pitch;
        control->lookRelative = true;
        return DEBUG_CONTROL_COMMAND_LOOK;
    }
    if (sscanf(line, "look %f %f %c", &yaw, &pitch, &trailing) == 2 &&
        isfinite(yaw) && isfinite(pitch) && fabsf(yaw) <= 1000.0f &&
        pitch >= -1.45f && pitch <= 1.45f) {
        control->lookYaw = yaw;
        control->lookPitch = pitch;
        control->lookRelative = false;
        return DEBUG_CONTROL_COMMAND_LOOK;
    }
    return DEBUG_CONTROL_COMMAND_NONE;
}

static DebugControlCommand DebugControlParseTeleport(DebugControl *control,
                                                     const char *line)
{
    DebugControlTeleport teleport = { 0 };
    char trailing = '\0';
    if (sscanf(line, "teleport %f %f %f %f %f %c",
               &teleport.x, &teleport.y, &teleport.z,
               &teleport.yaw, &teleport.pitch, &trailing) == 5 &&
        isfinite(teleport.x) && isfinite(teleport.y) &&
        isfinite(teleport.z) && isfinite(teleport.yaw) &&
        isfinite(teleport.pitch) && fabsf(teleport.x) <= 1000000.0f &&
        fabsf(teleport.y) <= 1000000.0f &&
        fabsf(teleport.z) <= 1000000.0f &&
        fabsf(teleport.yaw) <= 1000.0f &&
        teleport.pitch >= -1.45f && teleport.pitch <= 1.45f) {
        control->teleport = teleport;
        return DEBUG_CONTROL_COMMAND_TELEPORT;
    }
    return DEBUG_CONTROL_COMMAND_NONE;
}

static DebugControlCommand DebugControlParsePlayerInput(
    DebugControl *control, const char *line)
{
    DebugControlInput input = { 0 };
    int sprint = 0;
    unsigned frames = 0u;
    char trailing = '\0';
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
    return DEBUG_CONTROL_COMMAND_NONE;
}

static DebugControlCommand DebugControlParseMotion(DebugControl *control,
                                                   const char *line)
{
    DebugControlCommand command = DebugControlParseLook(control, line);
    if (command != DEBUG_CONTROL_COMMAND_NONE) return command;
    command = DebugControlParseTeleport(control, line);
    if (command != DEBUG_CONTROL_COMMAND_NONE) return command;
    return DebugControlParsePlayerInput(control, line);
}

static DebugControlCommand DebugControlParseBasic(DebugControl *control,
                                                  const char *line)
{
    if (strcmp(line, "start") == 0) return DEBUG_CONTROL_COMMAND_START;
    if (strcmp(line, "screenshot") == 0) {
        return DEBUG_CONTROL_COMMAND_SCREENSHOT;
    }
    if (strcmp(line, "status") == 0) return DEBUG_CONTROL_COMMAND_STATUS;
    if (strcmp(line, "world topology") == 0) {
        return DEBUG_CONTROL_COMMAND_WORLD_TOPOLOGY;
    }
    if (strcmp(line, "water debug") == 0) {
        control->waterDebugEnabled = !control->waterDebugEnabled;
        return DEBUG_CONTROL_COMMAND_WATER_DEBUG;
    }
    if (strcmp(line, "water debug through") == 0) {
        control->waterDebugThrough = !control->waterDebugThrough;
        return DEBUG_CONTROL_COMMAND_WATER_DEBUG_THROUGH;
    }
    if (strcmp(line, "water debug through on") == 0) {
        control->waterDebugThrough = true;
        return DEBUG_CONTROL_COMMAND_WATER_DEBUG_THROUGH;
    }
    if (strcmp(line, "water debug through off") == 0) {
        control->waterDebugThrough = false;
        return DEBUG_CONTROL_COMMAND_WATER_DEBUG_THROUGH;
    }
    if (strcmp(line, "water debug on") == 0) {
        control->waterDebugEnabled = true;
        return DEBUG_CONTROL_COMMAND_WATER_DEBUG;
    }
    if (strcmp(line, "water debug off") == 0) {
        control->waterDebugEnabled = false;
        return DEBUG_CONTROL_COMMAND_WATER_DEBUG;
    }
    if (strcmp(line, "save") == 0) return DEBUG_CONTROL_COMMAND_SAVE;
    if (strcmp(line, "load") == 0) return DEBUG_CONTROL_COMMAND_LOAD;
    if (strcmp(line, "map") == 0) return DEBUG_CONTROL_COMMAND_MAP;
    if (strcmp(line, "map layer liquids on") == 0) {
        control->mapLiquidsVisible = true;
        return DEBUG_CONTROL_COMMAND_MAP_LAYER_LIQUIDS;
    }
    if (strcmp(line, "map layer liquids off") == 0) {
        control->mapLiquidsVisible = false;
        return DEBUG_CONTROL_COMMAND_MAP_LAYER_LIQUIDS;
    }
    if (strcmp(line, "surface debug home") == 0) {
        return DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_HOME;
    }
    char style[16] = { 0 };
    unsigned seed = 0u;
    char trailing = '\0';
    if (sscanf(line, "surface debug planet %15s %u %c",
               style, &seed, &trailing) == 2) {
        if (strcmp(style, "temperate") == 0) {
            control->surfaceDebugStyle = DEBUG_CONTROL_SURFACE_TEMPERATE;
        } else if (strcmp(style, "desert") == 0) {
            control->surfaceDebugStyle = DEBUG_CONTROL_SURFACE_DESERT;
        } else if (strcmp(style, "ice") == 0) {
            control->surfaceDebugStyle = DEBUG_CONTROL_SURFACE_ICE;
        } else if (strcmp(style, "lava") == 0) {
            control->surfaceDebugStyle = DEBUG_CONTROL_SURFACE_LAVA;
        } else if (strcmp(style, "crater") == 0) {
            control->surfaceDebugStyle = DEBUG_CONTROL_SURFACE_CRATER;
        } else {
            return DEBUG_CONTROL_COMMAND_INVALID;
        }
        control->surfaceDebugSeed = (uint32_t)seed;
        return DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_PLANET;
    }
    if (strcmp(line, "quit") == 0) return DEBUG_CONTROL_COMMAND_QUIT;
    return DEBUG_CONTROL_COMMAND_NONE;
}

static DebugControlCommand DebugControlParseView(DebugControl *control,
                                                 const char *line)
{
    if (strcmp(line, "view first") != 0 &&
        strcmp(line, "view third") != 0) {
        return DEBUG_CONTROL_COMMAND_NONE;
    }
    control->thirdPerson = strcmp(line, "view third") == 0;
    return DEBUG_CONTROL_COMMAND_VIEW;
}

static DebugControlCommand DebugControlParseLine(DebugControl *control,
                                                 char *line)
{
    while (*line != '\0' && isspace((unsigned char)*line)) line++;
    char *end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    char normalized[DEBUG_CONTROL_BUFFER_SIZE];
    size_t length = strlen(line);
    for (size_t index = 0u; index <= length; index++) {
        normalized[index] = (char)tolower((unsigned char)line[index]);
    }

    if (line[0] == '\0') return DEBUG_CONTROL_COMMAND_NONE;
    if (strncmp(normalized, "marker", 6) == 0 &&
        (normalized[6] == '\0' || isspace((unsigned char)normalized[6]))) {
        return DebugControlParseMarker(control, line, normalized);
    }
    line = normalized;
    DebugControlCommand command = DebugControlParseBasic(control, line);
    if (command != DEBUG_CONTROL_COMMAND_NONE) return command;
    command = DebugControlParseStreamAudit(control, line);
    if (command != DEBUG_CONTROL_COMMAND_NONE) return command;
    command = DebugControlParseShip(control, line);
    if (command != DEBUG_CONTROL_COMMAND_NONE) return command;
    command = DebugControlParseView(control, line);
    if (command != DEBUG_CONTROL_COMMAND_NONE) return command;
    command = DebugControlParseFluid(control, line);
    if (command != DEBUG_CONTROL_COMMAND_NONE) return command;
    command = DebugControlParseBlock(control, line);
    if (command != DEBUG_CONTROL_COMMAND_NONE) return command;
    command = DebugControlParseFlora(control, line);
    if (command != DEBUG_CONTROL_COMMAND_NONE) return command;
    command = DebugControlParseWeather(control, line);
    if (command != DEBUG_CONTROL_COMMAND_NONE) return command;
    command = DebugControlParseEvolution(control, line);
    if (command != DEBUG_CONTROL_COMMAND_NONE) return command;
    command = DebugControlParseMotion(control, line);
    if (command != DEBUG_CONTROL_COMMAND_NONE) return command;
    return DEBUG_CONTROL_COMMAND_INVALID;
}

DebugControlCommand DebugControlParseText(DebugControl *control,
                                           const char *text)
{
    if (!control || !text) return DEBUG_CONTROL_COMMAND_INVALID;
    size_t length = strlen(text);
    char line[DEBUG_CONTROL_BUFFER_SIZE];
    if (length >= sizeof(line)) return DEBUG_CONTROL_COMMAND_INVALID;
    memcpy(line, text, length + 1u);
    return DebugControlParseLine(control, line);
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

DebugControlReadResult DebugControlReadLine(DebugControl *control,
                                             char *line, size_t lineSize)
{
    if (!control || !control->enabled || !line || lineSize == 0u) {
        return DEBUG_CONTROL_READ_NONE;
    }

    if (DebugControlTakeLine(control, line, lineSize)) {
        return DEBUG_CONTROL_READ_LINE;
    }
    if (control->inputClosed) return DEBUG_CONTROL_READ_EOF;

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(control->inputFd, &readSet);
    struct timeval timeout = { 0 };
    int selected = select(control->inputFd + 1, &readSet, NULL, NULL, &timeout);
    if (selected <= 0 || !FD_ISSET(control->inputFd, &readSet)) {
        return DEBUG_CONTROL_READ_NONE;
    }

    if (control->inputLength == sizeof(control->input)) {
        control->inputLength = 0;
        return DEBUG_CONTROL_READ_ERROR;
    }
    ssize_t count = read(control->inputFd,
                         control->input + control->inputLength,
                         sizeof(control->input) - control->inputLength);
    if (count < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return DEBUG_CONTROL_READ_NONE;
        }
        control->inputClosed = true;
        return DEBUG_CONTROL_READ_ERROR;
    }
    if (count == 0) {
        control->inputClosed = true;
        if (control->inputLength == 0) return DEBUG_CONTROL_READ_EOF;
        if (control->inputLength == sizeof(control->input)) {
            control->inputLength = 0;
            return DEBUG_CONTROL_READ_ERROR;
        }
        control->input[control->inputLength++] = '\n';
    } else {
        control->inputLength += (size_t)count;
    }

    if (DebugControlTakeLine(control, line, lineSize)) {
        return DEBUG_CONTROL_READ_LINE;
    }
    if (control->inputLength == sizeof(control->input)) {
        control->inputLength = 0;
        return DEBUG_CONTROL_READ_ERROR;
    }
    return control->inputClosed ? DEBUG_CONTROL_READ_EOF
                                : DEBUG_CONTROL_READ_NONE;
}

DebugControlCommand DebugControlPoll(DebugControl *control)
{
    char line[DEBUG_CONTROL_BUFFER_SIZE];
    for (;;) {
        DebugControlReadResult result = DebugControlReadLine(
            control, line, sizeof(line));
        if (result == DEBUG_CONTROL_READ_ERROR) {
            return DEBUG_CONTROL_COMMAND_INVALID;
        }
        if (result != DEBUG_CONTROL_READ_LINE) {
            return DEBUG_CONTROL_COMMAND_NONE;
        }
        DebugControlCommand command = DebugControlParseLine(control, line);
        if (command != DEBUG_CONTROL_COMMAND_NONE) return command;
    }
}

bool DebugControlReply(DebugControl *control, const char *format, ...)
{
    if (!control || !control->enabled || !format) return false;

    va_list arguments;
    va_start(arguments, format);
    va_list measurement;
    va_copy(measurement, arguments);
    int length = vsnprintf(NULL, 0, format, measurement);
    va_end(measurement);
    if (length < 0) {
        va_end(arguments);
        return false;
    }

    char stackMessage[1024];
    size_t capacity = (size_t)length + 1u;
    char *message = capacity <= sizeof(stackMessage)
        ? stackMessage : malloc(capacity);
    if (!message) {
        va_end(arguments);
        return false;
    }
    int formatted = vsnprintf(message, capacity, format, arguments);
    va_end(arguments);
    if (formatted != length) {
        if (message != stackMessage) free(message);
        return false;
    }

    size_t written = 0;
    while (written < (size_t)length) {
        ssize_t count = write(control->outputFd, message + written,
                              (size_t)length - written);
        if (count < 0) {
            if (errno == EINTR) continue;
            if (message != stackMessage) free(message);
            return false;
        }
        if (count == 0) {
            if (message != stackMessage) free(message);
            return false;
        }
        written += (size_t)count;
    }
    if (message != stackMessage) free(message);
    return true;
}
