#include "ecology_test_fixture.h"

#include "ecology.h"
#include "ecology_model.h"
#include "terrain.h"
#include "weather.h"
#include "weather_model.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint32_t ecologyPropertySeeds[] = {
    DEFAULT_WORLD_SEED, 1u, 0x12345678u, 0xdeadbeefu,
    0x31415926u, 0x9e3779b9u, 0xa5a5a5a5u, 0x6c8e9cf5u,
    0x2468ace0u, 0x13579bdfu, 0x0badc0deu, 0xfeedfaceu
};

static const SolarBodyStyle ecologyPropertyStyles[] = {
    SOLAR_STYLE_LAVA, SOLAR_STYLE_ICE, SOLAR_STYLE_DESERT,
    SOLAR_STYLE_GAS, SOLAR_STYLE_CRATER, SOLAR_STYLE_TEMPERATE
};

static const char *ecologyPropertyStyleNames[] = {
    "lava", "ice", "desert", "gas", "crater", "temperate"
};

enum { ECOLOGY_DISTRIBUTION_SEED_COUNT = 4096 };

typedef struct EcologyDistribution {
    int sampleCount;
    int originatedCount;
    int complexCount;
    int biomass[PLANET_BIOMASS_ANOMALOUS + 1];
    double originProbabilityTotal;
    double complexProbabilityTotal;
    double lifeDensityTotal;
    double temperatureTotal;
    double pressureTotal;
    double waterTotal;
    double iceTotal;
    double windTotal;
} EcologyDistribution;

enum { ECOLOGY_GENERATED_SYSTEM_RADIUS = 12 };

static float PropertyUnit(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (float)(*state & 0x00ffffffu) / 16777215.0f;
}

static float PropertySignedUnit(uint32_t *state)
{
    return PropertyUnit(state) * 2.0f - 1.0f;
}

static uint32_t EcologyDistributionSeed(int index)
{
    uint32_t value = (uint32_t)(index + 1) * 0x9e3779b9u;
    value ^= value >> 16;
    value *= 0x85ebca6bu;
    value ^= value >> 13;
    value *= 0xc2b2ae35u;
    value ^= value >> 16;
    return value == 0u ? DEFAULT_WORLD_SEED : value;
}

static void AssertUnit(float value)
{
    assert(isfinite(value));
    assert(value >= 0.0f && value <= 1.0f);
}

static void AssertSignedUnit(float value)
{
    assert(isfinite(value));
    assert(value >= -1.0f && value <= 1.0f);
}

static void AssertRateBetween(int count, int total,
                              float minimum, float maximum)
{
    assert(total > 0);
    float rate = (float)count / (float)total;
    assert(rate >= minimum && rate <= maximum);
}

static void AssertProfileValid(const PlanetEcologyProfile *profile,
                               SolarBodyStyle style)
{
    assert(profile);
    assert(profile->flora >= PLANET_FLORA_ALIEN_CANOPY &&
           profile->flora <= PLANET_FLORA_THERMAL_VENT);
    assert(profile->biomass >= PLANET_BIOMASS_BARREN &&
           profile->biomass <= PLANET_BIOMASS_ANOMALOUS);
    assert(profile->chemistry >= PLANET_CHEMISTRY_CARBON &&
           profile->chemistry <= PLANET_CHEMISTRY_SULFUR);
    assert(profile->bodyPlan >= PLANET_BODY_QUADRUPED &&
           profile->bodyPlan <= PLANET_BODY_COLONY);
    assert(profile->niche >= PLANET_NICHE_GRAZER &&
           profile->niche <= PLANET_NICHE_BIOLUMINESCENT_COLONY);
    AssertUnit(profile->floraDensity);
    AssertUnit(profile->faunaDensity);
    AssertUnit(profile->lifeDensity);
    AssertUnit(profile->lifeOriginProbability);
    AssertUnit(profile->complexLifeProbability);
    AssertUnit(profile->evolutionProgress);
    AssertUnit(profile->bodyArmor);
    AssertUnit(profile->movementSpeed);
    AssertUnit(profile->temperament);
    assert(isfinite(profile->planetAgeGyr) && profile->planetAgeGyr >= 0.0f);
    assert(isfinite(profile->organismScale));
    assert(profile->organismScale >= 0.48f &&
           profile->organismScale <= 2.20f);
    assert(profile->limbCount >= 0 && profile->limbCount <= 6);
    if (profile->biomass == PLANET_BIOMASS_BARREN) {
        assert(profile->floraDensity == 0.0f);
        assert(profile->faunaDensity == 0.0f);
    }
    if (profile->biomass == PLANET_BIOMASS_MICROBIAL) {
        assert(profile->faunaDensity == 0.0f);
    }
    if (!profile->lifeOriginated) {
        assert(profile->lifeDensity == 0.0f);
        assert(profile->biomass == PLANET_BIOMASS_BARREN);
    }
    if (!profile->hasComplexLife) {
        assert(profile->faunaDensity == 0.0f);
    }
    if (profile->biomass == PLANET_BIOMASS_FUNGAL ||
        profile->biomass == PLANET_BIOMASS_CRYSTALLINE ||
        profile->biomass == PLANET_BIOMASS_LUSH ||
        profile->biomass == PLANET_BIOMASS_ANOMALOUS) {
        assert(profile->hasComplexLife);
    }
    if (style == SOLAR_STYLE_GAS) {
        assert(profile->biomass == PLANET_BIOMASS_BARREN);
        assert(profile->floraDensity == 0.0f);
        assert(profile->faunaDensity == 0.0f);
    }
}

static void RecordEcologyDistribution(
    EcologyDistribution *distribution, const PlanetProfile *planet,
    const PlanetEcologyProfile *ecology)
{
    assert(distribution);
    assert(planet);
    assert(ecology);
    distribution->sampleCount++;
    distribution->originatedCount += ecology->lifeOriginated ? 1 : 0;
    distribution->complexCount += ecology->hasComplexLife ? 1 : 0;
    distribution->biomass[ecology->biomass]++;
    distribution->originProbabilityTotal += ecology->lifeOriginProbability;
    distribution->complexProbabilityTotal += ecology->complexLifeProbability;
    distribution->lifeDensityTotal += ecology->lifeDensity;
    distribution->temperatureTotal += planet->equilibriumTempK;
    distribution->pressureTotal += planet->surfacePressureAtm;
    distribution->waterTotal += planet->oceanCoverage;
    distribution->iceTotal += planet->iceCoverage;
    distribution->windTotal += planet->windStrength;
}

static int EcologyDistributionNonBarren(
    const EcologyDistribution *distribution)
{
    assert(distribution);
    return distribution->sampleCount -
           distribution->biomass[PLANET_BIOMASS_BARREN];
}

static void PrintEcologyDistribution(
    const char *group, const char *name,
    const EcologyDistribution *distribution)
{
    assert(distribution);
    assert(distribution->sampleCount > 0);
    double divisor = (double)distribution->sampleCount;
    printf("%s %-9s samples=%d origin=%d complex=%d "
           "mean-prob=%.3f/%.3f mean-density=%.3f "
           "climate=%.0fK/%.2fatm/%.2fwater/%.2fice/%.2fwind "
           "barren=%d microbial=%d fungal=%d crystalline=%d "
           "lush=%d anomalous=%d\n",
           group, name, distribution->sampleCount,
           distribution->originatedCount, distribution->complexCount,
           distribution->originProbabilityTotal / divisor,
           distribution->complexProbabilityTotal / divisor,
           distribution->lifeDensityTotal / divisor,
           distribution->temperatureTotal / divisor,
           distribution->pressureTotal / divisor,
           distribution->waterTotal / divisor,
           distribution->iceTotal / divisor,
           distribution->windTotal / divisor,
           distribution->biomass[PLANET_BIOMASS_BARREN],
           distribution->biomass[PLANET_BIOMASS_MICROBIAL],
           distribution->biomass[PLANET_BIOMASS_FUNGAL],
           distribution->biomass[PLANET_BIOMASS_CRYSTALLINE],
           distribution->biomass[PLANET_BIOMASS_LUSH],
           distribution->biomass[PLANET_BIOMASS_ANOMALOUS]);
}

static void AssertGeneratedStyleTargets(
    SolarBodyStyle style, const EcologyDistribution *distribution)
{
    int nonBarren = EcologyDistributionNonBarren(distribution);
    switch (style) {
    case SOLAR_STYLE_LAVA:
        AssertRateBetween(distribution->originatedCount,
                          distribution->sampleCount, 0.0f, 0.03f);
        AssertRateBetween(nonBarren, distribution->sampleCount,
                          0.0f, 0.02f);
        AssertRateBetween(distribution->complexCount,
                          distribution->sampleCount, 0.0f, 0.01f);
        break;
    case SOLAR_STYLE_ICE:
        AssertRateBetween(nonBarren, distribution->sampleCount,
                          0.02f, 0.07f);
        AssertRateBetween(distribution->complexCount,
                          distribution->sampleCount, 0.0005f, 0.005f);
        break;
    case SOLAR_STYLE_DESERT:
        AssertRateBetween(nonBarren, distribution->sampleCount,
                          0.03f, 0.12f);
        AssertRateBetween(distribution->complexCount,
                          distribution->sampleCount, 0.0f, 0.02f);
        break;
    case SOLAR_STYLE_GAS:
        assert(distribution->originatedCount == 0);
        assert(distribution->complexCount == 0);
        assert(nonBarren == 0);
        break;
    case SOLAR_STYLE_CRATER:
        AssertRateBetween(distribution->originatedCount,
                          distribution->sampleCount, 0.0005f, 0.01f);
        AssertRateBetween(nonBarren, distribution->sampleCount,
                          0.0f, 0.005f);
        AssertRateBetween(distribution->complexCount,
                          distribution->sampleCount, 0.0f, 0.005f);
        break;
    case SOLAR_STYLE_TEMPERATE:
        AssertRateBetween(distribution->originatedCount,
                          distribution->sampleCount, 0.15f, 0.35f);
        AssertRateBetween(distribution->complexCount,
                          distribution->sampleCount, 0.02f, 0.08f);
        break;
    case SOLAR_STYLE_SUN:
    default:
        assert(false);
        break;
    }
}

static void AssertEnvironmentValid(const PlanetLocalEnvironment *environment)
{
    assert(environment);
    assert(isfinite(environment->meanTemperatureK) &&
           environment->meanTemperatureK > 0.0f);
    assert(isfinite(environment->currentTemperatureK) &&
           environment->currentTemperatureK > 0.0f);
    assert(isfinite(environment->seasonalAmplitudeK) &&
           environment->seasonalAmplitudeK >= 0.0f);
    AssertUnit(environment->liquidWaterAccess);
    AssertUnit(environment->soilMoisture);
    AssertUnit(environment->meanPrecipitation);
    AssertUnit(environment->precipitationRate);
    AssertUnit(environment->meanUsableLight);
    AssertUnit(environment->currentUsableLight);
    AssertUnit(environment->stormExposure);
    AssertUnit(environment->currentStorm);
    AssertUnit(environment->elevation);
    AssertUnit(environment->slope);
    AssertUnit(environment->shelter);
    AssertUnit(environment->biomeSupport);
    AssertUnit(environment->disturbance);
    AssertUnit(environment->radiationExposure);
    AssertUnit(environment->ejectaExposure);
}

static void AssertSuitabilityValid(const PlanetEcologySuitability *suitability)
{
    assert(suitability);
    AssertUnit(suitability->carryingCapacity);
    AssertUnit(suitability->floraCapacity);
    AssertUnit(suitability->faunaCapacity);
    AssertUnit(suitability->floraActivity);
    AssertUnit(suitability->faunaActivity);
    AssertUnit(suitability->waterScore);
    AssertUnit(suitability->temperatureScore);
    AssertUnit(suitability->lightScore);
    AssertUnit(suitability->stormScore);
    AssertUnit(suitability->terrainScore);
    AssertUnit(suitability->seasonScore);
    AssertUnit(suitability->radiationScore);
    assert(suitability->limitingFactor >= PLANET_ECOLOGY_LIMIT_NONE &&
           suitability->limitingFactor <= PLANET_ECOLOGY_LIMIT_RADIATION);
}

static void AssertPopulationValid(const PlanetRegionalPopulation *population)
{
    assert(population);
    AssertUnit(population->floraDensity);
    AssertUnit(population->faunaDensity);
    AssertUnit(population->floraCarryingCapacity);
    AssertUnit(population->faunaCarryingCapacity);
    AssertUnit(population->seasonalMemory);
    AssertUnit(population->radiationMemory);
}

static void AssertMigrationValid(
    const PlanetPopulationMigrationState *migration)
{
    assert(migration);
    AssertSignedUnit(migration->floraNet);
    AssertSignedUnit(migration->faunaNet);
    AssertSignedUnit(migration->floraFlowX);
    AssertSignedUnit(migration->floraFlowZ);
    AssertSignedUnit(migration->faunaFlowX);
    AssertSignedUnit(migration->faunaFlowZ);
}

static void AssertLocalValid(const PlanetLocalEcology *local)
{
    assert(local);
    AssertEnvironmentValid(&local->environment);
    AssertSuitabilityValid(&local->suitability);
    AssertPopulationValid(&local->population);
    AssertMigrationValid(&local->migration);
}

static void TestInvalidPlanetClimateInputsStayFinite(void)
{
    const uint32_t seed = 0x4d3c2b1au;
    EcologyTestSetSeed(seed);
    EcologyTestActivatePlanetStyle(seed, 0, 0, SOLAR_STYLE_TEMPERATE);
    const PlanetProfile baseline = *PlanetWorldProfile();

#define ASSERT_INVALID_PLANET_FIELD(field, value) do {                    \
    PlanetProfile candidate = baseline;                                    \
    candidate.field = (value);                                             \
    PlanetEcologyProfile first = PlanetEcologyProfileForPlanet(            \
        &candidate, seed, false);                                          \
    PlanetEcologyProfile second = PlanetEcologyProfileForPlanet(           \
        &candidate, seed, false);                                          \
    AssertProfileValid(&first, candidate.style);                            \
    assert(memcmp(&first, &second, sizeof(first)) == 0);                   \
} while (0)

#define ASSERT_INVALID_PLANET_FIELD_ALL(field) do {                       \
    ASSERT_INVALID_PLANET_FIELD(field, NAN);                              \
    ASSERT_INVALID_PLANET_FIELD(field, INFINITY);                         \
    ASSERT_INVALID_PLANET_FIELD(field, -INFINITY);                        \
} while (0)

    ASSERT_INVALID_PLANET_FIELD_ALL(equilibriumTempK);
    ASSERT_INVALID_PLANET_FIELD_ALL(surfacePressureAtm);
    ASSERT_INVALID_PLANET_FIELD_ALL(atmosphereDensity);
    ASSERT_INVALID_PLANET_FIELD_ALL(oceanCoverage);
    ASSERT_INVALID_PLANET_FIELD_ALL(iceCoverage);
    ASSERT_INVALID_PLANET_FIELD_ALL(surfaceGravity);
    ASSERT_INVALID_PLANET_FIELD_ALL(ageGyr);
    ASSERT_INVALID_PLANET_FIELD_ALL(windStrength);

#undef ASSERT_INVALID_PLANET_FIELD_ALL
#undef ASSERT_INVALID_PLANET_FIELD
}

static void TestGeneratedEcologyBoundsAndLocalProperties(void)
{
    int profileCount = 0;
    int localCount = 0;

    for (size_t seedIndex = 0;
         seedIndex < sizeof(ecologyPropertySeeds) /
                     sizeof(ecologyPropertySeeds[0]);
         seedIndex++) {
        uint32_t seed = ecologyPropertySeeds[seedIndex];
        for (size_t styleIndex = 0;
             styleIndex < sizeof(ecologyPropertyStyles) /
                          sizeof(ecologyPropertyStyles[0]);
             styleIndex++) {
            SolarBodyStyle style = ecologyPropertyStyles[styleIndex];
            EcologyTestSetSeed(seed);
            EcologyTestActivatePlanetStyle(
                seed, (int)seedIndex * 97 - 431,
                (int)styleIndex * 113 - 287, style);
            PlanetEcologyResetState();

            PlanetEcologyProfile profile = PlanetEcologyCurrent();
            PlanetEcologyProfile derived = PlanetEcologyProfileForPlanet(
                PlanetWorldProfile(), PlanetWorldSeed(),
                PlanetWorldIsDarkSide());
            assert(memcmp(&profile, &derived, sizeof(profile)) == 0);
            AssertProfileValid(&profile, style);
            profileCount++;

            for (int cell = 0; cell < 8; cell++) {
                int x = cell * 47 + (int)(seed & 31u) - 320;
                int z = (cell * cell * 31 + (int)(seed >> 27)) % 640 - 320;
                PlanetEcologySuitability staticFirst =
                    PlanetEcologyStaticSuitabilityAt(x, z);
                PlanetEcologySuitability staticSecond =
                    PlanetEcologyStaticSuitabilityAt(x, z);
                assert(memcmp(&staticFirst, &staticSecond,
                              sizeof(staticFirst)) == 0);
                AssertSuitabilityValid(&staticFirst);

                PlanetLocalEcology local = PlanetEcologyLocalAt(
                    x, z, 0.18f + (float)cell * 0.10f);
                AssertLocalValid(&local);
                assert(local.suitability.floraCapacity <=
                       profile.floraDensity + 0.000001f);
                assert(local.suitability.faunaCapacity <=
                       profile.faunaDensity + 0.000001f);
                if (profile.floraDensity == 0.0f) {
                    assert(local.suitability.floraCapacity == 0.0f);
                }
                if (profile.faunaDensity == 0.0f) {
                    assert(local.suitability.faunaCapacity == 0.0f);
                }
                localCount++;
            }
        }
    }

    assert(profileCount ==
           (int)(sizeof(ecologyPropertySeeds) /
                 sizeof(ecologyPropertySeeds[0]) *
                 sizeof(ecologyPropertyStyles) /
                 sizeof(ecologyPropertyStyles[0])));
    assert(localCount == profileCount * 8);
}

static void TestGeneratedEcologyDistribution(void)
{
    EcologyDistribution distributions[
        sizeof(ecologyPropertyStyles) / sizeof(ecologyPropertyStyles[0])
    ] = { 0 };
    FILE *fixture = tmpfile();
    assert(fixture);

    int nonBarrenTotal = 0;
    int complexTotal = 0;
    for (size_t styleIndex = 0;
         styleIndex < sizeof(ecologyPropertyStyles) /
                      sizeof(ecologyPropertyStyles[0]);
         styleIndex++) {
        SolarBodyStyle style = ecologyPropertyStyles[styleIndex];
        EcologyDistribution *distribution = &distributions[styleIndex];

        for (int seedIndex = 0;
             seedIndex < ECOLOGY_DISTRIBUTION_SEED_COUNT;
             seedIndex++) {
            uint32_t seed = EcologyDistributionSeed(seedIndex);
            int originX = (int)(seed & 0x7ffu) - 1024;
            int originZ = (int)((seed >> 11) & 0x7ffu) - 1024;
            EcologyTestSetSeed(seed);
            EcologyTestActivatePlanetStyleWithFile(
                fixture, seed, originX, originZ, style);
            PlanetEcologyResetState();

            PlanetEcologyProfile profile = PlanetEcologyCurrent();
            const PlanetProfile *planet = PlanetWorldProfile();
            PlanetEcologyProfile derived = PlanetEcologyProfileForPlanet(
                planet, PlanetWorldSeed(), PlanetWorldIsDarkSide());
            assert(memcmp(&profile, &derived, sizeof(profile)) == 0);
            AssertProfileValid(&profile, style);
            assert(!profile.hasComplexLife || profile.lifeOriginated);
            RecordEcologyDistribution(distribution, planet, &profile);
        }

        int biomassTotal = 0;
        for (int biomass = PLANET_BIOMASS_BARREN;
             biomass <= PLANET_BIOMASS_ANOMALOUS;
             biomass++) {
            biomassTotal += distribution->biomass[biomass];
        }
        assert(biomassTotal == ECOLOGY_DISTRIBUTION_SEED_COUNT);
        assert(distribution->sampleCount == ECOLOGY_DISTRIBUTION_SEED_COUNT);
        int nonBarren = EcologyDistributionNonBarren(distribution);
        nonBarrenTotal += nonBarren;
        complexTotal += distribution->complexCount;
        if (style == SOLAR_STYLE_LAVA) {
            AssertRateBetween(nonBarren, ECOLOGY_DISTRIBUTION_SEED_COUNT,
                              0.005f, 0.03f);
            AssertRateBetween(distribution->complexCount,
                              ECOLOGY_DISTRIBUTION_SEED_COUNT,
                              0.0f, 0.005f);
        } else if (style == SOLAR_STYLE_ICE ||
                   style == SOLAR_STYLE_DESERT) {
            AssertRateBetween(nonBarren, ECOLOGY_DISTRIBUTION_SEED_COUNT,
                              0.05f, 0.15f);
        } else if (style == SOLAR_STYLE_GAS) {
            assert(distribution->originatedCount == 0);
            assert(distribution->complexCount == 0);
            assert(nonBarren == 0);
        } else if (style == SOLAR_STYLE_CRATER) {
            AssertRateBetween(nonBarren, ECOLOGY_DISTRIBUTION_SEED_COUNT,
                              0.0025f, 0.03f);
            AssertRateBetween(distribution->complexCount,
                              ECOLOGY_DISTRIBUTION_SEED_COUNT,
                              0.0f, 0.005f);
        } else if (style == SOLAR_STYLE_TEMPERATE) {
            AssertRateBetween(distribution->originatedCount,
                              ECOLOGY_DISTRIBUTION_SEED_COUNT,
                              0.25f, 0.45f);
            AssertRateBetween(distribution->complexCount,
                              ECOLOGY_DISTRIBUTION_SEED_COUNT,
                              0.05f, 0.12f);
        }

        PrintEcologyDistribution(
            "ecology template", ecologyPropertyStyleNames[styleIndex],
            distribution);
    }

    fclose(fixture);
    assert(nonBarrenTotal > 0);
    AssertRateBetween(
        complexTotal,
        ECOLOGY_DISTRIBUTION_SEED_COUNT *
            (int)(sizeof(ecologyPropertyStyles) /
                  sizeof(ecologyPropertyStyles[0])),
        0.01f, 0.04f);
}

static void TestGeneratedSolarEcologyDistribution(void)
{
    EcologyDistribution styles[SOLAR_STYLE_TEMPERATE + 1] = { 0 };
    EcologyDistribution multiplicities[4] = { 0 };
    int systemCounts[4] = { 0 };
    int systemCount = 0;
    int planetCount = 0;

    for (size_t seedIndex = 0;
         seedIndex < sizeof(ecologyPropertySeeds) /
                     sizeof(ecologyPropertySeeds[0]);
         seedIndex++) {
        EcologyTestSetSeed(ecologyPropertySeeds[seedIndex]);
        for (int anchorX = -ECOLOGY_GENERATED_SYSTEM_RADIUS;
             anchorX <= ECOLOGY_GENERATED_SYSTEM_RADIUS; anchorX++) {
            for (int anchorZ = -ECOLOGY_GENERATED_SYSTEM_RADIUS;
                 anchorZ <= ECOLOGY_GENERATED_SYSTEM_RADIUS; anchorZ++) {
                SolarSystemDef system;
                if (!StarSystemAt(anchorX, anchorZ, &system)) continue;

                SolarStellarBody stellarBodies[3];
                int stellarCount = SolarSystemStellarBodiesAtTime(
                    &system, 0.0, stellarBodies, 3);
                assert(stellarCount >= 1 && stellarCount <= 3);
                systemCounts[stellarCount]++;
                systemCount++;

                for (int planetIndex = 0;
                     planetIndex < system.planetCount; planetIndex++) {
                    PlanetProfile planet = SolarPlanetProfile(
                        &system, planetIndex);
                    PlanetProfile repeatedPlanet = SolarPlanetProfile(
                        &system, planetIndex);
                    assert(memcmp(&planet, &repeatedPlanet,
                                  sizeof(planet)) == 0);
                    assert(planet.style >= SOLAR_STYLE_LAVA &&
                           planet.style <= SOLAR_STYLE_TEMPERATE);

                    PlanetEcologyProfile ecology =
                        PlanetEcologyProfileForPlanet(
                            &planet, planet.seed, false);
                    PlanetEcologyProfile repeatedEcology =
                        PlanetEcologyProfileForPlanet(
                            &planet, planet.seed, false);
                    assert(memcmp(&ecology, &repeatedEcology,
                                  sizeof(ecology)) == 0);
                    AssertProfileValid(&ecology, planet.style);
                    assert(!ecology.hasComplexLife ||
                           ecology.lifeOriginated);
                    if (!planet.hasSolidSurface) {
                        assert(ecology.biomass == PLANET_BIOMASS_BARREN);
                        assert(!ecology.lifeOriginated);
                    }

                    RecordEcologyDistribution(
                        &styles[planet.style], &planet, &ecology);
                    RecordEcologyDistribution(
                        &multiplicities[stellarCount], &planet, &ecology);
                    planetCount++;
                }
            }
        }
    }

    assert(systemCount > 4096);
    assert(planetCount > systemCount);
    for (int stellarCount = 1; stellarCount <= 3; stellarCount++) {
        assert(systemCounts[stellarCount] > 0);
        assert(multiplicities[stellarCount].sampleCount > 0);
        AssertRateBetween(
            EcologyDistributionNonBarren(&multiplicities[stellarCount]),
            multiplicities[stellarCount].sampleCount, 0.01f, 0.06f);
    }
    AssertRateBetween(systemCounts[1], systemCount, 0.60f, 0.80f);
    AssertRateBetween(systemCounts[2], systemCount, 0.15f, 0.35f);
    AssertRateBetween(systemCounts[3], systemCount, 0.02f, 0.08f);

    int nonBarrenTotal = 0;
    int complexTotal = 0;
    for (size_t styleIndex = 0;
         styleIndex < sizeof(ecologyPropertyStyles) /
                      sizeof(ecologyPropertyStyles[0]);
         styleIndex++) {
        SolarBodyStyle style = ecologyPropertyStyles[styleIndex];
        assert(styles[style].sampleCount > 0);
        AssertGeneratedStyleTargets(style, &styles[style]);
        nonBarrenTotal += EcologyDistributionNonBarren(&styles[style]);
        complexTotal += styles[style].complexCount;
        PrintEcologyDistribution(
            "ecology systems ", ecologyPropertyStyleNames[styleIndex],
            &styles[style]);
    }
    AssertRateBetween(nonBarrenTotal, planetCount, 0.02f, 0.06f);
    AssertRateBetween(complexTotal, planetCount, 0.0005f, 0.005f);

    AssertRateBetween(styles[SOLAR_STYLE_LAVA].sampleCount,
                      planetCount, 0.005f, 0.03f);
    AssertRateBetween(styles[SOLAR_STYLE_ICE].sampleCount,
                      planetCount, 0.70f, 0.90f);
    AssertRateBetween(styles[SOLAR_STYLE_DESERT].sampleCount,
                      planetCount, 0.005f, 0.04f);
    AssertRateBetween(styles[SOLAR_STYLE_GAS].sampleCount,
                      planetCount, 0.04f, 0.12f);
    AssertRateBetween(styles[SOLAR_STYLE_CRATER].sampleCount,
                      planetCount, 0.04f, 0.12f);
    AssertRateBetween(styles[SOLAR_STYLE_TEMPERATE].sampleCount,
                      planetCount, 0.005f, 0.03f);

    static const char *multiplicityNames[] = {
        "none", "single", "binary", "triple"
    };
    for (int stellarCount = 1; stellarCount <= 3; stellarCount++) {
        PrintEcologyDistribution(
            "ecology stars   ", multiplicityNames[stellarCount],
            &multiplicities[stellarCount]);
    }
    printf("ecology generated systems=%d planets=%d multiplicity=%d/%d/%d\n",
           systemCount, planetCount, systemCounts[1], systemCounts[2],
           systemCounts[3]);
}

static void TestRandomizedCausalControls(void)
{
    uint32_t state = 0x17a3c5e9u;
    for (int sample = 0; sample < 12000; sample++) {
        PlanetEcologyTraits traits = {
            .preferredTemperatureK = 250.0f + PropertyUnit(&state) * 100.0f,
            .temperatureToleranceK = 18.0f + PropertyUnit(&state) * 52.0f,
            .waterDependence = PropertyUnit(&state),
            .lightDependence = PropertyUnit(&state),
            .stormResistance = PropertyUnit(&state),
            .altitudeTolerance = PropertyUnit(&state),
            .slopeTolerance = PropertyUnit(&state),
            .foodWebDependence = PropertyUnit(&state),
            .nocturnalFraction = PropertyUnit(&state)
        };
        PlanetLocalEnvironment base = {
            .meanTemperatureK = 245.0f + PropertyUnit(&state) * 115.0f,
            .currentTemperatureK = 245.0f + PropertyUnit(&state) * 115.0f,
            .seasonalAmplitudeK = PropertyUnit(&state) * 90.0f,
            .liquidWaterAccess = PropertyUnit(&state),
            .soilMoisture = PropertyUnit(&state),
            .meanPrecipitation = PropertyUnit(&state),
            .precipitationRate = PropertyUnit(&state),
            .meanUsableLight = PropertyUnit(&state),
            .currentUsableLight = PropertyUnit(&state),
            .stormExposure = PropertyUnit(&state),
            .currentStorm = PropertyUnit(&state),
            .elevation = PropertyUnit(&state),
            .slope = PropertyUnit(&state),
            .shelter = PropertyUnit(&state),
            .biomeSupport = 0.20f + PropertyUnit(&state) * 0.80f
        };
        float floraPotential = 0.05f + PropertyUnit(&state) * 0.95f;
        float faunaPotential = 0.05f + PropertyUnit(&state) * 0.95f;
        PlanetEcologySuitability baseline = PlanetEcologyEvaluateLocal(
            &base, &traits, floraPotential, faunaPotential);
        AssertSuitabilityValid(&baseline);

        PlanetLocalEnvironment water = base;
        water.liquidWaterAccess = 1.0f;
        water.soilMoisture = 1.0f;
        water.meanPrecipitation = 1.0f;
        PlanetEcologySuitability waterResult = PlanetEcologyEvaluateLocal(
            &water, &traits, floraPotential, faunaPotential);
        AssertSuitabilityValid(&waterResult);
        assert(waterResult.waterScore + 0.000001f >= baseline.waterScore);
        assert(waterResult.carryingCapacity + 0.000001f >=
               baseline.carryingCapacity);

        PlanetLocalEnvironment light = base;
        light.meanUsableLight = 1.0f;
        PlanetEcologySuitability lightResult = PlanetEcologyEvaluateLocal(
            &light, &traits, floraPotential, faunaPotential);
        AssertSuitabilityValid(&lightResult);
        assert(lightResult.lightScore + 0.000001f >= baseline.lightScore);
        assert(lightResult.carryingCapacity + 0.000001f >=
               baseline.carryingCapacity);

        PlanetLocalEnvironment calm = base;
        calm.stormExposure = 0.0f;
        PlanetEcologySuitability calmResult = PlanetEcologyEvaluateLocal(
            &calm, &traits, floraPotential, faunaPotential);
        AssertSuitabilityValid(&calmResult);
        assert(calmResult.stormScore + 0.000001f >= baseline.stormScore);
        assert(calmResult.carryingCapacity + 0.000001f >=
               baseline.carryingCapacity);

        PlanetLocalEnvironment gentleTerrain = base;
        gentleTerrain.elevation = 0.0f;
        gentleTerrain.slope = 0.0f;
        gentleTerrain.shelter = 1.0f;
        gentleTerrain.biomeSupport = 1.0f;
        PlanetEcologySuitability terrainResult = PlanetEcologyEvaluateLocal(
            &gentleTerrain, &traits, floraPotential, faunaPotential);
        AssertSuitabilityValid(&terrainResult);
        assert(terrainResult.terrainScore + 0.000001f >=
               baseline.terrainScore);
        assert(terrainResult.carryingCapacity + 0.000001f >=
               baseline.carryingCapacity);

        PlanetLocalEnvironment thermal = base;
        thermal.meanTemperatureK = traits.preferredTemperatureK;
        thermal.currentTemperatureK = traits.preferredTemperatureK;
        thermal.seasonalAmplitudeK = 0.0f;
        PlanetEcologySuitability thermalResult = PlanetEcologyEvaluateLocal(
            &thermal, &traits, floraPotential, faunaPotential);
        AssertSuitabilityValid(&thermalResult);
        assert(thermalResult.temperatureScore + 0.000001f >=
               baseline.temperatureScore);
        assert(thermalResult.seasonScore + 0.000001f >=
               baseline.seasonScore);
        assert(thermalResult.carryingCapacity + 0.000001f >=
               baseline.carryingCapacity);
    }
}

static void TestRandomizedPopulationAndMigration(void)
{
    uint32_t state = 0x9c31e7a5u;
    for (int sample = 0; sample < 12000; sample++) {
        PlanetPopulationInput input = {
            .floraCapacity = PropertyUnit(&state),
            .faunaCapacity = PropertyUnit(&state),
            .floraActivity = PropertyUnit(&state),
            .faunaActivity = PropertyUnit(&state)
        };
        PlanetRegionalPopulation first = PlanetPopulationInitialize(
            &input, PropertyUnit(&state), PropertyUnit(&state));
        PlanetRegionalPopulation second = PlanetPopulationInitialize(
            &input, PropertyUnit(&state), PropertyUnit(&state));
        AssertPopulationValid(&first);
        AssertPopulationValid(&second);

        PlanetMigrationHabitat firstHabitat = {
            .floraSuitability = PropertyUnit(&state),
            .faunaSuitability = PropertyUnit(&state),
            .stormPressure = PropertyUnit(&state)
        };
        PlanetMigrationHabitat secondHabitat = {
            .floraSuitability = PropertyUnit(&state),
            .faunaSuitability = PropertyUnit(&state),
            .stormPressure = PropertyUnit(&state)
        };
        float floraBefore = first.floraDensity + second.floraDensity;
        float faunaBefore = first.faunaDensity + second.faunaDensity;
        PlanetPopulationMigrationFlux flux =
            PlanetPopulationMigrationBetween(
                &first, &firstHabitat, &second, &secondHabitat,
                PropertySignedUnit(&state),
                0.1 + (double)PropertyUnit(&state) * 1200.0);
        AssertSignedUnit(flux.flora);
        AssertSignedUnit(flux.fauna);

        first.floraDensity = fminf(fmaxf(
            first.floraDensity - flux.flora, 0.0f), 1.0f);
        second.floraDensity = fminf(fmaxf(
            second.floraDensity + flux.flora, 0.0f), 1.0f);
        first.faunaDensity = fminf(fmaxf(
            first.faunaDensity - flux.fauna, 0.0f), 1.0f);
        second.faunaDensity = fminf(fmaxf(
            second.faunaDensity + flux.fauna, 0.0f), 1.0f);
        AssertPopulationValid(&first);
        AssertPopulationValid(&second);
        assert(fabsf((first.floraDensity + second.floraDensity) -
                     floraBefore) < 0.00001f);
        assert(fabsf((first.faunaDensity + second.faunaDensity) -
                     faunaBefore) < 0.00001f);

        PlanetPopulationAdvance(&first, &input,
                                (double)PropertyUnit(&state) * 900.0);
        PlanetPopulationApplyDisturbance(
            &first, PropertyUnit(&state), PropertyUnit(&state),
            (double)PropertyUnit(&state) * 1600.0);
        AssertPopulationValid(&first);
    }
}

static void TestSaveLoadReplayAcrossSeeds(void)
{
    const int caseCount = 12;
    for (int index = 0; index < caseCount; index++) {
        uint32_t seed = ecologyPropertySeeds[index];
        SolarBodyStyle style = ecologyPropertyStyles[index %
            (int)(sizeof(ecologyPropertyStyles) /
                  sizeof(ecologyPropertyStyles[0]))];
        EcologyTestSetSeed(seed);
        EcologyTestActivatePlanetStyle(seed, index * 43 - 240,
                                       index * 67 - 310, style);
        PlanetEcologyResetState();
        PlanetLocalEcology before = PlanetEcologyLocalAt(
            index * 29 - 140, index * 17 - 90, 0.73f);
        AssertLocalValid(&before);

        FILE *file = tmpfile();
        assert(file);
        EcologyTestSaveSimulationState(file);
        SpaceAdvanceTime(37.5f);
        PlanetLocalEcology continued = PlanetEcologyLocalAt(
            index * 29 - 140, index * 17 - 90, 0.73f);
        EcologyTestLoadSimulationState(file);
        SpaceAdvanceTime(37.5f);
        PlanetLocalEcology replay = PlanetEcologyLocalAt(
            index * 29 - 140, index * 17 - 90, 0.73f);
        assert(memcmp(&continued, &replay, sizeof(continued)) == 0);
        fclose(file);
    }
}

int main(void)
{
    TestInvalidPlanetClimateInputsStayFinite();
    TestGeneratedEcologyBoundsAndLocalProperties();
    TestGeneratedEcologyDistribution();
    TestGeneratedSolarEcologyDistribution();
    TestRandomizedCausalControls();
    TestRandomizedPopulationAndMigration();
    TestSaveLoadReplayAcrossSeeds();
    puts("ecology property tests passed");
    return 0;
}
