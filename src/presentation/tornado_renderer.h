#ifndef VOXELCRAFT_TORNADO_RENDERER_H
#define VOXELCRAFT_TORNADO_RENDERER_H

#include "presentation/render_quality.h"
#include "world/tornado_model.h"

#include "raylib.h"

void DrawTornadoFunnel(const Camera3D *camera, const TornadoState *tornado,
                       GraphicsQuality quality, float daylight);
void DrawTornadoOverlay(const Camera3D *camera, const TornadoState *tornado);

#endif
