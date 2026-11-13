# Player State Commit P4b（2026-11-13）

- 日期：2026-11-13
- 性质：阶段记录；实现 [Player、World Simulation 与参考系目标架构](29-player-world-simulation-target-2026-11-13.md) 的 P4 状态提交部分
- 范围：特殊 Player 状态入口、直接字段写入和统一 transition API

## 1. 实施结果

新增 `gameplay/player_state.[ch]`，提供 `PlayerHaltMotion()`、`PlayerSetLook()`、`PlayerSetFloating()`、`PlayerPlaceAt()`、`PlayerApplyMotionResult()` 和 `PlayerRestoreState()`。这些入口统一验证位置、速度、朝向和 floating 输入，并只提交调用者拥有的状态字段。

app 中不再存在直接写入 `game->player.position/velocity/yaw/pitch/onGround/floating` 的代码。spawn、world transition、landing 插值/落地、teleport、surface pose/address 切换、perf route、map/inventory pause、portal、ship/space 回写和读档恢复都经过 Player state/ActorBody commit API。

ship 内部仍直接更新它收到的 `ActorBody`；这是 ship simulation policy 的局部工作区，最终通过 `PlayerApplyActorBody()` 提交回 Player。Player locomotion 和 player_motion facade 也分别通过 `PlayerApplyActorBody()` 与 `PlayerApplyMotionResult()` 提交。

新增架构检查：app C 源码出现直接 Player pose/velocity 字段赋值时 `make test-architecture` 失败。

## 2. 验证

- 新增 `test_player_state`：覆盖 halt、look、floating、placement 验证、motion result commit 和 restore。
- `test_game_player_input`、`test_player_collision`、`test_ship_state`、`test_game_save_spherical`、`test_game_debug_gate`、`test_screenshot`：通过。
- `test_player_strafe_direction_e2e`、`test_cube_decorative_collision_e2e`：Hyprland 回放通过。
- `make test-architecture`、`make test-headers`、`make test-werror` 和完整程序构建通过。
- 注册测试分两批执行后全部通过；`test_entity_ecology` 单独运行通过。

## 3. 已知边界

- Player replay checkpoint 尚未纳入状态哈希；输入 fixed-step 已可复现，但跨进程 checkpoint 仍只覆盖现有模拟子系统和存档路径。
- ship 的 `ActorBody` 内部写入保留在 ship controller；它不写 Player 结构，最终通过显式 commit 返回。
- `PlayerRestoreState` 当前复制完整 Player；读档后的 runtime reset 仍由调用者执行，保持 V30 读取顺序不变。

## 4. 代码索引

- `src/gameplay/player_state.[ch]`：显式 Player transition/commit API。
- `src/gameplay/player_actor.h`：Player 与 ActorBody 的转换和基础提交。
- `src/gameplay/player_motion.c`：neutral motion result 到 Player 的提交。
- `src/app/game_world_transition.c`、`src/app/game_landing.c`：世界切换和 landing transition。
- `src/app/game_debug_player.c`、`src/app/game_debug_topology.c`：debug pose/teleport。
- `tests/test_player_state.c`：状态提交边界测试。
