# Cube-grid 激活契约（W0）

- 状态：**W0 冻结稿**，2026-10-22，基线 `c02812e`
- 作用：为后续 W1a/W1–W5 提供单一权威事实来源。与其它记录冲突时**以本文件为准**，并修正冲突方。
- 本文件只冻结契约，不改变运行时默认行为。

## 1. 版本矩阵（V 表）

代码中同时存在**五条独立版本序列**，任何文档或讨论都不得混用：

| 序列 | 当前值 | 定义位置 | 语义 | 推进条件 |
|---|---|---|---|---|
| 地形生成 | **11** | `TERRAIN_GENERATION_CURRENT`（terrain_generation.h:7） | 新世界基底生成算法；11 = 叠加历史火山地形层 | 算法或默认参数改变即递增 |
| cube 基础生成 | **9** | `TERRAIN_GENERATION_CUBE`（terrain_cube.h:8） | cube 专用采样原语的名义版本；**不代表运行时已启用** | cube 采样语义改变即递增 |
| 世界存档 | **V29** | `WORLD_SAVE_FORMAT_V29`（save_format.h:22） | 整世界序列化格式；只接受 V29 | 拓扑或字段布局改变即升到 V30 |
| 表面拓扑 | **legacy-map/1** | `WORLD_SURFACE_TOPOLOGY_DEFAULT`（surface_topology_version.h:26） | Home 与行星均 legacy-map；cube-grid/1 **被运行时拒绝** | 激活 cube 时切到 cube-grid/1 |
| CGA 语法 | 5 / 2 / 3 / 4 / 7 | `cga_voxel.h` | 按结构类型分别编号，与地形版本无关 | 单类语法改变只递增该类 |

**关键澄清：**

- “新世界地形 v11”**不表示** cube 已启用。
- “cube 基础 9”是原语命名版本，**不表示**运行时使用 cube。
- 地形版本与存档版本**可独立演进**；V29 中保存的地形版本字段是 2..CURRENT 的值，不是存档格式版本。
- 存档 trailer 中的 face address **不是** cube 体素证明；V29 的球面 trailer 与 cube 地址是不同概念。

## 2. 身份契约（I 表）

一个方块在全链路上必须有**唯一身份**，由 `WorldBlockAddress` 表达：

| 维度 | 身份 | 存储 | 备注 |
|---|---|---|---|
| Home 表面 | `surface{bodyId=0, face, u, v, radial}` | 表面编辑存储 | 永不经 map 坐标往返 |
| 行星表面 | `surface{bodyId=seed, face, u, v, radial}` | 表面编辑存储 | bodyId 非零，跨天体隔离 |
| Space | `cartesian{x, y, z}` | 空间编辑 | 不带 face |
| Nether | `cartesian{x, y, z}` | Nether 编辑 | 不带 face |

**不变量：**

1. 身份标签（`dimension`）决定有效成员；未使用的 union 字节**不参与**哈希或相等比较。
2. 跨面邻接必须用**整数方向折叠**（`SurfaceCubeCellNeighbor`），不得用浮点投影后取整——投影不保证双射。
3. 角点附近不允许假设 U/V 步进可交换。
4. 同一方块在生成、编辑、碰撞、选取、流体和存档中解析为同一身份。
5. 图距离（邻接球步数）**不等于**物理块距离。

## 3. 所有权契约（O 表）

| 状态 | 所有者 | 生命周期 | 消费者不得 |
|---|---|---|---|
| 表面快照 `WorldSurfaceSnapshot` | app 组合根 | 世界切换前排空并安装 | world 直接查 space 高层状态 |
| cube 生成上下文 `TerrainCubeContext` | 创建方持有引用 | 引用计数；工作队列保留 | worker 持有即可变世界指针 |
| cube 编辑存储 | world（主线程） | 进程内，**当前不落盘** | 在工作线程写入 |
| 区块槽位池 | world | 全局固定 625 槽 | 隐藏饱和或静默驱逐 |
| 存档事务 | app | 每次保存/载入 | 失败后留下半写文件 |
| 覆盖策略 | world | 每帧调用一次 | 用局部就绪冒充全视锥就绪 |

**生成上下文的版本捕获**（易错点，W0.2）：

`TerrainCubeContextCreate` 在**创建时刻**读取全局 `TerrainGenerationVersion()` 决定 `historyEnabled`（terrain_cube.c:71），并在该上下文内保持不变。因此：

- 上下文一旦创建，采样输入确定；
- 但**上下文输入契约必须显式包含地形版本**，不能假设只由 snapshot/direction/seed/profile 决定；
- 切换地形版本前必须排空工作并重建上下文。

## 4. 容量契约（C 表）

| 预算 | 值 | 来源 | 行为 |
|---|---:|---|---|
| 最大渲染距离 | 12 区块 | `MAX_RENDER_DISTANCE_CHUNKS` | 硬上限 |
| 区块槽位 | **625** = (12×2+1)² | `MAX_ACTIVE_CHUNKS` | 固定池，不动态扩张 |
| 覆盖枚举上限 | 4096 | `SURFACE_COVERAGE_MAX_CANDIDATES` | 枚举容量，非准入容量 |
| 覆盖物理半径 | 208 块 | `SURFACE_COVERAGE_MAX_RADIUS` | 地面覆盖，不含高空视锥承诺 |
| 实测最大请求 | **1560** | `test_surface_coverage` | **超过 625，真实路径会饱和** |
| cube 编辑上限 | 1,048,576 | `WORLD_CUBE_MAX_EDITS` | 超限拒绝，不静默丢弃 |

**饱和是合法状态，不是错误**：`SurfaceCoverageCollect` 会返回 `saturated=true` 并保留最近的 625 项。契约要求：

1. 任何报告**不得**把饱和结果标成“完整覆盖”；
2. 降级策略必须显式（当前为“最近优先保留”），不能靠加大距离掩盖；
3. 诊断必须分别输出 requested / admitted / ready / pending / missing / saturated；
4. `surface_ready` 只表示**局部**就绪，不等于视觉就绪。

**已知待决项（W0.3 输出）**：208 块半径下请求 1560 而槽位 625，意味着半径开口远大于容量。三种可选政策，需在 W1.5 前定案：

- **A**：按容量反推实际半径，让请求从不饱和（简单，但远处地面缺失）；
- **B**：保持半径、接受饱和并明确降级（当前行为，需可视化告警）；
- **C**：按径向/距离分层加载，近处全量、远处降精度（最复杂）。

本计划**暂采用 B**（与现有实现一致），W4 再依据实测决定是否转 A 或 C。

## 5. 与既有文档的冲突及处置

| 文档 | 现状 | 处置 |
|---|---|---|
| `README.md:92` | 称新世界使用 terrain v8 | 改为 v11，并注明 cube 未启用 |
| `todo/00-roadmap.md:11` | 称 new worlds use terrain v8 | 同上 |
| `todo/00-roadmap.md:15-18` | 称 queue / coverage policy 仍 pending | 标注“适配器已实现，默认未接入” |
| `todo/00-roadmap.md:26` | 称激活目标为 cube-grid/1 + terrain v9 | 改为“cube 基础 9 + 历史层 11 的组合”，版本统一留待 W1 |
| `docs/geographic-landforms-design.md:5,9` | 称 v9 基础生成器、v7/v8 | 标注 v11 为当前，v9 为 cube 原语 |
| `docs/surface-cube.md:77,100,121,138` | 多处 v7/v8 / v9-only | 增加指向本契约的说明 |
| `docs/architecture/README.md:35` | 称热伤害仍未来工作 | 按新尾注修正 |

**处置原则**：不改写历史验收记录的含义；在过时处加一行指向本契约的说明，并更新“当前状态”表述。

## 6. 退出条件（G0）

- [x] V 表：五条版本序列各自的值、位置、语义、推进条件齐全且无矛盾
- [x] I 表：四种维度的身份与五条不变量明确
- [x] O 表：状态所有者、生命周期、禁止事项明确
- [x] C 表：预算值、饱和语义、升策空间明确
- [x] 冲突文档已列出并给出处置
- [x] DSL 诊断字段已定义并实现（W0.5，见下）
- [x] 冲突文档已实际修改

**G0 状态：关闭（2026-10-22）。** 唯一未完成的是新 DSL 断言的真实图形验证——
本环境的 raylib 只链接 GLX、没有 Wayland 后端，`:0` 也不可达，图形路线无法运行。
该断言已写入 `tests/test_surface_topology_e2e.sh`，需在可用 Hyprland 环境中补跑。

## 7. W0.5：DSL 诊断字段定义

现有 `stream.*` 只覆盖旧网格。cube 激活需要新增一组**独立**字段，避免与既有语义混淆：

| 字段 | 类型 | 含义 |
|---|---|---|
| `stream.cube.active` | bool | cube 覆盖策略当前是否驱动流式 |
| `stream.cube.requested` | number | 本次覆盖请求的列数 |
| `stream.cube.admitted` | number | 通过容量准入的列数 |
| `stream.cube.ready` | number | 已生成且可绘制的列数 |
| `stream.cube.pending` | number | 已提交未完成的列数 |
| `stream.cube.missing` | number | 未提交或不可用的列数 |
| `stream.cube.saturated` | bool | 请求数是否超过准入容量 |
| `stream.cube.submitted` | number | 本次新增提交数 |
| `stream.cube.released` | number | 本次驱逐数 |
| `stream.cube.mesh_ready` | number | 已完成网格的列数 |

**约束：**

- 这些字段在 cube 未激活时全部返回 `0` / `false`，不得复用 `stream.surface_ready`。
- `saturated=true` 时，任何测试**不得**断言 `requested == admitted == ready`。
- 字段命名固定，后续新增只能追加，不能改义。
