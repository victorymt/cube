# Tornado Environment Sample P8.3（2026-11-14）

- 日期：2026-11-14
- 性质：阶段记录；把 tornado 力场加入统一 `WorldEnvironmentSample`
- 范围：tornado sample、组合环境查询、Player external-force 数据来源和回归
- 当前状态：见 [STATUS.md](../../STATUS.md)

## 1. Before / After

Before：water/gravity 已在组合环境样本中，但 Player tornado 外力仍由 app 直接调用：

```text
GameApplyTornadoPlayerForce
  -> TornadoForceAt(chest position)
  -> PlayerWaterStateAt(player position)
  -> direct Player velocity writes
```

After：

```text
WorldEnvironmentSampleAtWithRules
  -> water sample
  -> gravity sample
  -> tornado sample at position.y + eyeHeight * 0.52
  -> WorldEnvironmentSample

GameApplyTornadoPlayerForce(environment)
  -> environment.tornado
  -> environment.water.bodySubmerged
  -> shelter / resistance / clamps
  -> direct Player velocity writes (still outside motion pipeline)
```

## 2. Tornado Sample 语义

新增 `WorldTornadoSample`：

- `active`：当前采样点实际暴露于 tornado force field，即 `exposure > 0`；
- `acceleration`：原始 tornado acceleration；
- `exposure`：局部暴露强度；
- `localWindMps`：局部风速；
- `horizontalDistance`：到 tornado 中心的水平距离。

`WorldTornadoSampleAt(Vector3 samplePosition)` 直接包装现有 `TornadoForceAt()`。环境查询用当前 Player 胸口高度采样，保持与旧 app 外力位置一致。

环境和 tornado 规则边界如下：

- tornado 不加入 `WorldRuleSet`；
- water/gravity rule count 不受影响；
- sample 不包含 shelter、水中阻力或 Player 速度上限；这些仍是 gameplay external-force response。

## 3. 消费与行为保持

app 的 tornado 外力改用 `PlayerWorldOps.sampleEnvironment()`：

- 在 Player movement、space rebase 和 surface canonicalization 之后采样；
- 使用 `environment.tornado`，不再直接调用 `TornadoForceAt()`；
- 使用 `environment.water.bodySubmerged`，不再单独调用 `PlayerWaterStateAt()`；
- 保留 shelter `0.16`、水下 `0.10`、ground X/Z `0.72`、ground vertical `0.58/0.08`、水平 `24` 和垂直 `[-35,18]` 限制。

Tornado 仍不属于 `PlayerMotionStep()`，也没有增加第二套 Player 状态。

## 4. 验证

- `test_player_collision`：验证 tornado sample 字段复制、chest offset、inactive zero sample，以及与 water/gravity 组合。
- `test_tornado_model`、`test_tornado`：tornado force、runtime 和 spherical alias 不回归。
- Player motion simulation、fixed-step input、simulation integration、architecture、headers、Werror 和完整构建通过。
- `test_game_e2e` 的 tornado force 段落可执行；整条 route 最终停在既有 `block.catalog_count == 412` fixture 断言，与本阶段无关。
- `make test-architecture`、`make test-headers`（246 headers）、`make test-werror`、`make -j4 voxelcraft`：通过。
- 完整 `make test`：141 passed，0 failed；随后架构门禁通过。

## 5. 已知边界 / Deferred

- Tornado 外力仍在 app 中直接修改 Player velocity，尚未进入 external-force stage。
- `WorldEnvironmentSample.tornado` 不含遮蔽、介质阻力或 user-specific response。
- tornado 规则化、重力/水流/风暴的组合规则和实时持久化不在本阶段。
- V30、DSL、固定步调度和 replay 状态字段不变。

## 6. 代码索引

- `src/world/environment_query.[ch]`：`WorldTornadoSample` 和组合样本。
- `src/world/tornado.[ch]`：现有 tornado force field。
- `src/app/game.c`：Player tornado external-force consumption。
- `tests/test_player_collision.c`：sample 字段与 chest-offset fixture。
- `tests/test_tornado_model.c`、`tests/test_tornado.c`：底层 tornado 回归。
