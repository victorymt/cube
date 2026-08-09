#include "stellar.h"
#include "space_units.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void AssertNear(float actual, float expected, float tolerance)
{
    assert(fabsf(actual - expected) <= tolerance);
}

static void TestSolarProfile(void)
{
    StellarProfile solar = StellarSolarProfile();
    assert(solar.spectrum == SPECTRUM_YELLOW);
    assert(solar.stage == STELLAR_STAGE_MAIN_SEQUENCE);
    AssertNear(solar.massSolar, 1.0f, 0.0001f);
    AssertNear(solar.radiusSolar, 1.0f, 0.0001f);
    assert(fabs(solar.massKg / SPACE_UNITS_SOLAR_MASS_KG - 1.0) < 1e-12);
    assert(fabs(solar.radiusKm / SPACE_UNITS_SOLAR_RADIUS_KM - 1.0) < 1e-12);
    AssertNear(solar.temperatureK, 5772.0f, 0.1f);
    AssertNear(solar.luminositySolar, 1.0f, 0.0001f);
    AssertNear(solar.mainSequenceLifetimeGyr, 10.0f, 0.0001f);
}

static void TestMassRelations(void)
{
    StellarProfile low;
    StellarProfile high;
    assert(StellarProfileAtAge(0.20f, 1.0f, 1u, &low));
    assert(StellarProfileAtAge(2.00f, 0.1f, 2u, &high));
    assert(low.spectrum == SPECTRUM_RED_DWARF);
    assert(low.luminositySolar < 0.01f);
    assert(low.mainSequenceLifetimeGyr > 100.0f);
    assert(high.luminositySolar > 10.0f);
    assert(high.temperatureK > low.temperatureK);
    assert(high.mainSequenceLifetimeGyr < 2.0f);
}

static void TestGiantEvolution(void)
{
    StellarProfile mainSequence;
    assert(StellarProfileAtAge(1.50f, 0.1f, 3u, &mainSequence));
    float giantAge = 0.5f * (mainSequence.mainSequenceLifetimeGyr +
                             mainSequence.luminousLifetimeGyr);
    StellarProfile giant;
    assert(StellarProfileAtAge(1.50f, giantAge, 3u, &giant));
    assert(giant.stage == STELLAR_STAGE_RED_GIANT);
    assert(giant.spectrum == SPECTRUM_RED_GIANT);
    assert(giant.radiusSolar > mainSequence.radiusSolar * 20.0f);
    assert(giant.temperatureK < mainSequence.temperatureK);
    assert(!StellarProfileAtAge(1.50f, giant.luminousLifetimeGyr + 0.1f,
                                3u, &giant));
}

static void TestStefanBoltzmannConsistency(void)
{
    for (uint32_t seed = 0; seed < 5000u; seed++) {
        StellarProfile profile = StellarGenerate(seed);
        float temperatureRatio = profile.temperatureK / 5772.0f;
        float expectedLuminosity = profile.radiusSolar * profile.radiusSolar *
                                   powf(temperatureRatio, 4.0f);
        float tolerance = fmaxf(0.0001f, expectedLuminosity * 0.0002f);
        AssertNear(profile.luminositySolar, expectedLuminosity, tolerance);
        assert(profile.ageGyr <= profile.luminousLifetimeGyr);
        assert(profile.massSolar > 0.0f);
    }
}

static void TestInitialMassFunction(void)
{
    int lowMass = 0;
    int massive = 0;
    const int sampleCount = 100000;
    for (uint32_t seed = 0; seed < (uint32_t)sampleCount; seed++) {
        float mass = StellarSampleInitialMass(seed);
        assert(mass >= 0.08f && mass <= 50.0f);
        if (mass < 0.60f) lowMass++;
        if (mass >= 8.0f) massive++;
    }
    assert(lowMass > sampleCount * 75 / 100);
    assert(massive < sampleCount / 100);
}

static void TestPopulationDistribution(void)
{
    int counts[5] = { 0 };
    const int sampleCount = 100000;
    for (uint32_t seed = 0; seed < (uint32_t)sampleCount; seed++) {
        StellarProfile profile = StellarGenerate(seed);
        assert(profile.spectrum >= SPECTRUM_RED_DWARF &&
               profile.spectrum <= SPECTRUM_RED_GIANT);
        counts[profile.spectrum]++;
    }

    assert(counts[SPECTRUM_RED_DWARF] > sampleCount * 70 / 100);
    assert(counts[SPECTRUM_RED_DWARF] > counts[SPECTRUM_ORANGE]);
    assert(counts[SPECTRUM_YELLOW] < sampleCount * 15 / 100);
    assert(counts[SPECTRUM_BLUE_WHITE] < sampleCount * 2 / 100);
    assert(counts[SPECTRUM_RED_GIANT] < sampleCount * 2 / 100);
    printf("stellar population: red dwarf %.1f%%, orange %.1f%%, yellow %.1f%%, "
           "blue-white %.2f%%, red giant %.2f%%\n",
           100.0f * (float)counts[SPECTRUM_RED_DWARF] / (float)sampleCount,
           100.0f * (float)counts[SPECTRUM_ORANGE] / (float)sampleCount,
           100.0f * (float)counts[SPECTRUM_YELLOW] / (float)sampleCount,
           100.0f * (float)counts[SPECTRUM_BLUE_WHITE] / (float)sampleCount,
           100.0f * (float)counts[SPECTRUM_RED_GIANT] / (float)sampleCount);
}

int main(void)
{
    TestSolarProfile();
    TestMassRelations();
    TestGiantEvolution();
    TestStefanBoltzmannConsistency();
    TestInitialMassFunction();
    TestPopulationDistribution();
    puts("stellar tests passed");
    return 0;
}
