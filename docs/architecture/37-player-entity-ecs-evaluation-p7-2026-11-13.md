# Player/Entity ECS Evaluation P7（2026-11-13）

- 日期：2026-11-13
- 性质：阶段记录；完成 [Player、World Simulation 与参考系目标架构](29-player-world-simulation-target-2026-11-13.md) 的 P7 评估
- 范围：Player、Entity、相机、运动/环境查询、固定步调度和存档映射是否适合迁移到 ECS
- 当前状态：见 [STATUS.md](../../STATUS.md)

## 1. 评估结论

本阶段不采用完整 ECS，也不引入第三方 ECS 框架。继续保留显式的 `Player` 领域状态和固定的 `Entity` store；继续通过 `ActorBody`、`CameraTargetView`、world motion solver 和 environment sample 等窄值类型表达跨系统共享。

这不是因为“ECS 一定不适合游戏”，而是当前代码没有出现足以抵消迁移成本的真实组件查询需求。现有 Player 与 Entity 的主要差异不是组件组合不同，而是生命周期、控制来源、领域状态和持久化契约不同。直接组件化会把运动、生态、演化和存档的写入顺序藏进通用查询与系统表，反而削弱本阶段已经建立的确定性边界。

以下内容明确排除在近期迁移之外：

- 方块、chunk、地形生成、`SurfaceAddress`、流式覆盖和 mesh 数据不进入 ECS。
- V30 存档不因 ECS 评估而改变。
- 不为了未来组件化预先把 Player 或 Entity 改造成通用容器。
- 不把相机或 presentation 变成模拟状态的写入者。

## 2. 评估快照

### 2.1 Player

`Player` 是 gameplay 拥有的单一显式领域对象，保存连续位置、速度、朝向、接地/漂浮状态、入水过渡和脚步计时。普通 locomotion 已经按以下方向流动：

```text
InputSource / fixed-step staging
  -> PlayerInput
  -> UpdatePlayerWithWorld()
  -> PlayerWorldOps
  -> WorldActorResolveMotion() / WorldEnvironmentSampleAt()
  -> Player commit
  -> Camera / presentation / save
```

该结构的价值在于状态所有权清楚：`PlayerState` transition API 是特殊状态提交入口，world solver 不依赖 `Player`，app 只持有和编排运行时对象。Player 的“单一实体”并不构成 ECS 性能问题；当前也没有按组件批量查询多个玩家实体的需求。

### 2.2 Entity

`Entity` 是固定容量的活动槽记录，包含 `active`、`spawnSerial`、位姿/速度、物种与谱系、生态需求、行为状态、表型、繁殖、年龄、生命值、掉落和渲染姿态等数据。Entity 系统已经形成自己的确定性约定：

- 固定槽顺序扫描，`spawnSerial` 用于在相机和外部句柄中识别复用的槽。
- `EntitiesUpdate()` 与固定步模拟使用同一显式时序，但内部运动、碰撞、生态采样和演化逻辑并不等同于 Player locomotion。
- 生态区域演化、天气/火灾/龙卷危害、繁殖和物种事件通过独立系统入口调用。
- Entity 状态使用自己的版本化磁盘记录；渲染插值使用独立的历史快照，不改变权威状态。
- `EntitiesView()` 提供只读视图，渲染、相机和交互消费快照而不是把 `Entity` 头暴露给 world 核心。

这些字段虽然很多，但多数围绕一个明确的动物生命周期记录，并通过函数分区。将它们拆成稀疏组件后，当前系统仍需遍历同一批记录并同步生态、表型和存档状态，收益不会自动出现。

### 2.3 已共享的边界

当前已经存在的中性边界足以承载多数跨系统需求：

| 需求 | 现有边界 | 是否要求 ECS |
| --- | --- | --- |
| Player/Entity 相机目标 | `CameraTargetHandle` + `CameraTargetView` + spawn-safe handle | 否 |
| 运动学只读视图 | `ActorBody` | 否 |
| 世界几何与碰撞 | `WorldActorResolveMotion()`、`ActorCollider`、`ActorMotionResult` | 否 |
| 水/重力/环境样本 | `WorldEnvironmentSampleAt()` 或带规则的样本查询 | 否 |
| 固定步与提交时序 | `SimulationClock` + app composition | 否 |
| 多条相机 rig | `CameraManager` 与 target binding | 否 |
| 表现层读取位姿 | `EntityRenderPose`、`CameraTargetView`、插值快照 | 否 |

因此，“共享系统”已经通过数据契约和显式调用实现，而不是依赖组件存储实现。ECS 只会改变存储与调度形态，不会自动解决 Player/Entity 之间的领域差异。

## 3. 决策门槛

| 条件 | 本阶段事实 | 结论 |
| --- | --- | --- |
| 多个实体类别确实共享同一组运动/规则系统 | Player 使用 world solver；Entity 保留自己的生态行为与移动规则，二者主要共享只读运动视图 | 未满足 |
| 组件查询明显比固定结构更清晰 | 当前没有跨 Player/Entity 的统一组件查询热点；Entity 内部逻辑依赖整条动物生命周期记录 | 未满足 |
| 系统顺序与状态提交可以保持确定性 | 现有固定步、固定槽扫描、spawn serial 和 replay checkpoint 已可验证；ECS 迁移需要重建同等保证 | 迁移不是必要手段 |
| 存档映射可低风险完成 | Player 与 Entity 使用不同版本化记录和校验；组件存储会引入额外的映射与迁移层 | 当前风险高于收益 |
| 调试可读性不下降 | 现有直接字段、只读视图和 DSL 诊断能直接定位状态 | 没有组件化收益证据 |
| 不迫使方块/地形一起 ECS 化 | 世界几何、chunk 和地形与 actor 生命周期独立 | 可以隔离，但没有实施理由 |

结论是：ECS 不是 Player/world 解耦的前置条件，也不是当前共享系统的更简单表达方式。

## 4. 后续触发条件

未来只有在下列条件同时成立时才重新开启评估，并且应先做隔离原型，不直接改 V30 存档：

1. 出现第三个或更多真正共享同一套运动、规则或传感器系统的 actor 类别，而不仅是给现有 Entity 增加类别枚举。
2. 出现可测量的批量查询需求，例如大量 actor 的同一环境采样、碰撞或状态提交，能用组件数组明显降低复杂度或成本。
3. 系统阶段、优先级、状态提交和确定性 tie-break 能在一张显式系统表中完整表达，并保持与现有 replay 测试等价。
4. 存档可以提供双向适配：旧 Player/Entity 记录可读、新布局可回写，且失败时能回滚到显式记录实现。
5. 方块、地形、chunk 与流式数据仍保持非 ECS；迁移不扩大为世界存储重写。

若条件成立，优先考虑轻量组件存储和显式系统表，而不是引入完整框架。第一个原型必须独立于现有存档格式，并用 fake actor、固定顺序和失败回滚测试证明收益。

## 5. 确定性与回滚要求

如果未来启动 ECS 原型，以下约束不可省略：

- 固定 tick 内系统顺序必须显式且可记录，不允许依赖哈希表遍历。
- 活动实体迭代使用稳定顺序；复用槽必须继续携带 `spawnSerial`，外部句柄不能退化为裸索引。
- 环境查询保留显式位置、模拟时间和参考系；不允许组件访问隐式生成世界。
- 每个 tick 的运动最终状态只有一个提交者；相机、renderer 和生态危害不得写回权威位姿。
- Player 和 Entity 的磁盘版本各自独立推进；测试必须覆盖保存、加载、checkpoint continuation 和输入 divergence。
- 迁移以只读快照或双写适配器起步，旧实现保留到 replay、相机绑定、存档和调试证据齐全后再删除。

## 6. 验证与边界

本阶段是架构评估，不修改运行时代码、公共头文件、DSL 或存档格式，因此不运行 C 测试或 `docs-probe`。验证范围是文档门禁、代码索引路径和结论引用的一致性：

- `make check-docs`：验证本记录的可达性、anchors、registry 和引用。
- 结论中的 Player/Entity 行为由 `src/gameplay/player.[ch]`、`src/gameplay/player_types.h`、`src/world/actor_body.h`、`src/ecology/entity.[ch]`、`src/ecology/entity_simulation.c`、`src/ecology/entity_interpolation.c`、`src/app/game.c`、`src/app/game_camera.c` 和 `src/app/game_save.c` 支撑。
- 与 ECS 直接相关的现有测试边界：`tests/test_entity_replay.c`、`tests/test_entity_ecology.c`、`tests/test_simulation_integration.c` 和 `tests/test_camera.c`。

## 7. 代码索引

- `src/gameplay/player_types.h`：显式 Player 状态。
- `src/gameplay/player.c`：玩家控制策略与 world ops 调用。
- `src/gameplay/player_world.c`：Player 到 world motion/environment 的组合适配。
- `src/gameplay/player_state.c`：Player 状态提交和 replay hash。
- `src/world/actor_body.h`：跨系统中性运动视图。
- `src/world/actor_motion.c`：world-side 几何运动求解。
- `src/world/environment_query.c`：水、重力和规则化环境样本。
- `src/ecology/entity.h`：Entity 领域记录和公开生命周期接口。
- `src/ecology/entity.c`：Entity 存储、查询、存档和交互。
- `src/ecology/entity_simulation.c`：Entity 固定步行为、生态与演化。
- `src/ecology/entity_interpolation.c`：渲染插值快照。
- `src/app/game.c`：固定步 composition 和 Player/Entity 排序。
- `src/app/game_camera.c`：Player/Entity 到相机 target view 的读取边界。
- `src/app/game_save.c`：Player 与 Entity 的持久化组合根。
