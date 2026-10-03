#include "presentation/render_volcano.h"
#include "raymath.h"
#include <math.h>

void DrawVolcanoPreviews(const VolcanoVisual *visuals, unsigned count,
                         float windAngle, float windStrength)
{
    if (!visuals) return;
    for (unsigned i = 0u; i < count; i++) {
        const VolcanoVisual *v = &visuals[i];
        if (v->state.phase == VOLCANO_DORMANT || v->state.phase == VOLCANO_RECOVERY) continue;
        float strength = v->state.intensity;
        float phase = (float)(v->state.activeTicks % 4096u) * 0.5f;
        float activity = v->state.phase == VOLCANO_UNREST ? 0.28f :
            v->state.phase == VOLCANO_COOLING ? 0.12f : 1.0f;
        Vector3 base = v->position;
        base.y += 0.08f;
        DrawCylinder(base, 1.8f, 2.2f, 0.35f, 12, (Color){ 54, 39, 34, 255 });
        DrawCylinder((Vector3){ base.x, base.y + 0.36f, base.z },
            1.1f, 1.1f, 0.06f, 12, (Color){ 255, (unsigned char)(65 + activity * 65), 20, 255 });
        /* A low opaque core remains visible even with all particles disabled. */
        for (unsigned puff = 0u; puff < 10u; puff++) {
            float height = 1.0f + (float)puff * (1.0f + strength);
            float wobble = sinf(phase * 0.8f + (float)puff * 1.7f);
            Vector3 center = { base.x + cosf(windAngle) * height * windStrength * 0.4f + wobble * 0.2f,
                base.y + height,
                base.z + sinf(windAngle) * height * windStrength * 0.4f };
            float radius = (0.55f + height * 0.10f) * (0.55f + activity * 0.45f);
            DrawSphereEx(center, radius, 6, 8,
                (Color){ 83, 78, 73, (unsigned char)(145 - puff * 10u) });
        }
        if (v->state.phase == VOLCANO_ERUPTING || v->state.phase == VOLCANO_WANING) {
            for (unsigned spark = 0u; spark < 6u; spark++) {
                float t = fmodf(phase * 0.7f + (float)spark / 6.0f, 1.0f);
                float angle = (float)spark * 2.39996f;
                Vector3 point = { base.x + cosf(angle) * t * 4.0f,
                    base.y + 0.4f + 12.0f * t * (1.0f - t) * strength,
                    base.z + sinf(angle) * t * 4.0f };
                DrawSphereEx(point, 0.12f + strength * 0.10f, 4, 6, ORANGE);
            }
        }
    }
}
