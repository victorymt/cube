#include "presentation/creature_pose.h"

#define RAYMATH_STATIC_INLINE
#include "raymath.h"

#include <math.h>

Vector3 CreaturePosePoint(CreatureModulePose pose, float along,
                         float across, float up)
{
    /* Same local R_yaw * R_pitch * R_roll order as CreatureRendererDrawBox. */
    float x = across * cosf(pose.roll) - up * sinf(pose.roll);
    float y = across * sinf(pose.roll) + up * cosf(pose.roll);
    float z = along * cosf(pose.pitch) + y * sinf(pose.pitch);
    y = y * cosf(pose.pitch) - along * sinf(pose.pitch);
    return Vector3Add(pose.position, (Vector3){
        x * cosf(pose.yaw) + z * sinf(pose.yaw), y,
        -x * sinf(pose.yaw) + z * cosf(pose.yaw)
    });
}

static Vector3 SolveKnee(Vector3 hip, Vector3 foot, Vector3 bend,
                         float upper, float lower)
{
    Vector3 delta = Vector3Subtract(foot, hip);
    float distance = Vector3Length(delta);
    if (distance < 0.0001f) return Vector3Add(hip, Vector3Scale(bend, upper));
    Vector3 axis = Vector3Scale(delta, 1.0f / distance);
    float d = fminf(fmaxf(distance, fabsf(upper - lower) + 0.001f),
                     upper + lower - 0.001f);
    float along = (upper * upper - lower * lower + d * d) / (2 * d);
    float height = sqrtf(fmaxf(0, upper * upper - along * along));
    Vector3 plane = Vector3Subtract(bend, Vector3Scale(axis, Vector3DotProduct(bend, axis)));
    if (Vector3Length(plane) < 0.0001f) plane = Vector3CrossProduct(axis, (Vector3){0, 0, 1});
    return Vector3Add(hip, Vector3Add(Vector3Scale(axis, along),
                                      Vector3Scale(Vector3Normalize(plane), height)));
}

void CreaturePoseBuild(const Entity *entity, float scale,
                       CreatureGroundHeight ground, void *context,
                       CreatureModulePose poses[EVOLUTION_MAX_MODULES])
{
    const CreaturePhenotype *phenotype = &entity->phenotype;
    CreatureModulePose rest[EVOLUTION_MAX_MODULES] = {0};
    unsigned rows[EVOLUTION_MAX_MODULES] = {0};
    unsigned sideCounts[2] = {0};
    float lowest = 0;
    bool flying = phenotype->locomotion == CREATURE_LOCOMOTION_FLIGHT;
    FaunaBodyPose body = FaunaPoseEvaluate(&entity->pose, entity->phase,
        entity->needs.fatigue, entity->needs.stress, flying);
    if (entity->corpse) body = (FaunaBodyPose){0};
    for (unsigned i = 0; i < phenotype->moduleCount; i++) {
        const CreatureModule *m = &phenotype->modules[i];
        CreatureModulePose parent = {.yaw = entity->yaw};
        if (m->parentIndex >= 0 && m->parentIndex < (int)i) parent = rest[m->parentIndex];
        rest[i] = (CreatureModulePose){
            .position = CreaturePosePoint(parent, m->localX * scale,
                                          m->localY * scale, m->localZ * scale),
            .yaw = entity->yaw
        };
        lowest = fminf(lowest, rest[i].position.y - m->height * scale * 0.5f);
        if (m->type == CREATURE_MODULE_LIMB) {
            unsigned side = m->localY >= 0 ? 1 : 0;
            rows[i] = sideCounts[side]++;
        }
    }
    unsigned pairs = sideCounts[0] > sideCounts[1] ? sideCounts[0] : sideCounts[1];
    for (unsigned i = 0; i < phenotype->moduleCount; i++) {
        const CreatureModule *m = &phenotype->modules[i];
        CreatureModulePose parent = {
            .position = entity->position, .yaw = entity->yaw,
            .pitch = body.bodyPitch, .roll = body.bodyRoll
        };
        parent.position.y += -lowest + 0.025f * scale + body.bodyHeight * scale;
        if (m->parentIndex >= 0 && m->parentIndex < (int)i) parent = poses[m->parentIndex];
        poses[i] = parent;
        poses[i].position = CreaturePosePoint(parent, m->localX * scale,
                                              m->localY * scale, m->localZ * scale);
        if (m->type == CREATURE_MODULE_HEAD) {
            poses[i].position.y += body.headHeight * scale;
            poses[i].position = Vector3Add(poses[i].position, (Vector3){
                sinf(entity->yaw) * body.headForward * scale, 0,
                cosf(entity->yaw) * body.headForward * scale
            });
            /* Keep the muzzle above the support plane during drinking. */
            poses[i].position.y = fmaxf(poses[i].position.y,
                entity->position.y + scale * (m->height * 0.55f + m->length * 0.30f));
            poses[i].yaw += body.headYaw;
            poses[i].pitch += body.headPitch;
        }
        if (m->type == CREATURE_MODULE_TAIL) poses[i].yaw += body.tailYaw;
        if (m->type == CREATURE_MODULE_SENSOR) {
            poses[i].pitch -= entity->pose.weights[FAUNA_ACTION_ALERT] * 0.22f;
        }
        if (entity->corpse) {
            poses[i].position.y = entity->position.y + m->height * scale * 0.18f;
            poses[i].pitch = poses[i].roll = 0;
        }
    }
    if (entity->corpse) return;
    for (unsigned i = 0; i < phenotype->moduleCount; i++) {
        const CreatureModule *m = &phenotype->modules[i];
        int leg = m->parentIndex;
        if (m->type != CREATURE_MODULE_FOOT || leg < 0 || leg >= (int)i ||
            phenotype->modules[leg].type != CREATURE_MODULE_LIMB) continue;
        const CreatureModule *limb = &phenotype->modules[leg];
        int torso = limb->parentIndex;
        if (torso < 0 || torso >= leg) continue;
        float sign = limb->localY >= 0 ? 1 : -1;
        float hipAcross = sign * phenotype->modules[torso].width * scale * 0.40f;
        float hipUp = -phenotype->modules[torso].height * scale * 0.25f;
        Vector3 hip = CreaturePosePoint(poses[torso], limb->localX * scale, hipAcross, hipUp);
        Vector3 restHip = CreaturePosePoint(rest[torso], limb->localX * scale, hipAcross, hipUp);
        float upper = fmaxf(0.05f, Vector3Distance(restHip, rest[leg].position));
        float lower = fmaxf(0.05f, Vector3Distance(rest[leg].position, rest[i].position));
        if (flying) {
            /* Tuck feet during flight, extend them when resting/displaying. */
            float tuck = 1 - entity->pose.weights[FAUNA_ACTION_REST] - body.display * 0.7f;
            poses[i].position.y += fmaxf(0, tuck) * lower * 0.40f;
        } else {
            FaunaFootPose step = FaunaPoseFoot(&entity->pose,
                entity->organismScale * EntityEvolutionGrowthScale(entity),
                rows[leg], sign > 0 ? 1 : 0, pairs);
            poses[i].position = Vector3Add(entity->position, rest[i].position);
            poses[i].position.y = entity->position.y + m->height * scale * 0.5f;
            poses[i].position.x += sinf(entity->yaw) * step.along;
            poses[i].position.z += cosf(entity->yaw) * step.along;
            if (ground) poses[i].position.y = ground(poses[i].position, context) +
                                                 m->height * scale * 0.5f;
            poses[i].position.y += step.lift;
            poses[i].pitch = poses[i].roll = 0;
            poses[i].yaw = entity->yaw;
        }
        Vector3 bend = {cosf(entity->yaw) * sign, 0.15f, -sinf(entity->yaw) * sign};
        poses[leg].position = SolveKnee(hip, poses[i].position, bend, upper, lower);
    }
}
