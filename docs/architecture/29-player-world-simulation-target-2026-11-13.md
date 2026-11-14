# Player、World Simulation 与参考系目标架构（2026-11-13）

- 日期：2026-11-13
- 性质：目标架构与渐进迁移记录；本文不表示代码已经全部实现
- 编写基线：`a11de53`（2026-11-13，P3 开始时）
- 当前状态：见 [STATUS.md](../../STATUS.md)
- 前置记录：[Player/world topology decoupling](26-player-world-topology-decoupling-analysis-2026-11-12.md)、[Player camera coupling](27-player-camera-coupling-analysis-2026-11-13.md)、[Camera refactor implementation](28-camera-refactor-implementation-2026-11-13.md)

> P3 的实施证据见 [Player World Motion P3 实施记录](30-player-world-motion-p3-2026-11-13.md)。
> P4 的固定步输入与 locomotion 部分见 [Player Fixed-Step Locomotion P4a](31-player-fixed-step-locomotion-p4a-2026-11-13.md)；特殊状态提交和 replay checkpoint 见 [Player State Commit P4b](32-player-state-commit-p4b-2026-11-13.md)。
> P5 的 world environment query 实现见 [World Environment Query P5](33-environment-query-p5-2026-11-13.md)。
> P6 的规则与 frame 基础边界见 [World Rules and Frame Foundation P6](34-world-rules-frame-p6-2026-11-13.md)。
> P6b 的 gravity rule 环境消费见 [World Rule Environment Consumption P6b](35-world-rule-environment-p6b-2026-11-13.md)。
> P6c 的 surface/body/system/render/save frame API 见 [World Frame Cross-Reference P6c](36-world-frame-cross-p6c-2026-11-13.md)。
> P7 的 ECS 采用评估与决策见 [Player/Entity ECS Evaluation P7](37-player-entity-ecs-evaluation-p7-2026-11-13.md)。
> P8a 的运行时规则消费见 [Player Runtime Rule Integration P8a](38-player-runtime-rule-integration-p8a-2026-11-14.md)。
> P8b 的 save 边界采用与 renderer frame 定位见 [Frame Save Boundary P8b](39-frame-save-boundary-p8b-2026-11-14.md)。
> P8.1 的普通地面 Intent/Motion Simulation 见 [Intent-Driven Ground Locomotion P8.1](40-intent-driven-ground-locomotion-p8-1-2026-11-14.md)。
> P8.2 的统一 ground/water/space/floating pipeline 见 [Unified Player Environment Motion P8.2](41-unified-player-environment-motion-p8-2-2026-11-14.md)。

## 1. 结论

目标不是把 `Player` 搬进 `core`，也不以引入完整 ECS 为目标。目标是建立四条稳定边界：玩家控制器表达意图，世界几何求解器处理运动与碰撞，环境接口按位置和时间提供场，模拟调度器按确定顺序提交状态。

连续物理位置、离散方块地址和渲染相对坐标承担不同职责。统一的是位置和时间的语义、查询接口、运动结果与系统职责，不是强制所有空间使用同一个坐标系。

近期不重写存档、相机或实体系统。第一优先级是让 `Player` 不再拥有 legacy/cube 几何实现，并让所有 locomotion 状态写入经过同一个运动提交入口。

## 2. 编写时基线与差距（历史）

本节记录目标文档编写时的基线，不是今天的实现状态；当前状态只以 [STATUS.md](../../STATUS.md) 和实施记录为准。编写基线中，`Player` 仅保存 `position`、`velocity`、`yaw`、`pitch`、`onGround`、`floating`、`wasInWater` 和 `stepTimer`，输入采集已经位于 app，`PlayerInput` 是 gameplay 可消费的命令值，相机已经由 `CameraManager` 独立管理。

编写基线时，`UpdatePlayerWithWorld()` 已通过 `PlayerWorldOps` 使用移动、重叠、水和重力查询，但真正的 legacy/cube 几何仍位于 gameplay 的 `player_motion.c`；`ActorBody` 已存在，但 app 仍可能直接修改 Player 字段；普通玩家运动仍按渲染帧 `dt` 推进；环境查询仍是按需函数调用，没有统一的 `(position, time, frame)` 语义、规则层或 frame identity。

| 能力 | 编写时基线 | 目标落点与实施记录 |
| --- | --- | --- |
| 原始输入采集 | app 的 `InputSource` 已完成 | 保持输入快照边界；P4a |
| 玩家命令语义 | `PlayerInput` 已完成基础拆分 | 目标引入 ActorIntent；当前先保留 PlayerInput |
| 相机边界 | `CameraManager` 已完成 | Presentation 只读取身体/相机快照 |
| 运动端口 | `PlayerWorldOps` 已存在 | world solver 成为默认实现；P3 |
| cube/legacy 几何 | 编写基线时仍在 gameplay | world `actor_motion`；P3 |
| 状态写入所有权 | 编写基线时分散 | `player_state` commit API；P4b |
| 固定步运动 | 编写基线时尚未完成 | fixed-step input/locomotion；P4a |
| 环境场 | 编写基线时为按需函数 | world environment query；P5 |
| 规则与 frame | 编写基线时未建立 | rule registry/frame foundation；P6a |
| ECS | 未引入 | P6b 完成后再评估，不作为近期目标 |

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
  -> PlayerInput
  -> PlayerMotionIntent
       + ActorBody
       + PlayerMotionProfile
  -> PlayerMotionStep
       -> GROUND / WATER / SPACE / FLOATING simulation
       -> common ActorMotionRequest
       -> WorldActorResolveMotion
       -> moved environment sample
  -> PlayerMotionResult
  -> PlayerApplyActorBody
  -> gameplay effects
  -> Camera, presentation, persistence, diagnostics
```

`PlayerMotionIntent` 是普通 Player locomotion 的命令值；P8.2 把 ground、water、space、floating 都接到同一个 Intent → Mode Simulation → Common Collision → Commit 管线。现有固定步输入仍使用 `PlayerInput` 承载前后、左右、垂直、跳跃、冲刺和视角增量，再由 Player controller 转成 Intent。边沿动作必须在一 tick 内消费一次；输入层不得直接写 velocity 或 position。

现有 `ActorMotionRequest` 携带期望位移、`allowUnready` 和 `canStepUp`，身体尺寸由独立的 `ActorCollider` 提供。`ActorMotionResult` 返回最终 `ActorBody` 和是否发生位移；碰撞原因、接触面、实际经过时间和 frame identity 仍是后续增强项。

`WorldActorResolveMotion` 是无状态于具体玩家的 world-side 几何接口。它接收中性 `ActorBody`、`ActorCollider` 和 request，返回结果而不拥有玩家游戏状态。这样 legacy、cube 或未来第三种 topology 都能作为 backend 实现；P3 已把默认实现放到 `world/actor_motion.c`（见记录 30）。

`PlayerWorldOps` 保留为 gameplay 的组合端口；默认实现已经直接绑定 `WorldActorResolveMotion()`，不再由 gameplay 实现几何 backend。

长期 actor 权威状态统一使用一个中性语义；`SurfaceAddress` 只能由连续位姿和世界快照派生，不能成为连续运动的规范存储。玩家在表面附近使用 `SurfacePose` 作为高保真物理解算状态；P6a/P6c 已提供 `WorldFrameId`/`WorldFramePoint`、local ↔ body-centered、system 和 render-camera 位置转换（见记录 34、36）。P8b 后 save caller 已采用 save-local identity；renderer 保持 topology-native local-world 坐标，只有真实 camera-frame 消费者使用 camera-relative API。

## 5. 状态所有权与固定步

每个固定模拟 tick 必须按确定顺序执行：读取本 tick 不可变输入，更新控制意图，查询环境，求解运动，提交一次 actor 状态，再发布接触/落地/入水事件。Presentation 在 tick 完成后读取插值快照。

P4b 已提供 `PlayerHaltMotion()`、`PlayerSetLook()`、`PlayerSetFloating()`、`PlayerPlaceAt()`、`PlayerApplyMotionResult()` 和 `PlayerRestoreState()`（见记录 32）。普通 locomotion、ship 回写、传送、landing、出生、读档和 debug placement 的 app 入口已经迁移到这些 commit API；ship controller 内部仍直接操作它取得的 `ActorBody` 工作区，最终通过 commit 返回 Player。

`GameRuntime` 继续拥有当前玩家状态，但 app C 源码不得直接散写 `position`、`velocity`、`yaw`、`pitch`、`onGround` 或 `floating`；架构检查已经执行这一约束。新的特殊入口必须使用显式 transition/commit API。

P4a 已实现 render-frame input staging：持续轴使用最新值，look/jump/toggle-floating 边沿由一个 fixed tick 消费一次。P4b 的 `PlayerStateHash()` 已进入 `test_simulation_integration` 的 Player lane，完整 replay、checkpoint continuation 和改变输入的 divergence 都覆盖该状态（见记录 31、32）。

## 6. 环境查询与星球规则

Environment 查询的目标仍是显式位置、模拟时间和参考系。P5 已实现 `WorldEnvironmentSampleAt()` 的 position/time、water、gravity 组合样本；当前 frame 是 active local frame，显式 frame 参数尚未接入该接口（见记录 33）。

`PlayerGravitySample` 与 `PlayerWaterState` 现在是 world sample 的 gameplay aliases，`PlayerWorldOps` 默认 adapter 直接调用 world environment query。Player Controller 不再直接调用 `PlanetSurfaceAt()`、`WorldGravityScale()`、`FluidSampleAt()` 或 `WorldIsSpaceActive()`。

规则系统必须声明执行阶段、作用范围、作用对象和组合方式。P6a 的 `WorldRuleSet` 已固定 global/body/region specificity、priority、registration id 顺序，并提供 `ADD`、`MODIFY`、`REPLACE` 向量语义；P6b 已让 gravity/water 规则通过 `WorldEnvironmentSampleAtWithRules()` 分别影响 `WorldGravitySample.acceleration` 与 `WorldWaterSample.flowVelocity`（见记录 34、35）。P8a 已把运行时规则集绑定到 `PlayerWorldOps`，Player 通过组合环境样本实际消费最终重力和水流（见记录 38）。

新增特殊星球最终应通过规则或 backend 扩展完成，不要求在通用 Player Controller 中增加 `if (planetType)`。动态实体是否共享这些规则由能力/组件决定，而不是由 `entity.type == PLAYER` 决定。

## 7. 参考系与精度

目标位置类型必须显式携带 frame identity。P6a 已提供 `WorldFrameId`、`WorldFramePoint`、`WorldFrameCanTransform()` 和 `WorldFrameTransformPoint()`；P6c 又补充 system、render-camera 和 save-local 入口（见记录 34、36）。完整惯性、速度与旋转转换仍待后续。

在进行跨星球、高速度或远距离计算时，内部模拟应使用足够的精度，渲染再转换为相机附近的相对坐标。当前存档和 DSL 不要求立即切换到 double 或新帧格式；任何持久化格式变化必须独立设计迁移和版本升级。

`SurfaceAddress` 用于方块单元身份和邻居关系，`SurfacePose` 用于表面连续位姿，map 坐标用于现有交互和调试兼容。禁止把这些表示互相当作无条件等价的权威坐标。

## 8. ECS 的位置

不把完整 ECS 作为近期里程碑。P3-P6 已建立 world-side motion、状态写入所有权、固定步、环境查询、规则和 frame 边界；P7 评估没有发现足以支持组件化存储的共享查询或调度热点。此时引入 ECS，只会把仍有领域差异的运动、生态和存档写入隐藏在组件访问后面。

只有多个 actor 类别确实共享同一组运动/规则系统、组件查询比现有固定结构更清晰、系统顺序与状态提交仍可确定性重放，并且迁移不会迫使方块、区块和地形存储一起 ECS 化时，才重新评估 ECS。

即使评估通过，方块、chunk、地形生成、`SurfaceAddress` 和流式覆盖也不必进入 ECS。可以采用轻量组件存储和系统表，而不是引入第三方 ECS 框架。

## 9. 渐进迁移路线

### P3：迁移几何运动实现（实施记录 30）

- 将 `player_motion.c` 的 legacy/cube 位移、碰撞、step-up 和 body-box 查询迁入 world-side neutral motion solver。
- `PlayerWorldOps` 继续作为 gameplay 端口，默认 adapter 绑定 world solver。
- 保持玩家行为、V30 存档、DSL 字段和现有测试不变。

验收：新增一种几何 backend 不需要修改 Player Controller；gameplay 控制器和端口头不暴露 `SurfaceAddress`、cube chunk 或方块存储 API。

### P4a：固定步输入与 locomotion（实施记录 31）

- render-frame 输入 staging，首个 fixed tick 消费 look/jump/floating 边沿。
- 普通 locomotion、ship motion 和 tornado 外力进入固定步。
- 相机和流式更新顺序移动到 fixed-step 之后。

验收：输入在 catch-up tick 中不重复执行，E2E 行为不回归。

### P4b：统一状态提交与 replay checkpoint（实施记录 32）

- 清点所有 Player 字段写入入口，引入运动结果提交 API。
- 将 ship、landing、teleport、load 和 debug placement 接入统一状态验证/事件路径。
- 将 Player state hash 纳入 integration replay checkpoint。

验收：相同输入序列和初始状态产生相同结果；除显式 transition API 外，不存在散落的 locomotion 状态写入；`test_simulation_replay` 覆盖玩家 checkpoint。

### P5：统一环境查询（实施记录 33）

- 将水、重力、流场和后续气候作用整理为环境样本查询。
- 先增加 adapter，保留旧函数调用者，逐步删除 Player Controller 中的具体环境分支。

验收：替换重力或新增一个环境规则不需要修改 Player Controller；未加载区域和查询时间语义有确定性测试。

### P6a：规则与 frame 基础（实施记录 34）

- 定义规则阶段、作用域、优先级与 add/modify/replace 语义。
- 引入显式 frame identity，并实现 surface local ↔ body-centered 转换。

验收：规则顺序可重放；未支持的 frame pair 显式失败。

### P6b：gravity/water 规则消费（实施记录 35）

- 将 gravity/water `WorldRuleSet` 接入 `WorldEnvironmentSampleAtWithRules()`。
- 验证 body/region scope、rule count、acceleration 和 flow 结果。

验收：特殊星球 gravity/water 规则可注册并实际影响环境样本。

### P6c：跨参考系 API（实施记录 36）

- system anchor point、surface/body/system 组合转换、camera-relative position 和 save-local identity 已提供。
- 系统投影仍使用 game-distance 近似；完整惯性、速度和旋转转换待后续。

验收：各 frame pair 均有具名入口；位置 round trip 和不支持输入有回归测试。save caller 已在 P8b 接入 save-local identity；renderer 保持 topology-native local-world 坐标，只有真实 camera-frame 消费者使用 camera-relative API。

### P7：ECS 评估（实施记录 37）

- 已根据真实组件查询和系统调度需求完成评估；结论是暂不采用完整 ECS，也不引入第三方框架。
- 保留显式 `Player` 和固定 `Entity` store，共享行为继续通过 `ActorBody`、`CameraTargetView`、world motion/environment query 等窄接口表达。
- ECS 重新评估条件和未来轻量组件方案的确定性、存档与回滚要求记录在实施文档中。

验收：评估包含确定性、调试可读性、存档映射、测试成本和迁移回滚路径；ECS 不成为方块或地形存储的前置条件。

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
| 输入与重放 | `test_game_player_input`、`test_simulation_integration`、scripted input、catch-up |
| 状态所有权 | `test_player_state`、ship、landing、load、teleport、spawn、debug placement |
| 环境 | `test_player_collision` 的 world sample/adapter 一致性；后续增加规则消费测试 |
| 参考系 | `test_player_collision` 的 local/body round trip；后续增加 system/render/save |
| 规则 | `test_world_rules` 的 scope/priority/order 与 ADD/MODIFY/REPLACE |
| 回归 | 对应 Hyprland 平地、跨面、极区和固定步 E2E |

## 12. 代码索引

- `src/gameplay/player_types.h`：当前 Player 长期状态。
- `src/gameplay/player.c`：当前玩家控制与运动策略。
- `src/gameplay/player_world.h`：当前 gameplay 到 world 的窄端口。
- `src/gameplay/player_state.[ch]`：显式 Player transition/commit 和 state hash。
- `src/gameplay/player_motion.c`：Player-specific motion compatibility facade。
- `src/world/actor_motion.[ch]`：legacy/cube 几何、碰撞和 neutral motion solver。
- `src/world/actor_body.h`：跨系统中性身体状态。
- `src/world/environment_query.[ch]`：water/gravity/environment 样本查询。
- `src/world/world_rules.[ch]`：确定性规则注册与向量求值。
- `src/world/world_frame.[ch]`：frame identity 和已支持的 point 转换。
- `src/core/simulation_clock.*`：固定步与插值时序。
- `src/world/surface_pose.*`：表面连续位姿与局部/球面转换。
- `src/world/world_location.*`：身体盒和方块地址查询。
- `src/space/space_query.*`：空间重力查询与后续 system-frame 数据源。
- `src/app/game.c`：帧调度、输入 staging 和固定模拟顺序。

## 13. 验收定义

当 Player Controller 不再包含具体几何 backend、所有 locomotion 状态写入都经过单一提交入口、环境与规则可通过接口替换，并且 surface/space/render/save 的参考系转换显式且可测试时，本目标架构才算完成。

P3、P4a、P4b、P5、P6a、P6b、P6c API、P7 ECS 评估、P8a 运行时规则消费、P8b save/frame 边界、P8.1 普通地面 simulation 和 P8.2 统一环境 locomotion pipeline 已实现对应验收。Tornado external force 与 ship 仍为独立后续工作；具体星球规则内容与持久化属于 world definition 配置。

完成 ECS 评估并不等于必须采用 ECS。最终验收标准是职责边界和可验证行为，而不是某个框架、坐标类型或文件数量。

### P8a：运行时规则消费（实施记录 38）

- `GameRuntime` 持有运行时 `WorldRuleSet`，`PlayerWorldOps` 绑定该集合并通过组合环境样本查询当前 body 的水和重力。
- gravity 规则修改实际加速度，water 规则修改最终 flow；Player 的速度积分直接消费结果。
- 当前通用 region identity 尚未建立，Player 查询使用 `0`，global/body 规则已生效，region scope 留给后续 world definition。

验收：无规则行为不回归；body-scoped gravity/water 规则通过真实 PlayerWorldOps 改变运动；不匹配 body 不生效。

### P8b：save 与 renderer frame 边界（实施记录 39）

- Player save/load 通过 `WorldFrameSaveId()` 和 `WorldFrameTransformPoint()` 显式验证 local ↔ save-local；V30 数值和编码不变。
- renderer 继续使用 camera + topology-native local-world transform，不以 camera-relative API 重复平移；camera-frame API 只服务真实跨 frame 数值消费者。
- 速度、角速度和完整惯性 frame 仍不在本阶段伪造。

验收：save round trip 和非法输入有回归；公共 frame pair 测试不回归；renderer 坐标契约被明确记录。

### P8.1：普通地面 Intent / Motion Simulation（实施记录 40）

- 新增 `ActorLocomotionIntent`、`ActorLocomotionProfile`、`ActorLocomotionResult` 和单 tick `ActorGroundLocomotionStep()`。
- Player controller 只把普通地面输入转换为 Intent；中立 simulation 计算速度、jump 和候选位移，再调用既有 world geometry solver。
- 水、太空、漂浮、载具、特效和表现时序保持原实现；Player 仍是唯一状态提交者。

验收：普通地面行为不回归；通用 simulation 不依赖 Player、输入轮询或拓扑；不同 profile 可改变结果；固定步边沿与 replay 保持确定性。

### P8.2：统一 ground/water/space/floating（实施记录 41）

- 新增 `PlayerMotionIntent`、`PlayerMotionMode`、`PlayerMotionProfile`、`PlayerMotionResult` 和 `PlayerMotionStep()`。
- Player controller 不再包含 water/space/floating 速度分支；四种 mode 共用一次 world collision 和一次 `ActorBody` commit。
- water flow/buoyancy、space damping/clamp、floating vertical speed 原样迁入 mode simulation。
- tornado 和 ship 尚未进入统一 pipeline，保持原路径。

验收：四种 mode 行为与旧实现一致；mode priority 明确；每 tick 只有一次 collision 和 Player commit；fixed-step/replay/E2E 不回归。
