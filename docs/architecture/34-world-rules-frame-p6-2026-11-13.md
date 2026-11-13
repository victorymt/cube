# World Rules and Frame Foundation P6（2026-11-13）

> gravity rule 已接入 environment sample，见 [World Rule Environment Consumption P6b](35-world-rule-environment-p6b-2026-11-13.md)。
> system/render/save frame API 见 [World Frame Cross-Reference P6c](36-world-frame-cross-p6c-2026-11-13.md)。

- 日期：2026-11-13
- 性质：阶段记录；实现 [Player、World Simulation 与参考系目标架构](29-player-world-simulation-target-2026-11-13.md) 的 P6 基础边界
- 范围：确定性环境规则注册表、作用域/阶段/优先级与显式 frame identity

## 1. 实施结果

新增 `world/world_rules.[ch]`。规则携带 stage、scope、priority、operation、registration id 和向量值，使用固定容量存储；解析顺序为 scope specificity（global、body、region）、priority、registration id，保证同一输入产生稳定顺序。

支持三种向量规则操作：`ADD` 累加、`MODIFY` 逐分量乘、`REPLACE` 覆盖。当前开放 gravity 和 water 两个规则阶段，后续环境字段可以增加阶段而不修改 solver 约定。规则集合不持久化，当前也不直接修改世界状态。

新增 `world/world_frame.[ch]`。`WorldFrameId` 显式携带 frame kind 与 body id，`WorldFramePoint` 使用 double 坐标。当前实现支持同一 frame 的 identity 转换，以及 active surface 的 local ↔ body-centered 双向转换；system/render/camera 等尚未具备投影上下文的跨 frame 转换明确返回不支持，而不是静默把坐标当成等价。

## 2. 规则语义

| 顺序键 | 规则 |
| --- | --- |
| scope | global 先于 body，body 先于 region |
| priority | 数值小者先应用 |
| registration id | 最终稳定次序 |
| operation | ADD、MODIFY、REPLACE 按上述顺序执行 |

相同 scope/priority 的规则顺序由自增 id 确定；移除规则不重排剩余 id。新增特殊星球可以注册 body/region 规则，无需在 Player Controller 增加星球类型分支。

## 3. 参考系边界

`WorldFrameCanTransform()` 只声明已有实现能力的转换。surface local 与 body-centered 的转换复用 `WorldActorPoseFromLocal()` / `WorldActorLocalFromPose()`，并验证 active surface body id。无效、非有限或未支持的 frame pair 返回 `false`。

当前 space/system 坐标仍沿用既有本地空间约定；在真正建立系统中心投影和高精度相机相对转换前，不伪造 `SYSTEM` 与 surface/body 之间的转换。

## 4. 验证

- 新增 `test_world_rules`：验证 global/body/region 顺序、priority/id 稳定性、ADD/MODIFY/REPLACE、remove 和非法规则拒绝。
- `test_player_collision`：验证 surface-local ↔ body-centered round trip、identity、非法坐标和不支持的 frame pair。
- `make test-architecture`、`make test-headers`、`make test-werror`：通过。
- `make -j4 voxelcraft`：通过。
- 非 entity 注册测试全部通过；`test_entity_ecology` 单独运行通过。

## 5. 已知边界

- gravity 规则已通过 `WorldEnvironmentSampleAtWithRules()` 接入；water 规则阶段仍未消费。
- P6c 已补充 system/render/save frame API；渲染器和 save caller 的全面采用仍待后续集成。
- 规则集合与 frame 都是运行时结构，不改变 V30 存档格式。

## 6. 代码索引

- `src/world/world_rules.[ch]`：确定性规则注册与向量求值。
- `src/world/world_frame.[ch]`：frame identity 和已支持的 point 转换。
- `src/world/environment_query.[ch]`：后续规则接入的环境样本边界。
- `tests/test_world_rules.c`、`tests/test_player_collision.c`：规则和 frame 回归。
