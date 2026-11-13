#ifndef VOXELCRAFT_WORLD_RULES_H
#define VOXELCRAFT_WORLD_RULES_H

#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

#define WORLD_RULE_MAX_RULES 32u

typedef enum WorldRuleStage {
    WORLD_RULE_STAGE_GRAVITY = 0,
    WORLD_RULE_STAGE_WATER,
    WORLD_RULE_STAGE_COUNT
} WorldRuleStage;

typedef enum WorldRuleScopeKind {
    WORLD_RULE_SCOPE_GLOBAL = 0,
    WORLD_RULE_SCOPE_BODY,
    WORLD_RULE_SCOPE_REGION
} WorldRuleScopeKind;

typedef enum WorldRuleOperation {
    WORLD_RULE_ADD = 0,
    WORLD_RULE_MODIFY,
    WORLD_RULE_REPLACE
} WorldRuleOperation;

typedef struct WorldRuleScope {
    WorldRuleScopeKind kind;
    uint32_t bodyId;
    uint64_t regionId;
} WorldRuleScope;

typedef struct WorldRule {
    uint32_t id;
    WorldRuleStage stage;
    int priority;
    WorldRuleScope scope;
    WorldRuleOperation operation;
    Vector3 value;
} WorldRule;

typedef struct WorldRuleSet {
    WorldRule rules[WORLD_RULE_MAX_RULES];
    unsigned count;
    uint32_t nextId;
} WorldRuleSet;

void WorldRuleSetInit(WorldRuleSet *set);
int WorldRuleSetAdd(WorldRuleSet *set, const WorldRule *rule);
bool WorldRuleSetRemove(WorldRuleSet *set, uint32_t ruleId);
bool WorldRuleSetResolve(const WorldRuleSet *set, WorldRuleStage stage,
                         uint32_t bodyId, uint64_t regionId,
                         unsigned *outIndices, unsigned capacity,
                         unsigned *outCount);
bool WorldRuleSetApplyVector(const WorldRuleSet *set, WorldRuleStage stage,
                             uint32_t bodyId, uint64_t regionId,
                             Vector3 baseValue, Vector3 *outValue);

#endif
