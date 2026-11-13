# World Environment Query P5（2026-11-13）

- 日期：2026-11-13
- 性质：阶段记录；实现 [Player、World Simulation 与参考系目标架构](29-player-world-simulation-target-2026-11-13.md) 的 P5
- 范围：水、重力与组合环境样本的 world-side 查询边界

## 1. 实施结果

新增 `world/environment_query.[ch]`，定义 `WorldWaterSample`、`WorldGravitySample`、`WorldEnvironmentSample`，以及带显式 `simulationTime` 的水、重力和组合查询。当前后端仍使用局部参考系，时间参数为后续时变场和规则系统保留明确入口。

`PlayerWaterState` 现在是 `WorldWaterSample` 的 gameplay 类型别名，`PlayerWaterStateAt()` 仅使用 Player 眼高和身体高度调用 world 查询。`PlayerGravitySample` 同样成为 world 样本别名。

`PlayerWorldOps` 的水和重力回调现在接收 simulation time；默认 adapter 直接调用 world environment query，不再直接调用 `PlanetSurfaceAt()`、`WorldGravityScale()`、`FluidSampleAt()` 或 `WorldIsSpaceActive()`。Player Controller 继续只消费样本，不拥有环境实现。

水柱扫描、液体表面、flow velocity、空间/行星重力字段和默认重力 scale 的行为保持原样。组合查询一次返回 position、time、water 和 gravity，供后续规则层和动态实体复用。

## 2. 验证

- `test_player_collision`：新增 world water/environment 查询与 Player wrapper 一致性检查，并保留游泳、重力、流体和 cube/legacy 碰撞覆盖。
- `test_game_player_input`：fixed-step 输入路径不回归。
- `make test-architecture`：新增 environment query 边界检查通过。
- `make test-headers`、`make test-werror`、`make -j4 voxelcraft`：通过。
- `make -s benchmark-chunks`：P5 后报告 `median_total_ms=53.372`、`p95_total_ms=53.678`、`sync_rebuilds=0`。
- 注册测试分两批执行后全部通过；`test_entity_ecology` 单独运行通过。

## 3. 已知边界

- 查询仍使用活动局部参考系；显式 `FrameId` 和跨参考系转换属于 P6。
- 当前重力后端与 simulation time 无关；时间参数已经进入接口，但时变规则尚未实现。
- 水查询仍会读取活动流体状态，但不会触发生成或修改世界。

## 4. 代码索引

- `src/world/environment_query.[ch]`：water/gravity/environment 样本查询。
- `src/gameplay/player_sensors.[ch]`：Player 水样本兼容 wrapper。
- `src/gameplay/player_world.[ch]`：PlayerWorldOps 到 world environment query 的 adapter。
- `src/gameplay/player.c`：消费位置与时间样本的运动策略。
- `tests/test_player_collision.c`：world/player 环境查询一致性回归。
