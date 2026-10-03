#ifndef VOXELCRAFT_RENDER_DEBUG_HUD_INTERNAL_H
#define VOXELCRAFT_RENDER_DEBUG_HUD_INTERNAL_H

/* The UI asset loader resolves the font path; the HUD owns its font and
 * scrolling state. A missing font uses the ordinary UI text fallback. */
void DebugHudFontInit(const char *fontPath);
void DebugHudFontShutdown(void);

#endif
