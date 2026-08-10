#include "entity.h"
#include "ecology_model.h"
#include "terrain.h"
#include "weather_model.h"
#include "world.h"
#include "world_environment.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t testSeed = 0x51f15eadu;

uint32_t WorldGetSeed(void)
{
    return testSeed;
}

uint32_t WorldCurrentSurfaceId(void)
{
    return 0u;
}

bool PlanetWorldIsActive(void)
{
    return false;
}

uint32_t PlanetWorldSeed(void)
{
    return testSeed;
}

const PlanetProfile *PlanetWorldProfile(void)
{
    static PlanetProfile profile;
    return &profile;
}

PlanetBiome PlanetBiomeAt(int x, int z)
{
    (void)x;
    (void)z;
    return PLANET_BIOME_PLAINS;
}

PlanetLocalEcology PlanetEcologyLocalAt(int x, int z, float daylight)
{
    (void)x;
    (void)z;
    (void)daylight;
    PlanetLocalEcology local = { 0 };
    local.suitability.faunaActivity = 1.0f;
    local.suitability.faunaCapacity = 1.0f;
    return local;
}

PlanetEcologyProfile PlanetEcologyCurrent(void)
{
    PlanetEcologyProfile profile = { 0 };
    profile.faunaDensity = 1.0f;
    profile.bodyPlan = PLANET_BODY_QUADRUPED;
    profile.chemistry = PLANET_CHEMISTRY_CARBON;
    profile.niche = PLANET_NICHE_GRAZER;
    profile.organismScale = 1.0f;
    profile.movementSpeed = 0.85f;
    profile.limbCount = 4;
    profile.primaryBlock = BLOCK_GRASS;
    profile.accentBlock = BLOCK_DIRT;
    return profile;
}

float PlanetEcologyFaunaDensityAt(int x, int z, float daylight)
{
    (void)x;
    (void)z;
    (void)daylight;
    return 1.0f;
}

WeatherFieldSample WeatherFieldSampleAtWorld(int x, int z)
{
    (void)x;
    (void)z;
    WeatherFieldSample sample = { 0 };
    return sample;
}

float WeatherWindAngleAtWorld(int x, int z)
{
    (void)x;
    (void)z;
    return 0.0f;
}

WorldBlockRegion WorldBlockRegionAt(int y)
{
    return y >= 0 && y < WORLD_HEIGHT
        ? WORLD_BLOCK_REGION_SURFACE : WORLD_BLOCK_REGION_NONE;
}

float WorldGravityScale(void)
{
    return 1.0f;
}

int WorldSurfaceHeightAt(int x, int z)
{
    (void)x;
    (void)z;
    return 10;
}

BlockType GetBlockAt(int x, int y, int z)
{
    (void)x;
    (void)z;
    return y <= 10 ? BLOCK_DIRT : BLOCK_AIR;
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

void AudioPlayBreak(void)
{
}

static unsigned char *CaptureState(size_t *size)
{
    FILE *file = tmpfile();
    assert(file);
    assert(EntitiesSaveState(file));
    long end = ftell(file);
    assert(end > 0);
    *size = (size_t)end;
    unsigned char *data = malloc(*size);
    assert(data);
    rewind(file);
    assert(fread(data, 1, *size, file) == *size);
    fclose(file);
    return data;
}

static void RunFrames(Player *player, int count)
{
    for (int frame = 0; frame < count; frame++) {
        EntitiesUpdate(0.1f, player, 1.0f);
    }
}

static void LoadBytes(const unsigned char *data, size_t size)
{
    FILE *file = tmpfile();
    assert(file);
    assert(fwrite(data, 1, size, file) == size);
    rewind(file);
    assert(EntitiesLoadState(file));
    fclose(file);
}

static void TestEntityReplay(void)
{
    Player player = { 0 };
    player.position = (Vector3){ 0.5f, 12.0f, 0.5f };

    EntitiesInit();
    RunFrames(&player, 3);
    size_t checkpointSize = 0;
    unsigned char *checkpoint = CaptureState(&checkpointSize);
    RunFrames(&player, 80);
    size_t expectedSize = 0;
    unsigned char *expected = CaptureState(&expectedSize);

    LoadBytes(checkpoint, checkpointSize);
    RunFrames(&player, 80);
    size_t replaySize = 0;
    unsigned char *replay = CaptureState(&replaySize);
    assert(replaySize == expectedSize);
    assert(memcmp(replay, expected, expectedSize) == 0);

    srand(1);
    EntitiesInit();
    RunFrames(&player, 80);
    size_t firstSize = 0;
    unsigned char *first = CaptureState(&firstSize);
    srand(9999);
    EntitiesInit();
    RunFrames(&player, 80);
    size_t secondSize = 0;
    unsigned char *second = CaptureState(&secondSize);
    assert(firstSize == secondSize);
    assert(memcmp(first, second, firstSize) == 0);

    free(checkpoint);
    free(expected);
    free(replay);
    free(first);
    free(second);
}

static void TestEntityLoadIsAtomic(void)
{
    EntitiesInit();
    Player player = { 0 };
    player.position = (Vector3){ 0.5f, 12.0f, 0.5f };
    RunFrames(&player, 5);
    size_t beforeSize = 0;
    unsigned char *before = CaptureState(&beforeSize);

    FILE *file = tmpfile();
    assert(file);
    assert(fwrite(before, 1, beforeSize - 1, file) == beforeSize - 1);
    rewind(file);
    assert(!EntitiesLoadState(file));
    fclose(file);

    size_t afterSize = 0;
    unsigned char *after = CaptureState(&afterSize);
    assert(afterSize == beforeSize);
    assert(memcmp(after, before, beforeSize) == 0);
    free(before);
    free(after);
}

int main(void)
{
    TestEntityReplay();
    TestEntityLoadIsAtomic();
    puts("entity replay tests passed");
    return 0;
}
