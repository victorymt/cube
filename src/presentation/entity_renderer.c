#include "presentation/entity_renderer.h"

#include "presentation/creature_renderer.h"
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

static Vector3 AnimalPart(Vector3 origin, Vector3 forward, Vector3 side,
                          float along, float across, float y)
{
    Vector3 result = Vector3Add(origin, Vector3Scale(forward, along));
    result = Vector3Add(result, Vector3Scale(side, across));
    result.y += y;
    return result;
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
                                   Color body, Color accent, float scale)
{
    Vector3 positions[EVOLUTION_MAX_MODULES] = { 0 };
    Vector3 forward = { sinf(entity->yaw), 0.0f, cosf(entity->yaw) };
    Vector3 side = { forward.z, 0.0f, -forward.x };
    scale *= EntityEvolutionGrowthScale(entity);
    bool flying = entity->phenotype.locomotion == CREATURE_LOCOMOTION_FLIGHT;
    if (entity->corpse) {
        body = ColorLerp(body, (Color){ 74, 68, 62, 255 }, 0.68f);
        accent = ColorLerp(accent, body, 0.72f);
    }
    if (entity->phenotype.locomotion == CREATURE_LOCOMOTION_AQUATIC &&
        CreatureRendererDrawAquatic(entity, runtime, body, accent, scale)) return;

    float lowest = 0.0f;
    for (unsigned i = 0; i < entity->phenotype.moduleCount; i++) {
        const CreatureModule *m = &entity->phenotype.modules[i];
        Vector3 base = {0};
        if (m->parentIndex >= 0 && m->parentIndex < (int)i)
            base = positions[(unsigned)m->parentIndex];
        positions[i] = AlienPartPosition(base, forward, side,
            m->localX * scale, m->localY * scale, m->localZ * scale);
        float bottom = positions[i].y - m->height * scale * 0.5f;
        if (bottom < lowest) lowest = bottom;
    }
    /* The entity origin denotes the support surface. Lift the entire hierarchy,
     * not just the torso, so descendants keep their inherited relative poses. */
    float speed = Vector3Length(entity->velocity);
    float gait = entity->corpse ? 0.0f : fminf(speed / 1.4f, 1.0f);
    for (unsigned i = 0; i < entity->phenotype.moduleCount; i++) {
        const CreatureModule *m = &entity->phenotype.modules[i];
        positions[i] = Vector3Add(positions[i], entity->position);
        positions[i].y += -lowest + 0.025f * scale;
        if (m->type == CREATURE_MODULE_FOOT) {
            int leg = m->parentIndex;
            float phase = entity->phase * 3.0f +
                (leg >= 0 ? (float)(leg % 2) * PI +
                            (float)(leg / 2) * PI : 0.0f);
            positions[i] = Vector3Add(positions[i], Vector3Scale(forward,
                sinf(phase) * 0.13f * scale * gait));
            positions[i].y += fmaxf(0.0f, cosf(phase)) * 0.14f * scale * gait;
        }
        if (entity->corpse)
            positions[i].y = entity->position.y + m->height * scale * 0.18f;
    }
    Color dark = ColorLerp(body, (Color){18, 28, 32, 255}, 0.60f);
    Color belly = ColorLerp(body, (Color){224, 222, 184, 255}, 0.35f);
    Color shell = ColorLerp(body, accent, 0.22f);
    for (unsigned i = 0; i < entity->phenotype.moduleCount; i++) {
        const CreatureModule *m = &entity->phenotype.modules[i];
        Vector3 center = positions[i];
        float w = m->width * scale, h = m->height * scale;
        float l = m->length * scale;
        if (entity->corpse) h *= 0.36f;
        Vector3 size = {w, h, l};
        int parent = m->parentIndex;
        bool attached = parent >= 0 && parent < (int)i;
        if (attached && m->type != CREATURE_MODULE_WING) {
            Vector3 anchor = positions[parent];
            if (m->type == CREATURE_MODULE_LIMB) {
                float sign = m->localY >= 0.0f ? 1.0f : -1.0f;
                anchor = AlienPartPosition(anchor, forward, side,
                    m->localX * scale,
                    sign * entity->phenotype.modules[parent].width * scale * 0.40f,
                    -entity->phenotype.modules[parent].height * scale * 0.25f);
            }
            DrawModuleConnection(anchor, center, scale * 1.4f, dark);
        }
        switch ((CreatureModuleType)m->type) {
        case CREATURE_MODULE_TORSO:
            DrawOrientedBox(center, (Vector3){w * 0.82f, h * 0.78f, l},
                            entity->yaw, belly);
            for (int segment = 0; segment < 3; segment++) {
                float taper = segment == 1 ? 1.0f : 0.82f;
                Vector3 plate = AlienPartPosition(center, forward, side,
                    (float)(segment - 1) * l * 0.30f, 0, h * 0.19f);
                DrawOrientedBox(plate,
                    (Vector3){w * taper, h * 0.72f, l * 0.285f},
                    entity->yaw, segment == 1 ? shell : body);
            }
            break;
        case CREATURE_MODULE_HEAD:
            DrawOrientedBox(center, (Vector3){w, h, l * 0.78f}, entity->yaw, body);
            DrawOrientedBox(AlienPartPosition(center, forward, side,
                l * 0.48f, 0, -h * 0.12f),
                (Vector3){w * 0.58f, h * 0.34f, l * 0.46f}, entity->yaw, accent);
            for (int sign = -1; sign <= 1; sign += 2) {
                Vector3 eye = AlienPartPosition(center, forward, side,
                    l * 0.18f, sign * w * 0.50f, h * 0.18f);
                float e = fminf(w, h) * 0.24f;
                DrawOrientedBox(eye, (Vector3){e * 0.48f, e, e * 1.3f},
                                entity->yaw, (Color){8, 17, 23, 255});
                eye = Vector3Add(eye, Vector3Scale(side, sign * e * 0.25f));
                eye.y += e * 0.13f;
                DrawOrientedBox(eye, (Vector3){e * 0.12f, e * 0.32f, e * 0.32f},
                                entity->yaw, (Color){224, 248, 209, 255});
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
            Vector3 root = AlienPartPosition(center, forward, side,
                0, -sign * w * 0.5f, 0);
            float flap = entity->corpse ? 0.0f :
                sinf(entity->phase * 2.4f) * 0.18f * runtime.animationScale;
            if (attached) DrawModuleConnection(positions[parent], root, scale, body);
            for (int segment = 0; segment < 4; segment++) {
                float t = ((float)segment + 0.5f) / 4.0f;
                Vector3 part = AlienPartPosition(root, forward, side,
                    -l * t * 0.27f, sign * cosf(flap) * w * t,
                    sinf(flap) * w * t);
                CreatureRendererDrawBox(part,
                    (Vector3){w * 0.27f, h * (1.0f - t * 0.50f),
                               l * (1.0f - t * 0.65f)},
                    entity->yaw, 0, sign * flap,
                    segment == 3 ? dark : ColorLerp(body, accent, t * 0.7f));
            }
            break;
        }
        case CREATURE_MODULE_TAIL:
            for (int segment = 0; segment < 3; segment++) {
                float taper = 1.0f - (float)segment * 0.27f;
                Vector3 part = AlienPartPosition(center, forward, side,
                    l * (0.32f - (float)segment * 0.32f), 0, 0);
                DrawOrientedBox(part, (Vector3){w * taper, h * taper, l * 0.36f},
                                entity->yaw, segment == 2 ? accent : body);
            }
            if (flying) for (int sign = -1; sign <= 1; sign += 2) {
                Vector3 tip = AlienPartPosition(center, forward, side,
                    -l * 0.50f, sign * w * 0.40f, 0);
                DrawOrientedBox(tip, (Vector3){w * 0.50f, h * 0.5f, l * 0.48f},
                                entity->yaw + sign * 0.28f, accent);
            }
            break;
        case CREATURE_MODULE_SENSOR:
            DrawOrientedBox(center, (Vector3){w * 0.65f, h, l * 0.65f},
                            entity->yaw, accent);
            break;
        default:
            DrawOrientedBox(center, size, entity->yaw, shell);
            break;
        }
    }
    /* Preserve non-parent structural links created by mutation. */
    for (unsigned i = 0; i < entity->phenotype.connectionCount; i++) {
        const CreatureConnection *c = &entity->phenotype.connections[i];
        if (!c->articulated && c->first < entity->phenotype.moduleCount &&
            c->second < entity->phenotype.moduleCount)
            DrawModuleConnection(positions[c->first], positions[c->second], scale, dark);
    }
}

static void DrawAlienEntity(const Entity *entity)
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
    float armor = 1.0f + entity->bodyArmor * 0.38f;

    if (entity->evolvable && entity->phenotype.valid) {
        Color geneticAccent = ColorPalette256(
            20 + (int)(entity->genome.pigmentation % 216u));
        accent = ColorLerp(accent, geneticAccent, 0.32f);
        DrawEvolvedAlienEntity(entity, runtime, body, accent, scale);
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
            float radius = 0.58f * scale;
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
            float across = sinf(entity->phase + segment * 0.75f) * 0.16f * scale;
            DrawEntityBox(AlienPartPosition(pos, forward, side, along, across,
                                             0.30f * segmentScale),
                          (Vector3){ 0.48f * segmentScale, 0.48f * segmentScale,
                                     0.62f * segmentScale },
                          segment == 0 ? accent : body);
        }
        return;
    }

    if (entity->bodyPlan == PLANET_BODY_BIPED) {
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.0f, 0.0f,
                                        1.02f * scale),
                      (Vector3){ 0.78f * scale * armor, 1.10f * scale,
                                 0.62f * scale }, body);
        for (int pair = -1; pair <= 1; pair += 2) {
            DrawEntityBox(AlienPartPosition(pos, forward, side, -0.08f * scale,
                                            pair * 0.22f * scale, 0.42f * scale),
                          (Vector3){ 0.20f * scale, 0.84f * scale,
                                     0.20f * scale }, body);
        }
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.48f * scale,
                                        0.0f, 1.78f * scale),
                      (Vector3){ 0.52f * scale, 0.52f * scale,
                                 0.52f * scale }, accent);
        DrawEntityBox(AlienPartPosition(pos, forward, side, -0.10f * scale,
                                        -0.58f * scale, 1.16f * scale),
                      (Vector3){ 0.14f * scale, 0.70f * scale,
                                 0.14f * scale }, accent);
        DrawEntityBox(AlienPartPosition(pos, forward, side, -0.10f * scale,
                                        0.58f * scale, 1.16f * scale),
                      (Vector3){ 0.14f * scale, 0.70f * scale,
                                 0.14f * scale }, accent);
        return;
    }

    if (entity->bodyPlan == PLANET_BODY_HEXAPOD) {
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.0f, 0.0f,
                                        0.66f * scale),
                      (Vector3){ 1.20f * scale * armor, 0.58f * scale,
                                 1.35f * scale }, body);
        for (int row = -1; row <= 1; row++) {
            for (int pair = -1; pair <= 1; pair += 2) {
                DrawEntityBox(AlienPartPosition(pos, forward, side,
                                                row * 0.42f * scale,
                                                pair * 0.48f * scale,
                                                0.33f * scale),
                              (Vector3){ 0.14f * scale * armor, 0.66f * scale,
                                         0.14f * scale * armor }, body);
            }
        }
        DrawEntityBox(AlienPartPosition(pos, forward, side, 0.78f * scale,
                                        0.0f, 0.88f * scale),
                      (Vector3){ 0.56f * scale, 0.48f * scale,
                                 0.56f * scale }, accent);
        return;
    }

    DrawEntityBox(AlienPartPosition(pos, forward, side, 0.0f, 0.0f,
                                    0.58f * scale),
                  (Vector3){ 1.10f * scale * armor, 0.68f * scale,
                             1.45f * scale }, body);
    for (int row = -1; row <= 1; row += 2) {
        for (int pair = -1; pair <= 1; pair += 2) {
            DrawEntityBox(AlienPartPosition(pos, forward, side,
                                            row * 0.42f * scale,
                                            pair * 0.40f * scale,
                                            0.28f * scale),
                          (Vector3){ 0.16f * scale * armor, 0.56f * scale,
                                     0.16f * scale * armor }, body);
        }
    }
    DrawEntityBox(AlienPartPosition(pos, forward, side, 0.90f * scale,
                                    0.0f, 0.82f * scale),
                  (Vector3){ 0.58f * scale, 0.54f * scale,
                             0.58f * scale }, accent);
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
            entity = &interpolated;
        }

        if (EntityRenderUsesEcology(entity)) {
            DrawAlienEntity(entity);
            continue;
        }

        Vector3 pos = entity->position;
        bool small = entity->type == ENTITY_CHICKEN;
        float bodyW = small ? 0.45f : 0.7f;
        float bodyH = small ? 0.4f : 0.6f;
        float bodyL = small ? 0.6f : 1.0f;
        float headSize = small ? 0.34f : 0.5f;
        Vector3 forward = { sinf(entity->yaw), 0.0f, cosf(entity->yaw) };
        Vector3 side = { forward.z, 0.0f, -forward.x };
        float bodyY = 0.55f + bodyH * 0.5f;
        float speed = sqrtf(entity->velocity.x * entity->velocity.x +
                            entity->velocity.z * entity->velocity.z);
        float gait = fminf(speed / 1.4f, 1.0f);
        float bob = gait * 0.025f * sinf(entity->phase * 4.4f);
        DrawOrientedBox(AnimalPart(pos, forward, side, 0.0f, 0.0f,
                                   bodyY + bob),
                        (Vector3){ bodyW, bodyH, bodyL }, entity->yaw,
                        EntityBodyColor(entity->type));

        float legW = small ? 0.12f : 0.22f;
        float legH = small ? 0.35f : 0.55f;
        float legOff = small ? 0.12f : 0.22f;
        for (int k = 0; k < 4; k++) {
            int row = k / 2;
            int pair = k % 2 == 0 ? -1 : 1;
            float phase = entity->phase * 2.2f +
                          ((row + (pair > 0)) & 1) * PI;
            float swing = sinf(phase) * 0.12f * gait;
            float lift = fmaxf(0.0f, cosf(phase)) * 0.08f * gait;
            float along = row == 0 ? bodyL * 0.30f : -bodyL * 0.30f;
            float across = pair * legOff;
            float upperH = legH * 0.56f;
            float lowerH = legH - upperH;
            float kneeAlong = along + swing * 0.62f;
            float footAlong = along + swing;
            /* Two independently placed segments give the leg a visible knee. */
            DrawOrientedBox(AnimalPart(pos, forward, side,
                                       kneeAlong, across,
                                       lowerH + upperH * 0.5f),
                            (Vector3){ legW, upperH, legW }, entity->yaw,
                            EntityBodyColor(entity->type));
            DrawOrientedBox(AnimalPart(pos, forward, side,
                                       footAlong, across,
                                       lowerH * 0.5f + lift),
                            (Vector3){ legW * 0.88f, lowerH, legW * 0.88f },
                            entity->yaw,
                            small ? (Color){ 92, 74, 48, 255 } :
                                EntityBodyColor(entity->type));
            DrawOrientedBox(AnimalPart(pos, forward, side,
                                       kneeAlong + swing * 0.10f, across,
                                       lowerH),
                            (Vector3){ legW * 1.12f, legW * 1.05f,
                                       legW * 1.12f }, entity->yaw,
                            EntityBodyColor(entity->type));
        }

        float headAlong = bodyL * 0.52f;
        Vector3 headCenter = AnimalPart(pos, forward, side, headAlong, 0.0f,
                                        bodyY + bodyH * 0.28f);
        DrawOrientedBox(headCenter, (Vector3){ headSize, headSize, headSize },
                        entity->yaw, EntityHeadColor(entity->type));
        DrawOrientedBox(AnimalPart(pos, forward, side,
                                   headAlong + headSize * 0.42f, 0.0f,
                                   headCenter.y - pos.y - headSize * 0.13f),
                        (Vector3){ headSize * (small ? 0.36f : 0.62f),
                                   headSize * 0.30f, headSize * 0.34f },
                        entity->yaw,
                        small ? (Color){ 225, 158, 55, 255 } :
                            EntityHeadColor(entity->type));

        if (entity->type == ENTITY_CHICKEN) {
            DrawOrientedBox(AnimalPart(pos, forward, side, headAlong,
                                       0.0f, bodyY + bodyH * 0.28f +
                                       headSize * 0.60f),
                            (Vector3){ 0.1f, 0.12f, 0.1f }, entity->yaw,
                            (Color){ 214, 40, 36, 255 });
        }
    }
}

void EntityRendererDraw(const Entity *entities, int count)
{
    EntityRendererDrawInterpolated(entities, count, 1.0f);
}
