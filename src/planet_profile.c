#include "planet_profile.h"

#include "planet_climate.h"
#include "raymath.h"
#include "space_illumination.h"
#include "space_units.h"

#include <math.h>
#include <string.h>

static float PlanetProfileHashUnit(uint32_t seed, uint32_t lane)
{
    uint32_t h = seed ^ (lane * 0x9e3779b9u);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return (float)(h & 0x00ffffffu) / 16777215.0f;
}

static PlanetAtmosphereType ClassifyAtmosphere(
    SolarBodyStyle style, float pressureAtm, float temperatureK,
    float composition)
{
    if (pressureAtm < 0.01f) return PLANET_ATMOSPHERE_NONE;
    if (pressureAtm < 0.35f) return PLANET_ATMOSPHERE_THIN;
    if (style == SOLAR_STYLE_LAVA || temperatureK > 355.0f) {
        return PLANET_ATMOSPHERE_CORROSIVE;
    }
    if (style == SOLAR_STYLE_TEMPERATE && temperatureK >= 250.0f &&
        temperatureK <= 310.0f && pressureAtm <= 1.65f &&
        composition > 0.38f) {
        return PLANET_ATMOSPHERE_BREATHABLE;
    }
    return PLANET_ATMOSPHERE_DENSE;
}

static bool ApplyPlanetClimate(PlanetProfile *profile,
                               PlanetClimateInput input)
{
    PlanetClimateState climate;
    if (!profile || !PlanetClimateSolve(&input, &climate)) return false;
    profile->receivedIrradiance = input.stellarIrradianceEarth;
    profile->radiativeTempK = climate.radiativeTemperatureK;
    profile->equilibriumTempK = climate.surfaceTemperatureK;
    profile->surfacePressureAtm = climate.surfacePressureAtm;
    profile->atmosphereDensity = climate.atmosphereDensity;
    profile->albedo = climate.albedo;
    profile->greenhouseEffect = climate.greenhouseOpticalDepth;
    profile->oceanCoverage = climate.liquidWaterCoverage;
    profile->iceCoverage = climate.iceCoverage;
    profile->cloudCoverage = climate.cloudCoverage;
    profile->windStrength = climate.windStrength;
    return true;
}

static SolarBodyStyle ClassifyPlanetClimate(const PlanetProfile *profile)
{
    if (!profile || !profile->hasSolidSurface) return SOLAR_STYLE_GAS;
    if (profile->equilibriumTempK > 365.0f) return SOLAR_STYLE_LAVA;
    if (profile->surfacePressureAtm < 0.035f &&
        profile->oceanCoverage < 0.03f && profile->iceCoverage < 0.08f) {
        return SOLAR_STYLE_CRATER;
    }
    if (profile->iceCoverage > 0.38f || profile->equilibriumTempK < 245.0f) {
        return SOLAR_STYLE_ICE;
    }
    if (profile->oceanCoverage > 0.10f &&
        profile->equilibriumTempK <= 315.0f) {
        return SOLAR_STYLE_TEMPERATE;
    }
    return SOLAR_STYLE_DESERT;
}

static void DerivePlanetSurfaceHistory(PlanetProfile *profile)
{
    if (!profile) return;

    profile->axialTilt =
        (2.5f + PlanetProfileHashUnit(profile->seed, 21u) * 31.0f) * DEG2RAD;
    if (profile->tidallyLocked) profile->axialTilt *= 0.35f;
    profile->seasonPhase =
        PlanetProfileHashUnit(profile->seed, 22u) * 2.0f * PI;
    if (profile->yearLength <= 0.0f) {
        profile->yearLength = 1800.0f +
                              PlanetProfileHashUnit(profile->seed, 23u) *
                                  6200.0f;
    }
    profile->prevailingWindAngle =
        PlanetProfileHashUnit(profile->seed, 24u) * 2.0f * PI;

    float volcanicBase = profile->style == SOLAR_STYLE_LAVA ? 0.78f :
                         profile->style == SOLAR_STYLE_CRATER ? 0.20f : 0.08f;
    if (profile->equilibriumTempK > 340.0f) volcanicBase += 0.14f;
    profile->volcanicActivity = Clamp(
        volcanicBase + PlanetProfileHashUnit(profile->seed, 25u) * 0.22f,
        0.0f, 1.0f);
    float impactBase = profile->style == SOLAR_STYLE_CRATER ? 0.74f :
                       profile->style == SOLAR_STYLE_LAVA ? 0.32f : 0.12f;
    if (profile->atmosphereType == PLANET_ATMOSPHERE_NONE) impactBase += 0.12f;
    profile->impactRate = Clamp(
        impactBase + PlanetProfileHashUnit(profile->seed, 26u) * 0.20f,
        0.0f, 1.0f);
}

bool PlanetProfileGenerate(const PlanetProfileGenerationInput *input,
                           PlanetProfile *out)
{
    if (!input || !out) return false;
    memset(out, 0, sizeof(*out));

    float sizeUnit = PlanetProfileHashUnit(input->seed, 1u);
    float composition = PlanetProfileHashUnit(input->seed, 2u);
    float volatileSupply = PlanetProfileHashUnit(input->seed, 3u);
    double orbitAU = fmax(input->semiMajorAxisKm /
                          SPACE_UNITS_ASTRONOMICAL_UNIT_KM, 0.18);
    double totalLuminositySolar = 0.0;
    int stellarCount = input->stellarCount;
    if (stellarCount < 0) stellarCount = 0;
    if (stellarCount > PLANET_PROFILE_MAX_STARS) {
        stellarCount = PLANET_PROFILE_MAX_STARS;
    }
    for (int sourceIndex = 0; sourceIndex < stellarCount; sourceIndex++) {
        totalLuminositySolar += fmax(
            (double)input->stellarLuminositiesSolar[sourceIndex], 0.0);
    }
    if (totalLuminositySolar <= 0.0001) {
        float primaryLuminosity = input->stellarLuminositiesSolar[0] > 0.0f ?
            input->stellarLuminositiesSolar[0] : 1.0f;
        totalLuminositySolar = primaryLuminosity;
    }
    double eccentricity = fmax(0.0, fmin(input->orbitalEccentricity, 0.95));
    double irradiance = SpaceIlluminationOrbitMeanIrradianceEarth(
        totalLuminositySolar,
        orbitAU * SPACE_UNITS_ASTRONOMICAL_UNIT_KM, eccentricity);
    // Climate uses the time average; live light and weather use instantaneous
    // star positions from SolarSystemRuntimeState.
    irradiance = fmax(irradiance, 0.0001);
    float unshieldedTemperature = 278.5f *
                                  (float)pow(irradiance, 0.25);

    float solidRadiusEarth = (float)(input->physicalRadiusKm /
                                     SPACE_UNITS_EARTH_RADIUS_KM);
    solidRadiusEarth = Clamp(solidRadiusEarth, 0.35f, 1.75f);
    bool gasGiant = input->forcedGasGiant || input->formationGasGiant ||
                    (input->planetIndex > 0 &&
                     input->spaceProxyRadius >= 47.0f &&
                     unshieldedTemperature < 430.0f &&
                     input->formationMassEarth <= 0.0f &&
                     PlanetProfileHashUnit(input->seed, 5u) > 0.52f);

    out->seed = input->seed;
    float formationDelayGyr =
        0.01f + PlanetProfileHashUnit(input->seed, 15u) * 0.04f;
    float stellarAgeGyr = fmaxf(input->stellarAgeGyr, 0.0f);
    formationDelayGyr = fminf(formationDelayGyr, stellarAgeGyr * 0.35f);
    out->ageGyr = stellarAgeGyr - formationDelayGyr;
    out->spaceProxyRadius = input->spaceProxyRadius;
    out->hasSolidSurface = !gasGiant;
    float tidalProximity = Clamp(1.40f - orbitAU, 0.0f, 1.0f);
    out->tidalLockFactor = Clamp(
        tidalProximity *
            (0.68f + PlanetProfileHashUnit(input->seed, 13u) * 0.32f),
        0.0f, 1.0f);
    out->tidallyLocked =
        out->hasSolidSurface && out->tidalLockFactor > 0.58f;
    out->ringTilt =
        (14.0f + PlanetProfileHashUnit(input->seed, 14u) * 17.0f) * DEG2RAD;
    out->yearLength = input->orbitalPeriodGameTime;
    if (gasGiant) {
        float gasRadiusEarth = input->formationMassEarth > 0.0f
            ? (float)(input->physicalRadiusKm / SPACE_UNITS_EARTH_RADIUS_KM)
            : 2.8f + sizeUnit * 1.8f;
        double massEarth = input->formationMassEarth > 0.0f
            ? (double)input->formationMassEarth
            : 12.0 + (double)composition * 32.0;
        out->massKg = SpaceUnitsGameMassToKilograms(massEarth);
        out->physicalRadiusKm = (double)gasRadiusEarth *
                                SPACE_UNITS_EARTH_RADIUS_KM;
        double gravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
            out->massKg, out->physicalRadiusKm);
        double earthGravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
            SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM);
        out->surfaceGravity = Clamp((float)(gravity / earthGravity),
                                    0.75f, 2.40f);
        out->rotationRate =
            5.0f + PlanetProfileHashUnit(input->seed, 6u) * 3.0f;
        out->hasRings = input->forcedGasGiant ||
                        PlanetProfileHashUnit(input->seed, 7u) > 0.34f;
        out->tidalLockFactor = 0.0f;
        out->tidallyLocked = false;
    } else {
        float density = 0.78f + composition * 0.52f;
        double massEarth = input->formationMassEarth > 0.0f
            ? (double)input->formationMassEarth
            : (double)density * solidRadiusEarth * solidRadiusEarth *
                  solidRadiusEarth;
        out->massKg = SpaceUnitsGameMassToKilograms(massEarth);
        out->physicalRadiusKm = (double)solidRadiusEarth *
                                SPACE_UNITS_EARTH_RADIUS_KM;
        double gravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
            out->massKg, out->physicalRadiusKm);
        double earthGravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
            SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM);
        out->surfaceGravity = Clamp((float)(gravity / earthGravity),
                                    0.45f, 1.75f);
        out->rotationRate =
            0.7f + PlanetProfileHashUnit(input->seed, 11u) * 2.5f;
        out->hasRings = input->spaceProxyRadius >= 46.0f &&
                        PlanetProfileHashUnit(input->seed, 12u) > 0.92f;
        if (out->tidallyLocked && out->yearLength > 0.0f) {
            out->rotationRate = 360.0f / out->yearLength;
        }
    }

    PlanetClimateInput climateInput = {
        .stellarIrradianceEarth = irradiance,
        .volatileInventory = gasGiant ? 0.78f + volatileSupply * 0.22f
                                      : volatileSupply,
        .greenhouseGasFraction = gasGiant
            ? Clamp(0.42f + composition * 0.46f, 0.0f, 1.0f)
            : Clamp(0.04f + composition * 0.18f +
                    PlanetProfileHashUnit(input->seed, 20u) * 0.72f,
                    0.0f, 1.0f),
        .surfaceReflectivity = gasGiant
            ? 0.28f + PlanetProfileHashUnit(input->seed, 19u) * 0.18f
            : 0.08f + composition * 0.22f +
                  PlanetProfileHashUnit(input->seed, 19u) * 0.12f,
        .surfaceGravityEarth = out->surfaceGravity,
        .rotationRate = out->rotationRate,
        .tidalLockFactor = out->tidalLockFactor,
        .gasGiant = gasGiant
    };
    if (!ApplyPlanetClimate(out, climateInput)) return false;
    out->style = ClassifyPlanetClimate(out);
    out->atmosphereType = gasGiant ? PLANET_ATMOSPHERE_DENSE :
        ClassifyAtmosphere(out->style, out->surfacePressureAtm,
                           out->equilibriumTempK, composition);
    float roughnessBase = out->style == SOLAR_STYLE_CRATER ? 1.20f :
                          out->style == SOLAR_STYLE_LAVA ? 1.05f :
                          out->style == SOLAR_STYLE_DESERT ? 0.72f : 0.88f;
    out->terrainRoughness = gasGiant ? 0.0f : roughnessBase *
        (0.78f + PlanetProfileHashUnit(input->seed, 10u) * 0.48f);
    DerivePlanetSurfaceHistory(out);
    return true;
}

PlanetProfile PlanetProfileGenerateLegacy(uint32_t seed, SolarBodyStyle style,
                                          float terrainRadius)
{
    PlanetProfile profile;
    memset(&profile, 0, sizeof(profile));
    float legacyProxyRadius = fmaxf(1.0f,
                                    (terrainRadius - 3.6f) / 1.22f);
    float radiusEarth = Clamp(0.72f +
                              (legacyProxyRadius - 40.0f) * 0.095f,
                              0.62f, 1.55f);
    float composition = PlanetProfileHashUnit(seed, 2u);
    float density = 0.78f + composition * 0.52f;
    profile.seed = seed;
    profile.style = style;
    profile.ageGyr = 0.35f + PlanetProfileHashUnit(seed, 15u) * 8.65f;
    profile.spaceProxyRadius = legacyProxyRadius;
    double massEarth = (double)density * radiusEarth * radiusEarth * radiusEarth;
    profile.massKg = SpaceUnitsGameMassToKilograms(massEarth);
    profile.physicalRadiusKm = (double)radiusEarth *
                               SPACE_UNITS_EARTH_RADIUS_KM;
    double gravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
        profile.massKg, profile.physicalRadiusKm);
    double earthGravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
        SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM);
    profile.surfaceGravity = Clamp((float)(gravity / earthGravity),
                                   0.45f, 1.75f);
    profile.terrainRoughness =
        0.82f + PlanetProfileHashUnit(seed, 10u) * 0.42f;
    profile.rotationRate = style == SOLAR_STYLE_GAS ? 6.0f :
                           0.7f + PlanetProfileHashUnit(seed, 11u) * 2.5f;
    // Legacy saves may already be standing on a former gas-style world.
    profile.hasSolidSurface = true;
    profile.hasRings = false;
    profile.tidalLockFactor = style == SOLAR_STYLE_GAS ? 0.0f :
                              PlanetProfileHashUnit(seed, 13u) * 0.35f;
    profile.tidallyLocked = profile.tidalLockFactor > 0.54f;
    float irradiance = style == SOLAR_STYLE_LAVA ? 2.70f :
                       style == SOLAR_STYLE_DESERT ? 1.45f :
                       style == SOLAR_STYLE_ICE ? 0.28f :
                       style == SOLAR_STYLE_CRATER ? 0.14f :
                       style == SOLAR_STYLE_GAS ? 0.60f : 1.0f;
    float volatiles = style == SOLAR_STYLE_CRATER ? 0.04f :
                      style == SOLAR_STYLE_DESERT ? 0.32f :
                      style == SOLAR_STYLE_LAVA ? 0.55f :
                      style == SOLAR_STYLE_GAS ? 0.90f : 0.68f;
    float greenhouse = style == SOLAR_STYLE_LAVA ? 0.90f :
                       style == SOLAR_STYLE_DESERT ? 0.55f :
                       style == SOLAR_STYLE_ICE ? 0.25f :
                       style == SOLAR_STYLE_CRATER ? 0.05f : 0.38f;
    float reflectivity = style == SOLAR_STYLE_ICE ? 0.48f :
                         style == SOLAR_STYLE_CRATER ? 0.12f :
                         style == SOLAR_STYLE_LAVA ? 0.14f : 0.20f;
    ApplyPlanetClimate(&profile, (PlanetClimateInput){
        .stellarIrradianceEarth = irradiance,
        .volatileInventory = volatiles,
        .greenhouseGasFraction = greenhouse,
        .surfaceReflectivity = reflectivity,
        .surfaceGravityEarth = profile.surfaceGravity,
        .rotationRate = profile.rotationRate,
        .tidalLockFactor = profile.tidalLockFactor,
        .gasGiant = false
    });
    profile.atmosphereType = ClassifyAtmosphere(
        style, profile.surfacePressureAtm, profile.equilibriumTempK,
        composition);
    DerivePlanetSurfaceHistory(&profile);
    return profile;
}
