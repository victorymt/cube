#include "planet_observation.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static PlanetLightState LightAtAltitude(float altitudeDeg)
{
    float altitude = altitudeDeg * DEG2RAD;
    float incidence = fmaxf(sinf(altitude), 0.0f);
    PlanetLightState light = { 0 };
    light.sunDirection = (Vector3){ cosf(altitude), sinf(altitude), 0.0f };
    light.incidentIrradiance = incidence;
    light.daylight = 1.0f - expf(-incidence * 1.45f);
    light.sunset = incidence > 0.0f ? powf(1.0f - incidence, 2.0f) : 0.0f;
    light.dayLengthFraction = 0.5f;
    light.sourceCount = 1;
    return light;
}

static void TestDayTwilightNightTransitions(void)
{
    PlanetLightState noonLight = LightAtAltitude(65.0f);
    PlanetLightState civilLight = LightAtAltitude(-3.0f);
    PlanetLightState nauticalLight = LightAtAltitude(-9.0f);
    PlanetLightState nightLight = LightAtAltitude(-25.0f);
    PlanetObservationState noon = PlanetObservationEvaluate(&noonLight, 0.95f, 0.55f, 1.0f);
    PlanetObservationState civil = PlanetObservationEvaluate(&civilLight, 0.95f, 0.55f, 1.0f);
    PlanetObservationState nautical = PlanetObservationEvaluate(&nauticalLight, 0.95f, 0.55f, 1.0f);
    PlanetObservationState night = PlanetObservationEvaluate(&nightLight, 0.95f, 0.55f, 1.0f);

    assert(noon.phase == PLANET_OBSERVATION_DAY);
    assert(civil.phase == PLANET_OBSERVATION_CIVIL_TWILIGHT);
    assert(nautical.phase == PLANET_OBSERVATION_NAUTICAL_TWILIGHT);
    assert(night.phase == PLANET_OBSERVATION_NIGHT);
    assert(noon.skyBrightness > civil.skyBrightness);
    assert(civil.skyBrightness > night.skyBrightness);
    assert(noon.starVisibility < civil.starVisibility);
    assert(civil.starVisibility < night.starVisibility);
    assert(civil.horizonWarmth > night.horizonWarmth);
}

static void TestAtmosphereAndEclipseContrast(void)
{
    PlanetLightState clearLight = LightAtAltitude(55.0f);
    PlanetLightState eclipseLight = clearLight;
    eclipseLight.daylight *= 0.08f;
    eclipseLight.incidentIrradiance *= 0.08f;
    eclipseLight.eclipse = 0.92f;
    eclipseLight.specialEclipse = true;

    PlanetObservationState atmosphere = PlanetObservationEvaluate(
        &clearLight, 0.95f, 0.55f, 1.0f);
    PlanetObservationState airless = PlanetObservationEvaluate(
        &clearLight, 0.0f, 0.0f, 1.0f);
    PlanetObservationState eclipse = PlanetObservationEvaluate(
        &eclipseLight, 0.95f, 0.55f, 1.0f);

    assert(atmosphere.skyBrightness > airless.skyBrightness + 0.4f);
    assert(airless.starVisibility > atmosphere.starVisibility);
    assert(eclipse.phase == PLANET_OBSERVATION_ECLIPSE);
    assert(eclipse.skyBrightness < atmosphere.skyBrightness * 0.25f);
    assert(eclipse.starVisibility > atmosphere.starVisibility);
}

static void TestPolarDayAndNightObservations(void)
{
    PlanetLightState polarDay = LightAtAltitude(18.0f);
    polarDay.dayLengthFraction = 1.0f;
    polarDay.solarDeclination = 23.4f * DEG2RAD;
    PlanetLightState polarNight = LightAtAltitude(-18.5f);
    polarNight.dayLengthFraction = 0.0f;
    polarNight.solarDeclination = -23.4f * DEG2RAD;

    PlanetObservationState day = PlanetObservationEvaluate(
        &polarDay, 0.95f, 0.55f, 1.0f);
    PlanetObservationState night = PlanetObservationEvaluate(
        &polarNight, 0.95f, 0.55f, 1.0f);

    assert(day.phase == PLANET_OBSERVATION_DAY);
    assert(night.phase == PLANET_OBSERVATION_NIGHT);
    assert(day.skyBrightness > night.skyBrightness);
    assert(day.starVisibility < night.starVisibility);
}

static void TestMoonPhaseAndUmbraVisibility(void)
{
    PlanetLightState full = LightAtAltitude(-30.0f);
    full.hasMoon = true;
    full.moonDirection = (Vector3){ 0.2f, 0.8f, 0.1f };
    full.moonIllumination = 1.0f;
    PlanetLightState crescent = full;
    crescent.moonIllumination = 0.08f;
    PlanetLightState eclipsed = full;
    eclipsed.moonUmbra = 1.0f;

    PlanetObservationState fullState = PlanetObservationEvaluate(&full, 0.9f, 0.6f, 1.0f);
    PlanetObservationState crescentState = PlanetObservationEvaluate(&crescent, 0.9f, 0.6f, 1.0f);
    PlanetObservationState eclipsedState = PlanetObservationEvaluate(&eclipsed, 0.9f, 0.6f, 1.0f);

    assert(fullState.moonVisibility > crescentState.moonVisibility);
    assert(fullState.moonVisibility > eclipsedState.moonVisibility);
    assert(fullState.moonHaloStrength > crescentState.moonHaloStrength);
    assert(fullState.moonHaloStrength > eclipsedState.moonHaloStrength);
}

static void TestInvalidInputsStayFinite(void)
{
    PlanetLightState empty = { 0 };
    PlanetObservationState emptyState = PlanetObservationEvaluate(
        &empty, 0.9f, 0.5f, 1.0f);
    assert(!emptyState.valid);

    PlanetLightState invalid = LightAtAltitude(20.0f);
    invalid.sunDirection.y = NAN;
    invalid.daylight = INFINITY;
    invalid.moonIllumination = NAN;
    PlanetObservationState state = PlanetObservationEvaluate(
        &invalid, NAN, INFINITY, NAN);

    assert(isfinite(state.solarAltitudeRad));
    assert(isfinite(state.skyBrightness));
    assert(isfinite(state.starVisibility));
    assert(isfinite(state.moonVisibility));
}

int main(void)
{
    TestDayTwilightNightTransitions();
    TestAtmosphereAndEclipseContrast();
    TestPolarDayAndNightObservations();
    TestMoonPhaseAndUmbraVisibility();
    TestInvalidInputsStayFinite();
    puts("planet observation tests passed");
    return 0;
}
