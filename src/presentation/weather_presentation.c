#include "presentation/weather_presentation.h"

#include "space/space_state.h"
#include "world/weather.h"

#include <math.h>

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

    float atmosphereDensity = 0.0f;
    bool atmosphereActive = false;
    if (PlanetWorldIsActive()) {
        const PlanetProfile *profile = PlanetWorldProfile();
        if (profile && profile->atmosphereType != PLANET_ATMOSPHERE_NONE) {
            atmosphereDensity = WeatherPresentationClamp(
                profile->atmosphereDensity) *
                (1.0f - PlanetWorldAtmosphereFade(position));
            atmosphereActive = atmosphereDensity > 0.01f;
        }
    } else if (HomeWorldSurfaceIsActive()) {
        atmosphereDensity = 0.62f * (1.0f - HomeWorldSpaceFade(position));
        atmosphereActive = atmosphereDensity > 0.01f;
    }
    if (!atmosphereActive) return clear;

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
    return WeatherVisualStateEvaluate(&input);
}
