#include "world/world_rules.h"

#include "raymath.h"

#include <math.h>

static bool WorldRuleVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static int WorldRuleScopeSpecificity(WorldRuleScopeKind kind)
{
    return (int)kind;
}

static bool WorldRuleScopeMatches(WorldRuleScope scope, uint32_t bodyId,
                                  uint64_t regionId)
{
    if (scope.kind == WORLD_RULE_SCOPE_GLOBAL) return true;
    if (scope.bodyId != bodyId) return false;
    return scope.kind == WORLD_RULE_SCOPE_BODY || scope.regionId == regionId;
}

static bool WorldRuleOrderBefore(const WorldRule *left, const WorldRule *right)
{
    int leftSpecificity = WorldRuleScopeSpecificity(left->scope.kind);
    int rightSpecificity = WorldRuleScopeSpecificity(right->scope.kind);
    if (leftSpecificity != rightSpecificity)
        return leftSpecificity < rightSpecificity;
    if (left->priority != right->priority)
        return left->priority < right->priority;
    return left->id < right->id;
}

void WorldRuleSetInit(WorldRuleSet *set)
{
    if (set) *set = (WorldRuleSet){ .nextId = 1u };
}

int WorldRuleSetAdd(WorldRuleSet *set, const WorldRule *rule)
{
    if (!set || !rule || set->count >= WORLD_RULE_MAX_RULES ||
        rule->stage < 0 || rule->stage >= WORLD_RULE_STAGE_COUNT ||
        rule->operation < WORLD_RULE_ADD ||
        rule->operation > WORLD_RULE_REPLACE ||
        rule->scope.kind < WORLD_RULE_SCOPE_GLOBAL ||
        rule->scope.kind > WORLD_RULE_SCOPE_REGION ||
        !WorldRuleVectorIsFinite(rule->value)) {
        return -1;
    }
    WorldRule added = *rule;
    added.id = set->nextId++;
    if (set->nextId == 0u) set->nextId = 1u;
    set->rules[set->count++] = added;
    return (int)added.id;
}

bool WorldRuleSetRemove(WorldRuleSet *set, uint32_t ruleId)
{
    if (!set || ruleId == 0u) return false;
    for (unsigned index = 0; index < set->count; index++) {
        if (set->rules[index].id != ruleId) continue;
        for (unsigned next = index + 1u; next < set->count; next++)
            set->rules[next - 1u] = set->rules[next];
        set->count--;
        return true;
    }
    return false;
}

bool WorldRuleSetResolve(const WorldRuleSet *set, WorldRuleStage stage,
                         uint32_t bodyId, uint64_t regionId,
                         unsigned *outIndices, unsigned capacity,
                         unsigned *outCount)
{
    if (!set || stage < 0 || stage >= WORLD_RULE_STAGE_COUNT ||
        (!outIndices && capacity > 0u) || !outCount) return false;
    unsigned count = 0u;
    for (unsigned index = 0; index < set->count; index++) {
        const WorldRule *rule = &set->rules[index];
        if (rule->stage != stage ||
            !WorldRuleScopeMatches(rule->scope, bodyId, regionId)) continue;
        if (count >= capacity) return false;
        outIndices[count++] = index;
    }
    for (unsigned i = 1u; i < count; i++) {
        unsigned value = outIndices[i];
        unsigned j = i;
        while (j > 0u &&
               WorldRuleOrderBefore(&set->rules[value],
                                    &set->rules[outIndices[j - 1u]])) {
            outIndices[j] = outIndices[j - 1u];
            j--;
        }
        outIndices[j] = value;
    }
    *outCount = count;
    return true;
}

bool WorldRuleSetApplyVector(const WorldRuleSet *set, WorldRuleStage stage,
                             uint32_t bodyId, uint64_t regionId,
                             Vector3 baseValue, Vector3 *outValue)
{
    if (!outValue || !WorldRuleVectorIsFinite(baseValue)) return false;
    unsigned indices[WORLD_RULE_MAX_RULES];
    unsigned count = 0u;
    if (!WorldRuleSetResolve(set, stage, bodyId, regionId, indices,
                             WORLD_RULE_MAX_RULES, &count)) return false;
    Vector3 value = baseValue;
    for (unsigned i = 0; i < count; i++) {
        const WorldRule *rule = &set->rules[indices[i]];
        if (rule->operation == WORLD_RULE_ADD) value = Vector3Add(value, rule->value);
        else if (rule->operation == WORLD_RULE_MODIFY)
            value = (Vector3){ value.x * rule->value.x,
                               value.y * rule->value.y,
                               value.z * rule->value.z };
        else value = rule->value;
    }
    *outValue = value;
    return true;
}
