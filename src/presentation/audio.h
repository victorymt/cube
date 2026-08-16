#ifndef VOXELCRAFT_AUDIO_H
#define VOXELCRAFT_AUDIO_H

#include <stdbool.h>

#include "presentation/environment_presentation.h"

typedef struct AudioEnvironmentState {
    float rain;
    float wind;
    float forest;
    float water;
    float cave;
    float nether;
    float ship;
    float lightning;
} AudioEnvironmentState;

void AudioInit(void);
void AudioShutdown(void);
void AudioPlayBreak(void);
void AudioPlayPlace(void);
void AudioPlayStep(void);
void AudioPlayWaterStep(void);
void AudioPlaySplash(void);
void AudioPlayPick(void);
void AudioSetRain(bool enabled);
void AudioSetEnvironment(const AudioEnvironmentState *state);
AudioEnvironmentState AudioEnvironmentFromPresentation(
    const EnvironmentPresentationState *presentation);
void AudioSetVolumes(float master, float ambient, float music);
void AudioSetMusicEnabled(bool enabled);
float AudioMasterVolume(void);
float AudioAmbientVolume(void);
float AudioMusicVolume(void);
bool AudioToggleMusic(void);
bool AudioIsMusicEnabled(void);
void AudioUpdate(float dt);

#endif
