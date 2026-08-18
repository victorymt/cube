#ifndef VOXELCRAFT_WEATHER_MODEL_H
#define VOXELCRAFT_WEATHER_MODEL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum WeatherPhenomenon {
    WEATHER_PHENOMENON_CLEAR = 0,
    WEATHER_PHENOMENON_CLOUDY,
    WEATHER_PHENOMENON_FOG,
    WEATHER_PHENOMENON_FROST,
    WEATHER_PHENOMENON_DRIZZLE,
    WEATHER_PHENOMENON_SHOWERS,
    WEATHER_PHENOMENON_HEAVY_RAIN,
    WEATHER_PHENOMENON_THUNDERSTORM,
    WEATHER_PHENOMENON_LIGHTNING,
    WEATHER_PHENOMENON_SLEET,
    WEATHER_PHENOMENON_FREEZING_RAIN,
    WEATHER_PHENOMENON_HAIL,
    WEATHER_PHENOMENON_SNOW,
    WEATHER_PHENOMENON_BLIZZARD,
    WEATHER_PHENOMENON_STRONG_WIND,
    WEATHER_PHENOMENON_DUST_STORM,
    WEATHER_PHENOMENON_HEAT_WAVE,
    WEATHER_PHENOMENON_COLD_SNAP,
    WEATHER_PHENOMENON_RAINBOW,
    WEATHER_PHENOMENON_AURORA,
    WEATHER_PHENOMENON_COUNT
} WeatherPhenomenon;

typedef uint32_t WeatherPhenomenonFlags;

#define WEATHER_PHENOMENON_FLAG(phenomenon) \
    ((WeatherPhenomenonFlags)1u << (unsigned)(phenomenon))

typedef struct WeatherFieldInput {
    uint32_t seed;
    double simulationTime;
    float worldX;
    float worldZ;
    float temperatureK;
    float moisture;
    float cloudPotential;
    float windStrength;
    float prevailingWindAngle;
    float surfacePressureAtm;
    float atmosphereDensity;
    float relativeHumidity;
    float dewPointK;
    float wetBulbK;
    float instability;
    float orographicLift;
    float aridity;
    float dustAvailability;
    float latitude;
    float magneticLatitude;
    float magneticFieldStrength;
    float daylight;
    float solarElevation;
    bool atmosphereActive;
    bool supportsWaterCycle;
} WeatherFieldInput;

typedef struct WeatherFieldSample {
    WeatherPhenomenonFlags phenomena;
    WeatherPhenomenon dominantPhenomenon;
    float temperatureK;
    float temperatureAnomalyK;
    float pressureAtm;
    float pressureAnomaly;
    float relativeHumidity;
    float dewPointK;
    float wetBulbK;
    float instability;
    float cloudCover;
    float cloudBaseHeight;
    float precipitation;
    float drizzle;
    float rain;
    float snow;
    float sleet;
    float freezingRain;
    float hail;
    float storm;
    float lightning;
    float fog;
    float frost;
    float dust;
    float wind;
    float gust;
    float windAngle;
    float visibility;
    float heatWave;
    float coldSnap;
    float rainbow;
    float aurora;
} WeatherFieldSample;

WeatherFieldSample WeatherFieldSampleAt(const WeatherFieldInput *input);
float WeatherFieldSkyFactor(WeatherFieldSample sample);
const char *WeatherPhenomenonName(WeatherPhenomenon phenomenon);
bool WeatherPhenomenonFromName(const char *name,
                               WeatherPhenomenon *outPhenomenon);
bool WeatherSampleHasPhenomenon(WeatherFieldSample sample,
                                WeatherPhenomenon phenomenon);

#endif
