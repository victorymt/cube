#include "space/planet_observation.h"

#include <math.h>

static float ObservationClamp(float value, float minimum, float maximum)
{
    if (!isfinite(value)) return minimum;
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static float ObservationSmoothstep(float edge0, float edge1, float value)
{
    if (edge1 <= edge0) return value >= edge1 ? 1.0f : 0.0f;
    float t = ObservationClamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

PlanetObservationState PlanetObservationEvaluate(
    const PlanetLightState *light, float opticalDepth, float mieStrength,
    float atmosphereVisibility)
{
    PlanetObservationState state = { 0 };
    if (!light || light->sourceCount <= 0) return state;

    float sunY = ObservationClamp(light->sunDirection.y, -1.0f, 1.0f);
    float altitude = asinf(sunY);
    float altitudeDeg = altitude * (180.0f / PI);
    float atmosphere = ObservationClamp(atmosphereVisibility, 0.0f, 1.0f);
    float depth = ObservationClamp(opticalDepth, 0.0f, 1.5f) * atmosphere;
    float mie = ObservationClamp(mieStrength, 0.0f, 1.5f);
    float daylight = ObservationClamp(light->daylight, 0.0f, 1.0f);
    float eclipse = fmaxf(ObservationClamp(light->eclipse, 0.0f, 1.0f),
                          ObservationClamp(light->ringShadow, 0.0f, 1.0f) * 0.72f);

    float twilightRise = ObservationSmoothstep(-18.0f, -1.0f, altitudeDeg);
    float twilightFall = 1.0f - ObservationSmoothstep(-1.0f, 8.0f, altitudeDeg);
    float twilight = twilightRise * twilightFall;
    float scattering = 1.0f - expf(-depth * 1.28f);
    float daylightScatter = daylight * (0.72f + 0.28f *
                                         ObservationClamp(light->incidentIrradiance,
                                                          0.0f, 1.0f));
    float twilightScatter = twilight * (0.18f + mie * 0.17f);
    float skyBrightness = scattering * ObservationClamp(daylightScatter + twilightScatter,
                                                         0.0f, 1.0f);
    float directGlare = ObservationSmoothstep(0.08f, 0.88f, daylight);
    float starVisibility = 1.0f - ObservationSmoothstep(0.025f, 0.56f, skyBrightness);
    starVisibility *= 1.0f - directGlare * (0.14f + atmosphere * 0.62f);

    float moonAboveHorizon = ObservationSmoothstep(-0.08f, 0.12f,
                                                    light->moonDirection.y);
    float moonIllumination = ObservationClamp(light->moonIllumination, 0.0f, 1.0f);
    float moonUmbra = ObservationClamp(light->moonUmbra, 0.0f, 1.0f);
    float moonGlare = light->hasMoon ? moonAboveHorizon * moonIllumination *
                                      (1.0f - moonUmbra * 0.88f) : 0.0f;
    starVisibility *= 1.0f - moonGlare * scattering * 0.42f;

    float moonContrast = 1.0f - skyBrightness * 0.82f;
    float eclipsedMoonLight = moonIllumination * (0.16f + 0.84f * (1.0f - moonUmbra));
    float moonVisibility = light->hasMoon ? moonAboveHorizon * moonContrast *
                           (0.08f + 0.92f * eclipsedMoonLight) : 0.0f;
    float moonHalo = moonGlare * scattering * mie *
                     (0.18f + starVisibility * 0.34f);

    PlanetObservationPhase phase = PLANET_OBSERVATION_NIGHT;
    if (altitudeDeg >= 0.0f) phase = PLANET_OBSERVATION_DAY;
    else if (altitudeDeg >= -6.0f) phase = PLANET_OBSERVATION_CIVIL_TWILIGHT;
    else if (altitudeDeg >= -12.0f) phase = PLANET_OBSERVATION_NAUTICAL_TWILIGHT;
    else if (altitudeDeg >= -18.0f) phase = PLANET_OBSERVATION_ASTRONOMICAL_TWILIGHT;
    if ((light->specialEclipse || eclipse >= 0.35f) && altitudeDeg >= -1.0f) {
        phase = PLANET_OBSERVATION_ECLIPSE;
    }

    state.phase = phase;
    state.solarAltitudeRad = altitude;
    state.twilightStrength = ObservationClamp(twilight, 0.0f, 1.0f);
    state.skyBrightness = ObservationClamp(skyBrightness, 0.0f, 1.0f);
    state.horizonWarmth = ObservationClamp(
        twilight * (0.38f + mie * 0.44f) + light->sunset * mie * 0.54f +
        eclipse * mie * daylight * 0.16f,
        0.0f, 1.0f);
    state.starVisibility = ObservationClamp(starVisibility, 0.0f, 1.0f);
    state.moonVisibility = ObservationClamp(moonVisibility, 0.0f, 1.0f);
    state.moonHaloStrength = ObservationClamp(moonHalo, 0.0f, 0.32f);
    state.eclipseDarkening = eclipse;
    state.opticalDepth = depth;
    state.atmosphereVisibility = atmosphere;
    state.valid = isfinite(altitude) && isfinite(state.skyBrightness) &&
                  isfinite(state.starVisibility) && isfinite(state.moonVisibility);
    return state;
}

const char *PlanetObservationPhaseName(PlanetObservationPhase phase)
{
    switch (phase) {
    case PLANET_OBSERVATION_ASTRONOMICAL_TWILIGHT: return "astronomical twilight";
    case PLANET_OBSERVATION_NAUTICAL_TWILIGHT: return "nautical twilight";
    case PLANET_OBSERVATION_CIVIL_TWILIGHT: return "civil twilight";
    case PLANET_OBSERVATION_DAY: return "day";
    case PLANET_OBSERVATION_ECLIPSE: return "eclipse";
    case PLANET_OBSERVATION_NIGHT:
    default: return "night";
    }
}
