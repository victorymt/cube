#include "presentation/weather_visual.h"

#include <math.h>

static float WeatherVisualClamp(float value)
{
    if (!isfinite(value) || value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float WeatherVisualSmoothStep(float edge0, float edge1, float value)
{
    float t = WeatherVisualClamp((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float WeatherCloudVerticalDensity(float normalizedHeight)
{
    if (!isfinite(normalizedHeight) || normalizedHeight <= 0.0f ||
        normalizedHeight >= 1.0f) {
        return 0.0f;
    }
    float bottom = WeatherVisualSmoothStep(0.0f, 0.16f, normalizedHeight);
    float top = 1.0f - WeatherVisualSmoothStep(0.64f, 1.0f,
                                               normalizedHeight);
    float baseWeight = 0.72f + (1.0f - normalizedHeight) * 0.36f;
    return WeatherVisualClamp(bottom * top * baseWeight);
}

static bool WeatherVisualSampleIsFinite(WeatherFieldSample sample)
{
    return isfinite(sample.cloudCover) && isfinite(sample.precipitation) &&
           isfinite(sample.rain) && isfinite(sample.snow) &&
           isfinite(sample.storm) && isfinite(sample.wind);
}

WeatherVisualState WeatherVisualStateEvaluate(const WeatherVisualInput *input)
{
    WeatherVisualState state = { .visibility = 1.0f };
    if (!input || !input->atmosphereActive ||
        !WeatherVisualSampleIsFinite(input->weather) ||
        !isfinite(input->atmosphereDensity) || !isfinite(input->daylight) ||
        !isfinite(input->windAngle)) {
        return state;
    }

    float density = WeatherVisualClamp(input->atmosphereDensity);
    if (density <= 0.01f) return state;

    float cloud = WeatherVisualClamp(input->weather.cloudCover);
    float precipitation = WeatherVisualClamp(input->weather.precipitation);
    float storm = WeatherVisualClamp(input->weather.storm);
    float wind = WeatherVisualClamp(input->weather.wind);
    float daylight = WeatherVisualClamp(input->daylight);
    float snow = WeatherVisualClamp(input->weather.snow);

    state.active = true;
    state.atmosphereDensity = density;
    state.daylight = daylight;
    state.cloudCover = WeatherVisualClamp(
        cloud * (0.44f + density * 0.56f) + precipitation * 0.12f);
    state.cloudBaseHeight = 46.0f - state.cloudCover * 17.0f -
                            precipitation * 9.0f - storm * 6.0f;
    if (state.cloudBaseHeight < 14.0f) state.cloudBaseHeight = 14.0f;
    state.cloudThickness = 5.0f + state.cloudCover * 12.0f +
                           precipitation * 7.0f + storm * 5.0f;
    state.cloudOpacity = WeatherVisualClamp(
        state.cloudCover * (0.42f + density * 0.42f) + precipitation * 0.18f);
    state.precipitationVeil = WeatherVisualClamp(
        precipitation * (0.42f + storm * 0.36f) * density);
    state.fogDensity = WeatherVisualClamp(
        (state.cloudCover * 0.11f + precipitation * 0.39f + storm * 0.31f) *
        density * (0.80f + (1.0f - daylight) * 0.20f));
    state.stormDarkening = WeatherVisualClamp(
        state.cloudCover * 0.20f + precipitation * 0.34f + storm * 0.42f);
    state.visibility = WeatherVisualClamp(
        1.0f - state.fogDensity * 0.76f - state.precipitationVeil * 0.28f);
    state.windDrift = WeatherVisualClamp(wind * (0.42f + storm * 0.58f));
    state.windAngle = input->windAngle;
    state.snowFraction = precipitation > 0.001f ?
                             WeatherVisualClamp(snow / precipitation) : 0.0f;
    return state;
}
