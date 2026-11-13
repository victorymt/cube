# Player Runtime Rule Integration P8a（2026-11-14）

- 日期：2026-11-14
- 性质：阶段记录；把 [World Rule Environment Consumption P6b](35-world-rule-environment-p6b-2026-11-13.md) 的规则查询接入真实 Player 运动链
- 范围：运行时规则集所有权、组合环境样本、PlayerWorldOps、body/region scope 和重力/水流消费
- 当前状态：见 [STATUS.md](../../STATUS.md)

## 1. 实施结果

`GameRuntime` 现在拥有一个运行时 `WorldRuleSet`，并在初始化时创建绑定该集合的 `PlayerWorldOps`。未配置时规则集为空，Player 行为保持默认；世界定义或后续运行时装配可以向同一集合注册 gravity/water 规则。

`PlayerWorldOps` 的水和重力回调合并为一个 `sampleEnvironment`：

```text
PlayerWorldOpsWithRules(runtime->worldRules)
  -> WorldEnvironmentSampleAtWithRules(
         active bodyId,
         current regionId,
         runtime rule set)
  -> WorldEnvironmentSample { water, gravity }
  -> UpdatePlayerWithWorld()
  -> ActorBody commit
```

每次 tick 在移动前采样一次环境，在移动后再次采样水状态用于入水、溅水和脚步判断。这样水与重力使用同一个规则上下文，也避免 Player policy 分别实现两套 world 查询。

## 2. 重力与水流语义

`WorldGravitySample.acceleration` 现在是实际 game-space acceleration，而不是未缩放的相对强度：

- 非空间环境的基础值为 `-GRAVITY * clampedGravityScale`，继续使用 Player 原有的 `0.45..1.75` 相对重力限制。
- 空间中的行星重力继续使用原有 `10 * surfaceGravityScale * distanceFalloff`，近距离的 `0.25` 衰减保持不变。
- gravity 规则在 base acceleration 上执行 ADD/MODIFY/REPLACE，最终值直接进入 Player 速度积分。
- jump speed 仍由 `gravityScale` 决定，避免规则改变水平/额外加速度时意外改变跳跃输入曲线。
- water 规则修改 `WorldWaterSample.flowVelocity`；游泳水平和水下垂直目标速度直接消费最终 flow。

无规则时的基础运动结果与接入前一致。规则的效果不再只存在于独立样本测试，而会实际改变 Player 的速度和位置。

## 3. Scope 语义

Player 查询使用当前活动 surface 的 `bodyId`。body-scoped 规则只匹配当前天体；global 规则持续生效。

当前 world 还没有独立于生态或地图分区的通用 `regionId`，因此 Player 查询传入 `0`。这一选择保证 global/body 规则可用，同时不会把某个生态网格尺寸偷偷固定成全局规则区域约定。未来 world definition 提供显式 region identity 后，只需替换该输入，不改变 Player Controller。

## 4. 验证

- `test_player_collision`：
  - 校验无规则重力 base acceleration 为实际 game acceleration。
  - 通过 `PlayerWorldOpsWithRules()` 注册 body-scoped gravity 和 water 规则。
  - 验证 water rule 改变游泳速度、gravity rule 改变空中速度，而非只验证样本字段。
  - 验证不匹配 body 的规则不改变 Player。
- `test_world_rules`：规则顺序、scope、优先级和操作语义不回归。
- `test_game_player_input`、`test_game_ecology_tick`、`test_simulation_integration`：固定步、调度和 replay 回归通过。
- `make test-architecture`、`make test-headers`、`make test-werror`、`make -j4 voxelcraft`：通过。
- 完整 `make test`：139 passed，0 failed；随后架构门禁通过。

## 5. 已知边界

- 规则集合是运行时状态，不写入 V30；本轮没有世界定义/存档规则加载器。
- 运行时集合默认为空；本阶段完成的是可注入且真实消费的链路，不是具体星球内容配置。
- Player 查询的通用 region identity 尚未建立，region-scoped 规则仍保留到后续 world definition。
- renderer/save caller 的 frame API 全面采用仍是独立工作。

## 6. 代码索引

- `src/world/environment_query.[ch]`：实际重力加速度、规则应用和组合样本。
- `src/gameplay/player_world.[ch]`：运行时规则绑定、active body 查询和组合样本适配。
- `src/gameplay/player.c`：组合样本消费、速度积分和水状态转换。
- `src/app/game_runtime.[ch]`：运行时规则集所有权与初始化。
- `tests/test_player_collision.c`：规则通过 PlayerWorldOps 影响真实运动的回归。
- `tests/test_game_ecology_tick.c`：固定步调度测试的 Player motion seam。
