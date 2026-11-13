# World Rule Environment Consumption P6b（2026-11-13）

- 日期：2026-11-13
- 性质：阶段记录；扩展 [World Rules and Frame Foundation P6](34-world-rules-frame-p6-2026-11-13.md) 的环境规则消费
- 范围：gravity acceleration 样本、body/region rule scope、rule count 和 environment query 集成

## 1. 实施结果

`WorldGravitySample` 新增 baseline `acceleration` 和 `ruleCount`。非空间环境的基础加速度为局部向下 `gravityScale`，空间行星环境为 `direction * surfaceGravityScale`；没有环境数据时保持零向量。

新增 `WorldEnvironmentSampleAtWithRules()`。它在构造 water/gravity 样本后，按 `WorldRuleStage.GRAVITY` 解析与 active body/region 匹配的规则，并将结果应用到 gravity acceleration。无规则入口 `WorldEnvironmentSampleAt()` 保持原行为并将 `ruleCount` 置零。

规则 scope 现在会实际参与查询：global 规则影响全部 body，body 规则只影响指定 body，region 规则同时匹配 body id 与 region id。ADD、MODIFY、REPLACE 的确定顺序继续由规则集保证。

当前只消费 gravity 规则。water rule stage 已存在但尚未改变 water sample；system/render/save frame 转换仍属于后续 P6c。

## 2. 验证

- `test_world_rules`：scope/priority/id 顺序和三种操作。
- `test_player_collision`：baseline acceleration、body-scoped gravity rule、`ruleCount` 和 non-matching body。
- `make test-architecture`、`make test-headers`、`make test-werror`：通过。
- `make -j4 voxelcraft`：通过。
- 非 entity 注册测试全部通过；`test_entity_ecology` 单独运行通过。

## 3. 已知边界

- 规则结果尚未写入 V30；配置注册由运行时/未来 world definition 负责。
- water rule stage 尚未作用于水体样本。
- `SYSTEM`、render-camera 和 save-frame 转换仍未实现；P6c 继续处理。

## 4. 代码索引

- `src/world/environment_query.[ch]`：带规则的重力/环境样本。
- `src/world/world_rules.[ch]`：规则解析与向量求值。
- `tests/test_player_collision.c`：规则实际影响环境样本的回归。
