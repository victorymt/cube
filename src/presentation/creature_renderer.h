#ifndef VOXELCRAFT_CREATURE_RENDERER_H
#define VOXELCRAFT_CREATURE_RENDERER_H

#include "ecology/entity.h"

void CreatureRendererSetSilhouette(bool enabled);

void CreatureRendererDrawBox(Vector3 center, Vector3 size, float yaw,
                             float pitch, float roll, Color color);

bool CreatureRendererDrawAquatic(const Entity *entity,
                                 PlanetFaunaRuntimeState runtime,
                                 Color body, Color accent, float scale);

#endif
