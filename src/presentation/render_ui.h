#ifndef VOXELCRAFT_RENDER_UI_H
#define VOXELCRAFT_RENDER_UI_H

#include "raylib.h"

#include <stdbool.h>
#include <stddef.h>

int UiMeasureText(const char *text, int fontSize);
void UiDrawText(const char *text, int x, int y, int fontSize, Color color);

float HudHeadingFromYaw(float yawRadians);
const char *HudHeadingDirection(float headingDegrees);
void HudFormatStatusLine(char *buffer, size_t bufferSize,
                         Vector3 playerPosition, float yaw, float pitch,
                         float dayTime);
void DrawStatusHUD(Vector3 playerPosition, float yaw, float pitch,
                   float dayTime, bool autoSaveEnabled);

#endif
