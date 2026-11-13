# Player World Motion P3 实施记录（2026-11-13）

- 日期：2026-11-13
- 性质：阶段记录；实现 [Player、World Simulation 与参考系目标架构](29-player-world-simulation-target-2026-11-13.md) 的 P3
- 范围：把 legacy/cube 位移、碰撞、step-up、落地查询和 surface pose 转换迁移到 world-side neutral motion solver

## 1. 实施结果

新增 `world/actor_motion.[ch]`，公开 `ActorCollider`、`ActorMotionRequest`、`ActorMotionResult` 和 `WorldActorResolveMotion()`。求解器接收中性 `ActorBody`，不再依赖 gameplay 的 `Player` 类型，也不包含 ship、输入、相机或表现逻辑。

legacy 逐轴移动、cube `SurfacePose` 位移、身体盒枚举、碰撞、接地、step-up、未加载区域规则、landing 搜索以及 local/surface pose 转换现在都位于 world 实现中。`player_motion.c` 只保留 Player-specific collider、兼容 facade、ship 驱动标志和方向向量辅助函数。

`PlayerWorldOps.move` 改用 `ActorBody` 与 `ActorMotionRequest`，默认实现直接调用 `WorldActorResolveMotion()`；旧的 `PlayerBodyState` 和 `PlayerMoveRequest` 重复值类型已删除。`UpdatePlayerWithWorld()` 不再构造 gameplay 私有的 body 中间类型。

兼容 facade 保留现有 app 调用点，因此本轮没有改变 V30 存档、debug DSL、交互字段或玩家可见控制。后续 P4 再统一状态写入所有权和固定步。

## 2. 边界约束

- world 运动代码不得包含 gameplay 头文件或依赖 `Player`。
- gameplay facade 不得直接调用 `GetBlockAt`、`SurfaceContract`、`SurfaceAddress`、`WorldBlockBox` 或 `SurfacePoseDisplace`。
- legacy 与 cube 的选择仍依据 active contract 的能力，不暴露 topology enum。
- `ActorBody` 的 yaw/pitch 在求解前后保持不变；求解器只修改运动、接地和 floating 状态。
- 未加载区域对步行者仍是临时墙，对允许 `allowUnready` 的 ship/特殊路径仍可通行。

## 3. 验证

- `test_player_collision`：覆盖 legacy 平地、墙、台阶、悬崖、cube 跨面/极区、游泳、未加载 frontier、body-box、相机点查询和直接 neutral solver 调用。
- `test_ship_state`：ship 现有 MovePlayer facade 行为不回归。
- `test_game_player_input`：输入映射、debug handoff 和 script priority 不回归。
- `test_world_block_box`、`test_world_block_landing`：身体盒与着陆边界不回归。
- `make test-architecture`：新增 world/gameplay 边界检查通过。
- `make test-headers`：239 个公开头文件独立编译通过。
- `make -j4 voxelcraft`：完整程序构建通过。
- 注册测试分两批执行后全部通过；`test_entity_ecology` 单独运行通过。

## 4. 已知边界

- app 仍主要调用 `Player*` facade；这是 P3 为限制改动范围保留的兼容层。
- ship 仍通过 `MovePlayer()` facade 进入 world solver；把 ship 直接接到 neutral motion API 属于 P4 的状态所有权工作。
- 玩家运动仍在渲染帧 `dt` 上执行，尚未进入 fixed-step clock。
- 水、重力、环境场和参考系仍使用现有接口，属于 P5/P6。

## 5. 代码索引

- `src/world/actor_motion.[ch]`：neutral motion solver 和 world-side geometry。
- `src/gameplay/player_motion.[ch]`：Player collider 与兼容 facade。
- `src/gameplay/player_world.[ch]`：PlayerWorldOps 到 world solver 的组合边界。
- `src/gameplay/player.c`：玩家控制策略，现只通过 ops 提交中性 body motion。
- `tests/test_player_collision.c`：行为回归和直接 solver 测试。
