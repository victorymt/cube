#include "presentation/weather_presentation.h"

#include "space/space_state.h"
#include "world/weather.h"
#include "space/planet_atmosphere.h"

#include <math.h>
#include <stddef.h>

static float WeatherPresentationClamp(float value)
{
    if (!isfinite(value) || value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

WeatherVisualState WeatherVisualStateAtWorld(
    Vector3 position, double simulationTime, float daylight)
{
    WeatherVisualState clear = { .visibility = 1.0f };
    if (!isfinite(position.x) || !isfinite(position.y) ||
        !isfinite(position.z) || !isfinite(simulationTime) ||
        simulationTime < 0.0 || !isfinite(daylight)) {
        return clear;
    }

    if (!PlanetWorldIsActive() && !HomeWorldSurfaceIsActive()) return clear;
    const PlanetProfile *planet = PlanetWorldIsActive() ? PlanetWorldProfile() : NULL;
    AtmosphereSample air = PlanetAtmosphereAtHeight(planet, position.y);
    if (!air.valid || air.layer == ATMOSPHERE_LAYER_VACUUM) return clear;
    float atmosphereDensity = planet ? WeatherPresentationClamp(planet->atmosphereDensity) : 0.62f;

    WeatherFieldSample weather;
    if (!WeatherFieldSampleInterpolatedAtWorldTime(
            position.x, position.z, simulationTime, &weather)) {
        return clear;
    }
    WeatherVisualInput input = {
        .weather = weather,
        .atmosphereDensity = atmosphereDensity,
        .daylight = daylight,
        .windAngle = weather.windAngle,
        .atmosphereActive = true
    };
    WeatherVisualState state = WeatherVisualStateEvaluate(&input);
    float localAir = (float)(fmin(air.densityRatio, 1.0) * air.weatherActivity);
    state.fogDensity *= localAir;
    state.dustDensity *= localAir;
    state.precipitationVeil *= localAir;
    state.stormDarkening *= localAir;
    state.rainbowStrength *= localAir;
    state.lightningIntensity *= localAir;
    state.visibility = 1.0f - (1.0f - state.visibility) * localAir;
    return state;
}
