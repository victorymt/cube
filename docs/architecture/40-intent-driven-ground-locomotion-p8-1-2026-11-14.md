# Intent-Driven Ground Locomotion P8.1（2026-11-14）

- 日期：2026-11-14
- 性质：阶段记录；实现玩家普通地面行走、冲刺和跳跃策略的 actor-neutral simulation 抽离
- 范围：`ActorLocomotionIntent`、`ActorLocomotionProfile`、一 tick ground solver 和 Player 固定步接入
- 当前状态：见 [STATUS.md](../../STATUS.md)

## 1. Before / After

Before：

```text
PlayerInput
  -> UpdatePlayerWithWorld()
  -> Player speed/jump/gravity/horizontal policy
  -> PlayerWorldOps.move()
  -> Player commit
  -> post-move water/effects
```

After，普通地面分支：

```text
PlayerInput
  -> Player controller creates ActorLocomotionIntent
  -> ActorGroundLocomotionStep(
         ActorBody,
         Intent,
         Profile,
         combined environment sample,
         PlayerWorldOps)
  -> ActorMotionRequest
  -> WorldActorResolveMotion()
  -> ActorLocomotionResult
  -> PlayerApplyActorBody()
  -> post-move water/effects
```

`ActorGroundLocomotionStep()` 位于 `gameplay/actor_locomotion.[ch]`。它接收 `ActorBody`，不接收 `Player`；不读取键盘、raylib 输入、具体 cube/legacy 拓扑、音频、粒子或 UI。World 几何仍由既有 `PlayerWorldOps.move` 和 `WorldActorResolveMotion()` 完成。

## 2. Intent 与 Profile

`ActorLocomotionIntent` 的最小字段是：

- `forward`：沿 Actor yaw 前方，范围 `[-1,1]`。
- `strafe`：沿用现有 screen-right basis，范围 `[-1,1]`。
- `sprint`：选择 sprint speed。
- `jump`：已由 Player controller 合并 `jumpPressed` 与向上输入的固定 tick 边沿。

Player controller 负责从 `PlayerInput` 生成 Intent。look/yaw/pitch 和 toggle-floating 仍属于 Player controller，不进入通用地面 simulation。

`PlayerLocomotionProfile()` 提供：

- walk/sprint speed；
- base jump speed；
- gravity scale clamp；
- fall speed cap。

Profile 只是能力参数，不保存 position/velocity；当前运动状态始终只有 `ActorBody` 一份。

## 3. 行为保持

普通地面路径继续保留现有行为：

- 静止时施加重力并保持既有下落限制。
- 前后、横移、冲刺速度不变；斜向输入先归一化。
- jump 只在 `onGround` 时触发；同一 tick 先设置跳跃速度，再施加重力。
- 台阶请求仍由 World geometry 根据移动前 `onGround` 决定。
- 未就绪区域、墙、台阶和 cube/legacy 差异都由既有 world solver 处理。
- 水中、太空、漂浮、载具和龙卷风分支未迁移。
- 入水检测、水花、泡影和脚步计时仍在 PlayerGameplay 的表现后处理阶段。
- 每个 tick 仍只提交一个最终 `ActorBody`，没有第二套位置或速度权威。

当前重力语义沿用 P8a：`WorldGravitySample.acceleration` 已是实际 game-space acceleration。P8.1 不重新定义该字段，也不回退 runtime rule 消费。

## 4. 验证

- 新增 `test_actor_locomotion`：静止/重力、前进、横移、冲刺、斜向归一化、jump edge、fall clamp、fake geometry 阻挡、profile variation、确定性和非法输入。
- `test_player_collision`：新增真实 Player 固定步 profile/jump 集成检查；原有墙、台阶、cube、未加载 frontier、游泳和规则消费测试继续通过。
- `test_game_player_input`、`test_simulation_integration`：固定步输入边沿与 Player replay checkpoint 不回归。
- `make test-architecture`、`make test-headers`（245 headers）、`make test-werror`、`make -j4 voxelcraft`：通过。
- `test-player-strafe-direction-e2e`、`test-cube-decorative-collision-e2e`：Hyprland 下通过。
- 完整 `make test`：140 passed，0 failed；随后架构门禁通过。

## 5. 已知边界 / Deferred

- 只抽离普通地面移动与跳跃；水、太空、漂浮、载具和龙卷风策略保持原实现。
- `PlayerWorldOps` 名称未重命名；未来可评估 `ActorWorldOps`，但不属于 P8.1。
- 跨 frame 的速度/角速度语义、规则持久化和具体星球规则内容仍属于其他阶段。
- 未引入 ECS、Entity 存储改造、消息总线或新的世界状态副本。

## 6. 代码索引

- `src/gameplay/actor_locomotion.[ch]`：Intent、Profile、Result 和单 tick 地面 simulation。
- `src/gameplay/player_motion.[ch]`：Player collider 与 Player locomotion profile。
- `src/gameplay/player.c`：输入转 Intent、普通地面手递、特殊模式分支和唯一 Player commit。
- `src/gameplay/player_world.[ch]`：world environment 与 geometry dependency port。
- `src/world/actor_motion.[ch]`：topology-neutral geometry solver。
- `tests/test_actor_locomotion.c`：独立 simulation 回归。
- `tests/test_player_collision.c`：真实 World/Player 集成回归。
