#include "presentation/audio.h"

#include "raylib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_MAX_CHANNEL 28000

typedef void (*WaveGenerator)(short *samples, int count, int sampleRate);

static Sound soundBreak = { 0 };
static Sound soundPlace = { 0 };
static Sound soundStep = { 0 };
static Sound soundWaterStep = { 0 };
static Sound soundSplash = { 0 };
static Sound soundPick = { 0 };
static Sound soundRain = { 0 };
static Sound soundWind = { 0 };
static Sound soundForest = { 0 };
static Sound soundWater = { 0 };
static Sound soundCave = { 0 };
static Sound soundNether = { 0 };
static Sound soundShip = { 0 };
static Sound soundThunder = { 0 };
static Sound soundMusic = { 0 };
static bool audioReady = false;
static bool rainEnabled = false;
static bool musicEnabled = true;
static float masterVolume = 1.0f;
static float ambientVolume = 0.70f;
static float musicVolume = 0.22f;
static AudioEnvironmentState environmentTarget = { 0 };
static AudioEnvironmentState environmentCurrent = { 0 };
static float previousLightning = 0.0f;

static float WaveEnv(float t, float duration, float exponent)
{
    if (t >= duration) return 0.0f;
    float x = 1.0f - t / duration;
    return powf(x, exponent);
}

static void GenBreak(short *samples, int count, int sampleRate)
{
    for (int i = 0; i < count; i++) {
        float t = (float)i / (float)sampleRate;
        float env = WaveEnv(t, 0.12f, 1.2f);
        float noise = (float)(rand() % 1000) / 500.0f - 1.0f;
        samples[i] = (short)(noise * env * AUDIO_MAX_CHANNEL);
    }
}

static void GenPlace(short *samples, int count, int sampleRate)
{
    float duration = 0.09f;
    for (int i = 0; i < count; i++) {
        float t = (float)i / (float)sampleRate;
        float env = WaveEnv(t, duration, 2.0f);
        float freq = 520.0f - 180.0f * (t / duration);
        float tone = sinf(2.0f * PI * freq * t);
        float noise = (float)(rand() % 500) / 250.0f - 1.0f;
        samples[i] = (short)((tone * 0.8f + noise * 0.2f) * env * AUDIO_MAX_CHANNEL * 0.8f);
    }
}

static void GenStep(short *samples, int count, int sampleRate)
{
    for (int i = 0; i < count; i++) {
        float t = (float)i / (float)sampleRate;
        float env = WaveEnv(t, 0.07f, 1.8f);
        float v = sinf(2.0f * PI * 70.0f * t) * 0.7f + sinf(2.0f * PI * 45.0f * t) * 0.3f;
        samples[i] = (short)(v * env * AUDIO_MAX_CHANNEL * 0.55f);
    }
}

static void GenWaterStep(short *samples, int count, int sampleRate)
{
    for (int i = 0; i < count; i++) {
        float t = (float)i / (float)sampleRate;
        float env = WaveEnv(t, 0.09f, 1.5f);
        float noise = (float)(rand() % 600) / 300.0f - 1.0f;
        float tone = sinf(2.0f * PI * 210.0f * t);
        samples[i] = (short)((noise * 0.7f + tone * 0.3f) * env * AUDIO_MAX_CHANNEL * 0.45f);
    }
}

static void GenSplash(short *samples, int count, int sampleRate)
{
    float duration = 0.35f;
    for (int i = 0; i < count; i++) {
        float t = (float)i / (float)sampleRate;
        float pulse = 0.5f + 0.5f * sinf(2.0f * PI * 9.0f * t);
        float env = WaveEnv(t, duration, 1.4f) * pulse;
        float noise = (float)(rand() % 1000) / 500.0f - 1.0f;
        float tone = sinf(2.0f * PI * 300.0f * t) * 0.3f;
        samples[i] = (short)((noise + tone) * env * AUDIO_MAX_CHANNEL * 0.7f);
    }
}

static void GenPick(short *samples, int count, int sampleRate)
{
    float duration = 0.11f;
    for (int i = 0; i < count; i++) {
        float t = (float)i / (float)sampleRate;
        float env = WaveEnv(t, duration, 1.6f);
        float freq = 700.0f + 900.0f * (t / duration);
        float v = sinf(2.0f * PI * freq * t);
        samples[i] = (short)(v * env * AUDIO_MAX_CHANNEL * 0.7f);
    }
}

static void GenMusic(short *samples, int count, int sampleRate)
{
    static const float chords[4][3] = {
        { 130.81f, 164.81f, 196.00f },
        { 174.61f, 220.00f, 261.63f },
        { 110.00f, 130.81f, 164.81f },
        { 196.00f, 246.94f, 293.66f }
    };
    float chordDur = 2.0f;
    for (int i = 0; i < count; i++) {
        float t = (float)i / (float)sampleRate;
        int chordIndex = ((int)(t / chordDur)) % 4;
        float chordT = fmodf(t, chordDur);
        float env = sinf(PI * chordT / chordDur);
        float v = 0.0f;
        for (int k = 0; k < 3; k++) {
            float freq = chords[chordIndex][k];
            v += sinf(2.0f * PI * freq * t) * 0.30f;
            v += sinf(2.0f * PI * freq * 2.0f * t) * 0.08f;
        }
        float lfo = 0.75f + 0.25f * sinf(2.0f * PI * 0.13f * t);
        samples[i] = (short)(v * env * lfo * 9000.0f);
    }
}

static void GenRain(short *samples, int count, int sampleRate)
{
    (void)sampleRate;
    float lowpass = 0.0f;
    for (int i = 0; i < count; i++) {
        float noise = (float)(rand() % 1000) / 500.0f - 1.0f;
        lowpass = lowpass * 0.72f + noise * 0.28f;
        samples[i] = (short)(lowpass * AUDIO_MAX_CHANNEL * 0.42f);
    }
}

static void GenWind(short *samples, int count, int sampleRate)
{
    float slow = 0.0f;
    float fast = 0.0f;
    for (int i = 0; i < count; i++) {
        float noise = (float)(rand() % 2001) / 1000.0f - 1.0f;
        slow = slow * 0.992f + noise * 0.008f;
        fast = fast * 0.86f + noise * 0.14f;
        float t = (float)i / (float)sampleRate;
        float gust = 0.55f + 0.45f * sinf(2.0f * PI * 0.085f * t);
        samples[i] = (short)((slow * 2.2f + fast * 0.28f) * gust * 10500.0f);
    }
}

static void GenForest(short *samples, int count, int sampleRate)
{
    float rustle = 0.0f;
    for (int i = 0; i < count; i++) {
        float t = (float)i / (float)sampleRate;
        float noise = (float)(rand() % 2001) / 1000.0f - 1.0f;
        rustle = rustle * 0.94f + noise * 0.06f;
        float birds = sinf(2.0f * PI * (1150.0f + 180.0f * sinf(t * 0.7f)) * t);
        float birdGate = fmaxf(sinf(2.0f * PI * 0.18f * t), 0.0f);
        samples[i] = (short)((rustle * 0.34f + birds * birdGate * birdGate * 0.08f) *
                             8500.0f);
    }
}

static void GenNether(short *samples, int count, int sampleRate)
{
    float rumble = 0.0f;
    for (int i = 0; i < count; i++) {
        float t = (float)i / (float)sampleRate;
        float noise = (float)(rand() % 2001) / 1000.0f - 1.0f;
        rumble = rumble * 0.997f + noise * 0.003f;
        float tone = sinf(2.0f * PI * 38.0f * t) * 0.38f +
                     sinf(2.0f * PI * 57.0f * t) * 0.14f;
        samples[i] = (short)((tone + rumble * 2.5f) * 11000.0f);
    }
}

static void GenShip(short *samples, int count, int sampleRate)
{
    for (int i = 0; i < count; i++) {
        float t = (float)i / (float)sampleRate;
        float hum = sinf(2.0f * PI * 55.0f * t) * 0.55f +
                    sinf(2.0f * PI * 110.0f * t) * 0.18f;
        float pulse = 0.82f + 0.18f * sinf(2.0f * PI * 0.42f * t);
        samples[i] = (short)(hum * pulse * 7800.0f);
    }
}

static void GenThunder(short *samples, int count, int sampleRate)
{
    float rumble = 0.0f;
    float duration = (float)count / (float)sampleRate;
    for (int i = 0; i < count; i++) {
        float t = (float)i / (float)sampleRate;
        float noise = (float)(rand() % 2001) / 1000.0f - 1.0f;
        rumble = rumble * 0.985f + noise * 0.015f;
        float crack = t < 0.12f ? noise * (1.0f - t / 0.12f) : 0.0f;
        float envelope = WaveEnv(t, duration, 1.5f);
        float low = sinf(2.0f * PI * (42.0f + t * 7.0f) * t);
        samples[i] = (short)((crack * 0.7f + rumble * 2.6f + low * 0.25f) *
                             envelope * 15000.0f);
    }
}

static Sound LoadSynthesized(int sampleCount, WaveGenerator generator)
{
    short *samples = calloc((size_t)sampleCount, sizeof(*samples));
    if (!samples) return (Sound){ 0 };

    generator(samples, sampleCount, AUDIO_SAMPLE_RATE);

    Wave wave = { 0 };
    wave.frameCount = (unsigned int)sampleCount;
    wave.sampleRate = AUDIO_SAMPLE_RATE;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = samples;

    Sound sound = LoadSoundFromWave(wave);
    free(samples);
    return sound;
}

static Sound LoadAmbient(const char *path, int sampleCount,
                         WaveGenerator fallback)
{
    Sound sound = { 0 };
    if (FileExists(path)) sound = LoadSound(path);
    if (sound.frameCount == 0) {
        char applicationPath[512];
        const char *directory = GetApplicationDirectory();
        if (directory) {
            size_t length = strlen(directory);
            const char *separator = length > 0 && directory[length - 1] == '/' ?
                                        "" : "/";
            int written = snprintf(applicationPath, sizeof(applicationPath),
                                   "%s%s%s", directory, separator, path);
            if (written > 0 && (size_t)written < sizeof(applicationPath) &&
                FileExists(applicationPath)) {
                sound = LoadSound(applicationPath);
            }
        }
    }
    if (sound.frameCount == 0) sound = LoadSynthesized(sampleCount, fallback);
    return sound;
}

void AudioInit(void)
{
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;

    soundBreak = LoadSynthesized((int)(AUDIO_SAMPLE_RATE * 0.12f), GenBreak);
    soundPlace = LoadSynthesized((int)(AUDIO_SAMPLE_RATE * 0.09f), GenPlace);
    soundStep = LoadSynthesized((int)(AUDIO_SAMPLE_RATE * 0.07f), GenStep);
    soundWaterStep = LoadSynthesized((int)(AUDIO_SAMPLE_RATE * 0.09f), GenWaterStep);
    soundSplash = LoadSynthesized((int)(AUDIO_SAMPLE_RATE * 0.35f), GenSplash);
    soundPick = LoadSynthesized((int)(AUDIO_SAMPLE_RATE * 0.11f), GenPick);
    soundRain = LoadAmbient("assets/audio/rain.ogg",
                            (int)(AUDIO_SAMPLE_RATE * 4.0f), GenRain);
    soundWind = LoadSynthesized((int)(AUDIO_SAMPLE_RATE * 8.0f), GenWind);
    soundForest = LoadSynthesized((int)(AUDIO_SAMPLE_RATE * 8.0f), GenForest);
    soundWater = LoadAmbient("assets/audio/water.ogg",
                             (int)(AUDIO_SAMPLE_RATE * 4.0f), GenRain);
    soundCave = LoadAmbient("assets/audio/cave.ogg",
                            (int)(AUDIO_SAMPLE_RATE * 8.0f), GenNether);
    soundNether = LoadSynthesized((int)(AUDIO_SAMPLE_RATE * 8.0f), GenNether);
    soundShip = LoadSynthesized((int)(AUDIO_SAMPLE_RATE * 8.0f), GenShip);
    soundThunder = LoadSynthesized((int)(AUDIO_SAMPLE_RATE * 3.2f), GenThunder);
    SetSoundVolume(soundRain, 0.0f);
    SetSoundVolume(soundWind, 0.0f);
    SetSoundVolume(soundForest, 0.0f);
    SetSoundVolume(soundWater, 0.0f);
    SetSoundVolume(soundCave, 0.0f);
    SetSoundVolume(soundNether, 0.0f);
    SetSoundVolume(soundShip, 0.0f);
    soundMusic = LoadSynthesized((int)(AUDIO_SAMPLE_RATE * 8.0f), GenMusic);
    SetSoundVolume(soundMusic, 0.22f);

    audioReady = true;
}

void AudioShutdown(void)
{
    if (!audioReady) {
        CloseAudioDevice();
        return;
    }

    UnloadSound(soundBreak);
    UnloadSound(soundPlace);
    UnloadSound(soundStep);
    UnloadSound(soundWaterStep);
    UnloadSound(soundSplash);
    UnloadSound(soundPick);
    UnloadSound(soundRain);
    UnloadSound(soundWind);
    UnloadSound(soundForest);
    UnloadSound(soundWater);
    UnloadSound(soundCave);
    UnloadSound(soundNether);
    UnloadSound(soundShip);
    UnloadSound(soundThunder);
    UnloadSound(soundMusic);
    audioReady = false;
    CloseAudioDevice();
}

void AudioPlayBreak(void)
{
    if (audioReady) PlaySound(soundBreak);
}

void AudioPlayPlace(void)
{
    if (audioReady) PlaySound(soundPlace);
}

void AudioPlayStep(void)
{
    if (audioReady) PlaySound(soundStep);
}

void AudioPlayWaterStep(void)
{
    if (audioReady) PlaySound(soundWaterStep);
}

void AudioPlaySplash(void)
{
    if (audioReady) PlaySound(soundSplash);
}

void AudioPlayPick(void)
{
    if (audioReady) PlaySound(soundPick);
}

void AudioSetRain(bool enabled)
{
    if (rainEnabled == enabled) return;
    rainEnabled = enabled;
    environmentTarget.rain = enabled ? 0.72f : 0.0f;
}

static float AudioUnit(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    return value >= 1.0f ? 1.0f : value;
}

AudioEnvironmentState AudioEnvironmentFromPresentation(
    const EnvironmentPresentationState *presentation)
{
    if (!presentation) return (AudioEnvironmentState){ 0 };
    return (AudioEnvironmentState){
        .rain = AudioUnit(presentation->audioRain),
        .wind = AudioUnit(presentation->audioWind),
        .forest = AudioUnit(presentation->audioForest),
        .water = AudioUnit(presentation->audioWater),
        .cave = AudioUnit(presentation->audioCave),
        .nether = AudioUnit(presentation->audioNether),
        .ship = AudioUnit(presentation->audioShip),
        .lightning = AudioUnit(presentation->lightningFlash)
    };
}

void AudioSetEnvironment(const AudioEnvironmentState *state)
{
    if (!state) {
        environmentTarget = (AudioEnvironmentState){ 0 };
        return;
    }
    environmentTarget = (AudioEnvironmentState){
        .rain = AudioUnit(state->rain),
        .wind = AudioUnit(state->wind),
        .forest = AudioUnit(state->forest),
        .water = AudioUnit(state->water),
        .cave = AudioUnit(state->cave),
        .nether = AudioUnit(state->nether),
        .ship = AudioUnit(state->ship),
        .lightning = AudioUnit(state->lightning)
    };
}

void AudioSetVolumes(float master, float ambient, float music)
{
    masterVolume = AudioUnit(master);
    ambientVolume = AudioUnit(ambient);
    musicVolume = AudioUnit(music);
    if (audioReady) SetMasterVolume(masterVolume);
}

void AudioSetMusicEnabled(bool enabled)
{
    musicEnabled = enabled;
    if (audioReady && !musicEnabled) StopSound(soundMusic);
}

float AudioMasterVolume(void) { return masterVolume; }
float AudioAmbientVolume(void) { return ambientVolume; }
float AudioMusicVolume(void) { return musicVolume; }

static float AudioApproach(float current, float target, float dt)
{
    if (!isfinite(dt) || dt <= 0.0f) return current;
    float amount = 1.0f - expf(-fminf(dt, 0.25f) * 2.2f);
    return current + (target - current) * amount;
}

static void UpdateAmbientSound(Sound sound, float current, float target,
                               float gain)
{
    float volume = AudioUnit(current * ambientVolume * gain);
    SetSoundVolume(sound, volume);
    if ((current > 0.002f || target > 0.002f) && !IsSoundPlaying(sound)) {
        PlaySound(sound);
    } else if (current < 0.001f && target < 0.001f && IsSoundPlaying(sound)) {
        StopSound(sound);
    }
}

void AudioUpdate(float dt)
{
    if (!audioReady) return;
#define APPROACH(field) environmentCurrent.field = AudioApproach( \
    environmentCurrent.field, environmentTarget.field, dt)
    APPROACH(rain);
    APPROACH(wind);
    APPROACH(forest);
    APPROACH(water);
    APPROACH(cave);
    APPROACH(nether);
    APPROACH(ship);
#undef APPROACH

    UpdateAmbientSound(soundRain, environmentCurrent.rain,
                       environmentTarget.rain, 0.82f);
    UpdateAmbientSound(soundWind, environmentCurrent.wind,
                       environmentTarget.wind, 0.54f);
    UpdateAmbientSound(soundForest, environmentCurrent.forest,
                       environmentTarget.forest, 0.42f);
    UpdateAmbientSound(soundWater, environmentCurrent.water,
                       environmentTarget.water, 0.58f);
    UpdateAmbientSound(soundCave, environmentCurrent.cave,
                       environmentTarget.cave, 0.34f);
    UpdateAmbientSound(soundNether, environmentCurrent.nether,
                       environmentTarget.nether, 0.52f);
    UpdateAmbientSound(soundShip, environmentCurrent.ship,
                       environmentTarget.ship, 0.48f);
    if (environmentTarget.lightning > 0.18f && previousLightning <= 0.18f) {
        SetSoundVolume(soundThunder,
                       AudioUnit(environmentTarget.lightning * ambientVolume * 0.9f));
        PlaySound(soundThunder);
    }
    previousLightning = environmentTarget.lightning;
    SetSoundVolume(soundMusic, musicEnabled ? musicVolume : 0.0f);
    if (musicEnabled && musicVolume > 0.001f && !IsSoundPlaying(soundMusic)) {
        PlaySound(soundMusic);
    }
}

bool AudioToggleMusic(void)
{
    AudioSetMusicEnabled(!musicEnabled);
    return musicEnabled;
}

bool AudioIsMusicEnabled(void)
{
    return musicEnabled;
}
