#ifndef VOXELCRAFT_ENTITY_RENDERER_H
#define VOXELCRAFT_ENTITY_RENDERER_H

#include "ecology/entity.h"

void EntityRendererDraw(const Entity *entities, int count);
void EntityRendererDrawInterpolated(const Entity *entities, int count,
                                    float interpolationAlpha);

#endif
