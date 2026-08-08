#include "weather.h"

#include "particles.h"
#include "audio.h"

#include <stdlib.h>

#define WEATHER_AREA_RADIUS 26.0f
#define WEATHER_TOP_OFFSET 16.0f
#define WEATHER_SKY_SMOOTHING 0.10f

static Weather current = WEATHER_CLEAR;
static float skyFactor = 0.0f;
static float changeTimer = 40.0f;
static float emissionAccumulator = 0.0f;

void WeatherInit(void)
{
    current = WEATHER_CLEAR;
    skyFactor = 0.0f;
    changeTimer = 40.0f + (float)(rand() % 100);
    emissionAccumulator = 0.0f;
    AudioSetRain(false);
}

const char *WeatherName(void)
{
    switch (current) {
    case WEATHER_RAIN: return "Rain";
    case WEATHER_SNOW: return "Snow";
    default: return "Clear";
    }
}

Weather WeatherGetCurrent(void)
{
    return current;
}

float WeatherSkyFactor(void)
{
    return skyFactor;
}

static Weather PickNextWeather(bool coldArea)
{
    int roll = rand() % 100;
    if (current == WEATHER_CLEAR) {
        if (coldArea) {
            if (roll < 45) return WEATHER_SNOW;
            if (roll < 65) return WEATHER_RAIN;
        } else {
            if (roll < 45) return WEATHER_RAIN;
            if (roll < 60) return WEATHER_SNOW;
        }
        return WEATHER_CLEAR;
    }
    if (roll < 60) return WEATHER_CLEAR;
    if (coldArea) return WEATHER_SNOW;
    return WEATHER_RAIN;
}

void WeatherCycle(bool coldArea)
{
    current = PickNextWeather(coldArea);
    changeTimer = 60.0f + (float)(rand() % 180);
    AudioSetRain(current == WEATHER_RAIN);
}

static void EmitRain(Vector3 playerPosition)
{
    float x = playerPosition.x + ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f * WEATHER_AREA_RADIUS;
    float z = playerPosition.z + ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f * WEATHER_AREA_RADIUS;
    float y = playerPosition.y + WEATHER_TOP_OFFSET + (float)rand() / (float)RAND_MAX * 8.0f;

    ParticlesEmitOne((Vector3){ x, y, z }, (Vector3){ 0.0f, -20.0f, 0.0f },
                     (Color){ 168, 190, 215, 220 }, (Vector3){ 0.03f, 0.5f, 0.03f },
                     1.4f, 0.0f);
}

static void EmitSnow(Vector3 playerPosition)
{
    float x = playerPosition.x + ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f * WEATHER_AREA_RADIUS;
    float z = playerPosition.z + ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f * WEATHER_AREA_RADIUS;
    float y = playerPosition.y + WEATHER_TOP_OFFSET + (float)rand() / (float)RAND_MAX * 8.0f;

    float driftX = ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f;
    float driftZ = ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f;

    ParticlesEmitOne((Vector3){ x, y, z }, (Vector3){ driftX, -2.2f, driftZ },
                     (Color){ 235, 242, 248, 235 }, (Vector3){ 0.07f, 0.07f, 0.07f },
                     3.0f, 0.2f);
}

void WeatherUpdate(float dt, Vector3 playerPosition, bool coldArea)
{
    float targetFactor = 0.0f;
    if (current == WEATHER_RAIN) targetFactor = 0.55f;
    else if (current == WEATHER_SNOW) targetFactor = 0.35f;

    skyFactor += (targetFactor - skyFactor) * WEATHER_SKY_SMOOTHING * dt * 10.0f;
    if (skyFactor < 0.001f) skyFactor = 0.0f;

    changeTimer -= dt;
    if (changeTimer <= 0.0f) {
        current = PickNextWeather(coldArea);
        changeTimer = 60.0f + (float)(rand() % 180);
        AudioSetRain(current == WEATHER_RAIN);
    }

    if (current == WEATHER_CLEAR) return;

    float emitRate = (current == WEATHER_RAIN) ? 70.0f : 28.0f;
    emissionAccumulator += emitRate * dt;
    while (emissionAccumulator >= 1.0f) {
        emissionAccumulator -= 1.0f;
        if (current == WEATHER_RAIN) EmitRain(playerPosition);
        else EmitSnow(playerPosition);
    }
}
