#include "creature_visual.h"

#include <math.h>
#include <string.h>

static float CreatureVisualClamp(float value, float minimum, float maximum)
{
    if (!isfinite(value)) return minimum;
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static void CreatureVisualLargestModule(const CreatureModule *module,
                                        const CreatureModule **largest)
{
    if (!module || !largest) return;
    float volume = module->length * module->width * module->height;
    float largestVolume = *largest
        ? (*largest)->length * (*largest)->width * (*largest)->height
        : -1.0f;
    if (volume > largestVolume) *largest = module;
}

bool CreatureAquaticVisualProfileBuild(
    const CreaturePhenotype *phenotype,
    CreatureAquaticVisualProfile *outProfile)
{
    if (!phenotype || !outProfile || !phenotype->valid ||
        phenotype->locomotion != CREATURE_LOCOMOTION_AQUATIC) {
        return false;
    }

    const CreatureModule *torso = NULL;
    const CreatureModule *head = NULL;
    const CreatureModule *tail = NULL;
    const CreatureModule *largestFin = NULL;
    unsigned finCount = 0u;
    for (unsigned index = 0; index < phenotype->moduleCount; index++) {
        const CreatureModule *module = &phenotype->modules[index];
        switch ((CreatureModuleType)module->type) {
        case CREATURE_MODULE_TORSO:
            CreatureVisualLargestModule(module, &torso);
            break;
        case CREATURE_MODULE_HEAD:
            CreatureVisualLargestModule(module, &head);
            break;
        case CREATURE_MODULE_TAIL:
            CreatureVisualLargestModule(module, &tail);
            break;
        case CREATURE_MODULE_FIN:
            CreatureVisualLargestModule(module, &largestFin);
            finCount++;
            break;
        default:
            break;
        }
    }
    if (!torso || finCount < 2u) return false;

    CreatureAquaticVisualProfile profile = { 0 };
    profile.torsoLength = CreatureVisualClamp(torso->length, 0.80f, 3.00f);
    profile.torsoWidth = CreatureVisualClamp(torso->width, 0.42f, 1.45f);
    profile.torsoHeight = CreatureVisualClamp(torso->height * 0.82f,
                                               0.32f, 1.15f);

    profile.headLength = CreatureVisualClamp(
        (head ? head->length : profile.torsoLength * 0.38f) * 0.68f,
        0.28f, profile.torsoLength * 0.58f);
    profile.headWidth = CreatureVisualClamp(
        (head ? head->width : profile.torsoWidth * 0.72f) * 0.82f,
        profile.torsoWidth * 0.42f, profile.torsoWidth * 0.90f);
    profile.headHeight = CreatureVisualClamp(
        (head ? head->height : profile.torsoHeight * 0.72f) * 0.82f,
        profile.torsoHeight * 0.45f, profile.torsoHeight * 0.88f);

    profile.tailLength = CreatureVisualClamp(
        (tail ? tail->length : profile.torsoLength * 0.52f) * 0.78f,
        0.42f, profile.torsoLength * 0.82f);
    profile.tailWidth = CreatureVisualClamp(
        (tail ? tail->width : profile.torsoWidth * 0.40f) * 0.56f,
        0.12f, profile.torsoWidth * 0.54f);
    profile.tailHeight = CreatureVisualClamp(
        (tail ? tail->height : profile.torsoHeight * 0.48f) * 0.62f,
        0.12f, profile.torsoHeight * 0.58f);

    float finLong = largestFin
        ? fmaxf(largestFin->length, largestFin->width)
        : profile.torsoWidth;
    float finShort = largestFin
        ? fminf(largestFin->length, largestFin->width)
        : profile.torsoWidth * 0.58f;
    float finHeight = largestFin ? largestFin->height : 0.20f;
    profile.finSpan = CreatureVisualClamp(finLong * 0.72f, 0.34f, 1.45f);
    profile.finChord = CreatureVisualClamp(finShort * 0.58f, 0.22f, 0.88f);
    profile.finThickness = CreatureVisualClamp(finHeight * 0.32f,
                                                0.055f, 0.18f);
    profile.finPairs = finCount >= 4u ? 2u : 1u;

    *outProfile = profile;
    return true;
}
