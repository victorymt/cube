#ifndef VOXELCRAFT_AUDIO_H
#define VOXELCRAFT_AUDIO_H

#include <stdbool.h>

void AudioInit(void);
void AudioShutdown(void);
void AudioPlayBreak(void);
void AudioPlayPlace(void);
void AudioPlayStep(void);
void AudioPlayWaterStep(void);
void AudioPlaySplash(void);
void AudioPlayPick(void);
void AudioSetRain(bool enabled);
bool AudioToggleMusic(void);
bool AudioIsMusicEnabled(void);
void AudioUpdate(void);

#endif
