#include "presentation/entity_renderer.h"

#include "presentation/creature_renderer.h"
#include "presentation/creature_pose.h"
#include "ecology/ecology.h"
#include "space/space_state.h"
#include "world/world.h"

#include "raymath.h"
#include "rlgl.h"

#include <math.h>

static bool EntityRenderUsesEcology(const Entity *entity)
{
    return entity &&
           (entity->evolvable ||
            (entity->type >= ENTITY_ALIEN_GRAZER &&
             entity->type <= ENTITY_ALIEN_STRIDER));
}

static Color EntityBodyColor(EntityType type)
{
    switch (type) {
    case ENTITY_COW: return (Color){ 138, 96, 62, 255 };
    case ENTITY_SHEEP: return (Color){ 238, 236, 228, 255 };
    case ENTITY_PIG: return (Color){ 236, 176, 168, 255 };
    case ENTITY_CHICKEN: return (Color){ 240, 236, 222, 255 };
    case ENTITY_ALIEN_GRAZER:
    case ENTITY_ALIEN_HOPPER:
    case ENTITY_ALIEN_STRIDER:
        return ColorLerp(ColorPalette256((int)(PlanetWorldSeed() % 216u) + 20), WHITE, 0.12f);
    case ENTITY_ZOMBIE: return (Color){ 110, 150, 84, 255 };
    case ENTITY_SKELETON: return (Color){ 226, 226, 224, 255 };
    default: return MAGENTA;
    }
}

static Color EntityHeadColor(EntityType type)
{
    switch (type) {
    case ENTITY_COW: return (Color){ 92, 62, 40, 255 };
    case ENTITY_SHEEP: return (Color){ 218, 210, 200, 255 };
    case ENTITY_PIG: return (Color){ 226, 154, 148, 255 };
    case ENTITY_CHICKEN: return (Color){ 238, 232, 214, 255 };
    case ENTITY_ALIEN_GRAZER:
    case ENTITY_ALIEN_HOPPER:
    case ENTITY_ALIEN_STRIDER:
        return ColorLerp(ColorPalette256((int)((PlanetWorldSeed() >> 8) % 216u) + 20), WHITE, 0.22f);
    case ENTITY_ZOMBIE: return (Color){ 96, 134, 70, 255 };
    case ENTITY_SKELETON: return (Color){ 214, 214, 212, 255 };
    default: return MAGENTA;
    }
}

static Color AlienBodyColor(const Entity *entity)
{
    Color color = BlockBaseColor(entity->primaryBlock);
    return ColorLerp(color, WHITE, 0.08f + entity->bodyArmor * 0.10f);
}

static Color AlienAccentColor(const Entity *entity)
{
    Color color = BlockBaseColor(entity->accentBlock);
    if (entity->niche == PLANET_NICHE_BIOLUMINESCENT_COLONY) {
        color = ColorLerp(color, (Color){ 120, 244, 255, 255 }, 0.60f);
    }
    return ColorLerp(color, WHITE, 0.14f);
}

static void DrawEntityBox(Vector3 center, Vector3 size, Color color)
{
    DrawCubeV(center, size, color);
}

static void DrawOrientedBox(Vector3 center, Vector3 size, float yaw, Color color)
{
    CreatureRendererDrawBox(center, size, yaw, 0.0f, 0.0f, color);
}

static void DrawPoseBox(CreatureModulePose pose, Vector3 size, Color color)
{
    CreatureRendererDrawBox(pose.position, size, pose.yaw, pose.pitch, pose.roll, color);
}

static float EntityFootGround(Vector3 foot, void *context)
{
    const Entity *entity = context;
    int x = (int)floorf(foot.x), z = (int)floorf(foot.z);
    int support = (int)floorf(entity->position.y);
    for (int y = support + 1; y >= support - 2; y--) {
        float height = BlockCollisionHeight(GetBlockAt(x, y, z));
        if (height <= 0 || IsLiquidBlock(GetBlockAt(x, y, z))) continue;
        float surface = y + height;
        if (fabsf(surface - entity->position.y) <= 1.35f) return surface;
    }
    return entity->position.y;
}

/* Draw a short structural beam between two module centers.  Evolution
 * connections are part of the creature's silhouette, including articulated
 * joints; omitting them makes every appendage look detached in the atlas. */
static void DrawModuleConnection(Vector3 first, Vector3 second, float scale,
                                 Color color)
{
    Vector3 delta = Vector3Subtract(second, first);
    Vector3 center = Vector3Scale(Vector3Add(first, second), 0.5f);
    float length = Vector3Length(delta);
    if (length < 0.035f) return;
    Vector3 axis = Vector3Normalize(delta);
    float yaw = atan2f(axis.x, axis.z);
    float vertical = sqrtf(axis.x * axis.x + axis.z * axis.z);
    float pitch = atan2f(axis.y, fmaxf(vertical, 0.001f));
    CreatureRendererDrawBox(center,
        (Vector3){ 0.11f * scale, 0.11f * scale, length + 0.08f * scale },
        yaw, -pitch, 0.0f, color);
}

static Vector3 AlienPartPosition(Vector3 origin, Vector3 forward, Vector3 side,
                                 float along, float across, float y)
{
    Vector3 result = Vector3Add(origin, Vector3Scale(forward, along));
    result = Vector3Add(result, Vector3Scale(side, across));
    result.y += y;
    return result;
}

static Color AlienActivityColor(Color color, float visualPresence)
{
    float stress = 1.0f - fminf(fmaxf(visualPresence, 0.0f), 1.0f);
    return ColorLerp(color, (Color){ 72, 78, 82, 255 }, stress * 0.72f);
}

/* Anatomy is drawn in the developed parent hierarchy, shared by world and lab. */
static void DrawEvolvedAlienEntity(const Entity *entity,
                                   PlanetFaunaRuntimeState runtime,
                                   Color body, Color accent, float scale, bool terrain)
{
    CreatureModulePose poses[EVOLUTION_MAX_MODULES] = {0};
    FaunaBodyPose action = FaunaPoseEvaluate(&entity->pose, entity->phase,
        entity->needs.fatigue, entity->needs.stress, entity->airborne);
    scale *= EntityEvolutionGrowthScale(entity);
    bool flying = entity->phenotype.locomotion == CREATURE_LOCOMOTION_FLIGHT;
    if (entity->corpse) {
        action = (FaunaBodyPose){0};
        body = ColorLerp(body, (Color){ 74, 68, 62, 255 }, 0.68f);
        accent = ColorLerp(accent, body, 0.72f);
    }
    if (entity->phenotype.locomotion == CREATURE_LOCOMOTION_AQUATIC &&
        CreatureRendererDrawAquatic(entity, runtime, body, accent, scale)) return;

    CreaturePoseBuild(entity, scale, terrain ? EntityFootGround : NULL,
                      (void *)entity, poses);
    Color dark = ColorLerp(body, (Color){18, 28, 32, 255}, 0.60f);
    Color belly = ColorLerp(body, (Color){224, 222, 184, 255}, 0.35f);
    Color shell = ColorLerp(body, accent, 0.22f);
    for (unsigned i = 0; i < entity->phenotype.moduleCount; i++) {
        const CreatureModule *m = &entity->phenotype.modules[i];
        CreatureModulePose pose = poses[i];
        Vector3 center = pose.position;
        float w = m->width * scale, h = m->height * scale;
        float l = m->length * scale;
        if (entity->corpse) h *= 0.36f;
        Vector3 size = {w, h, l};
        int parent = m->parentIndex;
        bool attached = parent >= 0 && parent < (int)i;
        if (attached && m->type != CREATURE_MODULE_WING) {
            Vector3 anchor = poses[parent].position;
            if (m->type == CREATURE_MODULE_LIMB) {
                float sign = m->localY >= 0.0f ? 1.0f : -1.0f;
                anchor = CreaturePosePoint(poses[parent],
                    m->localX * scale,
                    sign * entity->phenotype.modules[parent].width * scale * 0.40f,
                    -entity->phenotype.modules[parent].height * scale * 0.25f);
            }
            DrawModuleConnection(anchor, center, scale * 1.4f, dark);
        }
        switch ((CreatureModuleType)m->type) {
        case CREATURE_MODULE_TORSO:
            DrawPoseBox(pose, (Vector3){w * 0.82f, h * 0.78f, l}, belly);
            for (int segment = 0; segment < 3; segment++) {
                float taper = segment == 1 ? 1.0f : 0.82f;
                Vector3 plate = CreaturePosePoint(pose,
                    (float)(segment - 1) * l * 0.30f, 0, h * 0.19f);
                CreatureRendererDrawBox(plate,
                    (Vector3){w * taper, h * 0.72f, l * 0.285f},
                    pose.yaw, pose.pitch, pose.roll, segment == 1 ? shell : body);
            }
            break;
        case CREATURE_MODULE_HEAD:
            DrawPoseBox(pose, (Vector3){w, h, l * 0.78f}, body);
            CreatureRendererDrawBox(CreaturePosePoint(pose,
                l * 0.48f, 0, -h * 0.12f),
                (Vector3){w * 0.58f, h * 0.34f, l * 0.46f},
                pose.yaw, pose.pitch, pose.roll, accent);
            for (int sign = -1; sign <= 1; sign += 2) {
                Vector3 eye = CreaturePosePoint(pose,
                    l * 0.18f, sign * w * 0.50f, h * 0.18f);
                float e = fminf(w, h) * 0.24f;
                CreatureRendererDrawBox(eye, (Vector3){e * 0.48f, e, e * 1.3f},
                    pose.yaw, pose.pitch, pose.roll, (Color){8, 17, 23, 255});
                eye = CreaturePosePoint(pose, l * 0.18f,
                    sign * (w * 0.50f + e * 0.25f), h * 0.18f + e * 0.13f);
                CreatureRendererDrawBox(eye, (Vector3){e * 0.12f, e * 0.32f, e * 0.32f},
                    pose.yaw, pose.pitch, pose.roll, (Color){224, 248, 209, 255});
            }
            break;
        case CREATURE_MODULE_LIMB:
            /* The limb module marks the knee; its children mark the feet. */
            DrawOrientedBox(center, (Vector3){w, fminf(h, w * 1.2f), l},
                            entity->yaw, shell);
            if (flying) {
                Vector3 toe = center; toe.y -= h * 0.5f;
                DrawModuleConnection(center, toe, scale, dark);
            }
            break;
        case CREATURE_MODULE_FOOT:
            DrawOrientedBox(center, size, entity->yaw, dark);
            break;
        case CREATURE_MODULE_WING: {
            float sign = m->localY >= 0.0f ? 1.0f : -1.0f;
            CreatureModulePose root = pose;
            root.position = CreaturePosePoint(pose, 0, -sign * w * 0.5f, 0);
            float effort = 0.30f + entity->pose.movement * 0.22f +
                entity->pose.weights[FAUNA_ACTION_FLEE] * 0.25f;
            float flap = entity->corpse ? 0 :
                sinf(entity->phase * 5.0f) * effort * runtime.animationScale -
                entity->pose.weights[FAUNA_ACTION_REST] * 0.70f;
            flap += action.display * 0.35f;
            if (attached) DrawModuleConnection(poses[parent].position, root.position, scale, body);
            for (int segment = 0; segment < 4; segment++) {
                float t = ((float)segment + 0.5f) / 4.0f;
                Vector3 part = CreaturePosePoint(root,
                    -l * t * 0.27f, sign * cosf(flap) * w * t,
                    sinf(flap) * w * t);
                CreatureRendererDrawBox(part,
                    (Vector3){w * 0.27f, h * (1.0f - t * 0.50f),
                               l * (1.0f - t * 0.65f)},
                    pose.yaw, pose.pitch, pose.roll + sign * flap,
                    segment == 3 ? dark : ColorLerp(body, accent, t * 0.7f));
            }
            break;
        }
        case CREATURE_MODULE_TAIL:
            for (int segment = 0; segment < 3; segment++) {
                float taper = 1.0f - (float)segment * 0.27f;
                Vector3 part = CreaturePosePoint(pose,
                    l * (0.32f - (float)segment * 0.32f), 0, 0);
                CreatureRendererDrawBox(part, (Vector3){w * taper, h * taper, l * 0.36f},
                    pose.yaw, pose.pitch, pose.roll, segment == 2 ? accent : body);
            }
            if (flying) for (int sign = -1; sign <= 1; sign += 2) {
                Vector3 tip = CreaturePosePoint(pose,
                    -l * 0.50f, sign * w * 0.40f, 0);
                DrawOrientedBox(tip, (Vector3){w * 0.50f, h * 0.5f, l * 0.48f},
                                entity->yaw + sign * 0.28f, accent);
            }
            break;
        case CREATURE_MODULE_SENSOR:
            DrawPoseBox(pose, (Vector3){w * 0.65f, h, l * 0.65f}, accent);
            break;
        default:
            DrawPoseBox(pose, size, shell);
            break;
        }
    }
    /* Preserve non-parent structural links created by mutation. */
    for (unsigned i = 0; i < entity->phenotype.connectionCount; i++) {
        const CreatureConnection *c = &entity->phenotype.connections[i];
        if (!c->articulated && c->first < entity->phenotype.moduleCount &&
            c->second < entity->phenotype.moduleCount)
            DrawModuleConnection(poses[c->first].position, poses[c->second].position, scale, dark);
    }
}

static void DrawSimpleAnimal(const Entity *entity, bool terrain, Color body,
                              Color head, float scale, unsigned rows, bool small)
{
    FaunaBodyPose action = FaunaPoseEvaluate(&entity->pose, entity->phase,
        entity->needs.fatigue, entity->needs.stress, false);
    float width = (small ? 0.45f : 0.7f) * scale;
    float height = (small ? 0.4f : 0.6f) * scale;
    float length = (small ? 0.6f : 1.0f) * scale;
    float legHeight = (small ? 0.35f : 0.55f) * scale;
    CreatureModulePose torso = {
        .position = entity->position, .yaw = entity->yaw,
        .pitch = action.bodyPitch, .roll = action.bodyRoll
    };
    torso.position.y += legHeight + height * 0.5f + action.bodyHeight * scale;
    torso.position.y = fmaxf(torso.position.y, entity->position.y + height * 0.55f);
    DrawPoseBox(torso, (Vector3){width, height, length}, body);
    for (unsigned row = 0; row < rows; row++) {
        float along = rows == 1 ? 0 : (0.3f - (float)row * 0.6f / (rows - 1)) * length;
        for (unsigned side = 0; side < 2; side++) {
            float across = (side == 0 ? -1 : 1) * width * 0.36f;
            FaunaFootPose step = FaunaPoseFoot(&entity->pose, scale, row, side, rows);
            Vector3 hip = CreaturePosePoint(torso, along, across, -height * 0.35f);
            CreatureModulePose base = {.position = entity->position, .yaw = entity->yaw};
            Vector3 foot = CreaturePosePoint(base, along + step.along, across, 0.045f * scale);
            if (terrain) foot.y = EntityFootGround(foot, (void *)entity) + 0.045f * scale;
            foot.y += step.lift;
            Vector3 knee = Vector3Lerp(hip, foot, 0.52f);
            knee = Vector3Add(knee, (Vector3){
                sinf(entity->yaw) * scale * (0.12f + action.crouch * 0.16f), 0,
                cosf(entity->yaw) * scale * (0.12f + action.crouch * 0.16f)
            });
            float thickness = small ? scale * 0.6f : scale;
            DrawModuleConnection(hip, knee, thickness, body);
            DrawModuleConnection(knee, foot, thickness * 0.85f, body);
            DrawOrientedBox(knee, (Vector3){0.13f * scale, 0.13f * scale, 0.13f * scale},
                entity->yaw, body);
            DrawOrientedBox(foot, (Vector3){0.14f * scale, 0.08f * scale, 0.20f * scale},
                entity->yaw, small ? (Color){92, 74, 48, 255} : body);
        }
    }
    float headSize = (small ? 0.34f : 0.5f) * scale;
    CreatureModulePose face = torso;
    face.position = CreaturePosePoint(torso, length * 0.52f + action.headForward * scale,
                                      0, height * 0.28f + action.headHeight * scale);
    face.position.y = fmaxf(face.position.y, entity->position.y + headSize * 0.7f);
    face.pitch += action.headPitch;
    face.yaw += action.headYaw;
    DrawModuleConnection(CreaturePosePoint(torso, length * 0.35f, 0, height * 0.15f),
                          face.position, scale * 1.3f, body);
    DrawPoseBox(face, (Vector3){headSize, headSize, headSize}, head);
    CreatureModulePose muzzle = face;
    muzzle.position = CreaturePosePoint(face, headSize * 0.48f, 0, -headSize * 0.13f);
    DrawPoseBox(muzzle, (Vector3){headSize * (small ? 0.36f : 0.62f),
        headSize * 0.30f, headSize * 0.34f}, small ? (Color){225, 158, 55, 255} : head);
    for (int side = -1; side <= 1; side += 2) {
        CreatureModulePose eye = face;
        eye.position = CreaturePosePoint(face, headSize * 0.24f, side * headSize * 0.50f,
                                         headSize * 0.18f);
        DrawPoseBox(eye, (Vector3){0.02f * scale, 0.07f * scale, 0.08f * scale},
                     (Color){10, 18, 24, 255});
    }
    if (small) {
        CreatureModulePose comb = face;
        comb.position = CreaturePosePoint(face, 0, 0, headSize * 0.60f);
        DrawPoseBox(comb, (Vector3){0.1f * scale, 0.12f * scale, 0.1f * scale},
                    (Color){214, 40, 36, 255});
    }
}

static void DrawAlienEntity(const Entity *entity, bool terrain)
{
    PlanetFaunaRuntimeState runtime = PlanetEcologyFaunaRuntime(
        entity->ecologyActivity, entity->ecologyCapacity);
    Vector3 pos = entity->position;
    Vector3 forward = { sinf(entity->yaw), 0.0f, cosf(entity->yaw) };
    Vector3 side = { forward.z, 0.0f, -forward.x };
    Color body = AlienActivityColor(AlienBodyColor(entity),
                                    runtime.visualPresence);
    Color accent = AlienActivityColor(AlienAccentColor(entity),
                                      runtime.visualPresence);
    float scale = entity->organismScale > 0.1f ? entity->organismScale : 1.0f;
    scale *= runtime.visualScale;
    FaunaBodyPose action = FaunaPoseEvaluate(&entity->pose, entity->phase,
        entity->needs.fatigue, entity->needs.stress, entity->airborne);

    if (entity->evolvable && entity->phenotype.valid) {
        Color geneticAccent = ColorPalette256(
            20 + (int)(entity->genome.pigmentation % 216u));
        accent = ColorLerp(accent, geneticAccent, 0.32f);
        DrawEvolvedAlienEntity(entity, runtime, body, accent, scale, terrain);
        return;
    }

    if (entity->bodyPlan == PLANET_BODY_FLOATING) {
        DrawEntityBox(pos, (Vector3){ 1.25f * scale, 0.66f * scale,
                                      1.55f * scale }, body);
        DrawEntityBox((Vector3){ pos.x, pos.y + 0.58f * scale, pos.z },
                      (Vector3){ 0.86f * scale, 0.34f * scale, 0.92f * scale }, accent);
        DrawEntityBox(AlienPartPosition(pos, forward, side, -0.42f * scale,
                                        -0.62f * scale, 0.0f),
                      (Vector3){ 0.58f * scale, 0.58f * scale, 0.58f * scale }, accent);
        DrawEntityBox(AlienPartPosition(pos, forward, side, -0.42f * scale,
                                        0.62f * scale, 0.0f),
                      (Vector3){ 0.58f * scale, 0.58f * scale, 0.58f * scale }, accent);
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.90f * scale,
                                        0.0f, 0.10f * scale),
                      (Vector3){ 0.12f * scale, 0.72f * scale, 0.12f * scale }, accent);
        return;
    }

    if (entity->bodyPlan == PLANET_BODY_COLONY) {
        Color glow = ColorLerp(accent, (Color){ 110, 250, 255, 255 },
                               0.55f * runtime.activityRatio);
        DrawEntityBox((Vector3){ pos.x, pos.y + 0.48f * scale, pos.z },
                      (Vector3){ 0.54f * scale, 0.72f * scale, 0.54f * scale }, body);
        for (int i = 0; i < 6; i++) {
            float angle = (float)i * 1.0471976f + entity->phase * 0.08f;
            float radius = (0.58f - 0.30f * entity->pose.weights[FAUNA_ACTION_FLEE] -
                0.18f * action.crouch + 0.10f * entity->pose.weights[FAUNA_ACTION_FORAGE]) * scale;
            Vector3 node = { pos.x + cosf(angle) * radius,
                             pos.y + 0.28f * scale + (float)(i & 1) * 0.20f * scale,
                             pos.z + sinf(angle) * radius };
            DrawEntityBox(node, (Vector3){ 0.26f * scale, 0.34f * scale,
                                           0.26f * scale }, glow);
        }
        return;
    }

    if (entity->bodyPlan == PLANET_BODY_SERPENTINE) {
        for (int segment = 0; segment < 5; segment++) {
            float segmentScale = scale * (1.0f - segment * 0.10f);
            float along = (0.42f - segment * 0.42f) * scale;
            float across = sinf(entity->pose.gaitPhase * 2 * PI - segment * 0.75f) *
                0.16f * scale * entity->pose.movement - entity->pose.turn * segment * 0.07f * scale;
            DrawEntityBox(AlienPartPosition(pos, forward, side, along, across,
                                             0.30f * segmentScale + (segment == 0 ? action.headHeight * 0.20f * scale : 0)),
                          (Vector3){ 0.48f * segmentScale, 0.48f * segmentScale,
                                     0.62f * segmentScale },
                          segment == 0 ? accent : body);
        }
        return;
    }

    unsigned rows = entity->bodyPlan == PLANET_BODY_BIPED ? 1 :
        entity->bodyPlan == PLANET_BODY_HEXAPOD ? 3 : 2;
    DrawSimpleAnimal(entity, terrain, body, accent, scale, rows, false);
}

void EntityRendererDrawInterpolated(const Entity *entities, int count,
                                    float interpolationAlpha)
{
    if (!entities || count <= 0) return;
    for (int i = 0; i < count; i++) {
        const Entity *entity = &entities[i];
        if (!entity->active) continue;
        Entity interpolated;
        EntityRenderPose pose;
        if (interpolationAlpha < 1.0f &&
            entities == EntitiesView() &&
            EntitiesRenderPoseAt(i, interpolationAlpha, &pose)) {
            interpolated = *entity;
            interpolated.position = pose.position;
            interpolated.yaw = pose.yaw;
            interpolated.phase = pose.phase;
            interpolated.pose = pose.pose;
            entity = &interpolated;
        }

        if (EntityRenderUsesEcology(entity)) {
            DrawAlienEntity(entity, entities == EntitiesView());
            continue;
        }

        bool chicken = entity->type == ENTITY_CHICKEN;
        DrawSimpleAnimal(entity, entities == EntitiesView(), EntityBodyColor(entity->type),
            EntityHeadColor(entity->type), 1.0f, chicken ? 1 : 2, chicken);
    }
}

void EntityRendererDraw(const Entity *entities, int count)
{
    EntityRendererDrawInterpolated(entities, count, 1.0f);
}
