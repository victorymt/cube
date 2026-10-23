#ifndef VOXELCRAFT_PRESENTATION_RAYLIB_COMPAT_H
#define VOXELCRAFT_PRESENTATION_RAYLIB_COMPAT_H

/* Thin shims over raylib API drift.
 *
 * The project is built against more than one raylib: the distro package is
 * typically 5.x, while `make wayland` fetches a newer release so the game can
 * open a window on a Wayland-only session. Version differences that would
 * otherwise scatter `#if RAYLIB_VERSION_MAJOR` across drawing code belong
 * here, named after what the caller means rather than how raylib spells it. */

#include "raylib.h"

/* raylib 6 changed DrawCircleGradient from taking an integer centre to a
 * Vector2. Callers only ever pass integer pixel coordinates, so keep that
 * shape and convert inside. */
static inline void VoxelcraftDrawCircleGradient(int centerX, int centerY,
                                                float radius, Color inner,
                                                Color outer)
{
#if defined(RAYLIB_VERSION_MAJOR) && RAYLIB_VERSION_MAJOR >= 6
    DrawCircleGradient((Vector2){ (float)centerX, (float)centerY },
                       radius, inner, outer);
#else
    DrawCircleGradient(centerX, centerY, radius, inner, outer);
#endif
}

#endif /* VOXELCRAFT_PRESENTATION_RAYLIB_COMPAT_H */
