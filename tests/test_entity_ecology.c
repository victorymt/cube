#include "chunks.h"
#include "ecology.h"
#include "ecology_test_fixture.h"
#include "entity.h"
#include "particles.h"
#include "space.h"
#include "terrain.h"
#include "world_environment.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t WorldCurrentSurfaceId(void)
{
    return PlanetWorldIsActive() ? PlanetWorldSeed() : 0u;
}

WorldBlockRegion WorldBlockRegionAt(int y)
{
    return y >= 0 && y < WORLD_HEIGHT
        ? WORLD_BLOCK_REGION_SURFACE : WORLD_BLOCK_REGION_NONE;
}

float WorldGravityScale(void)
{
    return PlanetWorldGravityScale();
}

bool IsLiquidBlock(BlockType type)
{
    return type == BLOCK_WATER || type == BLOCK_LAVA;
}

void ParticlesEmitBurst(Vector3 position, Color color, int count,
                        float speed, float life)
{
    (void)position;
    (void)color;
    (void)count;
    (void)speed;
    (void)life;
}

static unsigned char *CaptureEntityState(size_t *outSize)
{
    assert(outSize);
    FILE *file = tmpfile();
    assert(file);
    assert(EntitiesSaveState(file));
    long end = ftell(file);
    assert(end > 0);
    *outSize = (size_t)end;
    unsigned char *bytes = malloc(*outSize);
    assert(bytes);
    rewind(file);
    assert(fread(bytes, 1, *outSize, file) == *outSize);
    fclose(file);
    return bytes;
}

static void SaveSimulation(FILE *file)
{
    assert(file);
    assert(SpaceSaveState(file));
    assert(PlanetWorldSaveState(file));
    assert(PlanetEcologySaveState(file));
    assert(EntitiesSaveState(file));
}

static void LoadSimulation(FILE *file)
{
    assert(file);
    rewind(file);
    assert(SpaceLoadState(file));
    assert(PlanetWorldLoadState(file));
    assert(PlanetEcologyLoadState(file));
    assert(EntitiesLoadState(file));
}

static void RunFrames(Player *player, int frameCount, float daylight)
{
    const float dt = 0.1f;
    for (int frame = 0; frame < frameCount; frame++) {
        SpaceAdvanceTime(dt);
        EntitiesUpdate(dt, player, daylight);
    }
}

static uint32_t ActivateFertilePlanet(Player *player, float daylight,
                                     float *outFaunaActivity)
{
    assert(player && outFaunaActivity);
    for (uint32_t index = 0; index < 4096u; index++) {
        uint32_t seed = 0x51a7e5edu + index * 0x9e3779b9u;
        EcologyTestSetSeed(seed);
        EcologyTestActivatePlanet(seed, 0, 0);
        PlanetEcologyResetState();
        if (PlanetEcologyCurrent().faunaDensity <= 0.02f) continue;

        for (int sample = 0; sample < 256; sample++) {
            int x = ((sample * 83) % 1024) - 512;
            int z = ((sample * sample * 47) % 1024) - 512;
            float activity = PlanetEcologyFaunaDensityAt(x, z, daylight);
            if (PlanetFaunaPopulationCap(activity, MAX_ENTITIES - 4) <= 0) {
                continue;
            }
            int groundY = PlanetTerrainHeight(x, z);
            player->position = (Vector3){
                (float)x + 0.5f, (float)groundY + 2.0f, (float)z + 0.5f
            };
            *outFaunaActivity = activity;
            return seed;
        }
    }
    assert(false);
    return 0u;
}

static void TestEntityEcologySystemReplay(void)
{
    const float daylight = 0.72f;
    Player player = { 0 };
    float faunaActivity = 0.0f;
    uint32_t seed = ActivateFertilePlanet(
        &player, daylight, &faunaActivity);
    assert(seed != 0u);
    int expectedCap = PlanetFaunaPopulationCap(
        faunaActivity, MAX_ENTITIES - 4);
    assert(expectedCap > 0);

    assert(ChunksStartGenThread());
    UpdateChunks(player.position, MIN_RENDER_DISTANCE_CHUNKS);
    DrainChunkGen();
    EntitiesInit();

    int frames = 0;
    while (GetActiveEntityCount() == 0 && frames < 10000) {
        RunFrames(&player, 1, daylight);
        frames++;
    }
    assert(GetActiveEntityCount() > 0);
    assert(GetActiveEntityCount() <= expectedCap);

    FILE *checkpoint = tmpfile();
    assert(checkpoint);
    SaveSimulation(checkpoint);
    RunFrames(&player, 480, daylight);
    size_t expectedSize = 0;
    unsigned char *expected = CaptureEntityState(&expectedSize);

    EcologyTestSetSeed(seed);
    LoadSimulation(checkpoint);
    RunFrames(&player, 480, daylight);
    size_t replaySize = 0;
    unsigned char *replay = CaptureEntityState(&replaySize);
    assert(replaySize == expectedSize);
    assert(memcmp(replay, expected, expectedSize) == 0);

    EcologyTestActivatePlanetStyle(
        seed, 0, 0, SOLAR_STYLE_GAS);
    PlanetEcologyResetState();
    assert(PlanetEcologyFaunaDensityAt(
               (int)player.position.x, (int)player.position.z,
               daylight) == 0.0f);
    RunFrames(&player, MAX_ENTITIES * 20, daylight);
    assert(GetActiveEntityCount() == 0);

    free(replay);
    free(expected);
    fclose(checkpoint);
    UnloadAllChunks();
    ChunksShutdownGenThread();
}

int main(void)
{
    TestEntityEcologySystemReplay();
    puts("entity ecology tests passed");
    return 0;
}
