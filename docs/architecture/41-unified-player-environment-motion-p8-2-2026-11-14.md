# Unified Player Environment Motion P8.2（2026-11-14）

> tornado 随后加入组合环境样本，见 [Tornado Environment Sample P8.3](42-tornado-environment-sample-p8-3-2026-11-14.md)。

- 日期：2026-11-14
- 性质：阶段记录；统一 Player 的 ground、water、space、floating locomotion
- 范围：Intent、mode selection、mode simulation、common collision 和 single Player commit
- 当前状态：见 [STATUS.md](../../STATUS.md)

## 1. Before / After

P8.1 后只有 ground 走 neutral simulation；Player controller 仍保留 water、space、floating 的直接速度计算：

```text
PlayerInput
  -> ground: ActorGroundLocomotionStep
  -> water/space/floating: Player-local branches
  -> separate collision/commit paths
```

P8.2 后普通 Player locomotion 统一为：

```text
PlayerInput
  -> PlayerMotionIntent
  -> PlayerMotionStep
       -> sample environment
       -> select GROUND / WATER / SPACE / FLOATING
       -> mode simulation computes candidate ActorBody
       -> common world collision
       -> sample moved environment for water contact
  -> PlayerMotionResult
  -> PlayerApplyActorBody() once
  -> gameplay effects
```

`PlayerMotionStep()` 位于 `gameplay/player_motion_simulation.[ch]`。它不接收 `Player`，不读取键盘，不包含 cube/legacy 拓扑判断，也不发布音频、粒子或 UI 副作用。

## 2. Mode 语义

Mode priority 与旧 Player 分支一致：

1. `SPACE`
2. `FLOATING`
3. `WATER`
4. `GROUND`

只有 body/eyes submerged 才进入 WATER；feet-only 浸水仍由 GROUND 处理。`toggleFloating` 和 look delta 作为 Intent 在候选 body 上处理，最终随同一次 commit 写回。

各 mode 的旧参数迁入 `PlayerMotionProfile`：

- ground：walk/sprint、jump、gravity clamp、fall cap；
- water：水平/垂直目标速度、flow、响应系数、vertical clamp；
- space：垂直输入加速度、阻尼、vertical clamp；
- floating：垂直速度。

`ActorGroundLocomotionCompute()` 负责 ground mode 的候选 body；既有 `ActorGroundLocomotionStep()` 保留为 compute + collision 的兼容入口。

## 3. Collision 与 Commit

四种 mode 都调用同一个 `PlayerWorldOps.move`：

- ground：step-up 与地形碰撞；
- water：水体不阻断位移，陆地边界仍由 world solver 处理；
- space/floating：使用同一 world body collision；
- 未加载区域规则和 cube/legacy backend 差异继续由 `WorldActorResolveMotion()` 处理。

移动完成后再次采样环境，仅把 `feetInWater` 作为 contact state 返回。Player controller 负责：

- 一次 `PlayerApplyActorBody()` 提交；
- 入水音效、泡影和脚步计时；
- space rebase、world transition 和 tornado 外力等既有后续阶段。

因此每个 fixed tick 只有一份权威 position/velocity，不存在各 mode 自己的 commit 路径。

## 4. 验证

- 新增 `test_player_motion_simulation`：mode priority、ground pipeline、water flow/vertical response、space gravity/damping/clamp、floating、toggle/look、common collision、两次 environment sample、确定性和非法输入。
- `test_actor_locomotion`：ground compute + collision compatibility。
- `test_player_collision`：真实 world collision 下的普通地面、游泳、cube、墙、台阶和 frontier 回归。
- `test_game_player_input`、`test_simulation_integration`：fixed-step edge 与 replay checkpoint 不回归。
- architecture gate 新增检查：`player.c` 不得重新出现 mode-specific locomotion 数学，且只能调用一次最终 ActorBody commit。
- `make test-architecture`、`make test-headers`（246 headers）、`make test-werror`、`make -j4 voxelcraft`：通过。
- `test-player-strafe-direction-e2e`、`test-cube-decorative-collision-e2e`、`test-debug-graphics-e2e`：Hyprland 下通过；后者覆盖 ship debug route。
- 完整 `make test`：141 passed，0 failed；随后架构门禁通过。

## 5. 已知边界 / Deferred

- P8.3 后 Tornado 已进入组合环境样本，但仍由 app 在统一 pipeline 之后应用，尚未进入 external-force stage。
- Ship 仍使用独立 `ShipUpdate*()` 和 app commit 路径。
- Landing、world transition、teleport/debug placement 继续作为显式状态过渡。
- 跨 frame 的 velocity/rotation 语义未在本阶段扩展。
- V30、模拟频率、DSL 和 replay 状态字段不变。

## 6. 代码索引

- `src/gameplay/player_motion_simulation.[ch]`：统一 Intent、mode、profile、result 和 pipeline。
- `src/gameplay/actor_locomotion.[ch]`：ground mode compute 与兼容 step。
- `src/gameplay/player.c`：输入转 Intent、pipeline 调用、单次 commit 和 gameplay effects。
- `src/gameplay/player_motion.[ch]`：Player collider、ground profile 和完整 motion profile。
- `src/gameplay/player_world.[ch]`：world environment/motion port。
- `tests/test_player_motion_simulation.c`：统一 pipeline 单元回归。
