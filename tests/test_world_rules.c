/* World rule ordering must be deterministic across scope, priority, operation
 * and registration id without requiring a live surface backend. */
#include "world/world_rules.h"

#include "raymath.h"

#include <assert.h>

static int AddRule(WorldRuleSet *set, WorldRuleStage stage, int priority,
                   WorldRuleScope scope, WorldRuleOperation operation,
                   Vector3 value)
{
    return WorldRuleSetAdd(set, &(WorldRule){
        .stage = stage,
        .priority = priority,
        .scope = scope,
        .operation = operation,
        .value = value
    });
}

static void TestRuleOrderAndOperations(void)
{
    WorldRuleSet set;
    WorldRuleSetInit(&set);
    int global = AddRule(&set, WORLD_RULE_STAGE_GRAVITY, 0,
                         (WorldRuleScope){ .kind = WORLD_RULE_SCOPE_GLOBAL },
                         WORLD_RULE_ADD, (Vector3){ 1, 1, 1 });
    int body = AddRule(&set, WORLD_RULE_STAGE_GRAVITY, 5,
                       (WorldRuleScope){ .kind = WORLD_RULE_SCOPE_BODY,
                                         .bodyId = 7 },
                       WORLD_RULE_MODIFY, (Vector3){ 2, 1, 1 });
    int region = AddRule(&set, WORLD_RULE_STAGE_GRAVITY, 2,
                         (WorldRuleScope){ .kind = WORLD_RULE_SCOPE_REGION,
                                           .bodyId = 7, .regionId = 9 },
                         WORLD_RULE_ADD, (Vector3){ 0, 1, 0 });
    assert(global > 0 && body > 0 && region > 0);

    unsigned indices[WORLD_RULE_MAX_RULES];
    unsigned count = 0;
    assert(WorldRuleSetResolve(&set, WORLD_RULE_STAGE_GRAVITY, 7, 9,
                               indices, WORLD_RULE_MAX_RULES, &count));
    assert(count == 3u);
    assert(set.rules[indices[0]].id == (uint32_t)global);
    assert(set.rules[indices[1]].id == (uint32_t)body);
    assert(set.rules[indices[2]].id == (uint32_t)region);

    Vector3 result;
    assert(WorldRuleSetApplyVector(&set, WORLD_RULE_STAGE_GRAVITY, 7, 9,
                                   (Vector3){ 1, 2, 3 }, &result));
    assert(Vector3Distance(result, (Vector3){ 4, 4, 4 }) < 1e-6f);

    assert(WorldRuleSetApplyVector(&set, WORLD_RULE_STAGE_GRAVITY, 7, 10,
                                   (Vector3){ 1, 2, 3 }, &result));
    assert(Vector3Distance(result, (Vector3){ 4, 3, 4 }) < 1e-6f);
    assert(WorldRuleSetRemove(&set, (uint32_t)body));
    assert(!WorldRuleSetRemove(&set, (uint32_t)body));
}

static void TestReplaceAndInvalidRules(void)
{
    WorldRuleSet set;
    WorldRuleSetInit(&set);
    assert(AddRule(&set, WORLD_RULE_STAGE_WATER, 0,
                   (WorldRuleScope){ .kind = WORLD_RULE_SCOPE_GLOBAL },
                   WORLD_RULE_ADD, (Vector3){ 1, 1, 1 }) > 0);
    assert(AddRule(&set, WORLD_RULE_STAGE_WATER, 10,
                   (WorldRuleScope){ .kind = WORLD_RULE_SCOPE_GLOBAL },
                   WORLD_RULE_REPLACE, (Vector3){ 7, 8, 9 }) > 0);
    Vector3 result;
    assert(WorldRuleSetApplyVector(&set, WORLD_RULE_STAGE_WATER, 0, 0,
                                   (Vector3){ 1, 2, 3 }, &result));
    assert(Vector3Distance(result, (Vector3){ 7, 8, 9 }) < 1e-6f);
    assert(AddRule(&set, WORLD_RULE_STAGE_COUNT, 0,
                   (WorldRuleScope){ .kind = WORLD_RULE_SCOPE_GLOBAL },
                   WORLD_RULE_ADD, Vector3Zero()) < 0);
    assert(AddRule(&set, WORLD_RULE_STAGE_WATER, 0,
                   (WorldRuleScope){ .kind = WORLD_RULE_SCOPE_GLOBAL },
                   WORLD_RULE_ADD, (Vector3){ NAN, 0, 0 }) < 0);
}

int main(void)
{
    TestRuleOrderAndOperations();
    TestReplaceAndInvalidRules();
    return 0;
}
