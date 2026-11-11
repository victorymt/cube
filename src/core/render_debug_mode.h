#ifndef VOXELCRAFT_RENDER_DEBUG_MODE_H
#define VOXELCRAFT_RENDER_DEBUG_MODE_H

#include <stdbool.h>
#include <string.h>

/* World-surface shader views used by the debug DSL. Keep final first so a
 * zero-initialized control block and renderer start on the normal path. */
typedef enum RenderDebugMode {
    RENDER_DEBUG_MODE_FINAL = 0,
    RENDER_DEBUG_MODE_ALBEDO,
    RENDER_DEBUG_MODE_NORMAL,
    RENDER_DEBUG_MODE_AO,
    RENDER_DEBUG_MODE_DIFFUSE,
    RENDER_DEBUG_MODE_SPECULAR,
    RENDER_DEBUG_MODE_SHADOW,
    RENDER_DEBUG_MODE_COUNT
} RenderDebugMode;

static inline const char *RenderDebugModeName(RenderDebugMode mode)
{
    switch (mode) {
    case RENDER_DEBUG_MODE_FINAL: return "final";
    case RENDER_DEBUG_MODE_ALBEDO: return "albedo";
    case RENDER_DEBUG_MODE_NORMAL: return "normal";
    case RENDER_DEBUG_MODE_AO: return "ao";
    case RENDER_DEBUG_MODE_DIFFUSE: return "diffuse";
    case RENDER_DEBUG_MODE_SPECULAR: return "specular";
    case RENDER_DEBUG_MODE_SHADOW: return "shadow";
    case RENDER_DEBUG_MODE_COUNT: break;
    }
    return "final";
}

static inline bool RenderDebugModeParse(const char *text, RenderDebugMode *mode)
{
    if (!text || !mode) return false;
    if (strcmp(text, "final") == 0 || strcmp(text, "off") == 0)
        *mode = RENDER_DEBUG_MODE_FINAL;
    else if (strcmp(text, "albedo") == 0)
        *mode = RENDER_DEBUG_MODE_ALBEDO;
    else if (strcmp(text, "normal") == 0)
        *mode = RENDER_DEBUG_MODE_NORMAL;
    else if (strcmp(text, "ao") == 0)
        *mode = RENDER_DEBUG_MODE_AO;
    else if (strcmp(text, "diffuse") == 0)
        *mode = RENDER_DEBUG_MODE_DIFFUSE;
    else if (strcmp(text, "specular") == 0)
        *mode = RENDER_DEBUG_MODE_SPECULAR;
    else if (strcmp(text, "shadow") == 0)
        *mode = RENDER_DEBUG_MODE_SHADOW;
    else
        return false;
    return true;
}

#endif
