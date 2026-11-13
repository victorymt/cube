# Player 相机逻辑与耦合分析（2026-11-13）

- 日期：2026-11-13
- 性质：静态架构分析；不修改相机行为
- 范围：第一/第三人称相机、相机碰撞与遮挡、FOV、渲染距离、相机对环境和交互的消费链路
- 当前运行状态不在本文重复，见 [STATUS.md](../../STATUS.md)。

> 本文第 2 节至第 6 节记录重构前的基线。C0-C4 的实施边界、验证结果和仍留给后续的
> 多视口渲染工作见 [相机重构实施记录](28-camera-refactor-implementation-2026-11-13.md)。

## 1. 结论摘要

当前相机不是独立系统，而是 `Player`、`ship`、`interaction`、`world`、`presentation` 和 `app` 共同影响的派生视图。相机位置和朝向表面上由 `UpdatePlayerCamera()` 生成，但它的输入、碰撞、FOV 和渲染距离分别来自多个层次。

最深的耦合有三条：

1. **相机仍直接读取 `Player`**：位置、yaw、pitch 都来自 `Player`；ship 状态则通过全局 `ShipIsDriving()` / cruise / transit 查询进入相机。
2. **相机碰撞与遮挡仍走旧的 map-cell / AABB 路径**：`PlayerCameraPositionInsideSolid()` 使用 `GetBlockAt()`、`BlockCollisionHeightAt()` 和楼梯高度；`RaycastCameraOcclusion()` 使用自己的 DDA/AABB 射线，而不是 cube surface contract 或已经安装的 cube raycast hook。
3. **相机更新嵌在交互更新中**：`GameUpdateInteractionTargets()` 先更新相机，再用相机结果做瞄准射线；环境更新随后消费相机位置。相机没有独立更新阶段，也没有独立的相机世界查询端口。

这三条耦合中，前两条影响正确性，第三条影响更新时序和可测试性。FOV、环境表现和渲染距离的耦合则更多是职责边界问题。

## 2. 当前相机链路

### 2.1 姿态来源

相机的最终朝向以 `Player.yaw` 和 `Player.pitch` 为唯一来源。`UpdatePlayerWithWorld()` 在玩家更新阶段处理鼠标/键盘 look delta 并写入这两个字段；船和着陆系统也会直接修改它们。

相机位置则来自 `Player.position`。`UpdatePlayerCamera()` 根据 `WorldIsSpaceActive()` 选择眼高：

```c
float eyeHeight = WorldIsSpaceActive() ? 0.0f : EYE_HEIGHT;
Vector3 eye = Vector3Add(player->position, (Vector3){0, eyeHeight, 0});
Vector3 look = ForwardFromAngles(player->yaw, player->pitch);
```

因此相机不是“由相机状态独立插值”的，而是 Player/ship 运动状态的即时派生结果。

### 2.2 第一人称与第三人称

第一人称路径把 `eye` 同时作为 pivot 和 desired，然后调用 `PlayerResolveCameraPosition(eye, eye)`。如果 eye 在实体方块内，它会沿 eye 到自身的无效路径尝试，再退化为六个轴向候选位置。

第三人称路径以 `look` 的反方向作为期望相机方向：

```c
desired = pivot - look * distance;
occlusion = RaycastCameraOcclusion(pivot, -look, distance);
camera->position = PlayerResolveCameraPosition(pivot, desired);
```

船只驾驶时 pivot 会下移到 `player.position.y + 0.62`，距离为 `5.6`；非船只第三人称距离为 `3.5`。`thirdPerson` 本身由 app 的视图模式切换控制。

### 2.3 FOV

`CameraFovForHeight()` 将绝对 `eye.y` 映射到 `70..90` 度，使用平滑的 height factor。船只高速 transit/cruise 会分别额外增加 `24`/`12` 度，最后通过 `CAMERA_FOV_SMOOTHING` 插值。

这里的“height”是本地坐标的 y，不是相机到当地地面的高度，也不是与天体尺度无关的标准化高度。因此 FOV 和相机所在世界/位置之间存在隐式耦合。

### 2.4 相机碰撞与遮挡

`PlayerCameraPositionInsideSolid()` 只检查一个点：

- 将相机位置向下取整到整数 block 坐标；
- 用 `GetBlockAt()` 获取方块；
- 用 `BlockCollisionHeightAt()` 或楼梯高度判断该点是否落在方块顶部以下；
- 使用 `CAMERA_COLLISION_MARGIN` 避开边界。

它不是身体盒检测，也不是 surface-pose 检测。`PlayerResolveCameraPosition()` 先检查 desired；如果被阻挡，就沿 desired 到 pivot 的 24 个采样点寻找第一个非实体位置，若仍失败，再尝试六个轴对齐候选点。

`RaycastCameraOcclusion()` 是另一套独立的 map-cell DDA：

- 从相机 pivot 向观察反方向逐格前进；
- 每步调用 `GetBlockAt()`、`IsTranslucentBlock()`、`GetBlockBaseAt()` 和 `BlockCollisionHeightAt()`；
- 对非透明方块做轴对齐 AABB 测试。

它没有调用 `RaycastBlocksFiltered()`，也不会自动使用 app 安装的 cube raycast hook。因此相机遮挡和玩家交互射线可能对同一个 cube 表面给出不同的空间/遮挡结果。

### 2.5 渲染距离

`EffectiveRenderDistanceForHeight()` 把同一个 height factor 映射为额外的 `0..2` 个 chunk，并加到 `ChunksRenderDistance()` 上。

实际有两个来源：

- `GameUpdateWorldStreaming()` 使用 `player.position.y + EYE_HEIGHT` 计算 streaming distance；
- `GameUpdateInteractionTargets()` 使用 `game->camera.position.y` 重新计算并返回 `frame.effectiveRenderDistance`，供渲染、阴影和环境 presentation 使用。

第三人称、船只 pivot、眼高变化或相机碰撞都可能让两者不同。流式加载和渲染距离不是同一个数值来源。

## 3. 当前更新时序

每个 `GameUpdateFrame()` 的相关顺序大致是：

```text
LandingTransitionUpdate
  -> GameUpdateViewModes              // F4 切换 thirdPerson，E 退船
  -> GameUpdatePlayerMotion           // yaw/pitch、位置、速度、ship 更新
  -> GameUpdateWorldStreaming         // 用 player eye height 计算 streaming distance
  -> GameAdvanceSimulationFrame
  -> GameUpdateInteractions
       -> UpdatePlayerCamera          // 相机在这里更新
       -> 用相机做 aim ray / entity hit / body pick
  -> GameUpdateFrameEnvironment       // 用相机位置做天空、水下、火灾、音频、光照采样
  -> GameRenderFrame                  // BeginMode3D + 绘制
```

这个顺序意味着相机同时承担“交互输入”和“环境/渲染输入”的角色。`GameUpdateInteractionTargets()` 原本是交互职责，却成为相机唯一更新点；当 ecology catch-up 让交互更新被跳过时，相机也可能保留上一帧状态，而环境仍会消费旧相机。

## 4. 耦合清单

| 耦合对象 | 当前依赖 | 强耦合原因 |
|---|---|---|
| `Player` | position、yaw、pitch | 相机姿态和眼高直接由 Player 字段生成 |
| `Ship` | `ShipIsDriving()`、`ShipIsHighSpeedTransit()`、`ShipIsCruising()` | FOV、第三人称 pivot、距离和绘制模式依赖全局船状态 |
| `world` block API | `GetBlockAt()`、`BlockCollisionHeightAt()`、`GetBlockBaseAt()` | 相机点和遮挡射线使用整数 map block 语义 |
| cube surface | map-coordinate 投影到 cube cell 的 `GetBlockAt()` facade | 相机没有使用 `SurfaceContract`/physical pose/身体盒语义 |
| 楼层/楼梯 | `BlockCollisionHeightAt()`、楼梯顶面 | 相机碰撞只点采样，且和玩家身体碰撞模型不同 |
| app 交互阶段 | `GameUpdateInteractionTargets()` | 相机更新和瞄准/选取被放在同一个函数里 |
| streaming | `EffectiveRenderDistanceForHeight()` | 相机高度影响渲染距离，而 streaming 另用 player eye height |
| environment/presentation | 相机位置和 camera 方向 | 天空、水下、火灾、烟雾、音频、光照都按相机位置采样 |
| capture/debug | `PlayerCameraPositionInsideSolid(game->camera.position)` | 诊断输出直接暴露旧相机碰撞语义 |
| test fixture | `PlayerOverlapsWorld()` 和 `RaycastBlocksFiltered()` 相关 stub | 相机测试没有独立 fake world contract |

## 5. 深耦合等级

### 5.1 高：相机碰撞与 cube/legacy 语义

这是最值得优先处理的耦合。当前相机点和遮挡射线依赖 `GetBlockAt()`，它在 cube 世界内部通过 map 坐标映射到 cube cell。该 facade 能返回方块类型，但不提供相机的 physical body/cone sweep 语义，也不保证与 `SurfaceContractNeighbor()`、`SurfaceCellFromPoint()` 和 `RaycastBlocksFiltered()` 使用相同的边界规则。

风险场景：

- 相机靠近 cube face 边缘时，DDA 在 map 局部坐标里走格，而不是在 cube tangent 平面上走面/邻接 cell；
- 靠近极点时，map 方向和物理方向不是同一个尺度；
- 转身或第三人称相机穿过多面交界时，遮挡结果可能和射线选取结果不一致；
- 楼梯/半砖等部分高度只被相机点/自定义 AABB 使用，交互 raycast 和身体碰撞可能得到不同判断。

### 5.2 高：相机姿态与 Player 生命周期

相机没有自己的 look state 或 view state，而是直接读 `Player`。P2 已经引入 `ActorBody`，但 `UpdatePlayerCamera()` 仍接收 `Player *`，所以相机还没有从“玩家角色实体”解耦到“可被相机消费的 actor 视图”。

这不一定是错误：玩家和相机通常共享 yaw/pitch。但它使相机无法独立测试、回放或在观察者/自由相机模式下复用，也让 ship 的 yaw/pitch 修改和玩家走路状态共享同一组字段。

### 5.3 中高：相机更新在交互函数中

相机更新发生在 `GameUpdateInteractionTargets()`，而该函数的返回值还用于交互结果和渲染距离。交互模块因此承担了视图更新职责。

直接后果：

- 交互被暂停或跳过时，相机也可能不更新；
- 要测试相机，必须通过交互模块或其 stub；
- 相机必须早于环境更新，但更新位置不在帧阶段的相机步骤里；
- 交互瞄准方向与相机方向共享同一帧，但调度关系没有显式写成依赖。

### 5.4 中：FOV、眼高和渲染距离共用 height policy

`CameraHeightFactor()` 同时驱动：

- FOV；
- 第一/第三人称 camera targeting；
- effective render distance extra chunks。

这三件事的生命周期不同。相机 FOV 是视图策略，渲染距离是流式/性能策略，眼睛高度是角色/世界尺度策略。把它们绑在同一个 height factor 上，会让性能或世界高度变化意外改变 FOV 和流式预算。

### 5.5 中：环境与音频读取相机位置

`GameUpdateFrameEnvironment()` 使用相机位置计算天空、 atmosphere fade、水下判定、火灾/烟雾暴露、环境音频、光照和视图距离。对渲染环境来说这是合理的，但其中一些数据已经接近“环境采样”甚至“暴露度量”。如果未来存在独立观察者或回放相机，这些逻辑需要明确区分“模拟主体的位置”和“观察者的位置”。

### 5.6 低到中：调试和截图读取相机碰撞

`GameCaptureScreenshot()` 和 `GameDebugReplyStatus()` 直接调用 `PlayerCameraPositionInsideSolid(game->camera.position)`，截图报告还输出 camera position/target/FOV/third-person/inside-solid。

这不会改变游戏逻辑，但会把相机碰撞的旧语义固定进调试输出和回归断言。迁移相机查询时，必须同步更新这些诊断字段。

## 6. 必要依赖与额外耦合

### 6.1 必要依赖

相机确实需要：

- 一个 actor/body 的位置和朝向；
- 世界实体查询（点实体、射线遮挡）；
- 是否驾驶船、第三人称模式、眼高；
- 画面宽高比/FOV 策略；
- 可选的环境表现输入。

这些依赖不应被消除，只应被收窄为有明确语义的接口。

### 6.2 可去除的额外耦合

以下依赖不是相机语义本身要求的：

- 相机直接读 `Player` 的完整运行时结构；
- 相机构建在 `GameUpdateInteractionTargets()` 中；
- 相机遮挡直接使用 `GetBlockAt()`/AABB DDA；
- 相机高度因子同时决定 FOV 和 streaming/render distance；
- 相机通过全局 `ShipIsDriving()` 查询船状态；
- 相机点碰撞和玩家 body collision 使用两套边界算法；
- 诊断字段硬绑定旧的相机点碰撞函数。

## 7. 建议的目标边界

### 7.1 相机控制器

建议把相机拆成：

```text
CameraViewState      // thirdPerson, FOV target, desired distance
CameraPose           // position, target, up, fov
CameraWorldOps       // pointSolid(), raycastOcclusion(), maybe eyeHeight()
CameraUpdate(input actor pose, view state, world ops) -> camera pose
```

`CameraUpdate()` 应接收中性 `ActorBody` 或只读 `ActorPoseView`，而不是 `Player *`。P2 引入的 `ActorBody` 已经可以作为第一版输入类型。

### 7.2 world query 接口

`CameraWorldOps` 至少应暴露两个语义操作：

```c
bool (*pointInsideSolid)(void *user, Vector3 point);
float (*raycastOcclusion)(void *user, Vector3 origin,
                          Vector3 direction, float maxDistance);
```

默认实现应在内部选择 legacy 或 cube backend，但不应暴露 `SurfaceTopologyKind`。cube 分支应优先使用 `SurfaceContract` 和 cube raycast hook，而不是在相机代码里直接遍历 map cell 或复制一套 AABB 逻辑。

### 7.3 FOV 与渲染距离分离

建议拆成两个不同策略：

- `CameraFovPolicy`: 只负责相机 FOV；
- `ViewDistancePolicy`: 负责渲染/阴影距离；
- `StreamingDistancePolicy`: 负责 chunk 请求距离，尽量由稳定的 actor/body 高度计算，而不是相机遮挡后的位置。

在统一之前，至少要明确 streaming distance 和 render distance 是允许不同的，并在报告/调试中同时显示两者来源。

### 7.4 独立更新阶段

建议在 app 中增加显式的相机更新阶段，位置在玩家运动之后、交互瞄准之前：

```text
GameUpdatePlayerMotion
  -> GameUpdateCamera
  -> GameUpdateWorldStreaming
  -> GameUpdateInteractions / Environment / Render
```

或者保持流式顺序，但把相机更新从交互函数中抽出，交互只消费已经更新的相机。这样即使交互被暂停，相机也不会因为交互提前返回而保持旧状态。

## 8. 分阶段实施建议

### C0：相机输入与输出类型拆分

- `UpdatePlayerCamera` 从 `const Player *` 改为接收 `const ActorBody *` 或 `CameraActorView`；
- 抽出 `CameraViewState`（third person、FOV、目标距离）；
- 让 `thirdPerson` 和 ship FOV 状态显式进入相机参数。

### C1：独立相机更新阶段

- 把相机更新从 `GameUpdateInteractionTargets()` 移到 app 的显式 camera 更新步骤；
- 交互只消费 `game->camera`；
- 增加一个交互被暂停时相机仍更新的测试。

### C2：相机 world query 端口

- 添加 `CameraWorldOps.pointInsideSolid()` 和 `raycastOcclusion()`；
- 默认 adapter 分别服务 legacy/cube，保留现有行为；
- cube 实现使用 `SurfaceContract`/cube raycast hook，不再直接依赖 `GetBlockAt()` facade；
- 用 fake camera backend 分别测试第一/第三人称和遮挡。

### C3：距离策略拆分

- 拆分 `CameraFovForHeight()`、render distance 和 streaming distance；
- 判定第三人称、船只和相机碰撞是否应该影响 streaming；
- 在 debug trace 中同时记录 player-eye、camera 和 render-distance 来源。

### C4：回归覆盖

- cube face edge、pole、跨面相机遮挡；
- 第一人称 eye 位于实体内的恢复；
- 第三人称遮挡与交互射线一致性；
- ship transit/cruise FOV；
- `game_capture`/debug status 的 camera inside-solid 语义；
- `test_player_collision`、`test_interaction_raycast`、`test_game_debug_diagnostics`，以及必要时的 Hyprland 相机回放。

## 9. 验收标准

相机边界迁移完成的判断标准应是：

- `player.c` 不直接调用 `GetBlockAt()`、`BlockCollisionHeightAt()` 或 `RaycastCameraOcclusion()` 做相机查询；
- 相机更新不再由交互函数触发；
- `UpdatePlayerCamera` 不接收完整 `Player`；
- cube 和 legacy 共用同一相机 world query contract；
- streaming distance 与 render distance 的来源明确且可诊断；
- 相机与交互射线在 face edge/pole 上的行为有专门测试；
- V30 存档格式不变，相机仍然只是派生状态而不是新增持久化状态。

## 10. 总结

当前相机逻辑可以概括为：

```text
Player/ship pose
  -> eye/pivot/look
  -> height-based FOV
  -> map-cell point collision
  -> map-cell AABB occlusion DDA
  -> third-person pull-in
  -> environment/render/aim consumers
```

最深的耦合不是单一函数，而是“相机读取 Player + 相机使用旧 block DDA + 相机更新藏在交互阶段 + FOV 和渲染距离共用 height policy”这四条链一起形成的。

优先处理顺序建议是：

1. 把相机从 `Player` 改为 `ActorBody`/camera view；
2. 把相机更新从交互中独立出来；
3. 为相机碰撞/遮挡建立 world query 端口；
4. 再拆分 FOV、render distance 和 streaming distance。

## 11. 核查路径

```sh
rg -n 'UpdatePlayerCamera|PlayerCameraPositionInsideSolid|PlayerResolveCameraPosition' src tests
rg -n 'RaycastCameraOcclusion|RaycastBlocksFiltered|InteractionSetCubeRaycastHook' src/gameplay src/app
rg -n 'EffectiveRenderDistanceForHeight|CameraFovForHeight|CameraHeightFactor' src tests
rg -n 'game->camera|frame->effectiveRenderDistance' src/app src/presentation
make test-one TEST=test_player_collision
make test-one TEST=test_interaction_raycast
make test-architecture
```

## 12. 相关文档

- [Player 与世界拓扑类别解耦分析](26-player-world-topology-decoupling-analysis-2026-11-12.md)
- [Architecture overview](../architecture-overview.md)
- [Cube-grid 激活契约](../cube-contract.md)
- [Current status](../../STATUS.md)
