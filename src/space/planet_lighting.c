#include "space/space_internal.h"

static Vector3 PlanetSurfaceNormalAt(Vector3 surfacePosition)
{
    float radius = fmaxf(planetWorld.spaceProxyRadius, 24.0f);
    float longitude = surfacePosition.x / (radius * 0.82f);
    float latitude = surfacePosition.z / (radius * 0.82f);
    float cosLatitude = cosf(latitude);
    return Vector3Normalize((Vector3){
        sinf(longitude) * cosLatitude,
        cosf(longitude) * cosLatitude,
        sinf(latitude)
    });
}

static Vector3 PlanetRotateY(Vector3 direction, float angle)
{
    float c = cosf(angle);
    float s = sinf(angle);
    return (Vector3){
        direction.x * c + direction.z * s,
        direction.y,
        -direction.x * s + direction.z * c
    };
}

static Vector3 PlanetRotateX(Vector3 direction, float angle)
{
    float c = cosf(angle);
    float s = sinf(angle);
    return (Vector3){
        direction.x,
        direction.y * c - direction.z * s,
        direction.y * s + direction.z * c
    };
}

static SpaceSatelliteVector3 SatelliteVectorFromGame(Vector3 gameVector)
{
    double scale = SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE;
    return (SpaceSatelliteVector3){
        (double)gameVector.x * scale,
        (double)gameVector.y * scale,
        (double)gameVector.z * scale
    };
}

static SpaceSatelliteVector3 SatelliteVectorFromDirection(Vector3 direction,
                                                           double lengthKm)
{
    return (SpaceSatelliteVector3){
        (double)direction.x * lengthKm,
        (double)direction.y * lengthKm,
        (double)direction.z * lengthKm
    };
}

static Vector3 SatelliteVectorToRaylib(SpaceSatelliteVector3 value)
{
    return (Vector3){ (float)value.x, (float)value.y, (float)value.z };
}

static SpaceSatelliteVector3 SatelliteVectorSubtract(
    SpaceSatelliteVector3 a, SpaceSatelliteVector3 b)
{
    return (SpaceSatelliteVector3){ a.x - b.x, a.y - b.y, a.z - b.z };
}

static double SatelliteVectorLength(SpaceSatelliteVector3 value)
{
    return sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

static double SatelliteVectorDot(SpaceSatelliteVector3 a,
                                 SpaceSatelliteVector3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static SpaceSatelliteVector3 SatelliteVectorNormalize(
    SpaceSatelliteVector3 value)
{
    double length = SatelliteVectorLength(value);
    if (!(length > 0.0)) return (SpaceSatelliteVector3){ 0 };
    return (SpaceSatelliteVector3){ value.x / length, value.y / length,
                                    value.z / length };
}

bool SolarPlanetSatelliteOrbit(const SolarSystemDef *system, int planetIndex,
                               const PlanetProfile *profile,
                               SpaceSatelliteOrbit *out)
{
    if (!out) return false;
    *out = (SpaceSatelliteOrbit){ 0 };
    if (!system || !profile || system->planetCount < 0 ||
        system->planetCount > MAX_SOLAR_PLANETS || planetIndex < 0 ||
        planetIndex >= system->planetCount) {
        return false;
    }

    SolarSystemPhysicalSnapshot scratch;
    const SolarSystemPhysicalSnapshot *snapshot =
        SolarSystemPhysicalSnapshotForSystem(system, &scratch);
    if (!snapshot) return false;
    if (!snapshot->satellitesBuilt) {
        scratch = *snapshot;
        if (!SolarSystemPhysicalSnapshotBuildSatellites(system, &scratch)) {
            return false;
        }
        snapshot = &scratch;
    }
    const SpaceSatelliteOrbit orbit = snapshot->satelliteOrbits[planetIndex];
    if (!orbit.exists || !(orbit.semiMajorAxisKm > 0.0) ||
        !isfinite(orbit.semiMajorAxisKm) || orbit.eccentricity < 0.0 ||
        orbit.eccentricity >= 1.0 || !isfinite(orbit.eccentricity) ||
        !isfinite(orbit.inclinationRad) ||
        !isfinite(orbit.longitudeAscendingNodeRad) ||
        !isfinite(orbit.argumentPeriapsisRad) ||
        !isfinite(orbit.meanAnomalyAtEpochRad) || !(orbit.radiusKm > 0.0) ||
        !isfinite(orbit.radiusKm) || !(orbit.massKg > 0.0) ||
        !isfinite(orbit.massKg)) {
        return false;
    }
    *out = orbit;
    return true;
}

static float PlanetRingShadowForPoint(Vector3 surfacePosition, Vector3 sunDirection)
{
    if (!planetWorld.profile.hasRings) return 0.0f;

    Vector3 surfaceNormal = PlanetSurfaceNormalAt(surfacePosition);
    Vector3 surfacePoint = Vector3Scale(surfaceNormal,
                                        planetWorld.spaceProxyRadius);
    float tilt = planetWorld.profile.ringTilt;
    Vector3 ringNormal = { 0.0f, cosf(tilt), sinf(tilt) };
    float denominator = Vector3DotProduct(ringNormal, sunDirection);
    if (fabsf(denominator) < 0.0001f) return 0.0f;

    float distanceAlongRay = -Vector3DotProduct(ringNormal, surfacePoint) / denominator;
    if (distanceAlongRay <= 0.0f) return 0.0f;

    Vector3 ringHit = Vector3Add(surfacePoint,
                                 Vector3Scale(sunDirection, distanceAlongRay));
    float ringRadius = Vector3Length(ringHit);
    float innerRadius = planetWorld.spaceProxyRadius * 1.30f;
    float outerRadius = planetWorld.spaceProxyRadius * 1.86f;
    if (ringRadius < innerRadius || ringRadius > outerRadius) return 0.0f;

    float band = (ringRadius - innerRadius) / fmaxf(outerRadius - innerRadius, 0.001f);
    return 0.42f + 0.22f * (0.5f + 0.5f * sinf(band * 18.0f));
}

static bool PlanetWorldMoonGeometryAt(
    const SolarPlanetRuntimeState *runtimePlanet,
    SpaceSatelliteVector3 observerPositionKm, float spinPhase,
    PlanetLightState *out, SpaceSatelliteOrbit *outOrbit,
    SpaceSatelliteVector3 *outPositionKm)
{
    if (!runtimePlanet || !out || !outOrbit || !outPositionKm ||
        !runtimePlanet->satelliteOrbit.exists) {
        return false;
    }
    *outOrbit = runtimePlanet->satelliteOrbit;
    *outPositionKm = runtimePlanet->satelliteState.positionKm;
    SpaceSatelliteVector3 observerToSatelliteKm = SatelliteVectorSubtract(
        *outPositionKm, observerPositionKm);
    Vector3 moonInertialDirection = Vector3Normalize(
        SatelliteVectorToRaylib(observerToSatelliteKm));
    out->moonDirection = PlanetRotateY(
        PlanetRotateX(
            Vector3Normalize(PlanetWorldSkyDirection(moonInertialDirection)),
            -planetWorld.profile.axialTilt),
        -spinPhase);
    double moonDistanceKm = SatelliteVectorLength(observerToSatelliteKm);
    if (moonDistanceKm > outOrbit->radiusKm) {
        out->moonAngularRadius = (float)asin(Clamp(
            (float)(outOrbit->radiusKm / moonDistanceKm), 0.0f, 1.0f));
    }
    out->hasMoon = true;
    return true;
}

static double PlanetWorldStellarOccultationAt(
    int sourceIndex, int sourceCount, const SolarLightSource *sources,
    Vector3 planetCenter, float sourceDistance,
    SpaceSatelliteVector3 sourcePositionKm)
{
    double occultationTotal = 0.0;
    for (int otherIndex = 0; otherIndex < sourceCount; otherIndex++) {
        if (sourceIndex == otherIndex) continue;
        Vector3 toOther = Vector3Subtract(
            sources[otherIndex].center, planetCenter);
        float otherDistance = Vector3Length(toOther);
        if (otherDistance >= sourceDistance || otherDistance < 0.001f) {
            continue;
        }
        SpaceSatelliteVector3 otherPositionKm =
            SatelliteVectorFromGame(toOther);
        double occultation = SpaceIlluminationOccultationFraction(
            (SpaceIlluminationBody){
                .positionKm = {
                    otherPositionKm.x, otherPositionKm.y,
                    otherPositionKm.z
                },
                .radiusKm = sources[otherIndex].stellar.radiusKm
            },
            (SpaceIlluminationBody){
                .positionKm = {
                    sourcePositionKm.x, sourcePositionKm.y,
                    sourcePositionKm.z
                },
                .radiusKm = sources[sourceIndex].stellar.radiusKm
            });
        occultationTotal = 1.0 -
            (1.0 - occultationTotal) * (1.0 - occultation);
    }
    return occultationTotal;
}

static void PlanetWorldMoonIlluminationAt(
    const SolarLightSource *sources, int sourceCount,
    Vector3 planetCenter, SpaceSatelliteVector3 observerPositionKm,
    SpaceSatelliteVector3 satellitePositionKm, double satelliteRadiusKm,
    double planetRadiusKm, const SpaceSatelliteVector3 *sourcePositionsKm,
    PlanetLightState *out)
{
    SpaceSatelliteVector3 moonToObserver = SatelliteVectorNormalize(
        SatelliteVectorSubtract(observerPositionKm, satellitePositionKm));
    double illuminatedWeight = 0.0;
    double moonLightWeight = 0.0;
    for (int i = 0; i < sourceCount; i++) {
        SpaceSatelliteVector3 moonToSource = SatelliteVectorNormalize(
            SatelliteVectorSubtract(sourcePositionsKm[i],
                                    satellitePositionKm));
        double phase = (double)Clamp(
            (float)((1.0 +
                     SatelliteVectorDot(moonToSource, moonToObserver)) *
                    0.5),
            0.0f, 1.0f);
        double umbra = SpaceSatellitePlanetUmbraFraction(
            satellitePositionKm, satelliteRadiusKm, planetRadiusKm,
            sourcePositionsKm[i], sources[i].stellar.radiusKm);
        double sourceWeight = SolarLightIrradianceAt(&sources[i],
                                                     planetCenter);
        illuminatedWeight += phase * sourceWeight * (1.0 - umbra);
        moonLightWeight += sourceWeight;
        out->moonUmbra = fmaxf(out->moonUmbra, (float)umbra);
    }
    if (moonLightWeight > 0.0) {
        out->moonIllumination = Clamp(
            (float)(illuminatedWeight / moonLightWeight), 0.0f, 1.0f);
    }
}

static bool PlanetWorldLightStateForFiniteSurface(
    Vector3 surfacePosition, double simulationTime, PlanetLightState *out)
{
    if (!out) return false;
    *out = (PlanetLightState){ 0 };
    if (!planetWorld.active || !planetWorld.profile.hasSolidSurface) {
        return false;
    }

    SolarSystemDef system = { 0 };
    if (!SurfaceHostSystem(&system)) return false;
    int orbitIndex = planetWorld.planetIndex - 1;
    if (orbitIndex < 0 || orbitIndex >= system.planetCount) return false;

    SolarSystemRuntimeState runtime;
    if (!SolarSystemEvaluateAtElapsedTime(
            &system, simulationTime, &runtime)) {
        return false;
    }
    SolarLightSource sources[MAX_SOLAR_LIGHTS];
    int sourceCount = SolarSystemRuntimeLightSources(
        &runtime, sources, MAX_SOLAR_LIGHTS);
    if (sourceCount <= 0) return false;

    if (orbitIndex >= MAX_SOLAR_PLANETS ||
        !runtime.planets[orbitIndex].valid) return false;
    Vector3 planetCenter = runtime.planets[orbitIndex].center;
    PlanetSeasonState season = { 0 };
    float radius = fmaxf(planetWorld.spaceProxyRadius, 24.0f);
    float latitude = surfacePosition.z / (radius * 0.82f);
    if (!PlanetSeasonEvaluate(&planetWorld.profile, latitude,
                              SpacePeriodicSimulationTime(simulationTime),
                              &season)) {
        season = (PlanetSeasonState){ 0 };
    }
    float spinPhase = (float)(planetWorld.seed & 0xffffu) / 65535.0f * 2.0f * PI +
                      (float)SpacePeriodicSimulationTime(simulationTime) *
                          planetWorld.profile.rotationRate * DEG2RAD;
    Vector3 surfaceNormal = PlanetSurfaceNormalAt(surfacePosition);
    Vector3 inertialSurfaceNormal = PlanetWorldSpaceDirection(
        PlanetRotateY(surfaceNormal, spinPhase));
    SpaceSatelliteVector3 observerPositionKm = SatelliteVectorFromDirection(
        Vector3Normalize(inertialSurfaceNormal),
        planetWorld.profile.physicalRadiusKm);

    const SolarPlanetRuntimeState *runtimePlanet =
        &runtime.planets[orbitIndex];
    SpaceSatelliteOrbit satellite = { 0 };
    bool hasMoon = false;
    SpaceSatelliteVector3 satellitePositionKm = { 0 };
    hasMoon = PlanetWorldMoonGeometryAt(
        runtimePlanet, observerPositionKm, spinPhase, out, &satellite,
        &satellitePositionKm);

    float totalWeight = 0.0f;
    Vector3 weightedDirection = Vector3Zero();
    float weightedR = 0.0f;
    float weightedG = 0.0f;
    float weightedB = 0.0f;
    SpaceSatelliteVector3 sourcePositionsKm[MAX_SOLAR_LIGHTS] = { 0 };

    for (int i = 0; i < sourceCount; i++) {
        Vector3 toSource = Vector3Subtract(sources[i].center, planetCenter);
        float distance = Vector3Length(toSource);
        if (distance < 0.001f) continue;
        sourcePositionsKm[i] = SatelliteVectorFromGame(toSource);

        // Convert the inertial star direction into the rotating planet frame.
        // The inverse rotation keeps a tidally locked face pointed at its star.
        Vector3 direction = PlanetWorldSkyDirection(toSource);
        direction = PlanetRotateX(Vector3Normalize(direction),
                                  -planetWorld.profile.axialTilt);
        direction = PlanetRotateY(Vector3Normalize(direction), -spinPhase);
        float weight = SolarLightIrradianceAt(&sources[i], planetCenter);
        double stellarOccultation = PlanetWorldStellarOccultationAt(
            i, sourceCount, sources, planetCenter, distance,
            sourcePositionsKm[i]);
        float sourceVisibility = 1.0f;
        if (stellarOccultation > 0.001) {
            weight *= fmaxf(0.01f, 1.0f - (float)stellarOccultation);
            sourceVisibility = fmaxf(
                0.06f, 1.0f - (float)stellarOccultation * 0.94f);
            out->sourceOccultations[i] = (float)stellarOccultation;
            out->eclipse = fmaxf(out->eclipse, (float)stellarOccultation);
            out->specialEclipse = true;
        }
        if (hasMoon) {
            double occultation = SpaceSatelliteSolarOccultationFraction(
                observerPositionKm, satellitePositionKm, satellite.radiusKm,
                sourcePositionsKm[i], sources[i].stellar.radiusKm);
            double combinedOccultation = 1.0 -
                (1.0 - stellarOccultation) * (1.0 - occultation);
            out->sourceOccultations[i] = (float)combinedOccultation;
            if (occultation > 0.001) {
                weight *= fmaxf(0.01f, 1.0f - (float)occultation);
                sourceVisibility *= fmaxf(0.06f,
                                          1.0f - (float)occultation * 0.94f);
                out->eclipse = fmaxf(out->eclipse, (float)occultation);
                out->specialEclipse = true;
            }
        }
        Color color = SpectrumColor(sources[i].spectrum);
        out->sourceDirections[i] = direction;
        out->sourceColors[i] = color;
        out->sourceIntensities[i] = weight;
        out->sourceVisibility[i] = sourceVisibility;
        totalWeight += weight;
        weightedDirection = Vector3Add(weightedDirection, Vector3Scale(direction, weight));
        weightedR += (float)color.r * weight;
        weightedG += (float)color.g * weight;
        weightedB += (float)color.b * weight;
    }

    if (Vector3LengthSqr(weightedDirection) < 0.000001f ||
        totalWeight <= 0.0f) {
        *out = (PlanetLightState){ 0 };
        return false;
    }
    Vector3 sunDirection = Vector3Normalize(weightedDirection);
    float incidence = Vector3DotProduct(surfaceNormal, sunDirection);
    float incidentIrradiance = fmaxf(incidence, 0.0f) * totalWeight;
    float daylight = 1.0f - expf(-incidentIrradiance * 1.45f);

    if (hasMoon) {
        PlanetWorldMoonIlluminationAt(
            sources, sourceCount, planetCenter, observerPositionKm,
            satellitePositionKm, satellite.radiusKm,
            planetWorld.profile.physicalRadiusKm, sourcePositionsKm, out);
    }

    out->sunDirection = sunDirection;
    out->daylight = daylight;
    out->sunset = incidence > 0.0f ?
                  powf(1.0f - Clamp(incidence, 0.0f, 1.0f), 2.0f) *
                  Clamp(sqrtf(totalWeight), 0.0f, 1.0f) : 0.0f;
    out->ringShadow = PlanetRingShadowForPoint(surfacePosition, sunDirection);
    out->daylight *= (1.0f - out->ringShadow * 0.72f);
    out->daylight = Clamp(out->daylight, 0.0f, 1.0f);
    out->solarDeclination = season.solarDeclination;
    out->dayLengthFraction = season.dayLengthFraction;
    out->incidentIrradiance = incidentIrradiance;
    out->totalIntensity = totalWeight;
    out->sourceCount = sourceCount;
    out->starColor = (Color){
        (unsigned char)Clamp(weightedR / totalWeight, 0.0f, 255.0f),
        (unsigned char)Clamp(weightedG / totalWeight, 0.0f, 255.0f),
        (unsigned char)Clamp(weightedB / totalWeight, 0.0f, 255.0f),
        255
    };
    return true;
}

bool PlanetWorldLightStateAt(Vector3 surfacePosition, PlanetLightState *out)
{
    return PlanetWorldLightStateAtTime(
        surfacePosition, SpaceElapsedSimulationTime(), out);
}

bool PlanetWorldLightStateAtTime(Vector3 surfacePosition,
                                 double simulationTime,
                                 PlanetLightState *out)
{
    if (!out) return false;
    *out = (PlanetLightState){ 0 };
    if (!SpaceVectorIsFinite(surfacePosition) || !isfinite(simulationTime) ||
        simulationTime < 0.0) {
        return false;
    }
    return PlanetWorldLightStateForFiniteSurface(
        surfacePosition, simulationTime, out);
}

float PlanetWorldDaylightAt(Vector3 surfacePosition)
{
    PlanetLightState state;
    return PlanetWorldLightStateAt(surfacePosition, &state) ? state.daylight : 0.0f;
}

Vector3 PlanetWorldSpaceReference(void)
{
    if (!planetWorld.active) return Vector3Zero();

    int systemAx = SpaceAnchorForLocalCoordinate(planetWorld.bodyCenter.x, spaceOriginX);
    int systemAz = SpaceAnchorForLocalCoordinate(planetWorld.bodyCenter.z, spaceOriginZ);
    int orbitIndex = planetWorld.planetIndex - 1;
    SolarSystemDef system;
    if (StarSystemAt(systemAx, systemAz, &system) &&
        orbitIndex >= 0 && orbitIndex < system.planetCount) {
        return SolarSystemPlanetCenter(&system, orbitIndex);
    }
    return planetWorld.bodyCenter;
}

Vector3 PlanetWorldSkyDirection(Vector3 worldDirection)
{
    if (!SpaceVectorIsFinite(worldDirection)) return Vector3Zero();
    if (!planetWorld.active) return worldDirection;

    Vector3 up = Vector3Subtract(planetWorld.returnPosition, planetWorld.bodyCenter);
    if (Vector3LengthSqr(up) < 0.001f) up = (Vector3){ 0.0f, 1.0f, 0.0f };
    else up = Vector3Normalize(up);

    Vector3 reference = fabsf(up.y) > 0.92f ? (Vector3){ 0.0f, 0.0f, 1.0f }
                                            : (Vector3){ 0.0f, 1.0f, 0.0f };
    Vector3 east = Vector3Normalize(Vector3CrossProduct(reference, up));
    Vector3 north = Vector3Normalize(Vector3CrossProduct(up, east));
    return (Vector3){
        Vector3DotProduct(worldDirection, east),
        Vector3DotProduct(worldDirection, up),
        Vector3DotProduct(worldDirection, north)
    };
}

Vector3 PlanetWorldSpaceDirection(Vector3 skyDirection)
{
    if (!SpaceVectorIsFinite(skyDirection)) return Vector3Zero();
    if (!planetWorld.active) return skyDirection;

    Vector3 up = Vector3Subtract(planetWorld.returnPosition, planetWorld.bodyCenter);
    if (Vector3LengthSqr(up) < 0.001f) up = (Vector3){ 0.0f, 1.0f, 0.0f };
    else up = Vector3Normalize(up);

    Vector3 reference = fabsf(up.y) > 0.92f ? (Vector3){ 0.0f, 0.0f, 1.0f }
                                            : (Vector3){ 0.0f, 1.0f, 0.0f };
    Vector3 east = Vector3Normalize(Vector3CrossProduct(reference, up));
    Vector3 north = Vector3Normalize(Vector3CrossProduct(up, east));
    return Vector3Add(Vector3Add(Vector3Scale(east, skyDirection.x),
                                 Vector3Scale(up, skyDirection.y)),
                      Vector3Scale(north, skyDirection.z));
}

bool SurfaceHostSystem(SolarSystemDef *out)
{
    if (!out) return false;
    *out = (SolarSystemDef){ 0 };
    if (HomeWorldSurfaceIsActive()) return StarSystemAt(0, 0, out);
    if (!planetWorld.active) return false;

    int systemAx = SpaceAnchorForLocalCoordinate(planetWorld.bodyCenter.x, spaceOriginX);
    int systemAz = SpaceAnchorForLocalCoordinate(planetWorld.bodyCenter.z, spaceOriginZ);
    return StarSystemAt(systemAx, systemAz, out);
}

Color SpectrumColor(SpectrumType type)
{
    switch (type) {
    case SPECTRUM_RED_DWARF: return (Color){ 255, 120, 90, 255 };
    case SPECTRUM_ORANGE:    return (Color){ 255, 170, 90, 255 };
    case SPECTRUM_YELLOW:    return (Color){ 255, 214, 120, 255 };
    case SPECTRUM_BLUE_WHITE: return (Color){ 190, 210, 255, 255 };
    case SPECTRUM_RED_GIANT: return (Color){ 255, 90, 60, 255 };
    case SPECTRUM_WHITE_DWARF: return (Color){ 225, 238, 255, 255 };
    case SPECTRUM_NEUTRON_STAR: return (Color){ 150, 205, 255, 255 };
    case SPECTRUM_BLACK_HOLE: return (Color){ 48, 45, 52, 255 };
    default:                 return (Color){ 255, 214, 120, 255 };
    }
}

const char *SpectrumName(SpectrumType type)
{
    switch (type) {
    case SPECTRUM_RED_DWARF: return "Red Dwarf";
    case SPECTRUM_ORANGE:    return "Orange Star";
    case SPECTRUM_YELLOW:    return "Yellow Sun";
    case SPECTRUM_BLUE_WHITE: return "Blue-White Star";
    case SPECTRUM_RED_GIANT: return "Red Giant";
    case SPECTRUM_WHITE_DWARF: return "White Dwarf";
    case SPECTRUM_NEUTRON_STAR: return "Neutron Star";
    case SPECTRUM_BLACK_HOLE: return "Black Hole";
    default:                 return "Star";
    }
}

const char *SolarStyleName(SolarBodyStyle style)
{
    switch (style) {
    case SOLAR_STYLE_LAVA:   return "Lava Planet";
    case SOLAR_STYLE_ICE:    return "Ice Planet";
    case SOLAR_STYLE_DESERT: return "Desert Planet";
    case SOLAR_STYLE_GAS:    return "Gas Giant";
    case SOLAR_STYLE_CRATER: return "Cratered World";
    case SOLAR_STYLE_TEMPERATE: return "Temperate World";
    default:                 return "Planet";
    }
}

const char *PlanetAtmosphereName(PlanetAtmosphereType type)
{
    switch (type) {
    case PLANET_ATMOSPHERE_NONE:       return "Airless";
    case PLANET_ATMOSPHERE_THIN:       return "Thin atmosphere";
    case PLANET_ATMOSPHERE_BREATHABLE: return "Breathable atmosphere";
    case PLANET_ATMOSPHERE_DENSE:      return "Dense atmosphere";
    case PLANET_ATMOSPHERE_CORROSIVE:  return "Corrosive atmosphere";
    default:                           return "Unknown atmosphere";
    }
}
