#include "presentation/creature_pose.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

float EntityEvolutionGrowthScale(const Entity *entity)
{
    (void)entity;
    return 1;
}

static float Ground(Vector3 foot, void *context)
{
    (void)context;
    return foot.x > 0 ? 0.2f : 0;
}

static void TestModuleAttachment(void)
{
    Entity e = {.active = true, .evolvable = true, .organismScale = 1};
    for (unsigned seed = 1; seed <= 32; seed++) {
        e.genome = EvolutionGenomeSeed(seed, EVOLUTION_ARCHETYPE_GROUND);
        e.phenotype = EvolutionDevelop(&e.genome);
        assert(e.phenotype.valid);
        e.pose = (FaunaPoseState){0};
        CreatureModulePose standing[EVOLUTION_MAX_MODULES] = {0};
        CreatureModulePose feeding[EVOLUTION_MAX_MODULES] = {0};
        CreaturePoseBuild(&e, 1, Ground, NULL, standing);
        e.pose.weights[FAUNA_ACTION_FORAGE] = 1;
        CreaturePoseBuild(&e, 1, Ground, NULL, feeding);
        bool hasHead = false, hasFeet = false;
        for (unsigned i = 0; i < e.phenotype.moduleCount; i++) {
            CreatureModule *m = &e.phenotype.modules[i];
            Vector3 p = feeding[i].position;
            assert(isfinite(p.x) && isfinite(p.y) && isfinite(p.z));
            if (m->type == CREATURE_MODULE_HEAD) {
                hasHead = true;
                assert(feeding[i].pitch > standing[i].pitch + 0.5f);
                assert(p.y <= standing[i].position.y);
            }
            if (m->type == CREATURE_MODULE_FOOT) {
                hasFeet = true;
                assert(fabsf(p.y - Ground(p, NULL) - m->height * 0.5f) < 0.001f);
            }
            if (m->parentIndex >= 0 &&
                e.phenotype.modules[m->parentIndex].type == CREATURE_MODULE_HEAD) {
                Vector3 attached = CreaturePosePoint(feeding[m->parentIndex],
                    m->localX, m->localY, m->localZ);
                assert(fabsf(p.x - attached.x) < 0.001f);
                assert(fabsf(p.y - attached.y) < 0.001f);
                assert(fabsf(p.z - attached.z) < 0.001f);
            }
        }
        assert(hasHead && hasFeet);
    }
}

int main(void)
{
    TestModuleAttachment();
    puts("creature pose tests passed");
    return 0;
}
