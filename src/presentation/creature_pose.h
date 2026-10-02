#ifndef VOXELCRAFT_CREATURE_POSE_H
#define VOXELCRAFT_CREATURE_POSE_H

#include "ecology/entity.h"

typedef struct CreatureModulePose {
    Vector3 position;
    float yaw;
    float pitch;
    float roll;
} CreatureModulePose;

typedef float (*CreatureGroundHeight)(Vector3 foot, void *context);

Vector3 CreaturePosePoint(CreatureModulePose pose, float along,
                         float across, float up);
void CreaturePoseBuild(const Entity *entity, float scale,
                       CreatureGroundHeight ground, void *context,
                       CreatureModulePose poses[EVOLUTION_MAX_MODULES]);

#endif
