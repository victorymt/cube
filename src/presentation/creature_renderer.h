#ifndef VOXELCRAFT_CREATURE_RENDERER_H
#define VOXELCRAFT_CREATURE_RENDERER_H

#include "ecology/entity.h"

bool CreatureRendererDrawAquatic(const Entity *entity,
                                 PlanetFaunaRuntimeState runtime,
                                 Color body, Color accent, float scale);

#endif
