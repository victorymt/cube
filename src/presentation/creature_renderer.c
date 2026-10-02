#include "presentation/creature_renderer.h"

#include "ecology/creature_visual.h"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>

static bool silhouetteEnabled;

void CreatureRendererSetSilhouette(bool enabled)
{
    silhouetteEnabled = enabled;
}

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
    FaunaBodyPose pose;
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

void CreatureRendererDrawBox(Vector3 center, Vector3 size, float yaw,
                            float pitch, float roll, Color color)
{
    if (silhouetteEnabled) color = (Color){176, 238, 208, 255};
    rlPushMatrix();
    rlTranslatef(center.x, center.y, center.z);
    rlRotatef(yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);
    rlRotatef(pitch * RAD2DEG, 1.0f, 0.0f, 0.0f);
    rlRotatef(roll * RAD2DEG, 0.0f, 0.0f, 1.0f);
    /* Local face values remain legible even without a scene lighting shader. */
    const float vertices[6][12] = {
        {-1,-1,1, 1,-1,1, 1,1,1, -1,1,1},
        {1,-1,-1, -1,-1,-1, -1,1,-1, 1,1,-1},
        {-1,-1,-1, -1,-1,1, -1,1,1, -1,1,-1},
        {1,-1,1, 1,-1,-1, 1,1,-1, 1,1,1},
        {-1,1,1, 1,1,1, 1,1,-1, -1,1,-1},
        {-1,-1,-1, 1,-1,-1, 1,-1,1, -1,-1,1}
    };
    const float light[6] = {0.88f, 0.62f, 0.72f, 0.82f, 1.0f, 0.48f};
    rlBegin(RL_QUADS);
    for (int face = 0; face < 6; face++) {
        float value = silhouetteEnabled ? 1.0f : light[face];
        rlColor4ub((unsigned char)(color.r * value),
                   (unsigned char)(color.g * value),
                   (unsigned char)(color.b * value), color.a);
        for (int v = 0; v < 4; v++) {
            rlVertex3f(vertices[face][v * 3] * size.x * 0.5f,
                       vertices[face][v * 3 + 1] * size.y * 0.5f,
                       vertices[face][v * 3 + 2] * size.z * 0.5f);
        }
    }
    rlEnd();
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

    CreatureRendererDrawBox(context->origin,
                    (Vector3){ width, height, length * 0.62f },
                    context->yaw, 0.0f, 0.0f, context->body);
    CreatureRendererDrawBox(CreaturePartPosition(
                        context->origin, context->forward, context->side,
                        length * 0.36f, 0.0f, 0.0f),
                    (Vector3){ width * 0.82f, height * 0.84f,
                               length * 0.44f },
                    context->yaw, 0.0f, 0.0f, frontColor);
    CreatureRendererDrawBox(CreaturePartPosition(
                        context->origin, context->forward, context->side,
                        -length * 0.36f, 0.0f, 0.0f),
                    (Vector3){ width * 0.68f, height * 0.72f,
                               length * 0.42f },
                    context->yaw, 0.0f, 0.0f, context->body);
    CreatureRendererDrawBox(CreaturePartPosition(
                        context->origin, context->forward, context->side,
                        0.02f * length, 0.0f, -height * 0.38f),
                    (Vector3){ width * 0.72f, height * 0.16f,
                               length * 0.52f },
                    context->yaw, 0.0f, 0.0f, bellyColor);

    float headLength = profile->headLength * context->scale;
    float headWidth = profile->headWidth * context->scale;
    float headHeight = profile->headHeight * context->scale *
                       context->heightScale;
    float headAlong = length * 0.55f + headLength * 0.28f +
                      context->pose.strike * 0.18f * context->scale;
    Vector3 headCenter = CreaturePartPosition(
        context->origin, context->forward, context->side,
        headAlong, 0.0f, height * 0.04f);
    CreatureRendererDrawBox(headCenter,
                    (Vector3){ headWidth, headHeight, headLength * 0.72f },
                    context->yaw, 0.0f, 0.0f, frontColor);

    Color eyeColor = ColorLerp((Color){ 8, 18, 24, 255 },
                               context->accent, 0.16f);
    float eyeSize = fmaxf(0.055f, 0.075f * context->scale);
    float eyeAlong = headAlong + headLength * 0.26f;
    float eyeAcross = headWidth * 0.47f;
    float eyeY = headHeight * 0.17f;
    for (int sideSign = -1; sideSign <= 1; sideSign += 2) {
        CreatureRendererDrawBox(CreaturePartPosition(
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
    float span = profile->finSpan * context->scale * (1 + context->pose.display * 0.45f);
    float chord = profile->finChord * context->scale;
    float thickness = profile->finThickness * context->scale;

    for (int sideSign = -1; sideSign <= 1; sideSign += 2) {
        float sign = (float)sideSign;
        Vector3 center = CreaturePartPosition(
            context->origin, context->forward, context->side,
            length * 0.12f, sign * (width * 0.43f + span * 0.28f),
            -height * 0.08f);
        CreatureRendererDrawBox(center, (Vector3){ span, thickness, chord },
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
            CreatureRendererDrawBox(
                rearCenter,
                (Vector3){ rearSpan, thickness * 0.86f, chord * 0.68f },
                context->yaw + sign * 0.17f, 0.0f,
                sign * (0.06f - context->finWave * 0.10f), context->fin);
        }
    }

    Vector3 dorsalCenter = CreaturePartPosition(
        context->origin, context->forward, context->side,
        -length * 0.08f, 0.0f, height * 0.50f + span * 0.15f);
    CreatureRendererDrawBox(
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
    CreatureRendererDrawBox(
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
        CreatureRendererDrawBox(
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

    context.pose = FaunaPoseEvaluate(&entity->pose, entity->phase,
        entity->needs.fatigue, entity->needs.stress, true);
    if (entity->corpse) context.pose = (FaunaBodyPose){0};
    context.forward = (Vector3){0, 0, 1};
    context.side = (Vector3){ context.forward.z, 0.0f,
                              -context.forward.x };
    context.scale = scale;
    context.yaw = 0;
    context.heightScale = entity->corpse ? 0.36f : 1.0f;
    float animation = entity->corpse ? 0.0f : runtime.animationScale;
    float effort = 0.18f + entity->pose.movement * 0.55f +
                   entity->pose.weights[FAUNA_ACTION_FLEE] * 0.27f;
    context.tailWave = (sinf(entity->phase * 2.75f + entity->pose.gaitPhase * 2 * PI) *
        effort - entity->pose.turn * 0.55f) * animation;
    context.finWave = sinf(entity->phase * 3.40f + 0.65f) * animation;
    context.body = body;
    context.accent = accent;
    context.fin = ColorLerp(body, accent, 0.52f);
    context.origin = (Vector3){0};
    context.origin.y += context.profile.torsoHeight * scale *
                        (entity->corpse ? 0.15f : 0.38f);

    rlPushMatrix();
    rlTranslatef(entity->position.x, entity->position.y, entity->position.z);
    rlRotatef(entity->yaw * RAD2DEG, 0, 1, 0);
    rlRotatef((context.pose.bodyPitch + context.pose.headPitch * 0.25f) * RAD2DEG, 1, 0, 0);
    rlRotatef(context.pose.bodyRoll * RAD2DEG, 0, 0, 1);
    DrawAquaticBody(&context);
    DrawAquaticFins(&context);
    DrawAquaticTail(&context);
    rlPopMatrix();
    return true;
}
