# 架构总览

这份文档讲清楚三件事：代码分成哪几块、块与块之间允许怎么依赖、一个游戏帧里各块按什么顺序跑。它写的是必须保持的边界，不判断今天什么能用，后者只看 [STATUS.md](../STATUS.md)。

决策背后的取舍记在 [ADR 集](decisions/README.md)，阶段实现与验证证据记在 [架构记录索引](architecture/README.md)。三者冲突时，边界以本文与 ADR 为准，当前状态以 `STATUS.md` 为准。

## 目录与组件

生产代码全部位于 `src/` 下的七个目录，每个目录就是一个构建组件，由 `mk/components.mk` 的 `COMPONENT_NAMES` 枚举，各生成一个独立静态归档 `build/<variant>/modules/<name>.a`。

| 组件 | 职责 | 不负责 |
| --- | --- | --- |
| `core` | 中性基础设施：配置常量、模拟时钟、原子写与事务读、性能计数器、调试通道、插件宿主、中性效果与状态提示、渲染资源计量 | 任何领域知识（方块、区块、行星、生物） |
| `world` | 体素世界：方块分类与编辑存储、区块生成与网格、流式加载、拓扑与表面几何、地形与岩层、天气、水文、辐射、火山与岩浆、流体、下界 | 渲染与音频，玩家操作 |
| `space` | 行星系统：恒星目录、轨道、行星剖面与光照、空间区块、着陆与起飞目标选择 | 世界生成细节，渲染 |
| `ecology` | 生态：养分与种群、演化目录、实体行为与姿态、区域历史 | 实体渲染，区块存储 |
| `gameplay` | 玩家与操作：移动、交互、背包、飞船飞行、相册数据、地图标记、角色与结构物理 | UI 绘制 |
| `presentation` | 表现层：渲染、云与天空、粒子与音频、环境表现合成、各类 HUD 与面板 | 模拟状态的所有权 |
| `app` | 组合根：`GameRun`、帧循环、世界切换、存档编排、调试 DSL 与截图、性能路线 | 领域策略 |

`src/app/main.c` 只有一行函数体，转调 `GameRun`；`make test-architecture` 会拒绝它变长。

## 依赖方向

规则可以从代码里直接读出来，也可以跑 `sh tests/test_architecture.sh` 验证。核心约束有四条：

1. 只允许单向。`core` 不 include 任何上层组件的头；只有 `app` 可以 include `app/` 头；`gameplay` 不 include `presentation/`；`presentation` 不 include `app/`。
2. 局部 include 必须带模块前缀。`#include "world/chunks.h"` 合格，`#include "chunks.h"` 会被拒绝，`raylib.h`、`raymath.h`、`rlgl.h` 除外。
3. 跨域只走窄接口。`world` 读表面状态走 `world/world_surface_snapshot.h`，不直接调 `space` 的高层 getter；`space` 需要 `ecology`、`gameplay` 时只能经过 `space/space_dependencies.h` 这个显式汇聚点；`world` 消费生态运行时只能经过 `world/ecology_hooks.h` 的回调。`world/chunk_types.h`、`world/world_environment.h`、`world/world_extension.h`、`world/world_persistence.h`、`world/chunks.h`、`world/nether.h`、`world/weather_model.h`、`world/weather_impact.h` 不得 include `space/` 或 `ecology/`；`world/chunks.h`、`world/nether.h`、`space/space.h` 不得 include `presentation/`。`world/terrain.h` 内嵌 `PlanetSurfaceSample`，是这条规则的已知例外，也是 `world ↔ space` 值类型循环的来源。
4. 表面消费者只走 `world/surface_contract.h`。identity 用 `SurfaceAddress`，值用 `WorldCell`，连续状态用 `SurfacePose`，chunk/section 用 opaque handle；不得在 ecology、gameplay、presentation 或普通 app 逻辑中按 `SURFACE_TOPOLOGY_CUBE_GRID` 分支或直接调用 legacy map/cube chunk API。`tests/test_surface_contract_boundaries.sh` 校验精确文件 allowlist。
5. 公共头不暴露可变全局。`src/**/*.h`（`*_internal.h` 除外）中禁止出现可写的 `extern` 变量声明。

实测依赖矩阵与头文件扇入用下面的命令查，不要记数字：

```sh
python3 scripts/coupling-report.py
```

截至 2026-10-27，这份矩阵里还有三对互相 include 的组件：`world ↔ space`、`gameplay ↔ space`、`ecology ↔ space`。它们都是值类型循环，不是行为循环：`world/terrain.h` 按值内嵌 `PlanetSurfaceSample`，而 `space/planet_surface.h` 按值内嵌 `VolcanoTerrainSample`；后两对的逆向边只经过 `space_dependencies.h`。判断与代价见 [耦合度复查](architecture/21-coupling-review-2026-10-24.md)。

`world` 是全仓库的共享内核：`world/terrain.h`、`world/world_environment.h`、`world/chunks.h`、`world/world_state.h` 被六个组件读取，`world/surface_topology.h` 被五个组件读取。这让 `world` 的改动重建面很大，也解释了为什么 `world` 的类型层被拆成一个个叶子头：[world 类型解耦记录](architecture/24-world-type-decoupling-2026-10-25.md)。

## 构建结构

Make 是唯一的构建系统。`mk/modules.mk` 按目录通配出源文件列表，`mk/components.mk` 把它们映射成七个归档，`mk/tests.mk` 用 `define_test` 声明每个测试自己的源码闭包，`mk/plugins.mk` 展开 `plugins/manifest.mk` 里的插件条目，`mk/raylib.mk` 负责内置的 raylib，`mk/deps.mk` 是系统依赖的唯一声明处。

构建要点：

- 每个 `.c` 编译成一个对象和一个 `.d` 依赖文件，头部改动按真实依赖传播。
- 可执行文件由七个归档经 `-Wl,--start-group ... --end-group` 链接而成，跨组件调用不需要暴露私有头。
- `BUILD_ROOT` 决定产物根目录，`BUILD_VARIANT`（`normal`/`sanitize`/`coverage`/`ci`）区分标志集，变体之间不共享对象。
- `build/<variant>/.build-config` 记录有效编译配置的指纹，命令行覆盖了 `CFLAGS` 之类的变量才会触发全量重建。
- raylib 是内置的固定版本加校验和，与宿主机装了哪个版本无关，原因见 [ADR-001](decisions/ADR-001-vendored-raylib.md)。
- 可选扩展编译成独立 `.so`，运行时用 `--plugin-dir DIRECTORY` 加载，见 [ADR-002](decisions/ADR-002-plugin-abi-and-services.md)。

## 一个帧的执行顺序

`GameRun` 的主循环在 `src/app/game.c`。每次迭代先取帧时长与调试命令（`GameDispatchDebugCommand`），再调用 `GameUpdateFrame`。顺序本身就是契约：模拟不能看到本帧还没准备好的区块，渲染不能看到半上传的网格。

1. 应用待生效的 worker 重配置，性能路线按帧号驱动玩家。
2. 更新世界流式：提交生成与网格任务，消费已完成的生成结果，在预算内上传网格。
3. 推进固定步模拟：`GameAdvanceSimulationFrame` 经 `SimulationClock` 消费固定步；`GameWorldSimulationUserPaused` 覆盖的暂停、相册、图片导入、背包、地图、着陆过渡与生物图鉴都不消费模拟步。
4. 处理交互与方块编辑，重建脏网格。
5. 合成环境表现、采样天文与光照、更新生态视觉。
6. 插件 `on_update`，随后渲染整帧，插件 `on_render`，最后截图与调试追踪落盘。

模拟时间只从 `core/simulation_clock.h` 的固定步里来，不从 `GetFrameTime()` 直接积分；单帧最多追赶有限步，超出部分丢弃，避免死亡螺旋。约定与限制见 [统一模拟时钟](architecture/00-simulation-clock.md)。

## 线程模型

主线程独占窗口、OpenGL 上下文与所有 GPU 资源。模拟与网格构建在线程里完成纯数据计算，主线程只做上传。

| 线程 | 数量 | 职责 | 边界 |
| --- | --- | --- | --- |
| 主线程 | 1 | 输入、交互、固定步模拟、环境、渲染、上传 | 唯一可以碰 raylib 的地方 |
| 区块 worker 池 | 上限 `MAX_CHUNK_WORKER_THREADS`（8）；默认取 CPU 数的一半，至少 2 | 地形生成与网格构建共用同一个池，网格与生成交替优先 | 只读任务快照，不回写世界状态 |
| 生态种群 worker | 1 | 区域种群演算 | 结果交回主线程提交，可被取消 |

上传有预算：主线程每帧用于网格上传的时间上限由 `ProcessFinishedMeshJobs` 的 `uploadBudgetMs`（当前调用点为 2.0 ms）控制，超预算的任务退回队列并记 `upload_budget_exhausted`。生成上下文是引用计数的不变快照，worker 拿到后不得再读当前世界。设计记录见 [ADR-006](decisions/ADR-006-chunk-streaming-and-mesh-pipeline.md)。

## 状态所有权

几条容易越界的边界，各自有唯一所有者：

- 表面身份与拓扑策略由 `app` 在排空区块 worker 后安装成不可变快照（`WorldInstallSurfaceSnapshot`），`world` 只读。
- 方块编辑的持久化内容由 `world` 提供编解码，`app` 负责调用顺序与事务（`SaveIoWriteAtomic`、`SaveIoReadTransactional`）。跨模块的保存与恢复只允许 `app` 协调。
- 音频、粒子、提示这类表现副作用由领域层发布中性事件（`core/game_effects.h`、`core/game_notice.h`），`app` 在两个帧边界 `EffectDispatchPending` 分发给表现层。领域层直接调 `AudioPlay*` 或 `ParticlesEmit*` 会被边界测试拒绝。
- 渲染资源计量（`core/render_resources.h`）与物理光照值（`world/world_lighting.h`）属于中性数据，`world`、`space` 可以发布，合成与回退在 `presentation`。
- 天气只以物理采样值过界（`WeatherFieldSample`），视觉推导在 `presentation/weather_presentation.c`。
- 物理输入只有一个采集边界：`app/input_source` 调用 raylib 输入 API 并把结果封成当帧不可变的 `InputFrame`；`core/input_event` 定义事件类型、帧快照与独立的消费状态。上层只读事件并标记消费，不各自轮询同一份原始输入。

## 观测与调试边界

调试不是边角料，它是这个仓库的主要验证手段，因此有一条横切的路径：

- `core/debug_control.*` 定义命令面与 `DEBUG_CONTROL ready` 行，命令组由 `DebugControlCommandGroups()` 生成，文档与运行时靠它对齐。
- 启动页的确定性点击探针由 `ui click X Y` 经 `app` 注入；指针、命中与绘制诊断通过 DSL 运行时字段读取，不依赖桌面级点击工具。
- `core/debug_dsl.*` 是解析器与执行器，不含游戏知识；`app` 通过 `DebugDslCallbacks` 注入变量解析与命令执行。语法、命令目录与运行时字段见 [Debug DSL](debug-dsl.md)。
- 截图与同名键值报告由 `app` 写盘（`F10` 或 DSL `screenshot`），性能路线由 `--perf` 驱动并对照基线判定回归，见 [地形性能](terrain-performance.md)。
- 文档门禁把文档当代码验收：登记表、tier、陈旧检测与运行时名字探针，见 [ADR-004](decisions/ADR-004-documentation-truth-gate.md) 与 [文档规则](documentation-rules.md)。

场景脚本在隔离目录里跑，避免覆盖玩家存档与设置，入口是 `scripts/run-scene.sh`。

## 演化中的已知张力

以下三点是当前结构真实存在的成本，改架构时先读它们，不要当作已经解决：

1. `world` 承担共享内核角色。任何共享值类型放错位置，都会变成全仓重建。判断方法是 `python3 scripts/coupling-report.py` 加真实重建半径测量。
2. `GameRuntime` 是 `app` 内部的耦合中心，几乎所有 `app` 功能都要改它。它不泄漏到 `app/` 之外，但让 `app` 的提交面偏宽。
3. 表面拓扑正在从 legacy 经纬网格迁往 cube-grid，迁移期间两套坐标与两套准入策略并存，必须靠运行时门控隔离，见 [ADR-005](decisions/ADR-005-cube-grid-gated-topology.md) 与 [cube 激活契约](cube-contract.md)。

## 怎么核查本文

```sh
sh tests/test_architecture.sh   # 边界规则；失败信息会指出具体文件与规则
make test-headers               # 每个公共头独立编译
make test-modules               # 七个组件归档都能单独构建
python3 scripts/coupling-report.py
make check-docs                 # 本文自身的链接、锚点与散文规则
make test-cube-vertical-geometry-e2e  # Hyprland 下 legacy/cube 同一竖墙回放
```

规则清单本身在 `tests/test_architecture.sh`，本文只解释它为什么这样划；两者不一致时以脚本为准，并修本文。
