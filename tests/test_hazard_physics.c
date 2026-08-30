#include "world/hazard_physics.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void TestFireMassAndMoisture(void)
{
    HazardFireState fire = {
        .fuelKg = 100.0, .moistureKg = 1.0, .temperatureK = 800.0};
    HazardFireEnvironment env = HazardFireDefaultEnvironment();
    env.windSpeedMps = 10.0;
    HazardFireParameters params = HazardFireDefaultParameters();
    HazardFireFlux flux;
    assert(HazardFireStep(&fire, &params, &env, 10.0, &flux));
    assert(flux.burnedFuelKg > 0.0);
    assert(fire.fuelKg < 100.0);
    assert(fabs(flux.smokeProducedKg - flux.burnedFuelKg * params.smokeYield) <
           1e-9);
    assert(fire.smokeKg >= 0.0);

    HazardFireState wet = {
        .fuelKg = 100.0, .moistureKg = 100.0, .temperatureK = 800.0};
    assert(HazardFireStep(&wet, &params, &env, 10.0, &flux));
    assert(flux.burnedFuelKg == 0.0);
    assert(HazardFireSpreadPotential(&fire, &params, &env, 0.0) >=
           HazardFireSpreadPotential(&fire, &params, &env, 20.0));
}

static void TestSupercellFormationAndSteering(void)
{
    HazardSupercellState first = {
        .radiusMeters = 30.0};
    HazardSupercellState second = first;
    HazardSupercellEnvironment env = HazardSupercellDefaultEnvironment();
    HazardSupercellParameters params = HazardSupercellDefaultParameters();
    params.formationEnergyJ = 500.0;
    params.buoyancyEnergyPerSecond = 1000.0;
    HazardSupercellFlux fluxFirst;
    HazardSupercellFlux fluxSecond;
    assert(HazardSupercellStep(&first, &params, &env, 2.0, &fluxFirst));
    assert(HazardSupercellStep(&second, &params, &env, 2.0, &fluxSecond));
    assert(memcmp(&first, &second, sizeof(first)) == 0);
    assert(first.active);
    assert(first.pressureDeficitPa > 0.0);
    assert(first.positionX > 0.0);
    assert(fluxFirst.formed);
    assert(fabs(fluxFirst.pressureDeficitPa - first.pressureDeficitPa) < 1e-9);

    env.verticalShearMps = 0.0;
    assert(HazardSupercellStep(&first, &params, &env, 100.0, &fluxFirst));
    assert(!first.active);
    assert(fluxFirst.dissipated);
}

static void TestInvalidDurations(void)
{
    HazardFireState fire = { .fuelKg = 1.0, .temperatureK = 500.0 };
    HazardSupercellState cell = { .radiusMeters = 30.0 };
    assert(!HazardFireStep(&fire, NULL, NULL, NAN, NULL));
    assert(!HazardSupercellStep(&cell, NULL, NULL, -1.0, NULL));
    assert(!HazardFireStateValid(NULL));
    assert(!HazardSupercellStateValid(NULL));
}

int main(void)
{
    TestFireMassAndMoisture();
    TestSupercellFormationAndSteering();
    TestInvalidDurations();
    puts("hazard physics tests passed");
    return 0;
}
