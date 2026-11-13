# Player、World Simulation 与参考系目标架构（2026-11-13）

- 日期：2026-11-13
- 性质：目标架构与渐进迁移记录；本文不表示代码已经全部实现
- 当前状态：见 [STATUS.md](../../STATUS.md)
- 前置记录：[Player/world topology decoupling](26-player-world-topology-decoupling-analysis-2026-11-12.md)、[Player camera coupling](27-player-camera-coupling-analysis-2026-11-13.md)、[Camera refactor implementation](28-camera-refactor-implementation-2026-11-13.md)

## 1. 结论

目标不是把 `Player` 搬进 `core`，也不以引入完整 ECS 为目标。目标是建立四条稳定边界：玩家控制器表达意图，世界几何求解器处理运动与碰撞，环境接口按位置和时间提供场，模拟调度器按确定顺序提交状态。

连续物理位置、离散方块地址和渲染相对坐标承担不同职责。统一的是位置和时间的语义、查询接口、运动结果与系统职责，不是强制所有空间使用同一个坐标系。

近期不重写存档、相机或实体系统。第一优先级是让 `Player` 不再拥有 legacy/cube 几何实现，并让所有 locomotion 状态写入经过同一个运动提交入口。

## 2. 当前基线与真实差距

当前 `Player` 只保存 `position`、`velocity`、`yaw`、`pitch`、`onGround`、`floating`、`wasInWater` 和 `stepTimer`。输入采集已位于 app，`PlayerInput` 是 gameplay 可消费的命令值，相机已由 `CameraManager` 独立管理。

`UpdatePlayerWithWorld()` 负责视角、速度、游泳、重力、跳跃和音效策略，并通过 `PlayerWorldOps` 调用移动、重叠、水和重力查询。默认 adapter 位于 `player_world.c`。因此 Player 政策已经不再直接调用方块或 cube chunk API。

真正的几何实现仍位于 gameplay 的 `player_motion.c`：它包含 legacy 逐轴移动、cube `SurfacePose` 位移、身体盒枚举、碰撞、落地和坐标转换，并直接引用 `SurfaceContract` 与 `GetBlockAt`。这违反“世界几何归 world”的目标边界。

`ActorBody` 已提供中性的位置、速度、朝向、接地和 floating 状态，并被 ship、space、ecology 与相机消费。ship 运行时仍把 `GameRuntime.player` 当作可变 actor 存储槽，其他 app 子系统也仍可直接修改 Player 字段。

固定步长时钟已经存在，当前由 `GameAdvanceSimulationFrame()` 驱动生态和实体等模拟；普通玩家运动和输入仍在渲染帧的 `GameUpdatePlayerMotion()` 中按帧 `dt` 推进。因此玩家运动尚未进入确定性固定步。

环境查询已有 `PlayerGravitySample`、水体和空间重力等局部抽象，但入口仍是按当前位置调用具体函数，没有统一的 `(position, time, frame)` 查询语义，也没有可组合的星球规则层。

| 能力 | 当前状态 | 目标状态 |
| --- | --- | --- |
| 原始输入采集 | app 的 `InputSource` 已完成 | 保持，app 只产生不可变输入快照 |
| 玩家命令语义 | `PlayerInput` 已完成基础拆分 | Controller 输出稳定的动作意图 |
| 相机边界 | `CameraManager` 已完成 | Presentation 只读取身体/相机快照 |
| 运动端口 | `PlayerWorldOps` 已存在 | world solver 成为默认实现，gameplay 不持有几何 |
| cube/legacy 几何 | 仍在 `player_motion.c` | 封装在 world 内部 backend |
| 状态写入所有权 | 分散在多个 app/gameplay 模块 | 每类状态只有一个提交入口 |
| 固定步运动 | 尚未完成 | locomotion 由固定模拟步推进 |
| 环境场 | 按需函数查询 | 显式位置、时间、参考系 |
| 规则组合 | 代码分支 | 有序、明确作用域的规则集合 |
| ECS | 未引入 | 完成前四项后再评估，不作为近期目标 |

## 3. 目标职责边界

| 责任 | 所有者 | 必须遵守的约束 |
| --- | --- | --- |
| 键盘、鼠标、注入事件采集 | app | 只生成输入快照，不解释游戏动作 |
| 玩家命令、游泳/跳跃/冲刺策略 | gameplay Player Controller | 不读取方块、cube chunk 或具体 surface topology |
| 连续位姿、速度、接地状态 | neutral Actor State | 每个固定步只有一个提交者 |
| 位移、扫掠、碰撞、step-up、坠落 | world motion solver | legacy/cube 差异只在 backend 内 |
| 水、重力、风、温度等环境场 | world environment | 查询输入输出参考系明确，不暗中生成世界 |
| 星球特殊规则 | world rule set | 声明阶段、作用域和组合语义 |
| 模拟步顺序、输入分配、状态提交 | app composition/scheduler | 固定顺序、可重放、可诊断 |
| 相机、音效、HUD、粒子 | presentation | 不写模拟状态 |
| 存档编解码 | app save boundary | 只持久化明确的长期状态，不持久化派生 UI 状态 |

`Player` 保留为玩家游戏的长期状态载体，但其中的运动部分最终应投影到同一个 `ActorBody` 语义。没有完成 actor-state 迁移前，不得为了接口整洁复制第二套 position/velocity 真相。

## 4. 目标数据流与接口方向

目标调用方向如下，函数名是边界示意，不要求第一阶段同时改完所有符号：

```text
InputSourceCaptureFrame
  -> ActorIntent
  -> PlayerController
  -> MotionRequest
  -> WorldResolveMotion
  -> MotionResult
  -> ApplyActorMotion
  -> ActorBody / PlayerState
  -> Camera, presentation, persistence, diagnostics
```

`ActorIntent` 描述本 tick 期望的前后、左右、垂直、跳跃、冲刺和视角增量。边沿动作必须在一 tick 内消费一次；输入层不得直接写 velocity 或 position。

`MotionRequest` 至少携带期望位移、身体尺寸、是否允许未加载区域、是否可 step-up 和当前参考系。`MotionResult` 至少返回实际位移、速度修正、接地、碰撞/卡住原因和实际经过的模拟时间。只返回 `bool` 无法表达部分位移，目标接口必须保留可诊断结果。

`WorldResolveMotion` 是无状态于具体玩家的几何接口。它接收中性 body/pose 和 request，返回结果而不拥有玩家游戏状态。这样 legacy、cube 或未来第三种 topology 都能作为 backend 实现。

`PlayerWorldOps` 可以作为 gameplay 的组合端口保留，但其默认实现必须绑定 world motion solver，不能再次调用 gameplay 侧的 `PlayerMoveWithOptions()`。

长期 actor 权威状态统一使用一个中性语义；`SurfaceAddress` 只能由连续位姿和世界快照派生，不能成为连续运动的规范存储。玩家在表面附近可以使用 `SurfacePose` 作为高保真物理解算状态，在太空中使用系统模拟帧，但两者都必须带显式帧身份。

## 5. 状态所有权与固定步

每个固定模拟 tick 必须按确定顺序执行：读取本 tick 不可变输入，更新控制意图，查询环境，求解运动，提交一次 actor 状态，再发布接触/落地/入水事件。Presentation 在 tick 完成后读取插值快照。

普通 locomotion、ship、传送、landing、出生、读档和 debug teleport 都必须使用明确的状态提交 API。特殊入口可以绕过运动求解，但不能绕过状态验证、事件发布和单一写入所有权。

`GameRuntime` 可以继续拥有当前玩家状态，但 app 的其他模块不得在任意位置散写 `position`、`velocity`、`onGround` 或 `floating`。迁移期允许保留旧 API，新 API 必须逐步成为唯一入口。

固定步输入应可重放。渲染帧可以重复使用最近一次输入快照，但 jump、切换 floating 等边沿事件只能由一个 tick 消费；生成/流式加载的等待时间不得改变已经采样的模拟 tick 序列。

## 6. 环境查询与星球规则

Environment 查询必须显式接收位置、模拟时间和参考系。返回值中的方向和速度也必须标明所属参考系。重力、风、温度和水流不得通过查询隐式触发生成或修改世界。

近期可以保留现有 `PlayerGravitySample`、`PlayerWaterState` 和空间重力函数作为 adapter；目标接口应逐渐统一为世界环境样本，而不是让 Player 增加更多星球类型分支。

规则系统必须声明执行阶段、作用范围、作用对象和组合方式。最小组合语义为 `add`、`modify`、`replace`；同一阶段、同一作用域的冲突必须通过显式优先级或确定性顺序解决，不能依赖注册顺序的偶然结果。

新增特殊星球应通过规则或 backend 扩展完成，不要求在通用 Player Controller 中增加 `if (planetType)`。动态实体是否共享这些规则由能力/组件决定，而不是由 `entity.type == PLAYER` 决定。

## 7. 参考系与精度

目标位置类型必须显式携带 frame identity。局部 map 坐标、surface pose、系统坐标和相机相对坐标是不同表示；转换只能通过具名 API 完成，并必须注明时间与来源快照。

在进行跨星球、高速度或远距离计算时，内部模拟应使用足够的精度，渲染再转换为相机附近的相对坐标。当前存档和 DSL 不要求立即切换到 double 或新帧格式；任何持久化格式变化必须独立设计迁移和版本升级。

`SurfaceAddress` 用于方块单元身份和邻居关系，`SurfacePose` 用于表面连续位姿，map 坐标用于现有交互和调试兼容。禁止把这些表示互相当作无条件等价的权威坐标。

## 8. ECS 的位置

不把完整 ECS 作为近期里程碑。当前更紧迫的是 world-side motion、状态写入所有权、固定步和环境查询；在这些边界稳定前引入 ECS，只会把跨模块写入隐藏在组件访问后面。

当动态实体类型和共享系统达到以下条件时才重新评估 ECS：多个实体类别确实共享同一组运动/规则系统；组件查询比现有固定结构更清晰；系统顺序和状态提交可以保持确定性；迁移不会迫使方块、区块和地形存储一起 ECS 化。

即使评估通过，方块、chunk、地形生成、`SurfaceAddress` 和流式覆盖也不必进入 ECS。可以采用轻量组件存储和系统表，而不是引入第三方 ECS 框架。

## 9. 渐进迁移路线

### P3：迁移几何运动实现

- 将 `player_motion.c` 的 legacy/cube 位移、碰撞、step-up 和 body-box 查询迁入 world-side neutral motion solver。
- `PlayerWorldOps` 继续作为 gameplay 端口，默认 adapter 绑定 world solver。
- 保持玩家行为、V30 存档、DSL 字段和现有测试不变。

验收：新增一种几何 backend 不需要修改 Player Controller；gameplay 控制器和端口头不暴露 `SurfaceAddress`、cube chunk 或方块存储 API。

### P4：统一状态提交与固定步

- 清点所有 Player 字段写入入口，引入运动结果提交 API。
- 将普通 locomotion 放入现有 fixed-step clock；输入按 tick 快照消费。
- 将 ship、landing、teleport、load 和 debug placement 接入统一状态验证/事件路径。

验收：相同输入序列和初始状态产生相同结果；除显式 transition API 外，不存在散落的 locomotion 状态写入；`test_simulation_replay` 覆盖玩家 checkpoint。

### P5：统一环境查询

- 将水、重力、流场和后续气候作用整理为环境样本查询。
- 先增加 adapter，保留旧函数调用者，逐步删除 Player Controller 中的具体环境分支。

验收：替换重力或新增一个环境规则不需要修改 Player Controller；未加载区域和查询时间语义有确定性测试。

### P6：规则与参考系

- 定义规则阶段、作用域、优先级与 add/modify/replace 语义。
- 引入显式 frame identity 和转换 API，先覆盖 surface/space 边界，再覆盖跨星球场景。

验收：特殊星球规则可由配置或注册扩展；surface、space、render 和 save 之间的位置转换均有具名入口和回归测试。

### P7：ECS 评估

- 在 P3-P6 完成后，根据真实组件查询和系统调度需求决定是否采用 ECS。
- 评估必须包含确定性、调试可读性、存档映射、测试成本和迁移回滚路径。

验收：只有在现有架构无法清晰表达共享系统时才引入 ECS；ECS 不成为方块或地形存储的前置条件。

## 10. 非目标与约束

- 不追求所有空间使用同一种坐标系。
- 不要求一次性删除 legacy backend。
- 不在 P3-P5 中改动 V30 存档格式。
- 不让 world 模块依赖 gameplay 的 `Player` 类型。
- 不让 presentation 或 camera 成为模拟状态写入者。
- 不以消息总线替代明确的数据流；事件只在需要解耦发布者与消费者时引入。
- 不为未来 ECS 预先把当前结构改造成通用组件容器。

## 11. 验证矩阵

| 边界 | 必测场景 |
| --- | --- |
| legacy motion | 平地行走、墙、台阶、悬崖、未加载 frontier |
| cube motion | 平地、跨面、极区、游泳、碰撞滑动、body-box 边界 |
| 输入与重放 | 手动输入、scripted input、按下沿、暂停与 catch-up |
| 状态所有权 | ship 进出、landing、load、teleport、spawn、debug placement |
| 环境 | 重力替换、水流、不同高度、未加载查询、时间推进 |
| 参考系 | surface/space 转换、跨面、镜头相对渲染、长期运行精度 |
| 回归 | `test_player_collision`、`test_game_player_input`、`test_simulation_replay` 及对应 Hyprland E2E |

## 12. 代码索引

- `src/gameplay/player_types.h`：当前 Player 长期状态。
- `src/gameplay/player.c`：当前玩家控制与运动策略。
- `src/gameplay/player_world.h`：当前 gameplay 到 world 的窄端口。
- `src/gameplay/player_motion.c`：当前 legacy/cube 几何与碰撞实现，也是 P3 的主要迁移源。
- `src/world/actor_body.h`：跨系统中性身体状态。
- `src/core/simulation_clock.*`：固定步与插值时序。
- `src/world/surface_pose.*`：表面连续位姿与局部/球面转换。
- `src/world/world_location.*`：身体盒和方块地址查询。
- `src/space/space_query.*`：空间重力与环境查询基础。
- `src/app/game.c`：当前帧调度和输入/模式控制。

## 13. 验收定义

当 Player Controller 不再包含具体几何 backend、所有 locomotion 状态写入都经过单一提交入口、环境与规则可通过接口替换、参考系转换显式且可测试时，本目标架构才算完成。

完成 ECS 评估并不等于必须采用 ECS。最终验收标准是职责边界和可验证行为，而不是某个框架、坐标类型或文件数量。
