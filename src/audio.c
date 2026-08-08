#include "audio.h"

#include "raylib.h"

#include <math.h>
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
static Sound soundMusic = { 0 };
static bool audioReady = false;
static bool rainEnabled = false;
static bool musicEnabled = true;

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
    soundRain = LoadSynthesized((int)(AUDIO_SAMPLE_RATE * 4.0f), GenRain);
    SetSoundVolume(soundRain, 0.6f);
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
    if (!audioReady) return;

    if (enabled) {
        PlaySound(soundRain);
        SetSoundVolume(soundRain, 0.6f);
    } else {
        StopSound(soundRain);
    }
}

void AudioUpdate(void)
{
    if (!audioReady) return;
    if (rainEnabled && !IsSoundPlaying(soundRain)) PlaySound(soundRain);
    if (musicEnabled && !IsSoundPlaying(soundMusic)) PlaySound(soundMusic);
}

bool AudioToggleMusic(void)
{
    musicEnabled = !musicEnabled;
    if (audioReady && !musicEnabled) StopSound(soundMusic);
    return musicEnabled;
}

bool AudioIsMusicEnabled(void)
{
    return musicEnabled;
}
