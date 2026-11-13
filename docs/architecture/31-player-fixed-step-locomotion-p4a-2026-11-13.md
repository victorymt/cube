# Player Fixed-Step Locomotion P4a（2026-11-13）

- 日期：2026-11-13
- 性质：阶段记录；实现 [Player、World Simulation 与参考系目标架构](29-player-world-simulation-target-2026-11-13.md) 的 P4 固定步部分
- 范围：输入 staging、fixed-step locomotion、相机/流式时序和运动状态提交

## 1. 实施结果

普通玩家运动和 ship motion 现在由 `GameAdvanceSimulationStep()` 中的固定步回调推进，不再直接使用渲染帧 `dt`。`GameUpdateFrame()` 在进入 fixed-step clock 前完成输入采集，并在模拟结束后更新相机和世界流式请求。

新增 render-frame 到 simulation-tick 的输入 staging：方向、strafe、vertical 和 sprint 使用最新持续输入；鼠标视角、jump 和 toggle-floating 会跨渲染帧累积，并由一个 fixed tick 消费一次。这样不会因为帧率高于或低于 60 Hz 而重复触发边沿动作或丢失输入。

`GamePlayerInputStageFixed()` 与 `GamePlayerInputConsumeFixed()` 被拆成可单元测试的纯输入函数。`GameRuntime` 保存 staged input、边沿 pending 状态和当前 fixed-step 输入阻塞状态。

`UpdatePlayerWithWorld()` 现在通过 `PlayerApplyActorBody()` 提交玩家身体结果，减少 motion 路径中的重复字段拷贝。tornado 玩家外力也移动到 fixed-step motion 末尾，与玩家运动共享同一时间步。

## 2. 时序

```text
InputSourceCaptureFrame
  -> UI / mode / shortcuts
  -> GameStagePlayerInput
  -> SimulationClock fixed steps
       -> GameUpdatePlayerMotion
       -> ecology / weather / fluid / hazards
  -> GameUpdateCamera
  -> GameUpdateWorldStreaming
  -> interaction / environment / render
```

## 3. 输入语义

- `forward`、`strafe`、`vertical`、`sprint`：最近渲染帧覆盖，持续参与每个 fixed tick。
- `lookDelta`：跨帧累加，仅在下一 fixed tick 消费一次。
- `jumpPressed`、`toggleFloating`：跨帧逻辑 OR，仅消费一次。
- 输入被 UI、pause、ship 或加载状态阻塞时，staged 输入清零，避免恢复后执行过期动作。
- scripted input 仍优先于桌面输入；debug 未显式 manual handoff 时不读取物理输入。

## 4. 验证

- `test_game_player_input`：新增 staged/consume 测试，覆盖持续轴、累计视角、跳跃边沿和 floating 边沿。
- `test_player_collision`、`test_ship_state`、`test_world_block_box`、`test_world_block_landing`：通过。
- `test_player_strafe_direction_e2e`：Hyprland 脚本回放通过。
- `test_cube_decorative_collision_e2e`：平地、装饰方块与碰撞回放通过。
- `make test-headers`、`make test-werror`、`make test-architecture` 和完整程序构建通过。
- 注册测试分两批执行后全部通过；`test_entity_ecology` 单独运行通过。

## 5. 已知边界

- 特殊状态入口，包括 teleport、landing、load、map pause 和部分 debug placement，仍存在直接字段写入；后续 P4b 应统一为显式 transition API。
- replay checkpoint 尚未包含玩家运动状态；fixed-step 行为已可复现，但完整 player replay 仍需补充。
- ship 仍通过 Player facade 提交最终 actor body，P4b 将继续清理。

## 6. 代码索引

- `src/app/game.c`：输入 staging 和 fixed-step 调度顺序。
- `src/app/game_ecology.c`：fixed-step 回调中的玩家运动。
- `src/app/game_player_input.[ch]`：fixed-step 输入累计与消费。
- `src/app/game_runtime.h`：staged input 和 simulation input 状态。
- `src/gameplay/player.c`：通过 `PlayerApplyActorBody` 提交运动结果。
- `tests/test_game_player_input.c`：固定步输入语义测试。
