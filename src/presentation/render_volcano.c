#include "presentation/render_volcano.h"
#include "world/ash_runtime.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>

void DrawLavaCell(Matrix sectionTransform, Vector3 position, LavaThermalState thermal, LavaRock rock)
{
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(sectionTransform));
    float solid = (float)(thermal.volume - LavaLiquidVolume(thermal)) / LAVA_CAPACITY;
    float total = (float)thermal.volume / LAVA_CAPACITY;
    if (solid > 0.0f) {
        Vector3 center = { position.x + 0.5f, position.y + solid * 0.5f, position.z + 0.5f };
        Color color = { 57, 53, 50, 255 };
        if (rock == LAVA_ROCK_OBSIDIAN) color = (Color){ 22, 16, 30, 255 };
        if (rock == LAVA_ROCK_SCORIA) color = (Color){ 73, 48, 45, 255 };
        if (rock == LAVA_ROCK_PUMICE) color = (Color){ 167, 157, 143, 255 };
        if (rock == LAVA_ROCK_RHYOLITE) color = (Color){ 172, 128, 119, 255 };
        DrawCube(center, 1.0f, solid, 1.0f, color);
    }
    if (total > solid) {
        float hot = (float)Clamp((float)(LavaTemperature(thermal) - LAVA_FREEZING_K) / 350.0f, 0, 1);
        Vector3 center = { position.x + 0.5f, position.y + (total + solid) * 0.5f, position.z + 0.5f };
        DrawCube(center, 1.0f, total - solid, 1.0f,
            (Color){ (unsigned char)(190 + hot * 65), (unsigned char)(38 + hot * 85), 12, 255 });
    }
    rlPopMatrix();
}

void DrawAshCell(Matrix transform, Vector3 position, unsigned volume)
{
    if (!volume || volume > ASH_CAPACITY) return;
    float height = (float)volume / ASH_CAPACITY;
    rlPushMatrix(); rlMultMatrixf(MatrixToFloat(transform));
    DrawCube((Vector3){ position.x + 0.5f, position.y + height * 0.5f, position.z + 0.5f },
        1.0f, height, 1.0f, (Color){ 96, 89, 85, 255 });
    rlPopMatrix();
}

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

void DrawLavaSteam(Matrix sectionTransform, Vector3 position,
                   double recentWater, uint64_t ticks, float windAngle, float windStrength)
{
    if (!isfinite(recentWater) || recentWater < 0.05) return;
    float strength = (float)sqrt(fmin(1.0, recentWater / 3.0));
    float phase = (float)(ticks % 20u) / 20.0f;
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(sectionTransform));
    for (unsigned puff = 0u; puff < LAVA_STEAM_PUFFS_PER_CELL; puff++) {
        float age = fmodf(phase + (float)puff / LAVA_STEAM_PUFFS_PER_CELL, 1.0f);
        float rise = 0.15f + age * 2.4f;
        Vector3 center = { position.x + cosf(windAngle) * age * windStrength * 0.6f,
            position.y + rise, position.z + sinf(windAngle) * age * windStrength * 0.6f };
        float size = (0.18f + age * 0.65f) * (0.5f + strength * 0.5f);
        DrawCube(center, size, size * 0.7f, size,
            (Color){ 226, 234, 238, (unsigned char)(strength * (1.0f - age) * 155.0f) });
    }
    rlPopMatrix();
}
