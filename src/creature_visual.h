#ifndef VOXELCRAFT_CREATURE_VISUAL_H
#define VOXELCRAFT_CREATURE_VISUAL_H

#include "evolution.h"

#include <stdbool.h>

typedef struct CreatureAquaticVisualProfile {
    float torsoLength;
    float torsoWidth;
    float torsoHeight;
    float headLength;
    float headWidth;
    float headHeight;
    float tailLength;
    float tailWidth;
    float tailHeight;
    float finSpan;
    float finChord;
    float finThickness;
    unsigned finPairs;
} CreatureAquaticVisualProfile;

bool CreatureAquaticVisualProfileBuild(
    const CreaturePhenotype *phenotype,
    CreatureAquaticVisualProfile *outProfile);

#endif
