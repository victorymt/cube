#include "creature_renderer.h"

#include "creature_visual.h"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>

typedef struct AquaticDrawContext {
    CreatureAquaticVisualProfile profile;
    Vector3 origin;
    Vector3 forward;
    Vector3 side;
    float scale;
    float yaw;
    float heightScale;
    float tailWave;
    float finWave;
    Color body;
    Color accent;
    Color fin;
} AquaticDrawContext;

static Vector3 CreaturePartPosition(Vector3 origin, Vector3 forward,
                                    Vector3 side, float along, float across,
                                    float y)
{
    Vector3 result = Vector3Add(origin, Vector3Scale(forward, along));
    result = Vector3Add(result, Vector3Scale(side, across));
    result.y += y;
    return result;
}

static void DrawCreatureBox(Vector3 center, Vector3 size, float yaw,
                            float pitch, float roll, Color color)
{
    rlPushMatrix();
    rlTranslatef(center.x, center.y, center.z);
    rlRotatef(yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);
    rlRotatef(pitch * RAD2DEG, 1.0f, 0.0f, 0.0f);
    rlRotatef(roll * RAD2DEG, 0.0f, 0.0f, 1.0f);
    DrawCubeV(Vector3Zero(), size, color);
    rlPopMatrix();
}

static void DrawAquaticBody(const AquaticDrawContext *context)
{
    const CreatureAquaticVisualProfile *profile = &context->profile;
    float length = profile->torsoLength * context->scale;
    float width = profile->torsoWidth * context->scale;
    float height = profile->torsoHeight * context->scale *
                   context->heightScale;
    Color frontColor = ColorLerp(context->body, context->accent, 0.14f);
    Color bellyColor = ColorLerp(context->body,
                                 (Color){ 224, 232, 218, 255 }, 0.18f);

    DrawCreatureBox(context->origin,
                    (Vector3){ width, height, length * 0.62f },
                    context->yaw, 0.0f, 0.0f, context->body);
    DrawCreatureBox(CreaturePartPosition(
                        context->origin, context->forward, context->side,
                        length * 0.36f, 0.0f, 0.0f),
                    (Vector3){ width * 0.82f, height * 0.84f,
                               length * 0.44f },
                    context->yaw, 0.0f, 0.0f, frontColor);
    DrawCreatureBox(CreaturePartPosition(
                        context->origin, context->forward, context->side,
                        -length * 0.36f, 0.0f, 0.0f),
                    (Vector3){ width * 0.68f, height * 0.72f,
                               length * 0.42f },
                    context->yaw, 0.0f, 0.0f, context->body);
    DrawCreatureBox(CreaturePartPosition(
                        context->origin, context->forward, context->side,
                        0.02f * length, 0.0f, -height * 0.38f),
                    (Vector3){ width * 0.72f, height * 0.16f,
                               length * 0.52f },
                    context->yaw, 0.0f, 0.0f, bellyColor);

    float headLength = profile->headLength * context->scale;
    float headWidth = profile->headWidth * context->scale;
    float headHeight = profile->headHeight * context->scale *
                       context->heightScale;
    float headAlong = length * 0.55f + headLength * 0.28f;
    Vector3 headCenter = CreaturePartPosition(
        context->origin, context->forward, context->side,
        headAlong, 0.0f, height * 0.04f);
    DrawCreatureBox(headCenter,
                    (Vector3){ headWidth, headHeight, headLength * 0.72f },
                    context->yaw, 0.0f, 0.0f, frontColor);

    Color eyeColor = ColorLerp((Color){ 8, 18, 24, 255 },
                               context->accent, 0.16f);
    float eyeSize = fmaxf(0.055f, 0.075f * context->scale);
    float eyeAlong = headAlong + headLength * 0.26f;
    float eyeAcross = headWidth * 0.47f;
    float eyeY = headHeight * 0.17f;
    for (int sideSign = -1; sideSign <= 1; sideSign += 2) {
        DrawCreatureBox(CreaturePartPosition(
                            context->origin, context->forward, context->side,
                            eyeAlong, (float)sideSign * eyeAcross, eyeY),
                        (Vector3){ eyeSize, eyeSize, eyeSize },
                        context->yaw, 0.0f, 0.0f, eyeColor);
    }
}

static void DrawAquaticFins(const AquaticDrawContext *context)
{
    const CreatureAquaticVisualProfile *profile = &context->profile;
    float length = profile->torsoLength * context->scale;
    float width = profile->torsoWidth * context->scale;
    float height = profile->torsoHeight * context->scale *
                   context->heightScale;
    float span = profile->finSpan * context->scale;
    float chord = profile->finChord * context->scale;
    float thickness = profile->finThickness * context->scale;

    for (int sideSign = -1; sideSign <= 1; sideSign += 2) {
        float sign = (float)sideSign;
        Vector3 center = CreaturePartPosition(
            context->origin, context->forward, context->side,
            length * 0.12f, sign * (width * 0.43f + span * 0.28f),
            -height * 0.08f);
        DrawCreatureBox(center, (Vector3){ span, thickness, chord },
                        context->yaw + sign * 0.24f, 0.0f,
                        sign * (0.08f + context->finWave * 0.16f),
                        context->fin);

        if (profile->finPairs > 1u) {
            float rearSpan = span * 0.68f;
            Vector3 rearCenter = CreaturePartPosition(
                context->origin, context->forward, context->side,
                -length * 0.34f,
                sign * (width * 0.38f + rearSpan * 0.26f),
                -height * 0.10f);
            DrawCreatureBox(
                rearCenter,
                (Vector3){ rearSpan, thickness * 0.86f, chord * 0.68f },
                context->yaw + sign * 0.17f, 0.0f,
                sign * (0.06f - context->finWave * 0.10f), context->fin);
        }
    }

    Vector3 dorsalCenter = CreaturePartPosition(
        context->origin, context->forward, context->side,
        -length * 0.08f, 0.0f, height * 0.50f + span * 0.15f);
    DrawCreatureBox(
        dorsalCenter,
        (Vector3){ thickness * 1.10f, span * 0.42f, chord * 0.78f },
        context->yaw, -0.10f, 0.0f, context->fin);
}

static void DrawAquaticTail(const AquaticDrawContext *context)
{
    const CreatureAquaticVisualProfile *profile = &context->profile;
    float length = profile->torsoLength * context->scale;
    float tailLength = profile->tailLength * context->scale;
    float tailWidth = profile->tailWidth * context->scale;
    float tailHeight = profile->tailHeight * context->scale *
                       context->heightScale;
    float tailAcross = context->tailWave * tailLength * 0.14f;
    float tailYaw = context->yaw + context->tailWave * 0.24f;
    Vector3 tailCenter = CreaturePartPosition(
        context->origin, context->forward, context->side,
        -length * 0.54f - tailLength * 0.42f, tailAcross, 0.0f);
    DrawCreatureBox(
        tailCenter, (Vector3){ tailWidth, tailHeight, tailLength * 0.90f },
        tailYaw, 0.0f, 0.0f,
        ColorLerp(context->body, context->fin, 0.24f));

    float tailSpan = profile->finSpan * context->scale * 0.82f;
    float tailChord = profile->finChord * context->scale * 0.76f;
    float tailThickness = profile->finThickness * context->scale * 1.15f;
    Vector3 tailTip = CreaturePartPosition(
        context->origin, context->forward, context->side,
        -length * 0.54f - tailLength * 0.88f,
        context->tailWave * tailLength * 0.27f, 0.0f);
    for (int lobe = -1; lobe <= 1; lobe += 2) {
        Vector3 lobeCenter = tailTip;
        lobeCenter.y += (float)lobe * tailSpan * 0.24f;
        DrawCreatureBox(
            lobeCenter,
            (Vector3){ tailThickness, tailSpan * 0.58f, tailChord },
            tailYaw, (float)lobe * 0.48f, 0.0f, context->fin);
    }
}

bool CreatureRendererDrawAquatic(const Entity *entity,
                                 PlanetFaunaRuntimeState runtime,
                                 Color body, Color accent, float scale)
{
    AquaticDrawContext context = { 0 };
    if (!entity || !CreatureAquaticVisualProfileBuild(
                       &entity->phenotype, &context.profile)) {
        return false;
    }

    context.forward = (Vector3){ sinf(entity->yaw), 0.0f,
                                 cosf(entity->yaw) };
    context.side = (Vector3){ context.forward.z, 0.0f,
                              -context.forward.x };
    context.scale = scale;
    context.yaw = entity->yaw;
    context.heightScale = entity->corpse ? 0.36f : 1.0f;
    float animation = entity->corpse ? 0.0f : runtime.animationScale;
    context.tailWave = sinf(entity->phase * 2.75f) * animation;
    context.finWave = sinf(entity->phase * 3.40f + 0.65f) * animation;
    context.body = body;
    context.accent = accent;
    context.fin = ColorLerp(body, accent, 0.52f);
    context.origin = entity->position;
    context.origin.y += context.profile.torsoHeight * scale *
                        (entity->corpse ? 0.15f : 0.38f);

    DrawAquaticBody(&context);
    DrawAquaticFins(&context);
    DrawAquaticTail(&context);
    return true;
}
