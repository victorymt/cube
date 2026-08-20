#include "core/debug_control.h"

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

static void TestLongReply(void)
{
    int outputPipe[2];
    assert(pipe(outputPipe) == 0);
    DebugControl control;
    DebugControlInitFds(&control, true, -1, outputPipe[1]);

    char payload[2049];
    memset(payload, 'x', sizeof(payload) - 1u);
    payload[sizeof(payload) - 1u] = '\0';
    assert(DebugControlReply(&control, "%s\n", payload));
    close(outputPipe[1]);

    char response[4096];
    ssize_t responseLength = read(outputPipe[0], response, sizeof(response));
    assert(responseLength == (ssize_t)strlen(payload) + 1);
    assert(memcmp(response, payload, strlen(payload)) == 0);
    assert(response[responseLength - 1] == '\n');
    close(outputPipe[0]);
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
        "\n START \r\nscreenshot\nstatus\npause on\npause off\nworld topology\nwater debug on\nwater debug through on\n"
        "water debug\nwater debug through\nwater debug off\nstream audit 3\n"
        "stream audit at 15 110 -252 4\nstream wait\nstream wait 45\n"
        "save\nload\nmap\n"
        "fluid inspect\nfluid inspect 1 72 -4\n"
        "fluid set 1 72 -4 127\nfluid step 25\n"
        "block inspect Coral Limestone\nblock set 17 81 -9 Glass\n"
        "block gallery -8 81 14\n"
        "flora inspect Silver Birch\nflora sample 12 -9\n"
        "flora gallery -20 80 30\n"
        "teleport 1.5 72.0 -4.25 3.14 -0.4\n"
        "look 1.25 -0.3\nlook delta -0.5 0.1\n"
        "input 1 -0.5 1 1 120\n"
        "mouse left\nmouse right\n"
        "ship begin\nship enter\nship input 0.75 -0.25 1 180\n"
        "ship exhaust 0.65\n"
        "ship dust\n"
        "view third\nview first\nunknown\nquit\n";
    assert(write(inputPipe[1], commands, strlen(commands)) ==
           (ssize_t)strlen(commands));

    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_START);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_SCREENSHOT);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_STATUS);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_PAUSE);
    assert(control.pauseEnabled);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_PAUSE);
    assert(!control.pauseEnabled);
    assert(DebugControlPoll(&control) ==
           DEBUG_CONTROL_COMMAND_WORLD_TOPOLOGY);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_WATER_DEBUG);
    assert(control.waterDebugEnabled);
    assert(DebugControlPoll(&control) ==
           DEBUG_CONTROL_COMMAND_WATER_DEBUG_THROUGH);
    assert(control.waterDebugThrough);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_WATER_DEBUG);
    assert(!control.waterDebugEnabled);
    assert(DebugControlPoll(&control) ==
           DEBUG_CONTROL_COMMAND_WATER_DEBUG_THROUGH);
    assert(!control.waterDebugThrough);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_WATER_DEBUG);
    assert(!control.waterDebugEnabled);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_STREAM_AUDIT);
    assert(control.streamAuditRadius == 3);
    assert(control.streamAuditUsePlayerPosition);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_STREAM_AUDIT);
    assert(!control.streamAuditUsePlayerPosition);
    assert(control.streamAuditX == 15 && control.streamAuditY == 110 &&
           control.streamAuditZ == -252);
    assert(control.streamAuditRadius == 4);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_STREAM_WAIT);
    assert(control.streamWaitFrames ==
           DEBUG_CONTROL_STREAM_WAIT_DEFAULT_FRAMES);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_STREAM_WAIT);
    assert(control.streamWaitFrames == 45u);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_SAVE);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_LOAD);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_MAP);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_FLUID_INSPECT);
    assert(control.fluidUsePlayerPosition);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_FLUID_INSPECT);
    assert(!control.fluidUsePlayerPosition);
    assert(control.fluidX == 1 && control.fluidY == 72 &&
           control.fluidZ == -4);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_FLUID_SET);
    assert(control.fluidVolume == 127u);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_FLUID_STEP);
    assert(control.fluidTicks == 25u);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_BLOCK_INSPECT);
    assert(strcmp(control.blockQuery, "coral limestone") == 0);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_BLOCK_SET);
    assert(control.blockSetX == 17 && control.blockSetY == 81 &&
           control.blockSetZ == -9);
    assert(strcmp(control.blockQuery, "glass") == 0);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_BLOCK_GALLERY);
    assert(control.blockGalleryX == -8 && control.blockGalleryY == 81 &&
           control.blockGalleryZ == 14);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_FLORA_INSPECT);
    assert(strcmp(control.floraQuery, "silver birch") == 0);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_FLORA_SAMPLE);
    assert(control.floraSampleX == 12 && control.floraSampleZ == -9);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_FLORA_GALLERY);
    assert(control.floraGalleryX == -20 && control.floraGalleryY == 80 &&
           control.floraGalleryZ == 30);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_TELEPORT);
    assert(control.teleport.x == 1.5f);
    assert(control.teleport.y == 72.0f);
    assert(control.teleport.z == -4.25f);
    assert(control.teleport.pitch == -0.4f);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_LOOK);
    assert(!control.lookRelative);
    assert(control.lookYaw == 1.25f && control.lookPitch == -0.3f);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_LOOK);
    assert(control.lookRelative);
    assert(control.lookYaw == -0.5f && control.lookPitch == 0.1f);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_INPUT);
    assert(control.playerInput.forward == 1.0f);
    assert(control.playerInput.strafe == -0.5f);
    assert(control.playerInput.vertical == 1.0f);
    assert(control.playerInput.sprint);
    assert(control.playerInput.frames == 120u);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_MOUSE_LEFT);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_MOUSE_RIGHT);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_SHIP_BEGIN);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_SHIP_ENTER);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_SHIP_INPUT);
    assert(control.shipInput.forward == 0.75f);
    assert(control.shipInput.strafe == -0.25f);
    assert(control.shipInput.vertical == 1.0f);
    assert(control.shipInput.frames == 180u);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_SHIP_EXHAUST);
    assert(control.shipExhaustDemand == 0.65f);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_SHIP_DUST);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_VIEW);
    assert(control.thirdPerson);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_VIEW);
    assert(!control.thirdPerson);
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

static void TestBlockCommands(void)
{
    DebugControl control;
    DebugControlInit(&control, true);
    assert(DebugControlParseText(&control, "block inspect 136") ==
           DEBUG_CONTROL_COMMAND_BLOCK_INSPECT);
    assert(strcmp(control.blockQuery, "136") == 0);
    assert(DebugControlParseText(&control, "block inspect fire_ash") ==
           DEBUG_CONTROL_COMMAND_BLOCK_INSPECT);
    assert(strcmp(control.blockQuery, "fire_ash") == 0);
    assert(DebugControlParseText(&control, "block set -17 81 9 Coral Limestone") ==
           DEBUG_CONTROL_COMMAND_BLOCK_SET);
    assert(control.blockSetX == -17 && control.blockSetY == 81 &&
           control.blockSetZ == 9);
    assert(strcmp(control.blockQuery, "coral limestone") == 0);
    assert(DebugControlParseText(&control, "block gallery 1 -32 3") ==
           DEBUG_CONTROL_COMMAND_BLOCK_GALLERY);
    assert(control.blockGalleryX == 1 && control.blockGalleryY == -32 &&
           control.blockGalleryZ == 3);
    assert(DebugControlParseText(&control, "block inspect") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(&control, "block set 1 2 3") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(
               &control, "block set 1000001 2 3 glass") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(&control, "block gallery 1 2") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(
               &control, "block gallery 1000001 2 3") ==
           DEBUG_CONTROL_COMMAND_INVALID);
}

static void TestFloraCommands(void)
{
    DebugControl control;
    DebugControlInit(&control, true);
    assert(DebugControlParseText(&control, "flora inspect 12") ==
           DEBUG_CONTROL_COMMAND_FLORA_INSPECT);
    assert(strcmp(control.floraQuery, "12") == 0);
    assert(DebugControlParseText(&control, "flora inspect Quercus_robur") ==
           DEBUG_CONTROL_COMMAND_FLORA_INSPECT);
    assert(strcmp(control.floraQuery, "quercus_robur") == 0);
    assert(DebugControlParseText(&control, "flora sample -44 91") ==
           DEBUG_CONTROL_COMMAND_FLORA_SAMPLE);
    assert(control.floraSampleX == -44 && control.floraSampleZ == 91);
    assert(DebugControlParseText(&control, "flora gallery 1 -32 3") ==
           DEBUG_CONTROL_COMMAND_FLORA_GALLERY);
    assert(control.floraGalleryX == 1 && control.floraGalleryY == -32 &&
           control.floraGalleryZ == 3);
    assert(DebugControlParseText(&control, "flora inspect") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(&control, "flora sample 1") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(&control, "flora sample 1000001 0") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(&control, "flora gallery 1 2") ==
           DEBUG_CONTROL_COMMAND_INVALID);
}

static void TestTerrainMapCommands(void)
{
    DebugControl control;
    DebugControlInit(&control, true);
    assert(DebugControlParseText(&control, "map layer liquids on") ==
           DEBUG_CONTROL_COMMAND_MAP_LAYER_LIQUIDS);
    assert(control.mapLiquidsVisible);
    assert(DebugControlParseText(&control, "map layer liquids off") ==
           DEBUG_CONTROL_COMMAND_MAP_LAYER_LIQUIDS);
    assert(!control.mapLiquidsVisible);
    assert(DebugControlParseText(&control, "surface debug home") ==
           DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_HOME);

    const char *styles[] = { "temperate", "desert", "ice", "lava", "crater" };
    for (int style = 0; style < 5; style++) {
        char command[80];
        snprintf(command, sizeof(command), "surface debug planet %s %u",
                 styles[style], 100u + (unsigned)style);
        assert(DebugControlParseText(&control, command) ==
               DEBUG_CONTROL_COMMAND_SURFACE_DEBUG_PLANET);
        assert(control.surfaceDebugStyle == (DebugControlSurfaceStyle)style);
        assert(control.surfaceDebugSeed == 100u + (unsigned)style);
    }
    assert(DebugControlParseText(
               &control, "surface debug planet gas 1") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(
               &control, "surface debug planet lava") ==
           DEBUG_CONTROL_COMMAND_INVALID);
}

static void TestEvolutionCommands(void)
{
    int inputPipe[2];
    assert(pipe(inputPipe) == 0);
    DebugControl control;
    DebugControlInitFds(&control, true, inputPipe[0], STDOUT_FILENO);
    const char *commands =
        "evolution inspect\n"
        "evolution inspect 64\n"
        "evolution focus\n"
        "evolution focus 48\n"
        "evolution region\n"
        "evolution advance 96\n"
        "evolution bootstrap status\n"
        "evolution atlas\n"
        "evolution catalog\n";
    assert(write(inputPipe[1], commands, strlen(commands)) ==
           (ssize_t)strlen(commands));
    assert(DebugControlPoll(&control) ==
           DEBUG_CONTROL_COMMAND_EVOLUTION_INSPECT);
    assert(control.evolutionRadius == 24.0f);
    assert(DebugControlPoll(&control) ==
           DEBUG_CONTROL_COMMAND_EVOLUTION_INSPECT);
    assert(control.evolutionRadius == 64.0f);
    assert(DebugControlPoll(&control) ==
           DEBUG_CONTROL_COMMAND_EVOLUTION_FOCUS);
    assert(control.evolutionRadius == 24.0f);
    assert(DebugControlPoll(&control) ==
           DEBUG_CONTROL_COMMAND_EVOLUTION_FOCUS);
    assert(control.evolutionRadius == 48.0f);
    assert(DebugControlPoll(&control) ==
           DEBUG_CONTROL_COMMAND_EVOLUTION_REGION);
    assert(DebugControlPoll(&control) ==
           DEBUG_CONTROL_COMMAND_EVOLUTION_ADVANCE);
    assert(control.evolutionAdvanceDays == 96.0f);
    assert(DebugControlPoll(&control) ==
           DEBUG_CONTROL_COMMAND_EVOLUTION_BOOTSTRAP);
    assert(DebugControlPoll(&control) ==
           DEBUG_CONTROL_COMMAND_EVOLUTION_ATLAS);
    assert(DebugControlPoll(&control) ==
           DEBUG_CONTROL_COMMAND_EVOLUTION_CATALOG);
    close(inputPipe[0]);
    close(inputPipe[1]);
}

static void TestWeatherCommands(void)
{
    DebugControl control;
    DebugControlInit(&control, true);
    assert(DebugControlParseText(&control, "weather inspect") ==
           DEBUG_CONTROL_COMMAND_WEATHER_INSPECT);
    assert(DebugControlParseText(
               &control, "weather force freezing-rain 0.75 240") ==
           DEBUG_CONTROL_COMMAND_WEATHER_FORCE);
    assert(strcmp(control.weatherPhenomenon, "freezing-rain") == 0);
    assert(control.weatherIntensity == 0.75f);
    assert(control.weatherFrames == 240u);
    assert(DebugControlParseText(
               &control, "weather cloud cumulonimbus 0.85 360") ==
           DEBUG_CONTROL_COMMAND_WEATHER_CLOUD_FORCE);
    assert(strcmp(control.weatherCloudGenus, "cumulonimbus") == 0);
    assert(control.weatherCloudCoverage == 0.85f);
    assert(control.weatherCloudFrames == 360u);
    assert(DebugControlParseText(
               &control, "weather tornado force 0.9 720 64") ==
           DEBUG_CONTROL_COMMAND_WEATHER_TORNADO_FORCE);
    assert(control.weatherTornadoIntensity == 0.9f);
    assert(control.weatherTornadoFrames == 720u);
    assert(control.weatherTornadoDistance == 64.0f);
    assert(DebugControlParseText(
               &control, "weather tornado force 0.7 360") ==
           DEBUG_CONTROL_COMMAND_WEATHER_TORNADO_FORCE);
    assert(control.weatherTornadoDistance == 48.0f);
    assert(DebugControlParseText(&control, "weather tornado clear") ==
           DEBUG_CONTROL_COMMAND_WEATHER_TORNADO_CLEAR);
    assert(DebugControlParseText(
               &control, "weather fire ignite -12 73 44 0.85") ==
           DEBUG_CONTROL_COMMAND_WEATHER_FIRE_IGNITE);
    assert(control.weatherFireX == -12 && control.weatherFireY == 73 &&
           control.weatherFireZ == 44);
    assert(control.weatherFireIntensity == 0.85f);
    assert(DebugControlParseText(
               &control, "weather fire suppress -12 73 44 9.5 0.6") ==
           DEBUG_CONTROL_COMMAND_WEATHER_FIRE_SUPPRESS);
    assert(control.weatherFireRadius == 9.5f);
    assert(control.weatherFireSuppression == 0.6f);
    assert(DebugControlParseText(
               &control, "weather fire suppress -12 73 44 0") ==
           DEBUG_CONTROL_COMMAND_WEATHER_FIRE_SUPPRESS);
    assert(control.weatherFireRadius == 0.0f);
    assert(control.weatherFireSuppression == 1.0f);
    assert(DebugControlParseText(&control, "weather fire clear") ==
           DEBUG_CONTROL_COMMAND_WEATHER_FIRE_CLEAR);
    assert(DebugControlParseText(&control, "weather cloud clear") ==
           DEBUG_CONTROL_COMMAND_WEATHER_CLOUD_CLEAR);
    assert(DebugControlParseText(&control, "weather clear") ==
           DEBUG_CONTROL_COMMAND_WEATHER_CLEAR);
    assert(DebugControlParseText(&control, "weather damage off") ==
           DEBUG_CONTROL_COMMAND_WEATHER_DAMAGE);
    assert(!control.weatherDamageEnabled);
    assert(DebugControlParseText(&control, "weather damage on") ==
           DEBUG_CONTROL_COMMAND_WEATHER_DAMAGE);
    assert(control.weatherDamageEnabled);
    assert(DebugControlParseText(&control, "weather step 25") ==
           DEBUG_CONTROL_COMMAND_WEATHER_STEP);
    assert(control.weatherTicks == 25u);

    assert(DebugControlParseText(
               &control, "weather force hail -0.1 10") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(
               &control, "weather force hail 1.1 10") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(
               &control, "weather force hail 0.5 0") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(
               &control, "weather cloud cirrus -0.1 10") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(
               &control, "weather cloud cirrus 1.1 10") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(
               &control, "weather cloud cirrus 0.5 0") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(
               &control, "weather tornado force 1.1 10") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(
               &control, "weather tornado force 0.5 0") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(
               &control, "weather tornado force 0.5 10 7") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(
               &control, "weather fire ignite 0 70 0 0") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(
               &control, "weather fire ignite 1000001 70 0 0.5") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(
               &control, "weather fire suppress 0 70 0 65") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(
               &control, "weather fire suppress 0 70 0 4 1.1") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(&control, "weather step 0") ==
           DEBUG_CONTROL_COMMAND_INVALID);
    assert(DebugControlParseText(&control, "weather damage maybe") ==
           DEBUG_CONTROL_COMMAND_INVALID);
}

static void TestMarkerCommandsPreserveUtf8Names(void)
{
    int inputPipe[2];
    assert(pipe(inputPipe) == 0);
    DebugControl control;
    DebugControlInitFds(&control, true, inputPipe[0], STDOUT_FILENO);
    const char *commands =
        "MARKER ADD 12.5 -8 Cyan Base \xe8\x83\xa1\xe9\x9b\xaa\xe5\xb2\xa9\n"
        "marker list\n"
        "marker target 42\n"
        "marker target none\n"
        "marker remove 42\n";
    assert(write(inputPipe[1], commands, strlen(commands)) ==
           (ssize_t)strlen(commands));
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_MARKER_ADD);
    assert(control.marker.x == 12.5f && control.marker.z == -8.0f);
    assert(strcmp(control.marker.color, "cyan") == 0);
    assert(strcmp(control.marker.name,
                  "Base \xe8\x83\xa1\xe9\x9b\xaa\xe5\xb2\xa9") == 0);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_MARKER_LIST);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_MARKER_TARGET);
    assert(control.marker.id == 42u);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_MARKER_TARGET);
    assert(control.marker.id == 0u);
    assert(DebugControlPoll(&control) == DEBUG_CONTROL_COMMAND_MARKER_REMOVE);
    assert(control.marker.id == 42u);
    close(inputPipe[0]);
    close(inputPipe[1]);
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
        "input 0 0 0 0 601\n"
        "ship input 0 0 2 1\n"
        "ship input 0 0 0 0\n"
        "ship exhaust 1.1\n"
        "fluid set 0 1 0 256\n"
        "fluid step 0\n"
        "stream wait 0\n"
        "stream wait 3601\n"
        "stream wait 12 trailing\n"
        "marker add 1 2 red\n"
        "marker target 0\n";
    assert(write(inputPipe[1], commands, strlen(commands)) ==
           (ssize_t)strlen(commands));
    for (int index = 0; index < 14; index++) {
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
    TestLongReply();
    TestCommandStream();
    TestBlockCommands();
    TestFloraCommands();
    TestTerrainMapCommands();
    TestFinalCommandWithoutNewline();
    TestInvalidParameterizedCommands();
    TestEvolutionCommands();
    TestWeatherCommands();
    TestMarkerCommandsPreserveUtf8Names();
    puts("debug control tests passed");
    return 0;
}
