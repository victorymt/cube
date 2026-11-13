# 相机重构实施记录（2026-11-13）

- 日期：2026-11-13
- 性质：阶段记录；实现 [相机耦合分析](27-player-camera-coupling-analysis-2026-11-13.md) 的 C0-C4 边界
- 范围：相机目标输入、独立更新阶段、world query 端口、相机距离策略、实体绑定与非回归测试

> 相机之后的玩家运动、环境查询和参考系目标见 [Player、World Simulation 与参考系目标架构](29-player-world-simulation-target-2026-11-13.md)。

## 1. 实施结果

相机姿态不再由 `Player` 的完整结构生成。`gameplay/camera.[ch]` 只消费 `CameraTargetView`，位置、速度、yaw 和 pitch 由 app 组合根从 `ActorBody` 或实体快照投影得到。`player.c` 不再包含任何相机代码，也不再调用方块点查询或遮挡射线。

相机更新已经移出交互阶段。帧顺序现在显式经过 `GameUpdatePlayerMotion -> GameUpdateCamera -> GameUpdateWorldStreaming -> GameUpdateInteractions`；即使 ecology catch-up 跳过交互目标更新，渲染相机仍会更新，frame 的渲染距离也不再退回流式距离。

相机世界查询移入 `CameraWorldOps`。默认适配器位于 `camera_world.c`：方块点占用通过中性的 `WorldPointInsideSolid()` 查询，cube 世界在其中按物理 cell 解码；遮挡优先复用物理 cube raycast hook，legacy 世界保留原 DDA 行为。相机核心因此不知道 `SurfaceTopologyKind`，也不直接访问 cube storage。

`CameraManager` 可以保存多个独立 rig，并分别指定 render rig 和 interaction rig。目标句柄区分 player 与 entity；实体目标同时保存 slot 和 spawn serial，实体死亡、复用或重载后旧句柄会失效，不会被错误地绑定到新生物。多视口绘制、分镜编辑器和 DSL 控制面不在本次范围内；本次提供的是后续实现这些功能所需的稳定相机路由。

## 2. 目标与输出边界

```text
ActorBody / entity snapshot
  -> CameraTargetView
  -> CameraRig(config + target view + world ops)
  -> Camera3D
  -> render rig / interaction rig consumers
```

`CameraRig` 保存自己的 `thirdPerson`、FOV policy、target height 和第三人称距离，因此不同 rig 可以同时使用第一人称与第三人称配置。全局 F4 只同步当前 interaction rig 的 player 视图，不再每帧覆盖所有相机；绑定到实体的渲染 rig 保留自己的模式。

安全实体句柄的格式是 `CameraEntityTarget(index, spawnSerial)`。`spawnSerial` 在实体出生、加载和生态生成路径中分配；相机层不持久化该序号，也不改变 V30 存档格式。

## 3. world query 与碰撞语义

`CameraWorldOps` 暴露两个语义操作：

```c
bool pointInsideSolid(void *user, Vector3 point);
bool raycastOcclusion(void *user, Vector3 origin, Vector3 direction,
                      float maxDistance, float *outDistance);
```

点查询的物理分支把本地 map 坐标投影到 cube body space，再用 `WorldCubeBlockAtPoint()` 读取权威 cell。未知 section 或无法完成投影时按实体处理，避免相机进入尚未加载的地形。legacy 分支保留 partial block 和楼梯高度规则。

遮挡射线在 cube 世界使用 `RaycastBlocksFiltered(..., RAYCAST_BLOCK_SOLID)`，与交互选取共享同一个 cube hook 和命中距离；legacy 世界继续使用原有的 `RaycastCameraOcclusion()`。`HitResult.distance` 现在携带真实入射距离，cube hook 和 Cartesian walker 都必须填充它。

## 4. 距离策略

`camera_distance.[ch]` 提供两个独立入口：

- `CameraRenderDistanceForHeight()` 使用最终渲染相机高度，服务主渲染、阴影和环境表现；
- `CameraStreamingDistanceForHeight()` 使用受控 actor 的眼高，服务 chunk 覆盖与预加载。

两者目前采用相同的 height response，但调用点和生命周期已经分开。debug status 同时输出 `camera_render_distance` 与 `streaming_distance`，因此第三人称拉近、遮挡或实体分镜造成的差异可以直接观察，而不会静默改变流式覆盖。

## 5. 验证

2026-11-13 实施的定向验证包括：

- `test_camera`：第一/第三人称姿态、遮挡端口、碰撞回退、失效目标保持、rig 路由、spawn serial 身份和距离策略入口；测试不链接 `Player`、ship、terrain 或 interaction 实现。
- `test_player_collision`：legacy 相机退出实体、cube 物理点查询和极区 seam 点查询。
- `test_interaction_raycast`：cube hook 透传命中距离，legacy walker 返回真实表面距离。
- `make test-architecture`：相机核心不包含 player/ship/world-environment，交互阶段不调用相机更新。
- `make test-headers`、`make -j4 voxelcraft`：公开头文件和完整程序构建通过。
- `make test`：除既有 `test_world_block_landing` 极区断言外全部通过；该测试的源码闭包不包含本次相机改动。

## 6. 已知边界

- `CameraManager` 已支持多 rig 路由，但渲染器仍只绘制一个 render rig；同步多视口需要渲染层新增 viewport/scissor 循环。
- 当前没有 `camera` debug DSL 命令。后续分镜功能需要在 debug-control 中增加 additive/bind/remove/set-view 命令，并更新 `docs/debug-dsl.md`。
- 相机仍是派生运行时状态，不进入存档。实体绑定依赖 `spawnSerial`，不保证跨进程保持同一句柄。
- 点查询和遮挡查询都由 world adapter 选择 backend；相机核心不知道 legacy/cube 拓扑。

## 7. 代码索引

- `src/gameplay/camera.[ch]`：目标视图、rig 配置、姿态求解、多 rig manager。
- `src/gameplay/camera_world.[ch]`：相机 world query 默认适配器。
- `src/gameplay/camera_distance.[ch]`：render/streaming 距离策略入口。
- `src/world/world_collision.[ch]`：拓扑中性的世界点占用查询。
- `src/app/game_camera.c`：player/entity 到 `CameraTargetView` 的 app 适配和独立相机阶段。
- `src/app/game_interaction.c`：消费 interaction rig，不再更新相机。
- `tests/test_camera.c`、`tests/test_player_collision.c`、`tests/test_interaction_raycast.c`：本次边界和回归覆盖。
