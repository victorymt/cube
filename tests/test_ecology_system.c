#include "world/chunks.h"

#define chunks (ChunksMutableForTesting())
#include "ecology_test_fixture.h"
#include "ecology/ecology.h"
#include "space/space.h"
#include "space/space_system_physics.h"
#include "world/terrain.h"
#include "world/weather.h"
#include "world/weather_model.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void AddSurfaceDisturbanceEdits(void)
{
    static const BlockType editTypes[] = {
        BLOCK_AIR, BLOCK_LAVA, BLOCK_GLASS
    };
    EcologyTestClearBlockEdits();
    for (int index = 0; index < 36; index++) {
        int x = 16 + (index % 6) * 2;
        int z = 16 + (index / 6) * 2;
        EcologyTestAddBlockEdit(
            x, PlanetTerrainHeight(x, z), z,
            editTypes[index % (int)(sizeof(editTypes) /
                                    sizeof(editTypes[0]))]);
    }
}

static void AssertLocalEcologyEqual(PlanetLocalEcology actual,
                                    PlanetLocalEcology expected)
{
    assert(memcmp(&actual.environment, &expected.environment,
                  sizeof(actual.environment)) == 0);
    assert(memcmp(&actual.suitability, &expected.suitability,
                  sizeof(actual.suitability)) == 0);
    assert(memcmp(&actual.population, &expected.population,
                  sizeof(actual.population)) == 0);
    assert(memcmp(&actual.migration, &expected.migration,
                  sizeof(actual.migration)) == 0);
    assert(memcmp(&actual.diagnostics, &expected.diagnostics,
                  sizeof(actual.diagnostics)) == 0);

#define ASSERT_POPULATION_UNIT(field)                                      \
    assert(actual.population.field >= 0.0f &&                              \
           actual.population.field <= 1.0f)
    ASSERT_POPULATION_UNIT(floraDensity);
    ASSERT_POPULATION_UNIT(faunaDensity);
    ASSERT_POPULATION_UNIT(floraCarryingCapacity);
    ASSERT_POPULATION_UNIT(faunaCarryingCapacity);
    ASSERT_POPULATION_UNIT(seasonalMemory);
    ASSERT_POPULATION_UNIT(radiationMemory);
#undef ASSERT_POPULATION_UNIT

    assert(isfinite(actual.diagnostics.radiationMemory));
    assert(actual.diagnostics.radiationMemory >= 0.0f &&
           actual.diagnostics.radiationMemory <= 1.0f);

#define ASSERT_MIGRATION_SIGNED_UNIT(field)                                \
    assert(isfinite(actual.migration.field) &&                             \
           actual.migration.field >= -1.0f &&                             \
           actual.migration.field <= 1.0f)
    ASSERT_MIGRATION_SIGNED_UNIT(floraNet);
    ASSERT_MIGRATION_SIGNED_UNIT(faunaNet);
    ASSERT_MIGRATION_SIGNED_UNIT(floraFlowX);
    ASSERT_MIGRATION_SIGNED_UNIT(floraFlowZ);
    ASSERT_MIGRATION_SIGNED_UNIT(faunaFlowX);
    ASSERT_MIGRATION_SIGNED_UNIT(faunaFlowZ);
#undef ASSERT_MIGRATION_SIGNED_UNIT
}

static void AssertPlanetProfileEqual(const PlanetProfile *actual,
                                     const PlanetProfile *expected)
{
    assert(actual && expected);
#define ASSERT_PLANET_FIELD(field) \
    assert(actual->field == expected->field)
    ASSERT_PLANET_FIELD(seed);
    ASSERT_PLANET_FIELD(canonicalBodyId);
    ASSERT_PLANET_FIELD(style);
    ASSERT_PLANET_FIELD(atmosphereType);
    ASSERT_PLANET_FIELD(physicalRadiusKm);
    ASSERT_PLANET_FIELD(massKg);
    ASSERT_PLANET_FIELD(spaceProxyRadius);
    ASSERT_PLANET_FIELD(surfaceGravity);
    ASSERT_PLANET_FIELD(receivedIrradiance);
    ASSERT_PLANET_FIELD(radiativeTempK);
    ASSERT_PLANET_FIELD(equilibriumTempK);
    ASSERT_PLANET_FIELD(surfacePressureAtm);
    ASSERT_PLANET_FIELD(atmosphereDensity);
    ASSERT_PLANET_FIELD(oceanCoverage);
    ASSERT_PLANET_FIELD(iceCoverage);
    ASSERT_PLANET_FIELD(cloudCoverage);
    ASSERT_PLANET_FIELD(terrainRoughness);
    ASSERT_PLANET_FIELD(ageGyr);
    ASSERT_PLANET_FIELD(rotationRate);
    ASSERT_PLANET_FIELD(tidalLockFactor);
    ASSERT_PLANET_FIELD(ringTilt);
    ASSERT_PLANET_FIELD(albedo);
    ASSERT_PLANET_FIELD(greenhouseEffect);
    ASSERT_PLANET_FIELD(axialTilt);
    ASSERT_PLANET_FIELD(seasonPhase);
    ASSERT_PLANET_FIELD(yearLength);
    ASSERT_PLANET_FIELD(prevailingWindAngle);
    ASSERT_PLANET_FIELD(windStrength);
    ASSERT_PLANET_FIELD(volcanicActivity);
    ASSERT_PLANET_FIELD(impactRate);
    ASSERT_PLANET_FIELD(hasSolidSurface);
    ASSERT_PLANET_FIELD(hasRings);
    ASSERT_PLANET_FIELD(tidallyLocked);
#undef ASSERT_PLANET_FIELD
}

static unsigned char *CapturePlanetWorldState(size_t *outSize)
{
    assert(outSize);
    FILE *file = tmpfile();
    assert(file);
    assert(PlanetWorldSaveState(file));
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

static bool LoadPlanetWorldBytes(const unsigned char *bytes, size_t size)
{
    FILE *file = tmpfile();
    assert(file);
    if (size > 0) {
        assert(bytes);
        assert(fwrite(bytes, 1, size, file) == size);
    }
    rewind(file);
    bool loaded = PlanetWorldLoadState(file);
    fclose(file);
    return loaded;
}

static void AssertPlanetWorldStateEqual(const unsigned char *expected,
                                        size_t expectedSize)
{
    size_t actualSize = 0;
    unsigned char *actual = CapturePlanetWorldState(&actualSize);
    assert(actualSize == expectedSize);
    assert(memcmp(actual, expected, expectedSize) == 0);
    free(actual);
}

static bool LocalEcologyDiffers(PlanetLocalEcology left,
                                PlanetLocalEcology right)
{
    return memcmp(&left.environment, &right.environment,
                  sizeof(left.environment)) != 0 ||
           memcmp(&left.suitability, &right.suitability,
                  sizeof(left.suitability)) != 0 ||
           memcmp(&left.population, &right.population,
                  sizeof(left.population)) != 0 ||
           memcmp(&left.migration, &right.migration,
                  sizeof(left.migration)) != 0 ||
           memcmp(&left.diagnostics, &right.diagnostics,
                  sizeof(left.diagnostics)) != 0;
}

typedef struct EcologyConcurrentQuery {
    int x;
    int z;
    float daylight;
    PlanetLocalEcology result;
} EcologyConcurrentQuery;

static void *RunEcologyConcurrentQuery(void *opaque)
{
    EcologyConcurrentQuery *query = opaque;
    query->result = PlanetEcologyLocalAt(
        query->x, query->z, query->daylight);
    return NULL;
}

static void TestEcologyPopulationConcurrentQueries(void)
{
    const uint32_t seed = 0x6a09e667u;
    EcologyTestSetSeed(seed);
    EcologyTestActivatePlanet(seed, 120, -340);
    PlanetEcologyResetState();
    PlanetLocalEcology expected = PlanetEcologyLocalAt(37, -91, 0.73f);
    PlanetLocalEcology dark = PlanetEcologyLocalAt(37, -91, 0.0f);
    AssertLocalEcologyEqual(dark,
                            PlanetEcologyLocalAt(37, -91, NAN));

    enum { THREAD_COUNT = 8 };
    for (int round = 0; round < 12; round++) {
        pthread_t threads[THREAD_COUNT];
        EcologyConcurrentQuery queries[THREAD_COUNT] = { 0 };
        for (int index = 0; index < THREAD_COUNT; index++) {
            queries[index].x = 37;
            queries[index].z = -91;
            queries[index].daylight = 0.73f;
            assert(pthread_create(&threads[index], NULL,
                                  RunEcologyConcurrentQuery,
                                  &queries[index]) == 0);
        }
        for (int index = 0; index < THREAD_COUNT; index++) {
            assert(pthread_join(threads[index], NULL) == 0);
            AssertLocalEcologyEqual(queries[index].result,
                                    expected);
        }
    }
}

static float WeatherSampleDistance(WeatherFieldSample left,
                                   WeatherFieldSample right)
{
    return fabsf(left.cloudCover - right.cloudCover) +
           fabsf(left.precipitation - right.precipitation) +
           fabsf(left.rain - right.rain) +
           fabsf(left.snow - right.snow) +
           fabsf(left.storm - right.storm) +
           fabsf(left.wind - right.wind);
}

static void TestEcologyUsesPositionLocalWeather(void)
{
    const uint32_t seed = 0x6c8e9cf5u;
    EcologyTestSetSeed(seed);
    EcologyTestActivatePlanet(seed, 317, -911);
    SpaceAdvanceTime(87.25f);

    int wetX = 0;
    int wetZ = 0;
    WeatherFieldSample wetWeather = { 0 };
    bool foundWetCell = false;
    for (int index = 0; index < 512; index++) {
        int x = index * 37 - 4096;
        int z = ((index * index * 53) % 8192) - 4096;
        WeatherFieldSample sample = WeatherFieldSampleAtWorld(x, z);
        if (sample.precipitation > 0.12f) {
            wetX = x;
            wetZ = z;
            wetWeather = sample;
            foundWetCell = true;
            break;
        }
    }
    assert(foundWetCell);
    assert(WeatherPrecipitationRate() == 0.0f);

    PlanetLocalEcology local = PlanetEcologyLocalAt(wetX, wetZ, 0.84f);
    assert(local.environment.precipitationRate == wetWeather.precipitation);
    assert(local.environment.currentStorm == wetWeather.storm);

    float sky = WeatherFieldSkyFactor(wetWeather);
    float usableDaylight = fmaxf(0.0f, fminf(1.0f,
        0.84f * (1.0f - sky * 0.68f)));
    float expectedLight = fmaxf(0.0f, fminf(1.0f,
        usableDaylight * (float)PlanetWorldProfile()->receivedIrradiance));
    assert(fabsf(local.environment.currentUsableLight - expectedLight) < 0.00001f);

    WeatherFieldSample replayWeather = WeatherFieldSampleAtWorld(wetX, wetZ);
    PlanetLocalEcology replay = PlanetEcologyLocalAt(wetX, wetZ, 0.84f);
    assert(memcmp(&wetWeather, &replayWeather, sizeof(wetWeather)) == 0);
    AssertLocalEcologyEqual(replay, local);
}

static void TestEcologyCacheInvalidation(void)
{
    const uint32_t seed = 0x13579bdfu;
    const int sampleCount = 24;
    PlanetLocalEcology firstOrigin[sampleCount];
    PlanetLocalEcology movedOrigin[sampleCount];
    WeatherFieldSample firstWeather[sampleCount];
    EcologyTestSetSeed(seed);
    EcologyTestActivatePlanet(seed, 120, -340);

    PlanetEcologyProfile temperate = PlanetEcologyCurrent();
    PlanetEcologyProfile repeated = PlanetEcologyCurrent();
    assert(memcmp(&temperate, &repeated, sizeof(temperate)) == 0);
    for (int index = 0; index < sampleCount; index++) {
        int x = index * 83 - 900;
        int z = index * index * 19 - 700;
        firstWeather[index] = WeatherFieldSampleAtWorld(x, z);
        firstOrigin[index] = PlanetEcologyLocalAt(x, z, 0.74f);
        AssertLocalEcologyEqual(
            PlanetEcologyLocalAt(x, z, 0.74f), firstOrigin[index]);
    }

    EcologyTestActivatePlanetStyle(seed, 120, -340, SOLAR_STYLE_ICE);
    PlanetEcologyProfile ice = PlanetEcologyCurrent();
    assert(PlanetWorldProfile()->style == SOLAR_STYLE_ICE);
    assert(memcmp(&temperate, &ice, sizeof(temperate)) != 0);

    EcologyTestActivatePlanet(seed, 4100, -3700);
    int originChanges = 0;
    int weatherChanges = 0;
    for (int index = 0; index < sampleCount; index++) {
        int x = index * 83 - 900;
        int z = index * index * 19 - 700;
        WeatherFieldSample movedWeather = WeatherFieldSampleAtWorld(x, z);
        PlanetLocalEcology moved = PlanetEcologyLocalAt(x, z, 0.74f);
        movedOrigin[index] = moved;
        if (LocalEcologyDiffers(moved, firstOrigin[index])) {
            originChanges++;
        }
        if (WeatherSampleDistance(movedWeather, firstWeather[index]) > 0.0001f) {
            weatherChanges++;
        }
    }
    assert(originChanges > sampleCount / 2);
    assert(weatherChanges > sampleCount / 2);

    SpaceAdvanceTime(97.0f);
    int timeChanges = 0;
    for (int index = 0; index < sampleCount; index++) {
        int x = index * 83 - 900;
        int z = index * index * 19 - 700;
        PlanetLocalEcology advanced = PlanetEcologyLocalAt(x, z, 0.74f);
        PlanetLocalEcology replay = PlanetEcologyLocalAt(x, z, 0.74f);
        AssertLocalEcologyEqual(replay, advanced);
        if (LocalEcologyDiffers(advanced, movedOrigin[index])) {
            timeChanges++;
        }
    }
    assert(timeChanges > sampleCount / 2);
}

static void TestEcologyCrossSeedReplay(void)
{
    WeatherFieldSample previousWeather = { 0 };
    int distinctWeatherCount = 0;
    for (int index = 0; index < 64; index++) {
        uint32_t seed = 0x9e3779b9u * (uint32_t)(index + 1) ^ 0x61c88647u;
        int originX = index * 113 - 3500;
        int originZ = 2800 - index * 89;
        int sampleX = ((index * 997) % 7000) - 3500;
        int sampleZ = ((index * index * 131) % 7000) - 3500;
        EcologyTestSetSeed(seed);
        EcologyTestActivatePlanet(seed, originX, originZ);

        WeatherFieldSample firstWeather = WeatherFieldSampleAtWorld(
            sampleX, sampleZ);
        PlanetLocalEcology firstEcology = PlanetEcologyLocalAt(
            sampleX, sampleZ, 0.66f);
        EcologyTestActivatePlanet(seed, originX, originZ);
        WeatherFieldSample replayWeather = WeatherFieldSampleAtWorld(
            sampleX, sampleZ);
        PlanetLocalEcology replayEcology = PlanetEcologyLocalAt(
            sampleX, sampleZ, 0.66f);

        assert(memcmp(&firstWeather, &replayWeather,
                      sizeof(firstWeather)) == 0);
        AssertLocalEcologyEqual(replayEcology, firstEcology);
        assert(firstEcology.environment.precipitationRate ==
               firstWeather.precipitation);
        assert(firstEcology.environment.currentStorm == firstWeather.storm);
        if (index > 0 &&
            WeatherSampleDistance(previousWeather, firstWeather) > 0.001f) {
            distinctWeatherCount++;
        }
        previousWeather = firstWeather;
    }
    assert(distinctWeatherCount > 48);
}

static void TestEcologySaveLoadReplay(void)
{
    const uint32_t seed = 0x2468ace0u;
    const int sampleX = 725;
    const int sampleZ = -1384;
    EcologyTestSetSeed(seed);
    PlanetEcologyResetState();
    EcologyTestActivatePlanet(seed, -2048, 1024);
    SpaceAdvanceTime(163.5f);

    WeatherFieldSample beforeWeather = WeatherFieldSampleAtWorld(sampleX, sampleZ);
    float beforeWindAngle = WeatherWindAngleAtWorld(sampleX, sampleZ);
    PlanetLocalEcology beforeEcology = PlanetEcologyLocalAt(sampleX, sampleZ, 0.72f);

    FILE *file = tmpfile();
    assert(file);
    assert(fwrite(&seed, sizeof(seed), 1, file) == 1);
    assert(SpaceSaveState(file));
    assert(PlanetWorldSaveState(file));
    assert(PlanetEcologySaveState(file));

    EcologyTestSetSeed(0xdeadbeefu);
    EcologyTestActivatePlanet(0xdeadbeefu, 99, -77);
    SpaceAdvanceTime(41.0f);
    rewind(file);
    uint32_t loadedSeed = 0;
    assert(fread(&loadedSeed, sizeof(loadedSeed), 1, file) == 1);
    EcologyTestSetSeed(loadedSeed);
    assert(SpaceLoadState(file));
    assert(PlanetWorldLoadState(file));
    assert(PlanetEcologyLoadState(file));

    WeatherFieldSample afterWeather = WeatherFieldSampleAtWorld(sampleX, sampleZ);
    float afterWindAngle = WeatherWindAngleAtWorld(sampleX, sampleZ);
    PlanetLocalEcology afterEcology = PlanetEcologyLocalAt(sampleX, sampleZ, 0.72f);
    assert(memcmp(&beforeWeather, &afterWeather, sizeof(beforeWeather)) == 0);
    assert(beforeWindAngle == afterWindAngle);
    AssertLocalEcologyEqual(afterEcology, beforeEcology);

    SpaceAdvanceTime(19.75f);
    WeatherFieldSample continuedWeather = WeatherFieldSampleAtWorld(sampleX, sampleZ);
    PlanetLocalEcology continuedEcology = PlanetEcologyLocalAt(sampleX, sampleZ, 0.72f);

    rewind(file);
    assert(fread(&loadedSeed, sizeof(loadedSeed), 1, file) == 1);
    EcologyTestSetSeed(loadedSeed);
    assert(SpaceLoadState(file));
    assert(PlanetWorldLoadState(file));
    assert(PlanetEcologyLoadState(file));
    SpaceAdvanceTime(19.75f);
    WeatherFieldSample replayWeather = WeatherFieldSampleAtWorld(sampleX, sampleZ);
    float replayWindAngle = WeatherWindAngleAtWorld(sampleX, sampleZ);
    PlanetLocalEcology replayEcology = PlanetEcologyLocalAt(sampleX, sampleZ, 0.72f);
    assert(memcmp(&continuedWeather, &replayWeather,
                  sizeof(continuedWeather)) == 0);
    assert(beforeWindAngle == replayWindAngle);
    AssertLocalEcologyEqual(replayEcology, continuedEcology);
    fclose(file);
}

static void TestGeneratedPlanetProfileSaveLoadReplay(void)
{
    const uint32_t galaxySeed = 0x2468ace0u;
    const int sampleX = -815;
    const int sampleZ = 1327;
    EcologyTestSetSeed(galaxySeed);

    SolarSystemDef system;
    assert(StarSystemAt(3, -4, &system));
    assert(system.planetCount > 0);
    PlanetProfile generated = SolarPlanetProfile(&system, 0);
    assert(generated.seed != 0u);

    FILE *profileFile = tmpfile();
    assert(profileFile);
    assert(PlanetProfileSaveState(profileFile, &generated));
    rewind(profileFile);
    PlanetProfile profileRoundTrip = { 0 };
    assert(PlanetProfileLoadState(profileFile, &profileRoundTrip));
    AssertPlanetProfileEqual(&profileRoundTrip, &generated);

    uint32_t invalidStyle = (uint32_t)SOLAR_STYLE_TEMPERATE + 1u;
    assert(fseek(profileFile, (long)sizeof(generated.seed), SEEK_SET) == 0);
    assert(fwrite(&invalidStyle, sizeof(invalidStyle), 1, profileFile) == 1);
    rewind(profileFile);
    memset(&profileRoundTrip, 0xa5, sizeof(profileRoundTrip));
    assert(!PlanetProfileLoadState(profileFile, &profileRoundTrip));
    const PlanetProfile clearedProfile = { 0 };
    assert(memcmp(&profileRoundTrip, &clearedProfile,
                  sizeof(profileRoundTrip)) == 0);
    fclose(profileFile);

    PlanetProfile invalidProfile = generated;
    invalidProfile.equilibriumTempK = NAN;
    FILE *invalidFile = tmpfile();
    assert(invalidFile);
    assert(!PlanetProfileSaveState(invalidFile, &invalidProfile));
    fclose(invalidFile);

    FILE *activationFile = tmpfile();
    assert(activationFile);
    EcologyTestActivateGeneratedPlanetWithFile(
        activationFile, &system, 0, 913, -527);
    AssertPlanetProfileEqual(PlanetWorldProfile(), &generated);
    PlanetEcologyResetState();
    SpaceAdvanceTime(163.5f);

    bool darkSide = PlanetWorldIsDarkSide();
    PlanetEcologyProfile beforeProfile = PlanetEcologyCurrent();
    PlanetEcologyProfile directlyDerived = PlanetEcologyProfileForPlanet(
        &generated, generated.seed, darkSide);
    assert(memcmp(&beforeProfile, &directlyDerived,
                  sizeof(beforeProfile)) == 0);
    WeatherFieldSample beforeWeather = WeatherFieldSampleAtWorld(
        sampleX, sampleZ);
    PlanetLocalEcology beforeLocal = PlanetEcologyLocalAt(
        sampleX, sampleZ, 0.68f);

    FILE *stateFile = tmpfile();
    assert(stateFile);
    EcologyTestSaveSimulationState(stateFile);

    EcologyTestSetSeed(0xdeadbeefu);
    EcologyTestActivatePlanet(0xdeadbeefu, 17, -29);
    SpaceAdvanceTime(41.0f);
    EcologyTestSetSeed(galaxySeed);
    EcologyTestLoadSimulationState(stateFile);

    AssertPlanetProfileEqual(PlanetWorldProfile(), &generated);
    assert(PlanetWorldIsDarkSide() == darkSide);
    PlanetEcologyProfile afterProfile = PlanetEcologyCurrent();
    assert(memcmp(&afterProfile, &beforeProfile,
                  sizeof(beforeProfile)) == 0);
    WeatherFieldSample afterWeather = WeatherFieldSampleAtWorld(
        sampleX, sampleZ);
    PlanetLocalEcology afterLocal = PlanetEcologyLocalAt(
        sampleX, sampleZ, 0.68f);
    assert(memcmp(&afterWeather, &beforeWeather,
                  sizeof(beforeWeather)) == 0);
    AssertLocalEcologyEqual(afterLocal, beforeLocal);

    SpaceAdvanceTime(23.75f);
    WeatherFieldSample continuedWeather = WeatherFieldSampleAtWorld(
        sampleX, sampleZ);
    PlanetLocalEcology continuedLocal = PlanetEcologyLocalAt(
        sampleX, sampleZ, 0.68f);

    EcologyTestSetSeed(galaxySeed);
    EcologyTestLoadSimulationState(stateFile);
    SpaceAdvanceTime(23.75f);
    WeatherFieldSample replayWeather = WeatherFieldSampleAtWorld(
        sampleX, sampleZ);
    PlanetLocalEcology replayLocal = PlanetEcologyLocalAt(
        sampleX, sampleZ, 0.68f);
    assert(memcmp(&replayWeather, &continuedWeather,
                  sizeof(continuedWeather)) == 0);
    AssertLocalEcologyEqual(replayLocal, continuedLocal);

    fclose(stateFile);
    fclose(activationFile);
}

static void TestPlanetWorldStateCompatibilityAndAtomicity(void)
{
    const uint32_t legacySeed = 0x7a31c95du;
    EcologyTestSetSeed(legacySeed);
    EcologyTestActivatePlanetStyle(
        legacySeed, -341, 829, SOLAR_STYLE_DESERT);
    PlanetProfile legacyProfile = *PlanetWorldProfile();
    size_t upgradedSize = 0;
    unsigned char *upgraded = CapturePlanetWorldState(&upgradedSize);
    assert(upgradedSize > 2u);
    assert(upgraded[0] == 3u);

    const size_t remnantStateSize =
        sizeof(uint8_t) + sizeof(int32_t) + sizeof(float) * 3u;
    const size_t profileOffset =
        sizeof(uint8_t) * 2u + sizeof(uint32_t) * 2u +
        sizeof(int32_t) * 3u + sizeof(float) * 7u + 32u +
        remnantStateSize;
    const size_t legacyProfileOffset = profileOffset - remnantStateSize;
    assert(upgradedSize > remnantStateSize &&
           profileOffset <= upgradedSize);
    size_t version2Size = upgradedSize - remnantStateSize;
    unsigned char *version2 = malloc(version2Size);
    assert(version2);
    memcpy(version2, upgraded, legacyProfileOffset);
    memcpy(version2 + legacyProfileOffset, upgraded + profileOffset,
           upgradedSize - profileOffset);
    version2[0] = 2u;
    assert(LoadPlanetWorldBytes(version2, version2Size));
    assert(!PlanetWorldRemnantEnvironment().active);
    assert(LoadPlanetWorldBytes(upgraded, upgradedSize));
    free(version2);

    EcologyTestActivatePlanet(0xdeadbeefu, 11, -17);
    assert(LoadPlanetWorldBytes(upgraded, upgradedSize));
    AssertPlanetProfileEqual(PlanetWorldProfile(), &legacyProfile);
    AssertPlanetWorldStateEqual(upgraded, upgradedSize);

    const uint32_t galaxySeed = 0x2468ace0u;
    EcologyTestSetSeed(galaxySeed);
    SolarSystemDef system;
    assert(StarSystemAt(3, -4, &system));
    FILE *fixture = tmpfile();
    assert(fixture);
    EcologyTestActivateGeneratedPlanetWithFile(
        fixture, &system, 0, 137, -619);
    fclose(fixture);

    size_t baselineSize = 0;
    unsigned char *baseline = CapturePlanetWorldState(&baselineSize);
    for (size_t truncatedSize = 0; truncatedSize < baselineSize;
         truncatedSize++) {
        assert(!LoadPlanetWorldBytes(baseline, truncatedSize));
        AssertPlanetWorldStateEqual(baseline, baselineSize);
    }

    unsigned char *corrupt = malloc(baselineSize);
    assert(corrupt);
    memcpy(corrupt, baseline, baselineSize);
    corrupt[0] = 4u;
    assert(!LoadPlanetWorldBytes(corrupt, baselineSize));
    AssertPlanetWorldStateEqual(baseline, baselineSize);

    assert(profileOffset + sizeof(uint32_t) <= baselineSize);
    memcpy(corrupt, baseline, baselineSize);
    uint32_t mismatchedSeed = PlanetWorldSeed() ^ 0x9e3779b9u;
    memcpy(corrupt + profileOffset, &mismatchedSeed,
           sizeof(mismatchedSeed));
    assert(!LoadPlanetWorldBytes(corrupt, baselineSize));
    AssertPlanetWorldStateEqual(baseline, baselineSize);

    memcpy(corrupt, baseline, baselineSize);
    corrupt[baselineSize - 1u] = 2u;
    assert(!LoadPlanetWorldBytes(corrupt, baselineSize));
    AssertPlanetWorldStateEqual(baseline, baselineSize);

    free(corrupt);
    free(baseline);
    free(upgraded);
}

static void TestEcologyRespondsToRemnantExposure(void)
{
    SolarSystemDef system;
    assert(StarSystemAt(0, 0, &system));
    StellarProfile remnant;
    assert(StellarProfileAtAge(12.0f, 0.0, 0x5a17u, &remnant));
    double remnantAge = (double)remnant.luminousLifetimeGyr + 0.000001;
    assert(StellarProfileAtAge(12.0f, remnantAge, 0x5a17u, &remnant));
    assert(remnant.stage == STELLAR_STAGE_NEUTRON_STAR);
    system.star = remnant;
    system.spectrum = remnant.spectrum;
    system.starProxyRadius = SolarSystemStellarVisualRadius(&remnant);
    assert(SolarSystemPhysicalSnapshotBuild(
        &system, &system.physicalSnapshot));

    FILE *fixture = tmpfile();
    assert(fixture);
    EcologyTestActivateGeneratedPlanetWithFile(fixture, &system, 0, 0, 0);
    fclose(fixture);
    PlanetEcologyResetState();
    PlanetLocalEcology local = PlanetEcologyLocalAt(420, 75, 0.72f);
    SpaceRemnantEnvironment environment = PlanetWorldRemnantEnvironment();
    assert(environment.active && environment.remnantCount == 1);
    assert(local.environment.radiationExposure > 0.0f);
    assert(local.environment.ejectaExposure >= 0.0f);
    assert(local.population.radiationMemory > 0.0f);
    assert(local.suitability.radiationScore < 1.0f);
    PlanetLocalEcology repeated = PlanetEcologyLocalAt(420, 75, 0.72f);
    assert(memcmp(&local, &repeated, sizeof(local)) == 0);

    FILE *ecologySaved = tmpfile();
    assert(ecologySaved);
    assert(PlanetEcologySaveState(ecologySaved));
    PlanetEcologyResetState();
    rewind(ecologySaved);
    assert(PlanetEcologyLoadState(ecologySaved));
    PlanetLocalEcology restored = PlanetEcologyLocalAt(420, 75, 0.72f);
    assert(memcmp(&local, &restored, sizeof(local)) == 0);
    fclose(ecologySaved);

    FILE *saved = tmpfile();
    assert(saved);
    assert(PlanetWorldSaveState(saved));
    EcologyTestActivatePlanet(0xdeadbeefu, 17, -29);
    rewind(saved);
    assert(PlanetWorldLoadState(saved));
    SpaceRemnantEnvironment loaded = PlanetWorldRemnantEnvironment();
    assert(memcmp(&loaded, &environment, sizeof(loaded)) == 0);
    fclose(saved);
}

static void TestEcologyMigrationOrderAndTimePartition(void)
{
    uint32_t seed = 0u;
    static const int cells[4][2] = {
        { 32, 32 }, { 96, 32 }, { 32, 96 }, { 96, 96 }
    };
    static const int forwardOrder[4] = { 0, 1, 2, 3 };
    static const int reverseOrder[4] = { 3, 2, 1, 0 };
    PlanetLocalEcology singleAdvance[4];
    PlanetLocalEcology partitionedAdvance[4];

    for (uint32_t index = 0; index < 512u; index++) {
        uint32_t candidate = 0x8f3a21d7u + index * 0x9e3779b9u;
        EcologyTestSetSeed(candidate);
        PlanetEcologyResetState();
        EcologyTestActivatePlanet(candidate, 0, 0);
        if (PlanetEcologyCurrent().floraDensity > 0.08f) {
            seed = candidate;
            break;
        }
    }
    assert(seed != 0u);

    EcologyTestSetSeed(seed);
    EcologyTestActivatePlanet(seed, 0, 0);
    PlanetEcologyResetState();
    FILE *baseline = tmpfile();
    assert(baseline);
    assert(SpaceSaveState(baseline));
    for (int index = 0; index < 4; index++) {
        int cell = forwardOrder[index];
        PlanetEcologyLocalAt(cells[cell][0], cells[cell][1], 0.72f);
    }
    SpaceAdvanceTime(96.0f);
    float migrationSignal = 0.0f;
    for (int cell = 0; cell < 4; cell++) {
        singleAdvance[cell] = PlanetEcologyLocalAt(
            cells[cell][0], cells[cell][1], 0.72f);
        migrationSignal += fabsf(singleAdvance[cell].migration.floraNet) +
            fabsf(singleAdvance[cell].migration.faunaNet) +
            fabsf(singleAdvance[cell].migration.floraFlowX) +
            fabsf(singleAdvance[cell].migration.floraFlowZ) +
            fabsf(singleAdvance[cell].migration.faunaFlowX) +
            fabsf(singleAdvance[cell].migration.faunaFlowZ);
    }
    assert(migrationSignal > 0.000001f);

    FILE *migrationReplay = tmpfile();
    assert(migrationReplay);
    assert(PlanetEcologySaveState(migrationReplay));
    PlanetEcologyResetState();
    rewind(migrationReplay);
    assert(PlanetEcologyLoadState(migrationReplay));
    for (int cell = 0; cell < 4; cell++) {
        AssertLocalEcologyEqual(
            PlanetEcologyLocalAt(cells[cell][0], cells[cell][1], 0.72f),
            singleAdvance[cell]);
    }
    fclose(migrationReplay);

    rewind(baseline);
    assert(SpaceLoadState(baseline));
    PlanetEcologyResetState();
    EcologyTestActivatePlanet(seed, 0, 0);
    for (int index = 0; index < 4; index++) {
        int cell = reverseOrder[index];
        PlanetEcologyLocalAt(cells[cell][0], cells[cell][1], 0.72f);
    }
    SpaceAdvanceTime(48.0f);
    for (int cell = 0; cell < 4; cell++) {
        PlanetEcologyLocalAt(cells[cell][0], cells[cell][1], 0.72f);
    }
    SpaceAdvanceTime(48.0f);
    for (int cell = 0; cell < 4; cell++) {
        partitionedAdvance[cell] = PlanetEcologyLocalAt(
            cells[cell][0], cells[cell][1], 0.72f);
        AssertLocalEcologyEqual(
            partitionedAdvance[cell], singleAdvance[cell]);
    }
    fclose(baseline);
}

static void TestEcologyPlayerEditDisturbance(void)
{
    static const int cells[4][2] = {
        { 32, 32 }, { 96, 32 }, { 32, 96 }, { 96, 96 }
    };
    const float daylight = 0.72f;
    uint32_t seed = 0u;

    EcologyTestClearBlockEdits();
    for (uint32_t index = 0; index < 2048u; index++) {
        uint32_t candidate = 0x4c957f2du + index * 0x9e3779b9u;
        EcologyTestSetSeed(candidate);
        PlanetEcologyResetState();
        EcologyTestActivatePlanet(candidate, 0, 0);
        PlanetLocalEcology local = PlanetEcologyLocalAt(
            cells[0][0], cells[0][1], daylight);
        if (local.population.floraDensity > 0.05f &&
            local.population.faunaDensity > 0.03f) {
            seed = candidate;
            break;
        }
    }
    assert(seed != 0u);

    EcologyTestSetSeed(seed);
    EcologyTestActivatePlanet(seed, 0, 0);
    PlanetEcologyResetState();
    for (int cell = 0; cell < 4; cell++) {
        PlanetEcologyLocalAt(cells[cell][0], cells[cell][1], daylight);
    }
    FILE *baseline = tmpfile();
    assert(baseline);
    EcologyTestSaveSimulationState(baseline);

    SpaceAdvanceTime(96.0f);
    PlanetLocalEcology undisturbed = PlanetEcologyLocalAt(
        cells[0][0], cells[0][1], daylight);
    assert(undisturbed.environment.disturbance == 0.0f);

    EcologyTestLoadSimulationState(baseline);
    int terrainHeight = PlanetTerrainHeight(cells[0][0], cells[0][1]);
    EcologyTestAddBlockEdit(
        cells[0][0], terrainHeight - 20, cells[0][1], BLOCK_LAVA);
    PlanetLocalEcology deepEdit = PlanetEcologyLocalAt(
        cells[0][0], cells[0][1], daylight);
    assert(deepEdit.environment.disturbance == 0.0f);

    AddSurfaceDisturbanceEdits();
    EcologyTestResetBlockEditReadCount();
    PlanetLocalEcology disturbanceSignal = PlanetEcologyLocalAt(
        cells[0][0], cells[0][1], daylight);
    assert(disturbanceSignal.environment.disturbance > 0.5f);
    int initialEditReads = EcologyTestBlockEditReadCount();
    assert(initialEditReads == EcologyTestBlockEditCount());
    PlanetLocalEcology sameRegionSignal = PlanetEcologyLocalAt(
        cells[0][0] + 8, cells[0][1] + 8, daylight);
    assert(sameRegionSignal.environment.disturbance ==
           disturbanceSignal.environment.disturbance);
    assert(EcologyTestBlockEditReadCount() == initialEditReads);
    int unchangedEditCount = EcologyTestBlockEditCount();
    uint64_t previousEditRevision = EcologyTestBlockEditRevision();
    EcologyTestSetBlockEditType(0, BLOCK_FLOWER);
    assert(EcologyTestBlockEditCount() == unchangedEditCount);
    assert(EcologyTestBlockEditRevision() != previousEditRevision);
    PlanetLocalEcology revisedSignal = PlanetEcologyLocalAt(
        cells[0][0], cells[0][1], daylight);
    assert(revisedSignal.environment.disturbance <
           disturbanceSignal.environment.disturbance);
    assert(EcologyTestBlockEditReadCount() == initialEditReads * 2);
    SpaceAdvanceTime(96.0f);
    PlanetLocalEcology disturbed = PlanetEcologyLocalAt(
        cells[0][0], cells[0][1], daylight);
    assert(disturbed.population.floraDensity <
           undisturbed.population.floraDensity);
    assert(disturbed.population.faunaDensity <
           undisturbed.population.faunaDensity);

    FILE *disturbedSave = tmpfile();
    assert(disturbedSave);
    EcologyTestSaveSimulationState(disturbedSave);
    SpaceAdvanceTime(48.0f);
    PlanetLocalEcology continued = PlanetEcologyLocalAt(
        cells[0][0], cells[0][1], daylight);
    EcologyTestLoadSimulationState(disturbedSave);
    SpaceAdvanceTime(48.0f);
    PlanetLocalEcology replay = PlanetEcologyLocalAt(
        cells[0][0], cells[0][1], daylight);
    AssertLocalEcologyEqual(replay, continued);

    EcologyTestLoadSimulationState(disturbedSave);
    SpaceAdvanceTime(960.0f);
    PlanetLocalEcology persistentlyDisturbed = PlanetEcologyLocalAt(
        cells[0][0], cells[0][1], daylight);
    EcologyTestLoadSimulationState(disturbedSave);
    EcologyTestClearBlockEdits();
    PlanetLocalEcology cleared = PlanetEcologyLocalAt(
        cells[0][0], cells[0][1], daylight);
    assert(cleared.environment.disturbance == 0.0f);
    assert(memcmp(&cleared.population, &disturbed.population,
                  sizeof(cleared.population)) == 0);
    SpaceAdvanceTime(960.0f);
    PlanetLocalEcology recovered = PlanetEcologyLocalAt(
        cells[0][0], cells[0][1], daylight);
    assert(recovered.population.floraDensity >
           persistentlyDisturbed.population.floraDensity);
    assert(recovered.population.faunaDensity >
           persistentlyDisturbed.population.faunaDensity);

    fclose(disturbedSave);
    fclose(baseline);
    EcologyTestClearBlockEdits();
}

static void TestEcologyFaunaHarvestFeedback(void)
{
    uint32_t seed = 0u;
    PlanetLocalEcology baseline = { 0 };
    const int sampleX = 32;
    const int sampleZ = 32;
    for (uint32_t index = 0; index < 512u; index++) {
        uint32_t candidate = 0x31d4a7b9u + index * 0x9e3779b9u;
        EcologyTestSetSeed(candidate);
        PlanetEcologyResetState();
        EcologyTestActivatePlanet(candidate, 0, 0);
        PlanetEcologyResetState();
        baseline = PlanetEcologyLocalAt(sampleX, sampleZ, 0.72f);
        if (baseline.population.faunaDensity > 0.04f &&
            baseline.suitability.faunaCapacity > 0.04f) {
            seed = candidate;
            break;
        }
    }
    assert(seed != 0u);
    assert(baseline.population.faunaHarvestPressure == 0.0f);

    PlanetLocalEcology neighbor = PlanetEcologyLocalAt(
        sampleX + 64, sampleZ, 0.72f);
    assert(neighbor.population.faunaHarvestPressure == 0.0f);
    uint32_t epoch = PlanetEcologyTestPopulationEpoch();
    PlanetEcologyProfile profile = PlanetEcologyCurrent();
    assert(PlanetEcologyRecordFaunaHarvest(
        sampleX, sampleZ, 0.72f, profile.organismScale,
        baseline.suitability.faunaCapacity));
    assert(PlanetEcologyTestPopulationEpoch() != epoch);

    PlanetLocalEcology harvested = PlanetEcologyLocalAt(
        sampleX, sampleZ, 0.72f);
    assert(harvested.population.faunaHarvestPressure > 0.0f);
    assert(harvested.population.faunaDensity <
           baseline.population.faunaDensity);
    assert(harvested.population.floraDensity ==
           baseline.population.floraDensity);
    assert(harvested.suitability.faunaActivity <
           baseline.suitability.faunaActivity);
    assert(PlanetFaunaPopulationCap(
               harvested.suitability.faunaActivity, 128) <=
           PlanetFaunaPopulationCap(
               baseline.suitability.faunaActivity, 128));

    PlanetLocalEcology untouchedNeighbor = PlanetEcologyLocalAt(
        sampleX + 64, sampleZ, 0.72f);
    assert(untouchedNeighbor.population.faunaHarvestPressure ==
           neighbor.population.faunaHarvestPressure);

    FILE *harvestSave = tmpfile();
    assert(harvestSave);
    EcologyTestSaveSimulationState(harvestSave);
    float pressure = harvested.population.faunaHarvestPressure;
    SpaceAdvanceTime(48.0f);
    PlanetLocalEcology recovered = PlanetEcologyLocalAt(
        sampleX, sampleZ, 0.72f);
    assert(recovered.population.faunaHarvestPressure < pressure);
    assert(recovered.population.faunaHarvestPressure >= 0.0f);

    EcologyTestLoadSimulationState(harvestSave);
    PlanetLocalEcology loaded = PlanetEcologyLocalAt(
        sampleX, sampleZ, 0.72f);
    AssertLocalEcologyEqual(loaded, harvested);
    SpaceAdvanceTime(48.0f);
    PlanetLocalEcology replay = PlanetEcologyLocalAt(
        sampleX, sampleZ, 0.72f);
    AssertLocalEcologyEqual(replay, recovered);
    fclose(harvestSave);
}

static void TestEcologyLegacyPopulationStateLoad(void)
{
    FILE *legacy = tmpfile();
    assert(legacy);
    const uint32_t header[2] = { 1u, 0u };
    const uint64_t legacyAccessSerial = 0u;
    assert(fwrite(header, sizeof(header), 1, legacy) == 1);
    assert(fwrite(&legacyAccessSerial,
                  sizeof(legacyAccessSerial), 1, legacy) == 1);
    rewind(legacy);
    assert(PlanetEcologyLoadState(legacy));
    fclose(legacy);

    const uint32_t surfaceId = 0x93b7e421u;
    const int32_t coordinates[2] = { 3, -7 };
    const double lastUpdateTime = 24.0;
    const uint64_t lastAccess = 1u;
    const float population[5] = { 0.4f, 0.3f, 0.6f, 0.5f, 0.7f };
    const float migration[6] = {
        0.01f, -0.02f, 0.03f, -0.04f, 0.05f, -0.06f
    };
    FILE *version2 = tmpfile();
    assert(version2);
    const uint32_t version2Header[2] = { 2u, 1u };
    const uint64_t accessSerial = 1u;
    assert(fwrite(version2Header, sizeof(version2Header), 1, version2) == 1);
    assert(fwrite(&accessSerial, sizeof(accessSerial), 1, version2) == 1);
    assert(fwrite(&surfaceId, sizeof(surfaceId), 1, version2) == 1);
    assert(fwrite(coordinates, sizeof(coordinates), 1, version2) == 1);
    assert(fwrite(&lastUpdateTime, sizeof(lastUpdateTime), 1, version2) == 1);
    assert(fwrite(&lastAccess, sizeof(lastAccess), 1, version2) == 1);
    assert(fwrite(population, sizeof(population), 1, version2) == 1);
    assert(fwrite(migration, sizeof(migration), 1, version2) == 1);
    rewind(version2);
    assert(PlanetEcologyLoadState(version2));
    fclose(version2);

    FILE *upgraded = tmpfile();
    assert(upgraded);
    assert(PlanetEcologySaveState(upgraded));
    rewind(upgraded);
    uint32_t upgradedHeader[2] = { 0 };
    uint64_t upgradedAccessSerial = 0u;
    uint32_t upgradedSurfaceId = 0u;
    int32_t upgradedCoordinates[2] = { 0 };
    double upgradedLastUpdateTime = 0.0;
    uint64_t upgradedLastAccess = 0u;
    float upgradedPopulation[5] = { 0 };
    float upgradedMigration[6] = { 0 };
    float upgradedPressure = -1.0f;
    float upgradedRadiationMemory = -1.0f;
    PlanetEvolutionRegion upgradedEvolution = { 0 };
    assert(fread(upgradedHeader, sizeof(upgradedHeader), 1, upgraded) == 1);
    assert(fread(&upgradedAccessSerial,
                 sizeof(upgradedAccessSerial), 1, upgraded) == 1);
    assert(fread(&upgradedSurfaceId,
                 sizeof(upgradedSurfaceId), 1, upgraded) == 1);
    assert(fread(upgradedCoordinates,
                 sizeof(upgradedCoordinates), 1, upgraded) == 1);
    assert(fread(&upgradedLastUpdateTime,
                 sizeof(upgradedLastUpdateTime), 1, upgraded) == 1);
    assert(fread(&upgradedLastAccess,
                 sizeof(upgradedLastAccess), 1, upgraded) == 1);
    assert(fread(upgradedPopulation,
                 sizeof(upgradedPopulation), 1, upgraded) == 1);
    assert(fread(upgradedMigration,
                 sizeof(upgradedMigration), 1, upgraded) == 1);
    assert(fread(&upgradedPressure,
                 sizeof(upgradedPressure), 1, upgraded) == 1);
    assert(fread(&upgradedRadiationMemory,
                 sizeof(upgradedRadiationMemory), 1, upgraded) == 1);
    assert(fread(&upgradedEvolution,
                 sizeof(upgradedEvolution), 1, upgraded) == 1);
    assert(upgradedHeader[0] == 5u && upgradedHeader[1] == 1u);
    assert(upgradedAccessSerial == accessSerial);
    assert(upgradedSurfaceId == surfaceId);
    assert(memcmp(upgradedCoordinates, coordinates,
                  sizeof(coordinates)) == 0);
    assert(upgradedLastUpdateTime == lastUpdateTime);
    assert(upgradedLastAccess == lastAccess);
    assert(memcmp(upgradedPopulation, population,
                  sizeof(population)) == 0);
    assert(memcmp(upgradedMigration, migration,
                  sizeof(migration)) == 0);
    assert(upgradedPressure == 0.0f);
    assert(upgradedRadiationMemory == 0.0f);
    assert(upgradedEvolution.lineageCount == 0u);

    FILE *invalid = tmpfile();
    assert(invalid);
    const uint32_t version3Header[2] = { 3u, 1u };
    float invalidPressure = NAN;
    assert(fwrite(version3Header, sizeof(version3Header), 1, invalid) == 1);
    assert(fwrite(&accessSerial, sizeof(accessSerial), 1, invalid) == 1);
    assert(fwrite(&surfaceId, sizeof(surfaceId), 1, invalid) == 1);
    assert(fwrite(coordinates, sizeof(coordinates), 1, invalid) == 1);
    assert(fwrite(&lastUpdateTime, sizeof(lastUpdateTime), 1, invalid) == 1);
    assert(fwrite(&lastAccess, sizeof(lastAccess), 1, invalid) == 1);
    assert(fwrite(population, sizeof(population), 1, invalid) == 1);
    assert(fwrite(migration, sizeof(migration), 1, invalid) == 1);
    assert(fwrite(&invalidPressure, sizeof(invalidPressure), 1, invalid) == 1);
    rewind(invalid);
    assert(!PlanetEcologyLoadState(invalid));
    fclose(invalid);

    FILE *afterFailure = tmpfile();
    assert(afterFailure);
    assert(PlanetEcologySaveState(afterFailure));
    rewind(upgraded);
    rewind(afterFailure);
    int expectedByte = 0;
    int actualByte = 0;
    do {
        expectedByte = fgetc(upgraded);
        actualByte = fgetc(afterFailure);
        assert(expectedByte == actualByte);
    } while (expectedByte != EOF);
    fclose(afterFailure);
    fclose(upgraded);
}

static void TestEvolutionRegionAndGenomeReplay(void)
{
    const float daylight = 0.72f;
    const int sampleX = 32;
    const int sampleZ = 32;
    uint32_t seed = 0u;
    for (uint32_t index = 0; index < 2048u; index++) {
        uint32_t candidate = 0x71a5c39du + index * 0x9e3779b9u;
        EcologyTestSetSeed(candidate);
        PlanetEcologyResetState();
        EcologyTestActivatePlanet(candidate, 0, 0);
        PlanetLocalEcology local = PlanetEcologyLocalAt(
            sampleX, sampleZ, daylight);
        if (local.population.faunaDensity > 0.03f) {
            seed = candidate;
            break;
        }
    }
    assert(seed != 0u);

    EcologyTestSetSeed(seed);
    EcologyTestActivatePlanet(seed, 0, 0);
    PlanetEcologyResetState();
    PlanetEcologyLocalAt(sampleX, sampleZ, daylight);
    PlanetEvolutionRegion initial = { 0 };
    assert(PlanetEcologyEvolutionRegionAt(
        sampleX, sampleZ, daylight, &initial));
    assert(initial.lineageCount == 3u);
    assert(!initial.bootstrapComplete);
    assert(initial.herbivoreDensity > 0.0f);

    CreatureGenome first = { 0 };
    CreatureGenome replay = { 0 };
    uint32_t lineageId = 0u;
    uint32_t speciesId = 0u;
    assert(PlanetEcologySampleGenome(
        sampleX, sampleZ, daylight, 0x1234abcdu, &first,
        &lineageId, &speciesId));
    assert(PlanetEcologySampleGenome(
        sampleX, sampleZ, daylight, 0x1234abcdu, &replay,
        NULL, NULL));
    assert(lineageId != 0u && speciesId != 0u);
    assert(memcmp(&first, &replay, sizeof(first)) == 0);
    CreaturePhenotype phenotype = EvolutionDevelop(&first);
    assert(phenotype.valid);

    SpaceAdvanceTime(96.0f);
    PlanetEvolutionRegion evolved = { 0 };
    assert(PlanetEcologyEvolutionRegionAt(
        sampleX, sampleZ, daylight, &evolved));
    assert(evolved.bootstrapComplete);
    assert(evolved.bootstrapGeneration == 24u);
    assert(evolved.lineageCount == initial.lineageCount);
    float evolvedDensity = 0.0f;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        const PlanetEvolutionLineage *lineage = &evolved.lineages[index];
        if (!lineage->active) continue;
        assert(lineage->founderSeed != 0u);
        assert(lineage->lineageId != 0u);
        assert(lineage->speciesId != 0u);
        evolvedDensity += lineage->density;
    }
    PlanetLocalEcology evolvedLocal = PlanetEcologyLocalAt(
        sampleX, sampleZ, daylight);
    assert(fabsf(evolvedDensity - evolvedLocal.population.faunaDensity) <
           0.00001f);
    CreatureGenome evolvedSample = { 0 };
    uint32_t evolvedLineageId = 0u;
    assert(PlanetEcologySampleGenome(
        sampleX, sampleZ, daylight, 0x8f271abdu, &evolvedSample,
        &evolvedLineageId, NULL));
    CreaturePhenotype evolvedPhenotype = EvolutionDevelop(&evolvedSample);
    assert(evolvedPhenotype.valid);
    bool matchedEvolvedLineage = false;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        const PlanetEvolutionLineage *lineage = &evolved.lineages[index];
        if (!lineage->active || lineage->lineageId != evolvedLineageId) {
            continue;
        }
        matchedEvolvedLineage = true;
        assert(evolvedSample.generation == lineage->generation);
        assert(fabsf(evolvedPhenotype.diet - lineage->dietMean) < 0.25f);
    }
    assert(matchedEvolvedLineage);

    FILE *saved = tmpfile();
    assert(saved);
    assert(PlanetEcologySaveState(saved));
    PlanetEcologyResetState();
    rewind(saved);
    assert(PlanetEcologyLoadState(saved));
    PlanetEvolutionRegion loaded = { 0 };
    assert(PlanetEcologyEvolutionRegionAt(
        sampleX, sampleZ, daylight, &loaded));
    assert(memcmp(&loaded, &evolved, sizeof(loaded)) == 0);
    fclose(saved);

    float beforeBirth = 0.0f;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        if (loaded.lineages[index].lineageId == lineageId) {
            beforeBirth = loaded.lineages[index].density;
        }
    }
    assert(beforeBirth > 0.0f);
    assert(PlanetEcologyRecordEvolutionEvent(
        sampleX, sampleZ, daylight, lineageId,
        PLANET_EVOLUTION_EVENT_BIRTH, 8.0f));
    PlanetEvolutionRegion afterBirth = { 0 };
    assert(PlanetEcologyEvolutionRegionAt(
        sampleX, sampleZ, daylight, &afterBirth));
    float birthDensity = 0.0f;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        if (afterBirth.lineages[index].lineageId == lineageId) {
            birthDensity = afterBirth.lineages[index].density;
        }
    }
    assert(birthDensity > beforeBirth);
    assert(PlanetEcologyRecordEvolutionEvent(
        sampleX, sampleZ, daylight, lineageId,
        PLANET_EVOLUTION_EVENT_PREDATION_DEATH, 8.0f));
    assert(!PlanetEcologyRecordEvolutionEvent(
        sampleX, sampleZ, daylight, lineageId,
        (PlanetEvolutionEvent)99, 8.0f));
    PlanetEvolutionRegion afterDeath = { 0 };
    assert(PlanetEcologyEvolutionRegionAt(
        sampleX, sampleZ, daylight, &afterDeath));
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        if (afterDeath.lineages[index].lineageId == lineageId) {
            assert(afterDeath.lineages[index].density < birthDensity);
        }
    }
    float afterDeathDensity = 0.0f;
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        if (afterDeath.lineages[index].active) {
            afterDeathDensity += afterDeath.lineages[index].density;
        }
    }
    PlanetLocalEcology afterDeathLocal = PlanetEcologyLocalAt(
        sampleX, sampleZ, daylight);
    assert(fabsf(afterDeathDensity -
                 afterDeathLocal.population.faunaDensity) < 0.00001f);
}

static void TestHomeWorldEcologyAndEvolution(void)
{
    const float daylight = 0.72f;
    SpaceReset();
    EcologyTestSetSeed(0x13579bdfu);
    PlanetEcologyResetState();
    assert(HomeWorldSurfaceIsActive());

    PlanetEcologyProfile profile = PlanetEcologyCurrent();
    assert(profile.chemistry == PLANET_CHEMISTRY_CARBON);
    assert(profile.lifeOriginated && profile.hasComplexLife);
    assert(profile.supportsFlight);
    assert(profile.faunaDensity > 0.0f);

    PlanetLocalEcology local = PlanetEcologyLocalAt(0, 0, daylight);
    assert(local.suitability.faunaActivity >= 0.0f &&
           local.suitability.faunaActivity <= 1.0f);
    PlanetEvolutionRegion initial = { 0 };
    assert(PlanetEcologyEvolutionRegionAt(0, 0, daylight, &initial));
    assert(initial.lineageCount == 3u);
    assert(initial.herbivoreDensity + initial.omnivoreDensity +
           initial.carnivoreDensity > 0.0f);

    CreatureGenome first = { 0 };
    CreatureGenome replay = { 0 };
    uint32_t lineageId = 0u;
    uint32_t speciesId = 0u;
    assert(PlanetEcologySampleGenome(
        0, 0, daylight, 0x2468ace0u, &first, &lineageId, &speciesId));
    assert(PlanetEcologySampleGenome(
        0, 0, daylight, 0x2468ace0u, &replay, NULL, NULL));
    assert(memcmp(&first, &replay, sizeof(first)) == 0);
    assert(lineageId != 0u && speciesId != 0u);
    assert(EvolutionDevelop(&first).valid);

    SpaceAdvanceTime(96.0f);
    PlanetEvolutionRegion evolved = { 0 };
    assert(PlanetEcologyEvolutionRegionAt(0, 0, daylight, &evolved));
    assert(evolved.bootstrapComplete);
    assert(evolved.bootstrapGeneration == 24u);

    FILE *saved = tmpfile();
    assert(saved);
    assert(PlanetEcologySaveState(saved));
    PlanetEcologyResetState();
    rewind(saved);
    assert(PlanetEcologyLoadState(saved));
    PlanetEvolutionRegion loaded = { 0 };
    assert(PlanetEcologyEvolutionRegionAt(0, 0, daylight, &loaded));
    assert(memcmp(&loaded, &evolved, sizeof(loaded)) == 0);
    fclose(saved);
    SpaceReset();
}

typedef struct ChunkBlockSnapshot {
    int cx;
    int cz;
    FloraStructureInstance floraStructures[MAX_CHUNK_FLORA_STRUCTURES];
    int floraStructureCount;
    unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
} ChunkBlockSnapshot;

static void SnapshotChunkBlocks(
    const Chunk *chunk,
    unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE])
{
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int y = 0; y < WORLD_HEIGHT; y++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                blocks[lx][y][lz] =
                    (unsigned short)ChunkGetLocalBlock(chunk, lx, y, lz);
            }
        }
    }
}

static void AssertChunkSectionsHaveNoModels(const Chunk *chunk)
{
    for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
         sectionIndex++) {
        const ChunkSection *section = chunk->sections[sectionIndex];
        assert(!section->hasModel && !section->hasWaterModel &&
               !section->hasFloraModel);
        assert(section->floraTargetScales == NULL);
        assert(section->floraTargetWind == NULL);
        assert(section->floraTargetWindAngle == NULL);
        assert(section->floraTargetPresence == NULL);
        assert(section->floraBaseVertices == NULL);
        assert(section->floraBaseColors == NULL);
        assert(section->floraVisualInstances == NULL);
    }
}

static void FreeCpuMesh(Mesh *mesh)
{
    free(mesh->vertices);
    free(mesh->texcoords);
    free(mesh->texcoords2);
    free(mesh->normals);
    free(mesh->colors);
    *mesh = (Mesh){ 0 };
}

static void AssertMeshEqual(const Mesh *actual, const Mesh *expected)
{
    assert(actual->vertexCount == expected->vertexCount);
    assert(actual->triangleCount == expected->triangleCount);
    size_t vertexCount = (size_t)actual->vertexCount;
    assert(memcmp(actual->vertices, expected->vertices,
                  vertexCount * 3u * sizeof(float)) == 0);
    assert(memcmp(actual->texcoords, expected->texcoords,
                  vertexCount * 2u * sizeof(float)) == 0);
    assert(memcmp(actual->normals, expected->normals,
                  vertexCount * 3u * sizeof(float)) == 0);
    assert(memcmp(actual->colors, expected->colors,
                  vertexCount * 4u * sizeof(unsigned char)) == 0);
}

static bool BuildChunkFloraMesh(
    const Chunk *chunk, Mesh *outMesh,
    FloraVisualInstance **outInstances, int *outInstanceCount)
{
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    return BuildChunkFloraMeshData(
        chunk, faces, NULL, 0, outMesh, outInstances, outInstanceCount);
}

static Vector3 FindFloraGenerationCenter(
    uint32_t *outSeed, FloraStructureInstance *outStructure)
{
    for (uint32_t seedIndex = 0; seedIndex < 512u; seedIndex++) {
        uint32_t seed = 0x51a7e5edu + seedIndex * 0x9e3779b9u;
        EcologyTestSetSeed(seed);
        PlanetEcologyResetState();
        EcologyTestActivatePlanet(seed, 317, -911);
        PlanetEcologyProfile ecology = PlanetEcologyCurrent();
        if (ecology.floraDensity <= 0.08f) continue;

        Chunk probe = { 0 };
        for (int radius = 0; radius <= 12; radius++) {
            for (int cz = -radius; cz <= radius; cz++) {
                for (int cx = -radius; cx <= radius; cx++) {
                    if (radius > 0 && abs(cx) != radius &&
                        abs(cz) != radius) {
                        continue;
                    }
                    GenerateChunkTerrain(&probe, cx, cz,
                                         WorldTerrainMode());
                    int chunkMinX = cx * CHUNK_SIZE;
                    int chunkMinZ = cz * CHUNK_SIZE;
                    int chunkMaxX = chunkMinX + CHUNK_SIZE - 1;
                    int chunkMaxZ = chunkMinZ + CHUNK_SIZE - 1;
                    for (int index = 0;
                         index < probe.floraStructureCount; index++) {
                        FloraStructureInstance structure =
                            probe.floraStructures[index];
                        bool crossesBoundary =
                            structure.minX < chunkMinX ||
                            structure.maxX > chunkMaxX ||
                            structure.minZ < chunkMinZ ||
                            structure.maxZ > chunkMaxZ;
                        if (!crossesBoundary) continue;
                        *outSeed = seed;
                        *outStructure = structure;
                        Vector3 center = {
                            (float)chunkMinX + 0.5f,
                            18.0f,
                            (float)chunkMinZ + 0.5f
                        };
                        ChunkClearBlockStorage(&probe);
                        return center;
                    }
                }
            }
        }
        ChunkClearBlockStorage(&probe);
    }
    assert(false);
    return (Vector3){ 0 };
}

static void AssertSemanticFloraStructure(
    const FloraStructureInstance *structure)
{
    assert(structure);
    switch (structure->kind) {
    case FLORA_STRUCTURE_ALIEN_CANOPY:
        assert(structure->primaryBlock == BLOCK_LIVING_STEM);
        assert(structure->accentBlock == BLOCK_CANOPY_FROND);
        break;
    case FLORA_STRUCTURE_CRYSTAL:
        assert(structure->primaryBlock == BLOCK_CRYSTAL_BLOOM);
        assert(structure->accentBlock == BLOCK_CRYSTAL_BLOOM);
        break;
    case FLORA_STRUCTURE_SPORE:
        assert(structure->primaryBlock == BLOCK_FUNGAL_STEM);
        assert(structure->accentBlock == BLOCK_SPORE_CAP);
        break;
    case FLORA_STRUCTURE_THERMAL_VENT:
        assert(structure->primaryBlock == BLOCK_VENT_CHIMNEY);
        assert(structure->accentBlock == BLOCK_CHEMO_MAT);
        break;
    }
}

static void TestPlanetGroundCoverContracts(void)
{
    assert(PlanetEcologyTestGroundCoverBlock(
        PLANET_BIOMASS_BARREN, PLANET_FLORA_SPORE,
        PLANET_BIOME_PLAINS, 1u) == BLOCK_AIR);
    assert(PlanetEcologyTestGroundCoverBlock(
        PLANET_BIOMASS_MICROBIAL, PLANET_FLORA_SPORE,
        PLANET_BIOME_PLAINS, 0u) == BLOCK_LICHEN);
    assert(PlanetEcologyTestGroundCoverBlock(
        PLANET_BIOMASS_MICROBIAL, PLANET_FLORA_SPORE,
        PLANET_BIOME_PLAINS, 1u) == BLOCK_MICROBIAL_MAT);
    assert(PlanetEcologyTestGroundCoverBlock(
        PLANET_BIOMASS_FUNGAL, PLANET_FLORA_SPORE,
        PLANET_BIOME_PLAINS, 0u) == BLOCK_LICHEN);
    assert(PlanetEcologyTestGroundCoverBlock(
        PLANET_BIOMASS_FUNGAL, PLANET_FLORA_SPORE,
        PLANET_BIOME_PLAINS, 1u) == BLOCK_MYCELIUM);
    assert(PlanetEcologyTestGroundCoverBlock(
        PLANET_BIOMASS_CRYSTALLINE, PLANET_FLORA_CRYSTAL,
        PLANET_BIOME_PLAINS, 0u) == BLOCK_LICHEN);
    assert(PlanetEcologyTestGroundCoverBlock(
        PLANET_BIOMASS_CRYSTALLINE, PLANET_FLORA_CRYSTAL,
        PLANET_BIOME_PLAINS, 1u) == BLOCK_MICROBIAL_MAT);
    assert(PlanetEcologyTestGroundCoverBlock(
        PLANET_BIOMASS_ANOMALOUS, PLANET_FLORA_SPORE,
        PLANET_BIOME_PLAINS, 0u) == BLOCK_MYCELIUM);
    assert(PlanetEcologyTestGroundCoverBlock(
        PLANET_BIOMASS_ANOMALOUS, PLANET_FLORA_SPORE,
        PLANET_BIOME_PLAINS, 1u) == BLOCK_MICROBIAL_MAT);
    assert(PlanetEcologyTestGroundCoverBlock(
        PLANET_BIOMASS_LUSH, PLANET_FLORA_ALIEN_CANOPY,
        PLANET_BIOME_FOREST, 1u) == BLOCK_MOSS_CARPET);
    assert(PlanetEcologyTestGroundCoverBlock(
        PLANET_BIOMASS_LUSH, PLANET_FLORA_ALIEN_CANOPY,
        PLANET_BIOME_PLAINS, 0u) == BLOCK_FERN);
    assert(PlanetEcologyTestGroundCoverBlock(
        PLANET_BIOMASS_LUSH, PLANET_FLORA_ALIEN_CANOPY,
        PLANET_BIOME_PLAINS, 1u) == BLOCK_LICHEN);
    assert(PlanetEcologyTestGroundCoverBlock(
        PLANET_BIOMASS_LUSH, PLANET_FLORA_THERMAL_VENT,
        PLANET_BIOME_VOLCANIC_RIDGE, 1u) == BLOCK_CHEMO_MAT);

    EcologyTestActivatePlanet(0x4e434f53u, 0, 0);
    Chunk microbial = { .cx = 0, .cz = 0 };
    PlanetEcologyProfile profile = PlanetEcologyCurrent();
    profile.biomass = PLANET_BIOMASS_MICROBIAL;
    profile.floraDensity = 1.0f;
    profile.flora = PLANET_FLORA_ALIEN_CANOPY;
    PlanetEcologyTestApplyProfileToChunk(&microbial, 0, 0, &profile);
    assert(microbial.floraStructureCount == 0);
    ChunkClearBlockStorage(&microbial);
}

static void TestFloraMeshDeformationProperties(void)
{
    enum { vertexCount = 64 };
    for (int sample = 0; sample < 512; sample++) {
        float baseVertices[vertexCount * 3];
        float vertices[vertexCount * 3];
        for (int vertex = 0; vertex < vertexCount; vertex++) {
            baseVertices[vertex * 3] = (float)(vertex - 24) * 0.25f;
            baseVertices[vertex * 3 + 1] = 10.0f +
                (float)((vertex * 7 + sample * 3) % 9 + 1) * 0.5f;
            baseVertices[vertex * 3 + 2] =
                (float)(17 - vertex) * 0.125f;
        }
        memcpy(vertices, baseVertices, sizeof(vertices));

        int firstVertex = sample % 19;
        int available = vertexCount - firstVertex;
        int rangeCount = 1 + (sample * 29) % available;
        if (sample % 7 == 0) rangeCount += vertexCount;
        FloraVisualInstance instance = {
            .firstVertex = firstVertex,
            .vertexCount = rangeCount,
            .anchor = { 3.5f, 10.0f, -8.5f },
            .height = 5.0f,
            .windResponse = 1.0f
        };
        float targetScale = 0.15f + (float)(sample % 13) * 0.06f;
        float sway = (float)(sample % 17 - 8) * 0.015f;
        float windAngle = (float)sample * 0.13f;
        float appliedScale = 0.0f;
        bool changed = false;
        assert(DeformFloraMeshInstance(
            vertices, baseVertices, vertexCount, &instance,
            targetScale, 1.0f, sway, windAngle,
            &appliedScale, &changed));
        assert(changed);
        assert(fabsf(appliedScale - targetScale) < 0.000001f);

        int lastVertex = firstVertex + rangeCount;
        if (lastVertex > vertexCount) lastVertex = vertexCount;
        for (int vertex = 0; vertex < vertexCount; vertex++) {
            const float *base = &baseVertices[vertex * 3];
            const float *current = &vertices[vertex * 3];
            if (vertex < firstVertex || vertex >= lastVertex) {
                assert(memcmp(current, base, 3u * sizeof(float)) == 0);
                continue;
            }
            float heightFraction = fminf(fmaxf(
                (base[1] - instance.anchor.y) / instance.height,
                0.0f), 1.0f);
            float expectedX = base[0] +
                cosf(windAngle) * sway * heightFraction;
            float expectedY = instance.anchor.y +
                (base[1] - instance.anchor.y) * targetScale;
            float expectedZ = base[2] +
                sinf(windAngle) * sway * heightFraction;
            assert(fabsf(current[0] - expectedX) < 0.000001f);
            assert(fabsf(current[1] - expectedY) < 0.000001f);
            assert(fabsf(current[2] - expectedZ) < 0.000001f);
        }

        float stableVertices[vertexCount * 3];
        memcpy(stableVertices, vertices, sizeof(stableVertices));
        changed = true;
        assert(DeformFloraMeshInstance(
            vertices, baseVertices, vertexCount, &instance,
            targetScale, 1.0f, sway, windAngle,
            &appliedScale, &changed));
        assert(!changed);
        assert(memcmp(vertices, stableVertices, sizeof(vertices)) == 0);

        assert(DeformFloraMeshInstance(
            vertices, baseVertices, vertexCount, &instance,
            1.0f, 1.0f, 0.0f, 0.0f,
            &appliedScale, &changed));
        assert(memcmp(vertices, baseVertices, sizeof(vertices)) == 0);
    }

    float fragmentABase[4 * 3] = {
        -100.0f, -100.0f, -100.0f,
        2.0f, 11.0f, 7.0f,
        2.0f, 12.0f, 7.0f,
        -100.0f, -100.0f, -100.0f
    };
    float fragmentBBase[5 * 3] = {
        -100.0f, -100.0f, -100.0f,
        -100.0f, -100.0f, -100.0f,
        2.0f, 12.0f, 7.0f,
        2.0f, 14.0f, 7.0f,
        -100.0f, -100.0f, -100.0f
    };
    float fragmentA[4 * 3];
    float fragmentB[5 * 3];
    memcpy(fragmentA, fragmentABase, sizeof(fragmentA));
    memcpy(fragmentB, fragmentBBase, sizeof(fragmentB));
    FloraVisualInstance instanceA = {
        .firstVertex = 1, .vertexCount = 2,
        .anchor = { 2.0f, 10.0f, 7.0f }, .height = 4.0f
    };
    FloraVisualInstance instanceB = instanceA;
    instanceB.firstVertex = 2;
    float appliedScale = 0.0f;
    bool changed = false;
    assert(DeformFloraMeshInstance(
        fragmentA, fragmentABase, 4, &instanceA,
        0.65f, 1.0f, 0.18f, 0.7f, &appliedScale, &changed));
    assert(DeformFloraMeshInstance(
        fragmentB, fragmentBBase, 5, &instanceB,
        0.65f, 1.0f, 0.18f, 0.7f, &appliedScale, &changed));
    assert(memcmp(&fragmentA[2 * 3], &fragmentB[2 * 3],
                  3u * sizeof(float)) == 0);

    float guarded[4 * 3];
    memcpy(guarded, fragmentABase, sizeof(guarded));
    FloraVisualInstance invalid = instanceA;
    invalid.firstVertex = -1;
    assert(!DeformFloraMeshInstance(
        guarded, fragmentABase, 4, &invalid,
        0.5f, 1.0f, 0.1f, 0.0f, &appliedScale, &changed));
    assert(memcmp(guarded, fragmentABase, sizeof(guarded)) == 0);
    invalid.firstVertex = 2;
    invalid.vertexCount = INT_MAX;
    assert(DeformFloraMeshInstance(
        guarded, fragmentABase, 4, &invalid,
        0.5f, 1.0f, 0.1f, 0.0f, &appliedScale, &changed));
    assert(memcmp(guarded, fragmentABase,
                  2u * 3u * sizeof(float)) == 0);
}

static void TestChunkUnloadReloadDeterminism(void)
{
    const int expectedChunkCount =
        (MIN_RENDER_DISTANCE_CHUNKS * 2 + 1) *
        (MIN_RENDER_DISTANCE_CHUNKS * 2 + 1);
    uint32_t seed = 0;
    FloraStructureInstance crossingStructure = { 0 };
    Vector3 playerPosition = FindFloraGenerationCenter(
        &seed, &crossingStructure);
    AssertSemanticFloraStructure(&crossingStructure);
    assert(seed != 0u);
    assert(WorldGetSeed() == seed);
    assert(ChunksStartGenThread());

    UpdateChunks(playerPosition, MIN_RENDER_DISTANCE_CHUNKS);
    DrainChunkGen();
    assert(GetActiveChunkCount() == expectedChunkCount);

    ChunkBlockSnapshot *snapshots = calloc(
        (size_t)expectedChunkCount, sizeof(*snapshots));
    assert(snapshots);
    int snapshotCount = 0;
    int crossingFragmentCount = 0;
    Chunk *crossingChunks[expectedChunkCount];
    int crossingChunkCount = 0;
    Chunk *floraChunk = NULL;
    for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
        Chunk *chunk = &chunks[index];
        if (!chunk->loaded) continue;
        assert(!chunk->generating);
        AssertChunkSectionsHaveNoModels(chunk);
        ChunkBlockSnapshot *snapshot = &snapshots[snapshotCount++];
        snapshot->cx = chunk->cx;
        snapshot->cz = chunk->cz;
        snapshot->floraStructureCount = chunk->floraStructureCount;
        memcpy(snapshot->floraStructures, chunk->floraStructures,
               (size_t)chunk->floraStructureCount *
               sizeof(FloraStructureInstance));
        SnapshotChunkBlocks(chunk, snapshot->blocks);
        bool containsCrossingStructure = false;
        for (int structureIndex = 0;
             structureIndex < chunk->floraStructureCount; structureIndex++) {
            const FloraStructureInstance *structure =
                &chunk->floraStructures[structureIndex];
            if (structure->kind == crossingStructure.kind &&
                structure->shapeHash == crossingStructure.shapeHash &&
                structure->rootX == crossingStructure.rootX &&
                structure->rootZ == crossingStructure.rootZ) {
                crossingFragmentCount++;
                containsCrossingStructure = true;
                if (!floraChunk) floraChunk = chunk;
            }
        }
        if (containsCrossingStructure) {
            assert(crossingChunkCount < expectedChunkCount);
            crossingChunks[crossingChunkCount++] = chunk;
        }
    }
    assert(snapshotCount == expectedChunkCount);
    assert(crossingFragmentCount >= 2);
    assert(crossingChunkCount == crossingFragmentCount);
    assert(floraChunk);

    int heightOwner[WORLD_HEIGHT + 1];
    float displacementX[WORLD_HEIGHT + 1] = { 0 };
    float displacementZ[WORLD_HEIGHT + 1] = { 0 };
    for (int y = 0; y <= WORLD_HEIGHT; y++) heightOwner[y] = -1;
    int matchedFragmentMeshCount = 0;
    int sharedHeightComparisons = 0;
    for (int fragment = 0; fragment < crossingChunkCount; fragment++) {
        Mesh mesh = { 0 };
        FloraVisualInstance *instances = NULL;
        int instanceCount = 0;
        assert(BuildChunkFloraMesh(
            crossingChunks[fragment], &mesh, &instances, &instanceCount));

        int matchingInstance = -1;
        for (int instanceIndex = 0; instanceIndex < instanceCount;
             instanceIndex++) {
            FloraVisualInstance *instance = &instances[instanceIndex];
            if (instance->anchor.x !=
                    (float)crossingStructure.rootX + 0.5f ||
                instance->anchor.y !=
                    (float)crossingStructure.groundY + 1.0f ||
                instance->anchor.z !=
                    (float)crossingStructure.rootZ + 0.5f) {
                continue;
            }
            assert(matchingInstance < 0);
            matchingInstance = instanceIndex;
        }
        assert(matchingInstance >= 0);
        FloraVisualInstance *instance = &instances[matchingInstance];
        assert(instance->height ==
               (float)(crossingStructure.maxY - crossingStructure.groundY));
        assert(instance->windResponse == crossingStructure.windResponse);

        size_t coordinateCount = (size_t)mesh.vertexCount * 3u;
        float *baseVertices = malloc(coordinateCount * sizeof(float));
        assert(baseVertices);
        memcpy(baseVertices, mesh.vertices,
               coordinateCount * sizeof(float));
        float appliedScale = 0.0f;
        bool changed = false;
        assert(DeformFloraMeshInstance(
            mesh.vertices, baseVertices, mesh.vertexCount, instance,
            0.63f, 1.0f, 0.14f, 0.79f,
            &appliedScale, &changed));
        assert(changed);
        assert(fabsf(appliedScale - 0.63f) < 0.000001f);
        matchedFragmentMeshCount++;

        int firstVertex = instance->firstVertex;
        int lastVertex = firstVertex + instance->vertexCount;
        assert(firstVertex >= 0 && lastVertex <= mesh.vertexCount);
        for (int vertex = firstVertex; vertex < lastVertex; vertex++) {
            const float *base = &baseVertices[vertex * 3];
            const float *current = &mesh.vertices[vertex * 3];
            float heightFraction = fminf(fmaxf(
                (base[1] - instance->anchor.y) / instance->height,
                0.0f), 1.0f);
            float expectedY = instance->anchor.y +
                (base[1] - instance->anchor.y) * 0.63f;
            assert(fabsf(current[1] - expectedY) < 0.000001f);
            if (heightFraction <= 0.001f) continue;

            int layer = (int)lroundf(base[1]);
            if (layer < 0 || layer > WORLD_HEIGHT ||
                fabsf(base[1] - (float)layer) >= 0.0001f) {
                continue;
            }
            float dx = current[0] - base[0];
            float dz = current[2] - base[2];
            if (heightOwner[layer] < 0) {
                heightOwner[layer] = fragment;
                displacementX[layer] = dx;
                displacementZ[layer] = dz;
            } else if (heightOwner[layer] != fragment) {
                assert(fabsf(dx - displacementX[layer]) < 0.000001f);
                assert(fabsf(dz - displacementZ[layer]) < 0.000001f);
                sharedHeightComparisons++;
            }
        }
        free(baseVertices);
        free(instances);
        FreeCpuMesh(&mesh);
    }
    assert(matchedFragmentMeshCount == crossingChunkCount);
    assert(sharedHeightComparisons > 0);

    int floraCx = floraChunk->cx;
    int floraCz = floraChunk->cz;
    Mesh firstFloraMesh = { 0 };
    FloraVisualInstance *firstInstances = NULL;
    int firstInstanceCount = 0;
    assert(BuildChunkFloraMesh(
        floraChunk, &firstFloraMesh, &firstInstances, &firstInstanceCount));
    assert(firstFloraMesh.vertexCount > 0);
    assert(firstInstances);
    assert(firstInstanceCount > 0);
    bool hasVariableStructureRange = false;
    bool hasCrossingStructureLayout = false;
    for (int index = 0; index < firstInstanceCount; index++) {
        const FloraVisualInstance *instance = &firstInstances[index];
        assert(instance->firstVertex >= 0);
        assert(instance->vertexCount > 0);
        assert(instance->firstVertex + instance->vertexCount <=
               firstFloraMesh.vertexCount);
        if (instance->vertexCount != 12) hasVariableStructureRange = true;
        if (instance->anchor.x == (float)crossingStructure.rootX + 0.5f &&
            instance->anchor.y == (float)crossingStructure.groundY + 1.0f &&
            instance->anchor.z == (float)crossingStructure.rootZ + 0.5f) {
            assert(instance->height ==
                   (float)(crossingStructure.maxY -
                           crossingStructure.groundY));
            assert(instance->windResponse == crossingStructure.windResponse);
            hasCrossingStructureLayout = true;
        }
    }
    assert(hasVariableStructureRange);
    assert(hasCrossingStructureLayout);

    ChunkSection *floraSection = ChunkGetSection(
        floraChunk, crossingStructure.groundY / SURFACE_SECTION_HEIGHT, true);
    assert(floraSection);
    floraSection->floraTargetScales = malloc(sizeof(float));
    floraSection->floraTargetWind = malloc(sizeof(float));
    floraSection->floraTargetWindAngle = malloc(sizeof(float));
    floraSection->floraTargetPresence = malloc(sizeof(float));
    floraSection->floraBaseVertices = malloc(3u * sizeof(float));
    floraSection->floraBaseColors = malloc(4u);
    floraSection->floraVisualInstances = malloc(sizeof(FloraVisualInstance));
    floraSection->floraTargetScaleCount = 1;
    assert(floraSection->floraTargetScales && floraSection->floraTargetWind &&
           floraSection->floraTargetWindAngle &&
           floraSection->floraTargetPresence && floraSection->floraBaseVertices &&
           floraSection->floraBaseColors && floraSection->floraVisualInstances);

    UnloadAllChunks();
    assert(GetActiveChunkCount() == 0);
    for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
        assert(!chunks[index].loaded);
        assert(chunks[index].sections == NULL);
        assert(chunks[index].sectionCount == 0);
        assert(chunks[index].sectionCapacity == 0);
        chunks[index].floraStructureCount = MAX_CHUNK_FLORA_STRUCTURES;
        memset(chunks[index].floraStructures, 0xa5,
               sizeof(chunks[index].floraStructures));
    }

    UpdateChunks(playerPosition, MIN_RENDER_DISTANCE_CHUNKS);
    DrainChunkGen();
    assert(GetActiveChunkCount() == expectedChunkCount);
    for (int index = 0; index < snapshotCount; index++) {
        Chunk *chunk = FindChunk(snapshots[index].cx, snapshots[index].cz);
        assert(chunk);
        assert(!chunk->generating);
        unsigned short reloaded[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
        SnapshotChunkBlocks(chunk, reloaded);
        assert(memcmp(reloaded, snapshots[index].blocks,
                      sizeof(reloaded)) == 0);
        assert(chunk->floraStructureCount ==
               snapshots[index].floraStructureCount);
        assert(memcmp(chunk->floraStructures,
                      snapshots[index].floraStructures,
                      (size_t)chunk->floraStructureCount *
                      sizeof(FloraStructureInstance)) == 0);
    }

    Chunk *reloadedFloraChunk = FindChunk(floraCx, floraCz);
    assert(reloadedFloraChunk);
    Mesh secondFloraMesh = { 0 };
    FloraVisualInstance *secondInstances = NULL;
    int secondInstanceCount = 0;
    assert(BuildChunkFloraMesh(
        reloadedFloraChunk, &secondFloraMesh,
        &secondInstances, &secondInstanceCount));
    AssertMeshEqual(&secondFloraMesh, &firstFloraMesh);
    assert(secondInstanceCount == firstInstanceCount);
    assert(memcmp(secondInstances, firstInstances,
                  (size_t)firstInstanceCount *
                  sizeof(FloraVisualInstance)) == 0);

    free(secondInstances);
    free(firstInstances);
    FreeCpuMesh(&secondFloraMesh);
    FreeCpuMesh(&firstFloraMesh);
    free(snapshots);
    UnloadAllChunks();
    ChunksShutdownGenThread();
}

int main(void)
{
    TestEcologyPopulationConcurrentQueries();
    TestEcologyUsesPositionLocalWeather();
    TestEcologyCacheInvalidation();
    TestEcologyCrossSeedReplay();
    TestEcologySaveLoadReplay();
    TestGeneratedPlanetProfileSaveLoadReplay();
    TestPlanetWorldStateCompatibilityAndAtomicity();
    TestEcologyRespondsToRemnantExposure();
    TestEcologyMigrationOrderAndTimePartition();
    TestEcologyPlayerEditDisturbance();
    TestEcologyFaunaHarvestFeedback();
    TestEcologyLegacyPopulationStateLoad();
    TestEvolutionRegionAndGenomeReplay();
    TestHomeWorldEcologyAndEvolution();
    TestPlanetGroundCoverContracts();
    TestFloraMeshDeformationProperties();
    TestChunkUnloadReloadDeterminism();
    puts("ecology system tests passed");
    return 0;
}
