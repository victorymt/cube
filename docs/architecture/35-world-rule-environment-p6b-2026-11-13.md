# World Rule Environment Consumption P6b（2026-11-13）

> system/render/save frame API 已由 [World Frame Cross-Reference P6c](36-world-frame-cross-p6c-2026-11-13.md) 补充。

- 日期：2026-11-13
- 性质：阶段记录；扩展 [World Rules and Frame Foundation P6](34-world-rules-frame-p6-2026-11-13.md) 的环境规则消费
- 范围：gravity acceleration 样本、body/region rule scope、rule count 和 environment query 集成

## 1. 实施结果

`WorldGravitySample` 新增 baseline `acceleration` 和 `ruleCount`。非空间环境的基础加速度为局部向下 `gravityScale`，空间行星环境为 `direction * surfaceGravityScale`；没有环境数据时保持零向量。

新增 `WorldEnvironmentSampleAtWithRules()`。它在构造 water/gravity 样本后，按 `WorldRuleStage.GRAVITY` 解析与 active body/region 匹配的规则，并将结果应用到 gravity acceleration。无规则入口 `WorldEnvironmentSampleAt()` 保持原行为并将 `ruleCount` 置零。

规则 scope 现在会实际参与查询：global 规则影响全部 body，body 规则只影响指定 body，region 规则同时匹配 body id 与 region id。ADD、MODIFY、REPLACE 的确定顺序继续由规则集保证。

同一查询也消费 water 规则：`WorldEnvironmentSample` 新增 `waterRuleCount`，规则结果修改 `WorldWaterSample.flowVelocity`；无规则入口保持原 flow 不变。

## 2. 验证

- `test_world_rules`：scope/priority/id 顺序和三种操作。
- `test_player_collision`：baseline acceleration、body-scoped gravity/water rule、rule count、flow modification 和 non-matching body。
- `make test-architecture`、`make test-headers`、`make test-werror`：通过。
- `make -j4 voxelcraft`：通过。
- 非 entity 注册测试全部通过；`test_entity_ecology` 单独运行通过。

## 3. 已知边界

- 规则结果尚未写入 V30；配置注册由运行时/未来 world definition 负责。
- water 与 gravity rule 只产生 environment sample，不直接修改世界或存档。
- system/render/save frame API 已由 P6c 提供；渲染器和 save caller 的全面采用仍待后续集成。

## 4. 代码索引

- `src/world/environment_query.[ch]`：带规则的重力/环境样本。
- `src/world/world_rules.[ch]`：规则解析与向量求值。
- `tests/test_player_collision.c`：规则实际影响环境样本的回归。
