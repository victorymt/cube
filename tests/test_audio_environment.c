#include "presentation/audio.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void TestPresentationMapping(void)
{
    EnvironmentPresentationState presentation = {
        .audioRain = 0.72f,
        .audioWind = 0.45f,
        .audioForest = 0.33f,
        .audioWater = 0.64f,
        .audioCave = 0.22f,
        .audioNether = 0.81f,
        .audioShip = 0.58f,
        .audioTornado = 0.76f,
        .lightningFlash = 0.91f
    };
    AudioEnvironmentState audio = AudioEnvironmentFromPresentation(&presentation);
    assert(audio.rain == presentation.audioRain);
    assert(audio.wind == presentation.audioWind);
    assert(audio.forest == presentation.audioForest);
    assert(audio.water == presentation.audioWater);
    assert(audio.cave == presentation.audioCave);
    assert(audio.nether == presentation.audioNether);
    assert(audio.ship == presentation.audioShip);
    assert(audio.tornado == presentation.audioTornado);
    assert(audio.lightning == presentation.lightningFlash);

    presentation.audioRain = NAN;
    presentation.audioWind = 8.0f;
    audio = AudioEnvironmentFromPresentation(&presentation);
    assert(audio.rain == 0.0f);
    assert(audio.wind == 1.0f);
    audio = AudioEnvironmentFromPresentation(NULL);
    assert(audio.rain == 0.0f && audio.ship == 0.0f);
}

static void TestVolumeCategoriesWithoutDevice(void)
{
    AudioSetVolumes(0.8f, 0.6f, 0.3f);
    assert(fabsf(AudioMasterVolume() - 0.8f) < 0.0001f);
    assert(fabsf(AudioAmbientVolume() - 0.6f) < 0.0001f);
    assert(fabsf(AudioMusicVolume() - 0.3f) < 0.0001f);
    AudioSetVolumes(NAN, -1.0f, 2.0f);
    assert(AudioMasterVolume() == 0.0f);
    assert(AudioAmbientVolume() == 0.0f);
    assert(AudioMusicVolume() == 1.0f);
}

int main(void)
{
    TestPresentationMapping();
    TestVolumeCategoriesWithoutDevice();
    puts("audio environment tests passed");
    return 0;
}
