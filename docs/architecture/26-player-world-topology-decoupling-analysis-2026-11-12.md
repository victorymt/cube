# Player 与世界拓扑类别解耦分析（2026-11-12）

- 日期：2026-11-12
- 性质：静态架构分析、可行性与迁移建议；不是实现记录
- 范围说明：本文把“信息类别”按当前讨论上下文解释为“世界表示/拓扑类别”，包括 `cube-grid`、`legacy-map`、`space`、`nether`，同时分析和 Player 混在同一个类型/文件中的输入、传感器、相机和交互信息类型。
- 当前运行状态不在本文重复，见 [STATUS.md](../../STATUS.md)。

> 后续的整合目标边界和迁移路线见 [Player、World Simulation 与参考系目标架构](29-player-world-simulation-target-2026-11-13.md)。

## 1. 结论摘要

结论是：**可以实现，而且值得做，但不应理解成“让 Player 完全不认识世界”**。Player 必须依赖碰撞、地面、水体和重力这些世界语义；真正应该去掉的是 Player 对“由哪个 backend 表示世界”以及“哪一套 CPU 碰撞算法”的具体知识。

推荐目标不是把 Player 搬进 `core`，也不是立刻重写成 ECS，而是把现有 `gameplay/player.c` 拆成三部分：长期角色状态、每帧命令/传感器结果、以及一个窄的 world motion/environment 端口。端口由 `app` 组合，具体 backend 分支只留在 adapter 内。

本文判断的三个优先级如下。

| 优先级 | 工作 | 判断 |
|---|---|---|
| P0 | 拆开 `Player`、`PlayerInput`、`PlayerWaterState`、`HitResult`、相机和运动查询这些不同生命周期的信息 | 低风险，应最先做 |
| P1 | 把 `MovePlayer`、`PlayerOverlapsWorld`、pose 转换、水位/重力/区块就绪查询从 Player 政策中抽出 | 这是真正消除 cube/legacy 耦合的核心 |
| P2 | 让 ship、ecology、space 不再共享 `Player*` 作为可变 actor 状态 | 更有利于长期边界，但不是第一阶段的前提 |

不建议一开始做完整 ECS、把所有状态放进一个通用 `World` 接口，或把运行时的 map 坐标一次性改成 `SurfacePose`。这些改动会同时触碰存档、渲染、交互、生态和调试，风险远高于收益。

## 2. 当前结构

### 2.1 Player 类型本身不是主要问题

`Player` 当前只有位置、速度、视角、接地/飞行标志、水中音效标志和脚步计时器。它没有直接保存 `SurfaceTopologyKind`，也没有保存 chunk key，因此“状态结构”本身已经接近拓扑中立。

真正的问题是围绕它的函数边界：`player.c` 同时拥有 Player 政策、世界碰撞、流体采样、相机碰撞、着陆查询和 surface pose 转换。结果是任何世界表示变化都会传播到 Player，而不是只影响一个世界 adapter。

### 2.2 当前调用链

```text
GameReadPlayerInput()
  -> PlayerInputFromKeyboard()                 // 直接读取 raylib
  -> UpdatePlayerWithInput()
       -> PlayerWaterStateAt()
       -> WorldIsSpaceActive() / WorldGravityScale()
       -> PlanetSurfaceAt()
       -> MovePlayer()
            -> cube: PlayerCubeDisplace()
            -> legacy/generic: MovePlayerStep()
```

另外还有几条独立但相关的链路：

```text
GameSaveWritePlayerState()
  -> WorldSurfaceContext / SurfacePoseFromGeographic
  -> SurfacePoseBasis

UpdatePlayerCamera()
  -> PlayerResolveCameraPosition()
  -> PlayerCameraPositionInsideSolid()
  -> GetBlockAt() / BlockCollisionHeightAt()

ShipUpdate*()
  -> 直接读写 Player 的 position/velocity/yaw/onGround/floating
  -> ShipMoveForFrame()
       -> MovePlayer()
```

这些链路说明，Player 目前不只是“玩家角色的状态”，还是飞船移动状态、生态目标位置来源、相机碰撞查询入口和存档编解码的中间对象。

### 2.3 当前混在一起的信息类别

| 信息类别 | 当前定义/位置 | 生命周期 | 合适的所有者 |
|---|---|---|---|
| 角色长期状态 | `Player`，`gameplay/player_types.h` | 跨帧、跨存档 | gameplay |
| 输入命令 | `PlayerInput`，`gameplay/player.h` | 每帧 | app 输入采集 + gameplay 命令 |
| 水体传感器结果 | `PlayerWaterState`，`gameplay/player.h` | 派生值 | gameplay/world sensor value |
| 交互结果 | `HitResult`，`gameplay/player_types.h` | 单次查询 | gameplay/interaction |
| 世界连续位姿 | `SurfacePose`，`world/surface_pose.h` | 存档/世界边界 | world |
| 相机状态/查询 | `UpdatePlayerCamera` 等，`gameplay/player.c` | 每帧视图 | gameplay 相机政策 + world 查询 |
| 区块就绪信息 | `PlayerMissingSurfaceChunkCount` 等 | 每帧诊断/流式 | world streaming |

`player_types.h` 把 `Player` 和 `HitResult` 放在一起，`player.h` 又把输入、传感器、碰撞、着陆、相机和 pose 转换放在一起。这是最直接的类型级别耦合，应该在动运动逻辑之前先拆掉。

## 3. 方块路径与旧地图路径的实际差异

`MovePlayer()` 的分叉条件不是 `SurfaceTopologyKind == CUBE_GRID`，而是 `WorldIsSurfaceActive() && SurfaceContractSupportsPhysicalPose(...)`。这比直接读 enum 好，但仍把两条运动实现放在同一个函数里。

| 维度 | cube physical path | legacy/generic path |
|---|---|---|
| 位置语义 | 本地 `x/z` 经世界转换成为球面/立方体 pose，按物理块位移 | 直接把 `x/y/z` 当作笛卡尔方块坐标 |
| 水平移动 | `SurfacePoseDisplace()`，按局部 right/forward 做圆弧位移 | `position.x += delta.x`、`position.z += delta.z` |
| 碰撞对象 | `WorldBlockBoxCollect()` 枚举身体盒覆盖的 cube cells，再由 `SurfaceContractCellRead()` 读取 | 整数 AABB 枚举，再查 `GetBlockAt()` |
| 方块形状 | 主要按 block catalog 是否具有碰撞高度判断整格阻挡 | 使用 `BlockCollisionHeightAt()` 和楼梯 `StairsCollisionTop()` 的部分高度 |
| 分步顺序 | 每个 substep 先 Y，再 X，再 Z | 每个 substep 先 X，再 Z，最后 Y |
| 台阶 | 没有调用 `TryStepUp()` | 落地且非飞行时调用 `TryStepUp()` |
| 大位移 | 保留完整位移，最多 64 个 substep | 超过 `0.25 * 64` 时直接缩短位移 |
| 未加载区块 | 通过 contract cell 查询判断身体涉及的 cube cell 是否已知 | `SurfaceBlockReadyAt()` / `SpaceBlockReadyAt()` |
| 接地标志 | 向下移动前清除，向下被阻挡时恢复 | 每个 substep 先清除，最后 Y 轴被阻挡且向下时恢复 |

这些差异不应被一个“看起来统一”的 `MovePlayer()` 接口掩盖。统一接口之前必须先明确哪些差异是语义要求，哪些只是尚未完成的实现：

- cube 是否应该支持和 legacy 相同的半格台阶？
- cube 对楼梯、半砖、植被的碰撞形状是否应该与 legacy 一致？
- 超长位移是应该保留完整距离，还是统一截断？
- 跨 cube face 或在极点时，velocity 和 yaw 的旋转应如何定义？
- 未知区块在步行和飞船状态下是否应继续保持不同的通行策略？

如果这些问题没有答案，抽象层只会把不一致藏起来，测试仍然会在不同 backend 上得到不同结果。

## 4. 具体耦合清单

### 4.1 Player 直接持有的世界/空间依赖

`src/gameplay/player.c` 当前直接包含并使用：

- `world/block_catalog.h`：方块碰撞属性
- `world/chunks.h`：旧 chunk 坐标与就绪状态
- `world/world_location.h`：`WorldBlockAddress`、身体盒
- `world/fluid.h`：水体体积和流速
- `world/block_edits.h`、`world/block_types.h`：方块读写
- `world/surface_contract.h`：cube/legacy 能力查询
- `world/world_environment.h`：空间、表面、重力、min/max Y
- `world/world_surface_snapshot.h`：当前 surface context
- `space/space_chunks.h`、`space/space_query.h`：空间重力与空间区块就绪
- `gameplay/ship.h`：碰撞时读取 `ShipIsDriving()`，并让飞船调用 `MovePlayer()`

这不是“一个函数里的偶然 include”，而是 Player 同时承担了世界查询、流体查询和飞船碰撞入口。

### 4.2 被外部模块读取/修改的 Player

`Player` 还被以下模块直接使用：

- `space/space_chunks.c`：`SpaceRebasePlayer()` 直接修改 `Player.position`
- `space/space_state.c`：根据 `Player.position` 恢复 Home 状态
- `ecology/entity_simulation.c`：读取 `Player.position`、`velocity`
- `gameplay/ship.c`：把 `Player` 当作飞船 body，直接写位置、速度、yaw、pitch、floating、onGround
- `app/game_save.c`：把 Player 本地状态转换为 `SurfacePose`
- `app/game_debug_*`、`presentation/homeworld_map.c`、`presentation/starmap.c`：读取 Player 位置/视角做调试和显示

因此，“Player 解耦”不能只看 `player.c`。如果只是把 `MovePlayer` 包一层，ship、space 和 ecology 仍然会把 `Player` 当作通用 actor state。

### 4.3 输入边界也没有解耦

`PlayerInputFromKeyboard()` 在 gameplay 内直接调用 `GetMouseDelta()`、`IsKeyDown()` 和 `IsKeyPressed()`。仓库已经有 `InputSource`/`InputFrame` 的单一采集边界，因此这是一个独立的边界问题。

建议把物理输入映射移回 `app/input_source` 或 `app/game_player_input`，gameplay 只接收已经封好的 `PlayerInput`。这样 Player 既不轮询 raylib，也能在 DSL、手动输入和回放之间保持同一条命令路径。

## 5. 可行性分析

### 5.1 可以实现的部分

以下目标在现有结构上可实现，且不需要改变存档格式：

1. 让 `player.c` 不再出现 cube/legacy 的碰撞分支，只调用一个 `WorldActorMotion` 或 `PlayerWorldOps` 接口。
2. 把 `PlayerOverlapsWorld()`、`PlayerMissingSurfaceChunkCount()`、pose 转换和着陆查询放到 world adapter，不放在 Player 政策中。
3. 把水体、重力、空间状态等每帧查询封装成 sensor/environment 端口。
4. 把 `HitResult`、`PlayerInput`、`PlayerWaterState` 和相机信息拆成独立类型/头文件。
5. 在测试中注入 fake motion backend，不再依赖全局 world stub。

这些目标完成后，Player 仍然知道“我需要一个运动查询”，但不知道当前是 cube 还是 legacy。

### 5.2 不能或不应承诺的部分

1. **不能消除 Player 对世界的所有依赖**。碰撞、地面、水体和重力本来就是 Player 的输入；正确的目标是把依赖收窄为语义接口。
2. **不建议立刻把运行时位置改成 `SurfacePose`**。当前运行时 `position.x/z` 仍然是 map/local 坐标编码，存档已经有 canonical `SurfacePose`。如果直接改运行时表示，要同时改 save、debug、map、render、interaction、ecology 和 ship，应该作为独立阶段。
3. **不建议把 Player 放进 `core`**。`core` 的架构约束是不依赖领域类型；Player 是 gameplay 领域状态。应保持 gameplay 所有权。
4. **不建议为了抽象而抽象成完整 ECS**。当前只有一个玩家 actor，且碰撞和存档语义强；完整 ECS 会增加同步、组件查询和调试成本，不能直接解决 backend 分支问题。

### 5.3 可行性评级

| 目标 | 可行性 | 主要成本 | 建议 |
|---|---|---|---|
| 类型/头文件拆分 | 高 | 机械改 include，更新测试 | 先做 |
| 将 `MovePlayer` 后端隔离到一个 adapter | 高 | 需要定义运动语义和错误回退 | P1 核心 |
| 抽出水体/重力/space sensor | 高 | 需要统一 sensor 失败语义 | P1/P2 |
| 抽出相机碰撞查询 | 中高 | 相机可能需要 world ray/solid query | P2 |
| ship/ecology 使用中性 actor state | 中 | 调用面广，测试需要更新 | P2 |
| 运行时改用统一 `WorldPose` | 中低 | 存档、渲染、交互、调试同时变化 | 延后 |
| 完整 ECS 化 | 低收益/高风险 | 大范围重写 | 不建议 |

## 6. 建议的目标结构

### 6.1 推荐方案：窄端口 + 单一 world adapter

第一阶段可以保留 `Player` 结构不变，只改变函数归属。目标调用关系如下：

```text
PlayerInput (from InputFrame)
        |
        v
PlayerUpdate(PlayerState *, const PlayerInput *, const PlayerWorldOps *, dt)
        |
        +--> wish/velocity/jump/water policy (gameplay)
        |
        +--> PlayerWorldOps.move() / sample_water() / sample_gravity()
                         |
                         v
              world actor motion / sensors
                         |
             +-----------+-----------+
             |                       |
        cube adapter             legacy/space adapter
```

`PlayerWorldOps` 应当是 gameplay 能理解的最小语义接口，不暴露 `SurfaceContract`、`SurfaceTopologyKind`、`WorldDimension`、chunk key 或 legacy map 地址。建议至少包含：

```c
typedef struct PlayerMoveRequest {
    Vector3 delta;
    bool canStepUp;
    bool allowUnready;
} PlayerMoveRequest;

typedef struct PlayerWorldOps {
    void *user;
    bool (*move)(void *user, PlayerBodyState *body,
                 const PlayerMoveRequest *request);
    bool (*overlaps)(void *user, Vector3 position);
    bool (*sample_water)(void *user, Vector3 position,
                         PlayerWaterSample *out);
    bool (*sample_gravity)(void *user, Vector3 position,
                           PlayerGravitySample *out);
    bool (*canonicalize)(void *user, PlayerBodyState *body, float *yaw);
} PlayerWorldOps;
```

如果希望 world 侧拥有真正的碰撞实现，可以把 `move` 和 `overlaps` 的实现放到 `world/actor_motion.c`，由 `app` 或 gameplay 的 adapter 绑定；如果希望先降低迁移风险，则让 adapter 暂时留在 gameplay 的一个单独文件中，只把 `player.c` 变成纯政策代码。

### 6.2 推荐的类型拆分

建议按生命周期而不是按“当前都在 player.c”来拆：

- `gameplay/player_state.h`：`Player` 长期状态；只依赖通用的 `Vector3` 和基础 bool/float。
- `gameplay/player_input.h`：`PlayerInput` 命令；由 app 输入层生成。
- `gameplay/player_sensors.h`：`PlayerWaterState`、重力采样等派生结果。
- `gameplay/interaction_types.h`：`HitResult`，从 `player_types.h` 移出。
- `gameplay/player_motion.h`：`PlayerBodyState`、`PlayerMoveRequest`、motion port。
- `gameplay/player_world_ops.h` 或 `world/actor_motion.h`：world 侧窄接口。
- `gameplay/player_camera.c`：相机政策；world 碰撞通过 query 端口访问。
- `gameplay/player.c`：只负责输入到速度、跳跃/重力政策、调用 port 和发效果。

不建议把 `PlayerWaterStateAt()` 作为 `Player` 的成员函数长期保留。它是由位置查询得到的传感器结果，应该由一个明确的 environment/sensor 查询返回；现有大量 HUD、调试、天气和生态调用点可以先保留一个薄 wrapper，再逐步迁移。

### 6.3 数据所有权建议

- `PlayerBodyState` 只包含 position、velocity、onGround、floating 这类运动状态。
- `Player` 可以组合 `PlayerBodyState`、yaw/pitch、运行时效果计时器和水中状态。
- `SurfacePose` 只用于 world/save/debug 边界，不进入 `Player` 的公开 API。
- `HitResult` 归 interaction，不归 Player。
- `PlayerInput` 归输入命令，不归长期状态。
- `PlayerWaterState` 归派生传感器结果，不归长期玩家状态。
- 飞船应有自己的 actor/body 状态，或者使用中性的 `ActorBody`；不要让 `ShipUpdate()` 继续读写 `Player*` 作为长期接口。

## 7. 迁移计划

### 阶段 A：类型和头文件拆分（无行为变化）

工作：

- 将 `HitResult` 从 `player_types.h` 移到 interaction 类型头。
- 将 `PlayerInput`、`PlayerWaterState`、相机函数从 `player.h` 分开。
- 让 `player_types.h` 不再因 `HitResult` 引入 `world/world_location.h`。
- 将 `PlayerInputFromKeyboard()` 的物理采样迁移到 app 输入层，Player 只接收命令。
- 更新 include、测试和 `tests/test_architecture.sh` 的边界规则。

退出条件：

- `Player` 类型头只描述长期状态。
- 输入、传感器、交互和相机类型各自有单一所有者。
- `test_game_player_input`、`test_player_collision`、`make test-headers` 通过；如果头文件/build graph 改变，按仓库规则运行 `make test`。

### 阶段 B：抽取 world motion port

工作：

- 定义 `PlayerBodyState`、`PlayerMoveRequest` 和 `PlayerWorldOps`。
- 将 `MovePlayer()`、`MovePlayerStep()`、cube 分支和 legacy 分支移到 adapter。
- 保留现有两套行为作为实现，先不改变游戏手感。
- 将 `allowUnready`（飞船/未知区块策略）和 `canStepUp` 作为显式请求参数，不再让 adapter 直接调用 `ShipIsDriving()`。
- 加 fake backend 测试，验证 Player 政策不依赖具体 world。

退出条件：

- `player.c` 不包含 `SurfaceContractSupportsPhysicalPose`、`WorldToChunkLocal`、cube chunk API 或 legacy chunk lookup。
- cube 和 legacy 的现有碰撞测试继续通过。
- 至少有一个 fake backend 的 `UpdatePlayerWithInput` 单元测试。

### 阶段 C：抽取 sensor/environment 查询

工作：

- 将水体和流速查询封装为 `sample_water()`。
- 将太空重力、行星近地重力、`WorldGravityScale()` 封装为 `sample_gravity()`。
- 将 `WorldCanonicalizeSurfacePose()` 的跨极点/坐标归一化封装为 `canonicalize()` 或明确的 post-step 操作。
- 将 `PlayerFindLandingSpot()` 和 `PlayerMissingSurfaceChunkCount()` 迁到 world streaming/spawn 查询。

退出条件：

- `UpdatePlayerWithInput` 不直接调用 `GetBlockAt`、`FluidSampleAt`、`FluidGetVolumeAt`、`WorldIsSpaceActive`、`WorldGravityScale` 或 `PlanetSurfaceAt`。
- 水面、游泳、太空和极点重定位测试通过。

### 阶段 D：统一运动语义

这是最容易把 bug 藏起来的阶段，不能只做接口搬家。需要明确并测试：

- 台阶高度和 `canStepUp` 行为；
- 半砖/楼梯/植被的碰撞形状；
- 最大位移和 substep 截断策略；
- 未知区块在步行和飞船下的处理；
- face 边界、极点和跨面 velocity/yaw 旋转；
- `onGround` 状态转换。

如果短时间不能统一语义，接口应显式提供 capability（例如 `supportsStepUp`），并让 Player 只根据语义能力处理，而不是根据 topology 名称分支。

### 阶段 E：ship/ecology/space 解耦

工作：

- 引入中性 `ActorBody` 或 `ActorPoseView`。
- 让飞船使用自己的状态或 `ActorBody*`，不再把 `Player` 作为飞船长期类型。
- 让 ecology 只读 actor position/velocity 的视图。
- 让 space rebase 只操作 actor position 或明确的 world-origin adapter。
- 从 `space/space_dependencies.h` 移除不必要的 `gameplay/player_types.h`，缩小 value-type cycle。

这一阶段不一定是 Player backend 解耦的前置条件，但它能解决 `Player` 被多个领域当作共享可变结构的问题。

## 8. 测试策略

第一阶段应优先补的是“边界测试”，不是 `make test`。

| 测试 | 目的 |
|---|---|
| `test_player_collision` | cube/legacy 两套移动行为不能回退 |
| `test_world_block_box` | 身体盒和 surface cell 的碰撞契约 |
| `test_world_block_landing` | landing/spawn 查询一致性 |
| `test_surface_pose` | pose、位移、极点/面边界基础语义 |
| `test_game_player_input` | 物理、手动、脚本输入优先级不变化 |
| 新增 fake motion backend 测试 | Player 政策可在没有真实 world 的情况下验证 |
| `test_ship_state` | 飞船迁移到 actor state 后状态不丢 |
| `test_game_save_spherical` | V30 `SurfacePose` 读写不变 |
| `make test-architecture` | 阻止 gameplay 直接重新引用 backend chunk API |
| `make test-headers` | 新拆分头文件可独立编译 |

如果只改文档，不运行 C 测试；只运行 `make check-docs`。实现阶段的测试选择应按仓库规则分层：局部改动用 `make test-one TEST=<name>`，公共头/构建图/存档契约用 `make test`，合并前再考虑更完整的门禁。

## 9. 风险与注意事项

### 9.1 坐标与状态双源

当前 `Player.position/velocity` 是运行时 local 编码，`SurfacePose` 是保存/世界边界的 canonical 表示。迁移时不要让两者同时成为可变权威状态；否则会出现位置、速度、yaw 和 bodyId 不同步的问题。

### 9.2 旋转和极点

`SurfacePoseDisplace()` 会旋转 velocity 和 heading；`WorldCanonicalizeSurfacePose()` 会在跨极点/经度反射时翻转 `velocity.z` 和 `yaw`。运动 adapter 必须保留这些语义，并覆盖跨面、极点和高纬移动。

### 9.3 水上/水下状态

Player 在移动前后都会采样水体。把 `PlayerWaterStateAt` 移出 Player 后，要保证同一个 tick 的水体采样顺序、procedural water fallback 和 flow velocity 不被改变。

### 9.4 相机碰撞

`PlayerCameraPositionInsideSolid()` 目前使用整数 `GetBlockAt()` 和旧式碰撞高度逻辑。`GetBlockAt()` 在 cube 世界内部会把 map 坐标映射到 cube cell，但相机查询仍然不是玩家的 physical body-box 语义；如果只解耦 Player 移动而不处理相机，仍然会保留一条 map-coordinate facade 依赖。

### 9.5 飞船碰撞

`ShipMoveForFrame()` 当前调用 `MovePlayer()` 做逐段碰撞，并且 `PlayerOverlapsWorld()` 在未知区块下通过 `ShipIsDriving()` 改变通行策略。新 motion port 必须把“是否允许未加载区块”作为请求能力或 ship 状态显式传入，不能靠 gameplay 全局状态隐式决定。

### 9.6 头文件重建半径

仓库已经因为宽头文件付出过重建成本。新增接口应尽量使用不透明句柄或前置声明，避免 `player.h` 重新引入 `surface_pose.h`、`world_location.h`、`chunks.h` 和 `space/` 头。

### 9.7 调试和 DSL

`PlayerWaterStateAt`、player position、camera 和 save/debug 字段被大量 DSL、截图和状态信息使用。拆分时优先保留原有运行时字段名和查询语义；如果必须改 DSL，按仓库规则先扩展并更新 `docs/debug-dsl.md`。

### 9.8 不要用弱符号或全局回调兜底

仓库的 `test_architecture.sh` 禁止生产代码使用 weak symbols。world/space/gameplay 之间的绑定应由 `app` 组合，或由明确的 adapter 对象完成；不要重新引入全局可变注册表。

## 10. 必要性与最终建议

### 10.1 必要性

如果目标只是维持当前 Home cube + planet legacy 的行为，那么 Player 现在的两条路径“能工作”，完整解耦不是立即修复某个断点的必要条件。

如果目标是后续把行星也迁移到 cube-grid、增加新的碰撞 backend、让飞船/生态共享 actor 状态，或者让 Player 的单元测试不依赖真实 world stub，那么解耦是有必要的。现有 `MovePlayer()` 已经把同一个入口分成两套坐标、两套碰撞、两种台阶和两种大位移策略；每新增一个移动相关修复，都可能要证明两遍。

### 10.2 推荐决策

推荐采用**分阶段的行为解耦**，不采用一次性大重写：

1. 先拆类型和输入边界，保持行为不变。
2. 再抽出 `WorldActorMotion`/`PlayerWorldOps`，把 cube/legacy 分支移出 `player.c`。
3. 再迁移水体、重力、landing、区块就绪和相机查询。
4. 最后按需把 ship/ecology/space 改成中性 actor/body 视图。

第一阶段的验收标准应当是：`player.c` 不再直接知道 cube/legacy backend，`player.h` 不再把输入、传感器、交互和相机混成一个公共 API；而不是代码行数减少或引入一个名字更抽象的大接口。

## 11. 核查方式

本文依据的是当前源码的静态阅读，不需要 GPU 或运行时截图。以下命令可用于复核：

```sh
rg -n 'SurfaceContractSupportsPhysicalPose|WorldToChunkLocal|SurfaceBlockReadyAt|SpaceBlockReadyAt|GetBlockAt|FluidSampleAt|WorldGravityScale|PlanetSurfaceAt' src/gameplay/player.c
rg -n 'typedef struct (Player|HitResult)|PlayerInput|PlayerWaterState' src/gameplay/player_types.h src/gameplay/player.h
python3 scripts/coupling-report.py
sed -n '1,120p' build/normal/obj/gameplay/player.d
make test-architecture
```

实现后的额外门禁：

- `player.c` 只通过窄 motion/environment port 访问 world/space；
- `world` 公共 API 不反向依赖 `gameplay/player_types.h`；
- `gameplay` 不直接出现 backend 的 cube chunk key、legacy chunk lookup 或 `SurfaceTopologyKind` 路由；
- cube 和 legacy 的运动一致性测试、V30 pose roundtrip 测试和 ship/space 回归测试通过。

## 12. 相关文档

- [Cube-grid 激活契约](../cube-contract.md)
- [Homeworld cube-grid migration execution plan](../cube-migration-execution-plan.md)
- [ADR-005: cube-grid 运行时门控](../decisions/ADR-005-cube-grid-gated-topology.md)
- [Architecture overview](../architecture-overview.md)
- [World type decoupling record](24-world-type-decoupling-2026-10-25.md)
