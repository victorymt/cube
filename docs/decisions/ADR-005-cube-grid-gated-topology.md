# ADR-005: cube-grid 以运行时门控引入，并在完整迁移后切换 Home 默认

## 状态
已采纳

## 日期
2026-10-27（记录日期）

## 背景
legacy 表面网格把每一圈纬度固定成同样多的区块，`docs/cube-contract.md` 第 4.1 节按 `SURFACE_EQUATOR_BLOCKS` 与 `SURFACE_RADIUS_BLOCKS` 算出：距极点越近，整圈物理周长越短，单个区块越窄；渲染距离 12 时极点得到的是 0.23 块宽、400 块高的窄条，槽位却只有 `MAX_ACTIVE_CHUNKS`（渲染距离 12 时为 625）。放大渲染距离不解决问题：覆盖宽度随距离线性增长，槽位随平方增长。`docs/cube-activation-plan.md` 的 W1a 因此被实测否决，极点窄条不是枚举 bug，而是经纬网格无法表示极点附近地面的结构性缺陷。另一条约束是切换代价：两套网格给同一位置不同身份，切换只能重建世界而不能重新解释数据，所以任何半迁移都会让同一方块在画面、碰撞和存档里解析成不同身份。

## 决策
把 cube-grid 做成一条独立的拓扑序列 `SURFACE_TOPOLOGY_CUBE_GRID`（版本 `SURFACE_CUBE_TOPOLOGY_VERSION` 为 `1u`），与地形生成版本、存档版本互不共用编号。迁移期间默认策略先保持两边 `SURFACE_TOPOLOGY_MAP_V1`，消费者全部转到 `SurfaceContract` 后，M4.10 将 `WORLD_SURFACE_TOPOLOGY_DEFAULT` 更新为 Home cube-grid/1 + planets legacy-map/1。运行时门控 `WorldCubeRuntimeEnabled` 在低层仍默认关闭，单次 new-world/load/debug 组合根在创建 cube contract 前显式打开，因此门控保留为安装边界而不是用户选择。`SurfaceTopologyIsRuntimeSupported` 描述 backend 是否存在；`WorldSurfaceTopologyPolicyIsSupported` 接受逐表面独立或混合策略。关闭门控时 cube snapshot/contract 不能安装，`WorldSaveReadHeader` 拒绝含 cube 的存档，`CubeSwitchBegin` 返回假并把进度置为 `CUBE_SWITCH_ROLLED_BACK`。切换本身由 `cube_switch.h` 的状态机排序（排空 worker、建上下文、清依赖状态、落地、按覆盖提交或回滚），未到 `CUBE_SWITCH_COMMITTED` 之前旧世界仍是已提交的那个。M1 后活动 `SurfaceContract` 是唯一长期 cube context 所有者，事务安装成功后 world 才释放旧 contract。

## 备选方案
### 备选 1：直接切换默认拓扑到 cube-grid，一次改完所有消费者
- 优点：没有双轨期，不存在两套坐标同时活跃的窗口；改动集中在一次大改里，不用维护门控分支。
- 缺点：玩家、碰撞、射线、流体、生态、CGA、火山、水文和存档都要在同一次改动里换语义，任何一处漏改都会让同一方块在不同系统里身份不一致；`docs/cube-activation-plan.md` 把这类风险记为“坐标语义半迁移”。
- 未采用原因：`docs/cube-activation-plan.md` 选定以“完整正常游戏集成”为激活闸门，本 ADR 据此不切换默认策略，直到移动、碰撞、交互、模拟与持久化全部转换完成。

### 备选 2：先独立修极点覆盖，再决定是否迁移拓扑
- 优点：改动面小，只碰候选枚举与加载优先级，不牵动身份与存档。
- 缺点：几何上不成立。每圈纬度固定区块数，距极点 16 块时整圈只有约 100 块，单区块宽约 0.098 块，铺满该半径就要超出 625 槽位；即使把枚举重写得完美，能做的也只是让窄条变宽一点。
- 未采用原因：实测数据把“候选枚举是主因”这个前提否决了，`docs/cube-activation-plan.md` 的 W1a 因此撤销；极点只能由 cube-grid 从结构上解决，因为极点落在立方体面内部而不是坐标奇异点。

### 备选 3：用双轨并存代替门控，长期同时维护两套网格
- 优点：旧网格随时可用，迁移可以慢慢来，任何时刻都有可回退路径。
- 缺点：两套网格给同一位置不同身份，消费者必须同时理解两种坐标，每处调用点都要做两遍推理；`WorldBlockAddress` 已经要用 `dimension` 标签区分有效成员，再叠一层网格区分会让身份契约更难守。
- 未采用原因：计划书明确不采用长期双轨并存，门控期是过渡而不是终态；目标仍是 cube-grid 全量迁移、旧网格下线。

## 后果
- 门控关闭时策略仍可声明，但 cube 不能安装：`tests/test_world_surface_snapshot.c` 断言 gate 关闭时 cube snapshot 被拒且代次、快照与旧 contract 不变；`tests/test_save_format.c` 的 `TestTopologyHeader` 断言 mixed/cube 头可写，但读档在 gate 关闭时拒绝。
- 拒绝是显式且原子的：`WorldInstallSurfaceSnapshotWithContract` 在交换前校验拓扑、body 和 contract，失败时快照、策略、世界代次与旧 contract 都不改变；`WorldSaveReadHeader` 先只读校验再赋值 `outPolicy`。
- 契约边界：`SurfaceCubeCellNeighbor` 与 `SurfaceCubeChunkNeighbor` 用整数边折叠而不是浮点投影取整，跨面一步可逆；`SurfaceCubeNeighborhood` 按图距离枚举，半径 12 的最坏容量由 `SURFACE_CUBE_NEIGHBORHOOD_CAPACITY` 给出，容量不足时拒绝而不是返回误导性的部分结果；图距离不等于物理块距离。
- 代价：门控把“backend 存在”与“当前组合根允许安装”拆成两个问题；正常新 Homeworld、V30 load 和显式调试切换分别打开门控。`GameDebugEnterCubeWorld` 设置 Home cube + Planets legacy，失败时精确恢复旧门控和旧 policy；`GameDebugTransitionSurface` 让 `surface debug home` 保持显式 legacy baseline。普通消费者通过 `SurfaceContractSupportsPhysicalCoverage/Raycast/Pose` 查询能力，不读 topology kind。
- V30 存档头用 `TOPOL002` 记录逐表面策略；fresh process 可直接安装 cube backend，不再要求启动时执行 `surface debug cube`。
- 代价：覆盖预算仍然紧张，`SURFACE_COVERAGE_MAX_RADIUS` 为 208 块而 `SURFACE_COVERAGE_MAX_CANDIDATES` 是 4096，实测请求峰值会超过 `MAX_ACTIVE_CHUNKS`，饱和因此是一等状态而不是错误，任何报告都不得把饱和结果标成完整覆盖。
- 验证方式：`make test-surface-cube`、`make build/normal/tests/test_world_surface_snapshot`、`make build/normal/tests/test_cube_switch`、`make build/normal/tests/test_save_format`、`make test-surface-topology-e2e`。
- 本 ADR 只陈述拓扑与门控的契约，不声称任何一条路径当前可用；那类断言只写在 [STATUS.md](../../STATUS.md)。

## 证据
- `src/world/surface_topology_version.h`：`SurfaceTopologyKind`、`SURFACE_TOPOLOGY_CUBE_GRID`、`SURFACE_TOPOLOGY_MAP_V1`、`WORLD_SURFACE_TOPOLOGY_DEFAULT`
- `src/world/surface_topology_version.c`：`SurfaceTopologyIsKnown`、两类 backend 均存在的 runtime support、允许 mixed policy 的 `WorldSurfaceTopologyPolicyIsSupported`
- `src/world/surface_contract.h` / `surface_contract.c`：消费者中性 address/cell/handle/sample/neighbor/region/coverage 操作
- `src/world/surface_legacy_contract.c` / `surface_cube_contract.c`：两套 adapter；cube adapter 是长期 context 的唯一所有者
- `tests/test_surface_contract.c`：共享 adapter 语义与事务安装
- `src/world/world_surface_snapshot.h`：`WorldCubeRuntimeEnabled`、`WorldSetCubeRuntimeEnabled`、`WorldInstallTopologyPolicy`
- `src/world/world_surface_snapshot.c`：门控默认值、`WorldNextSurfaceGeneration`、事务化 `WorldInstallSurfaceSnapshotWithContract`、活动 contract 所有权
- `src/world/surface_cube.h`：`SURFACE_CUBE_TOPOLOGY_VERSION`、`SURFACE_CUBE_NEIGHBORHOOD_RADIUS_MAX`、`SurfaceCubeCellNeighbor`、`SurfaceCubeNeighborhood`
- `src/world/surface_types.h`：`SURFACE_FACE_BLOCKS`、`SURFACE_EQUATOR_BLOCKS`、`SURFACE_RADIUS_BLOCKS`
- `src/world/surface_coverage.h`：`SURFACE_COVERAGE_MAX_RADIUS`、`SURFACE_COVERAGE_MAX_CANDIDATES`、`SurfaceCoverageCollect`
- `src/world/cube_switch.h`：`CubeSwitchStage`、`CubeSwitchBegin`、`CubeSwitchReportCoverage`
- `src/world/cube_switch.c`：`CubeSwitchPermitted` 读门控、`CubeSwitchBegin` 的门控拒绝
- `src/world/save_format.c`：`WorldSaveWriteHeader` 接受 mixed policy，`WorldSaveReadHeader` 在 gate 关闭时拒绝含 cube 的头
- `src/app/game_debug_topology.c`：`GameDebugEnterCubeWorld` 安装 Home cube + Planets legacy，并精确恢复失败的旧 gate/policy；`GameDebugTopologyDslResolve` 提供诊断字段
- `tests/test_world_surface_snapshot.c`：门控默认值与开启后的安装行为
- `tests/test_save_format.c`：`TestTopologyHeader`
- `tests/test_cube_switch.c`：门控关闭时状态机回滚、覆盖不足时的有界回滚
- [cube 激活契约](../cube-contract.md)
- [cube-grid 地基](../surface-cube.md)
- [cube 激活计划](../cube-activation-plan.md)
