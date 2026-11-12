# 程序化生成现状分析（2026-11-12）

本文是对当前工作树程序化生成链路的静态审查记录，不是当前状态声明；当前可用性、阻塞项和测试状态仍以 [`STATUS.md`](../STATUS.md) 为准。审查基线为 `f2208c1041d400ba2ea38581f387d55f3074f07a`，并包含审查时工作树中尚未提交的 Homeworld cube-only 改动。除特别说明外，本文没有修改或重新验证游戏逻辑。

## 1. 结论摘要

当前生成系统已经形成一条相当完整的“种子 → 行星/母星物理参数 → 连续表面场 → 地貌排水图 → 体素柱 → 水体与材料 → 结构”链路，主要优点是以纯哈希/纯函数采样为核心，生成输入由快照和版本捕获，chunk 结果通过工作代次校验后才提交。Homeworld 新世界已经默认使用 cube-grid；行星仍走 legacy-map。

本次审查发现四个最值得优先处理的问题：

1. **Homeworld 的 cube 生成路径没有接上植被、地表覆盖和树木。** cube 只生成基础地形、水、岩层、洞穴和 CGA；legacy Homeworld 路径中的 shoreline cover、ground cover 和 Home tree 不会执行，cube 生成也没有调用 ecology/POI decorator hook。由于当前工作树已进一步把 Homeworld 限定为 cube-only，这会让正常新世界的可玩地表缺少植被内容。
2. **CGA 在 cube 面边缘、角点和极区存在明确的内容缺口。** 结构规划仍以 map-space 矩形为输入；无法形成单一 canonical map rectangle 的 cube chunk 会直接保留基础地形并跳过结构。代码注释和迁移计划都已承认该限制。
3. **生成性能和启动成本尚未被现有 benchmark 覆盖，而且首次同步创建存在重复地貌图谱构建。** 世界同步时先准备 `LandformRuntime` 图谱，随后 `TerrainCubeContextCreate` 又构建一份相同输入的地貌图谱；两者都是约 98,304 个粗网格单元的完整构建。现有 `benchmark-chunks` 只测 mesh，不覆盖这两次构建、CGA 规划或 chunk 生成。
4. **Homeworld 的 legacy 与 cube 生成规则存在两套实现。** 高程基准和部分生物群系规则共享，但材料分层、植被和装饰流程已经分叉。当前测试对 cube 的覆盖主要是地形身份、mesh、覆盖和部分 CGA，并未建立“完整内容等价”或“跨 worker 内容哈希”的闭环。

因此，当前系统适合描述为：**地形与水体生成骨架已经可用，拓扑和调度基础较扎实；但“完整世界内容生成”在 Homeworld cube 路径上尚未闭环。**

## 2. 审查范围与方法

### 2.1 关注范围

本次分析覆盖：

- Homeworld 与行星的基础高程、生物群系、气候和海底场；
- 全球排水图谱、河流/湖泊/湿地/沉积/阶地；
- 洞穴、含水层、矿石和岩层；
- 海洋、河流、湖泊及体素水；
- Homeworld 地表植被、行星生态植被；
- CGA 房屋、矿井、地牢、神庙和庭院复合体；
- 火山山体、历史熔岩沉积和实时火山系统的生成边界；
- chunk 生成队列、worker、快照/工作代次校验、cube 覆盖和流式生成。

不把渲染、网格压缩、天气演算、玩家编辑和存档格式本身当作生成算法主体；只在它们影响生成结果或确定性时提及。

### 2.2 方法

采用源码优先审查。知识图谱索引时间为 2026-11-09，早于当前工作树中的若干改动，且 `CgaPlanStructureFeaturesForBounds` 等新函数未进入旧索引；因此本报告中的代码事实均以当前源码为准，不把图谱缺失当作代码不存在。对图谱标记为 partial 的 `src/world/subsurface.c:65` 和 `src/world/chunks_flora.c:263-264` 已直接读取核对。

本次没有重新运行完整测试或 Hyprland 图形回归；文中“已验证”均指仓库中已有的测试/记录，不表示本次重新执行通过。

## 3. 总体架构

### 3.1 版本与输入

生成输入不是一个单独的 seed，而是多条彼此独立的版本/身份序列：

| 序列 | 当前实现 | 作用 |
|---|---|---|
| 地形生成版本 | `TERRAIN_GENERATION_CURRENT == 11` | 选择地貌、历史火山层等语义 |
| cube 基础生成版本 | `TERRAIN_GENERATION_CUBE == 9` | cube 采样原语的语义版本 |
| 表面拓扑 | Homeworld cube-grid/1；行星 legacy-map/1 | 决定坐标、chunk identity 和生成入口 |
| CGA 语法 | 5 / 2 / 3 / 4 / 7 | 每类结构独立演进 |
| 世界 seed | `WorldGetSeed()` / snapshot seed | 所有哈希和行星 profile 的种子 |

版本定义见 `src/world/terrain_generation.h:7-10` 和 `src/world/terrain_cube.h:8-11`。新 Homeworld 在 `src/app/game.c:164-183` 中重置世界并同步 surface snapshot；当前工作树的拓扑策略又进一步限制 Homeworld 只能使用 cube 后端，见 `src/world/surface_topology_version.c:27-35`。

### 3.2 分层数据流

```text
seed + body/profile + generation version
        |
        +--> persistent climate / surface fields
        |       (temperature, moisture, continentality, erosion, ridge, trench)
        |
        +--> landform atlas
        |       6 faces x 128 x 128
        |       priority flood -> watershed -> river/lake/deposit/terrace samples
        |
        +--> column sample
        |       elevation + sea level + bathymetry + biome + water level
        |
        +--> voxel column
        |       bedrock -> soil/sediment -> rock/ore -> cave/aquifer -> liquid
        |
        +--> decoration
        |       legacy Home: ground cover + trees + CGA
        |       legacy planet: shoreline + ecology + POI + CGA
        |       cube Home: base terrain + CGA only
        |
        +--> chunk staging -> worker completion -> mesh / render / collision
```

### 3.3 两条实际运行的生成路径

**Homeworld 新世界：cube 路径。** `WorldSurfaceTopologyPolicyIsSupported` 只允许 Homeworld 的 cube-grid/1。`SurfaceCubeContractCreate` 捕获一个 immutable `TerrainCubeContext`；`TerrainCubeGenerateChunk` 先为每个列采样，确定需要物化的 section 带，再逐 section 调用 `TerrainCubeGenerateSection`，最后执行 CGA。见 `src/world/surface_cube_contract.c:566-589`、`src/world/terrain_cube_chunk.c:183-248`。

**行星：legacy-map 路径。** `GeneratePlanetChunkTerrain` 在 `src/world/terrain.c:1451-1503` 执行完整的 planet column、base、liquid、shoreline、decoration、ecology 和 POI 阶段；planet 的 `SurfaceTerrainAt`/`TerrainHeight` 仍通过 `PlanetTerrainSampleAt` 和 `LandformRuntime` 查询全球地貌。行星的运行时拓扑仍是 legacy-map，见 `src/app/game_environment.c:36-85` 和当前拓扑契约。

## 4. 当前算法细节

### 4.1 基础高程与气候

Homeworld 基础高程由方向噪声构成：continentalness、erosion、ridge、peak、trench、seamount 和 detail 是独立 hash lane 的分形值，海洋/陆地分支分别产生海底和陆地高程；陆地高程由大陆度和山地遮罩组合，并被限制在 8–240 格。见 `src/world/terrain.c:335-381`。Homeworld 的最终 cube 径向值还会经过 `HomeShellElevation`，把较深海底压缩到 `TERRAIN_CUBE_MIN_RADIAL` 附近，见 `src/world/terrain_cube.c:27-34`。

行星先从 seed 生成物理和气候 profile：轨道辐照、axial tilt、挥发物、温室、反射率、重力、旋转、海洋/冰盖/云量、风、火山活动和撞击率。气候分类再选 lava/ice/desert/crater/temperate/gas 风格。见 `src/space/planet_profile.c:119-289`。

静态生成使用 baseline 采样，不读取当前天气；季节性变化通过 `PlanetSampleGlobalSurfaceAtTime` 进入运行时环境采样，而地形生成使用不应用季节的 baseline。这个边界避免“地形 → 天气 → 地形”的递归。相关入口见 `src/world/terrain_planet_sample.c:30-58` 和 `src/space/planet_surface.c:405-420`。

行星表面采样有 5 个主要分形通道，并叠加撞击坑的 impact/ejecta/rim 场；随后按 style 生成 21 种 `PlanetBiome` 之一。见 `src/space/planet_surface.c:160-402`。地形高度再按 style 和 biome 修正 seabed、火山岭、玄武岩平原、冰川、沙丘、撞击盆地等，见 `src/world/terrain_planet_height.c:51-208`。

Homeworld 的生物群系只有 `PLAINS/FOREST/DESERT/SNOW/MOUNTAIN/SWAMP` 六类，主要依据高程、ridge、温湿噪声、坡度和近海低地判定，见 `src/world/terrain.c:424-439`；cube 路径另有一套等价但独立的判定，见 `src/world/terrain_cube.c:152-195`。

### 4.2 全球排水与地貌图谱

`LandformBuildWithInput` 在每面 128×128 的粗网格上构建图谱，总计 98,304 个 cell，每个 cell 的平面尺寸是 32 格。流程是：

1. 采样 `LandformBaseline{elevation, runoff, hardness}`；
2. 以海平面 cell 和满足条件的闭合盆地作为优先队列种子；
3. priority flood 计算 spill，并用 cell id 作为稳定 tie-break；
4. 在保持无环 flood order 的约束下改善下坡路径；
5. 逆序累计汇水面积和 discharge；
6. 由面积/岩性确定河道宽度，并采样湖泊、河岸、洪泛平原、冲积扇和阶地。

核心实现见 `src/world/landform_model.c:115-227`。`LandformSampleAt` 只查询中心 cell 附近 7×7 个粗 cell，将查询点投影到河段，得到最近河道、水面、沉积厚度、材料比例、阶地和湖泊候选；见 `src/world/landform_model.c:314-425`。`LandformCellArea` 使用球面立体角计算单元面积，而不是简单平面面积，见同文件 `50-62`。

Homeworld 和行星使用同一个构建契约，但 strategy 的 capability 不同：

- Homeworld：恒有 drainage/liquid water/sediment，sea level 80，物理半径 6371 km；
- 温带且有液态水的行星：使用行星 profile 的半径、重力、旋转和海洋覆盖；
- 干燥、冰、熔岩或气体风格可返回无 drainage capability，继续走旧的基础高度路径。

见 `src/world/planet_landform_strategy.c:7-42` 和 `src/world/landform_runtime.c:147-158`。地貌 runtime 有 4 个 cache slot，并按 surface kind、body、seed、terrain version、profile fingerprint、physics fingerprint、strategy fingerprint 判等；见 `src/world/landform_runtime.c:31-55`。

### 4.3 洞穴、含水层和岩层

地下采样是按方向缓存的确定性三角函数场。每个柱先转到一个固定的三维方向基，构建 tunnel、chamber、shaft 三类空洞场；`openness >= 0.52` 时形成洞穴。低于局部含水层且 aquifer 阈值满足时，洞穴被标记为 flooded。见 `src/world/subsurface.c:67-193`。

`SubsurfaceColumnAt` 使用 thread-local 单柱缓存，因此同一 worker 的相邻竖向采样成本较低；但不同线程不共享该缓存。`src/world/subsurface.c:46-114`。

材料分三层决定：

- Homeworld：生物群系表土、湿地/沼泽沉积、海底材料、深度岩层和矿石；核心逻辑集中在 `TerrainHomeBaseBlockFromSample`，见 `src/world/terrain_home_materials.c:122-304`；
- 行星：按 style 选择 lava/ice/desert/gas/crater/temperate 子表，再叠加矿石和生物群系表层，入口为 `TerrainGeologyPlanetSubsurfaceBlock`，见 `src/world/terrain_geology.c:311-339`；
- Homeworld 的洞穴和矿石在 `StoneOrCaveBlock` 中组合，见 `src/world/terrain.c:709-736`。

### 4.4 水体和海底

`BathymetrySample` 同时保存 sea level、seabedY、waterDepth、zone 和 material；海底层级分为 coast/shelf/slope/abyssal/trench/seamount。Homeworld 和行星使用不同的深度阈值与材料选择，但都最终映射到 sand/mud/gravel 等方块材料。见 `src/world/terrain.c:183-237`、`318-326`。

legacy planet 的生成顺序是：先采样所有 16×16 列 → 填 base column → 单独填 ocean/lake/wetland liquid → 放 shoreline reed → 选 decoration → ecology → POI，见 `src/world/terrain.c:1451-1503`。这个顺序让水线在植被判断前已经稳定。

Homeworld legacy 也采用先采样所有列、再生成可见 section、shoreline cover、仙人掌、ground cover、树木和 CGA 的顺序，见 `src/world/terrain.c:1832-1925`。

### 4.5 植被与生态

Homeworld legacy 有两套植物路径：

- `HomeGroundCoverBlock`/`HomeTaxonGroundCoverBlock` 放置花、蘑菇、落叶、蕨、苔藓、芦苇、高草等；
- `HomeTreeCandidateAt`/`ShouldPlaceTree`/`PlaceHomeTree` 依据生物群系、taxon、间距和 crown radius 放置树木，并用 `HomeTreeShapeEmit` 生成树干、分枝和树叶。
  见 `src/world/terrain.c:489-845`。

行星生态由 `PlanetEcologyApplyToChunk` 执行：以生态 profile 的 biomass/flora density 和每列 carrying capacity 决定 ground cover 或 alien canopy/crystal/spore/thermal vent，并登记 flora structure 供 mesh 和 flood/风场使用。见 `src/ecology/ecology_flora.c:355-404`。

`Chunk` 最多保存 32 个 `FloraStructureInstance`，见 `src/core/config.h:41` 和 `src/world/chunk_types.h:98-99`。flora mesh 的生成与结构 partition 有独立测试，例如 `tests/test_chunk_atlas.c:855-1103`。

### 4.6 CGA 结构

CGA 规划按固定 anchor spacing 枚举候选：

- 大型庭院复合体：320 格；
- 房屋/矿井：64 格；
- 地牢/神庙：128 格。

候选先做地形适合性检查、地形高差检查和冲突消解；同一个 chunk 最多 16 个 feature，整个 workspace 最多 2048 个 voxel op，classical complex 单独限制 1500 op。见 `src/world/cga_voxel.h:12-19` 和 `src/world/cga_voxel.c:1308-1473`。

冲突消解和操作执行都有稳定排序：feature 按 id/anchor，op 按 stage/priority/feature id/sequence。这样同一输入不会因为 qsort 或 worker 完成顺序不同而改变最终覆盖结果。见 `src/world/cga_voxel.c:98-130`、`1149-1213`。

cube 路径用 `SurfaceAddress` visitor 将逻辑 map cell 投影回 surface cell；跨面结构可以被两个 chunk 分别写入。相关转换见 `src/world/terrain_cube_chunk.c:83-141` 和 `src/world/cga_voxel.c:1216-1249`。但 map-space bounds 不能表示所有面边缘、角点和极区 chunk，`GenerateCubeStructures` 在这些情况下直接返回基础地形。见 `src/world/terrain_cube_chunk.c:104-127`。

### 4.7 火山与历史地形

程序化火山由 `VolcanoTerrainDefine`/`VolcanoTerrainShape` 在球面局部正交基上生成，包含山脚衰减、火山口、放射状沟谷、breached-crater 通道和 foundation blend，见 `src/world/volcano_terrain.c:53-137`。

历史火山层由 `VolcanoHistoryBuild` 在局部切平面网格上运行 deposition model：每个事件供应熔岩、流动、冷却并写入累计厚度、岩石和 flow mask；默认 3 次事件、格宽 4、最多 4 次事件。见 `src/world/volcano_history.c:21-145`。

`VolcanoHistoricalTerrainAt` 对符合条件的干燥固态行星使用 8 项 history LRU 和 4 项 profile cache；候选域跨面/极区统一使用同一中心基。见 `src/world/volcano_history_cache.c:12-208`。文档记录的冷构建成本约为 0.62–1.39 秒/样本，热缓存约 2.5 毫秒，常驻历史结果约 7.57 MiB；这是历史样本数据，不是本次重新测量。

### 4.8 生成调度与确定性

chunk worker 同时消费 generation 和 mesh lane，优先处理 priority mesh；普通情况下生成和 mesh 交替执行。cube job 在提交时保留 `TerrainCubeContext` 引用，并记录 world generation、cube key、chunk generation、scope 和 sectionY。结果完成前必须重新匹配这些身份，失败或过期结果只增加 cancel 统计，不写入目标 chunk。见 `src/world/chunks_streaming.c:104-240`、`479-817`。

cube 的物理覆盖由 `SurfaceCoverageCollect` 在整数 cube chunk key 上 BFS 枚举，按球面中心距离和 patch 最小距离排序，最多准入 625 个 chunk；覆盖观测区分 requested/admitted/ready/pending/missing/submitted/released/duplicates/stale/saturated。见 `src/world/surface_coverage.c:98-130` 和 `src/world/chunks_cube.c:265-333`。

当前固定上限：

- `MAX_ACTIVE_CHUNKS = 625`；
- generation job 64、mesh job 64；
- worker 最多 8；
- cube face 4096×4096，每面 256×256 个 chunk，总计 393,216 个 chunk 地址；
- 每个 `ChunkSection` 的方块数组为 16×16×16 个 `uint16_t`，约 8 KiB；若分配水体体积数组再增加 4 KiB。

相关常量见 `src/core/config.h:4-42` 和 `src/world/surface_types.h:11-16`。

## 5. 验证现状与测试覆盖

仓库已有较强的生成验证基础：

- `tests/test_terrain_scale.c`：Homeworld/行星 baseline、生物群系、岩层、海底、洞穴水、树木形状、landform、阶地、火山历史、生成阶段顺序和样本契约；
- `tests/test_cube_generation.c`：cube context 生命周期、worker queue、极区/面边缘生成、编辑、section、mesh、覆盖准入、CGA 跨面 surface cell 和确定性；
- `tests/test_planet_surface.c`：行星采样确定性、经度 seam、季节、地轴倾角、偏心率、biome domain 和 fractal spherical alias；
- `tests/test_landform.c` / `tests/test_planet_landform_quality.c`：沉积、阶地、河岸、drainage 无环性、face seam 和流域数值检查；
- `tests/test_surface_coverage.c`：按物理投影查询 cube chunk 覆盖和 bounds；
- `tests/test_cga_voxel.c`：feature/op 排序、房屋 expansion、complex op budget 和结构语法；
- `tests/benchmark_chunks.c`：只测 `BuildChunkMeshData` 及相关 mesh emitter，不等价于完整生成 benchmark。

已有测试能证明很多局部不变量，但还不能证明以下内容：

1. Homeworld cube 的完整方块、水体、CGA 和植被内容与 legacy 路径等价；
2. cube 全列内容在不同 worker 数量、chunk 到达顺序和 reload 后字节稳定；
3. cube 面边缘/角点/极区所有结构类型都有实际 feature 而不是静默跳过；
4. 首次 world sync 的 `LandformRuntimePrepareSurface`、`TerrainCubeContextCreate`、CGA 规划和 VolcanoHistory 冷构建的 p50/p95/内存峰值；
5. 活跃 Homeworld cube 路径的 ground cover/tree/flora structure 是否存在，因为现有测试主要验证地形、mesh、覆盖和 CGA。

## 6. 主要风险与缺口

### P0-1：Homeworld cube 缺少完整植被与地表装饰阶段

**事实链：**

- 新 Homeworld 默认使用 cube，且当前工作树只允许 Homeworld 的 cube 拓扑：`src/app/game.c:164-183`、`src/world/surface_topology_version.c:27-35`；
- cube column 只生成 `TerrainCubeBlock` 并执行 CGA：`src/world/terrain_cube_chunk.c:213-248`；
- `TerrainCubeBlock` 只处理 bedrock、水体、历史岩、沉积、地质、洞穴和材料，没有花、草、树或生态结构：`src/world/terrain_cube.c:200-257`；
- `WorldEcologyDecorateChunk` 只在 legacy planet 的 `GeneratePlanetChunkTerrain` 中被调用：`src/world/terrain.c:1497`；cube 路径没有任何 decorator 调用点；
- Home tree、ground cover 和 shoreline cover 只在 legacy `GenerateChunkTerrain` 中调用：`src/world/terrain.c:1832-1925`；
- `GameWorldDecorateChunkWithEcology` 是 hook，但 cube generation 没有任何调用点：`src/app/game_biology.c:35-49`、`src/world/ecology_hooks.c:64-69`。

**影响：** 正常新 Homeworld 的 cube 地表会缺少 legacy 路径中的花、草、芦苇、树和生态 structure；依赖植被的生态、燃烧、风场和视觉验收内容会出现“地形有了但内容没有”的空洞。这个问题比单纯视觉缺失更严重，因为它改变了生态初始条件和可交互资源。

**建议：** 把“装饰”提升为 surface generation 的独立阶段，而不是 legacy chunk 的内部细节。阶段输入必须是 immutable `TerrainCubeContext`，输出应为 `SurfaceAddress`/cell 级别的 deterministic writes，并在 face seam/pole 上有明确的 halo 与去重规则。最小验收应包含：同一 seed 的 cube 与 legacy 完整 column 内容哈希、跨面树的边界写入、极区树/草的生成、reload 后结构数一致。

### P0-2：CGA 在 cube seam/pole 静默缺失

`GenerateCubeStructures` 在 `CubeChunkMapBounds` 失败时直接 `return true`；在 map span 超过阈值时也直接 `return true`；规划 overflow 时再次保留基础地形。见 `src/world/terrain_cube_chunk.c:104-127`。这不是 CGA 语法问题，而是 map-space 规划矩形无法表达 cube 面的拓扑边界。

**影响：** 面边缘、角点和极区不是“结构可能偏移”，而是整类结构不出现；这会让地形种子决定的结构分布在不同 cube 区域不一致，也会让性能验收测到的是较轻的 terrain-only 工作量。`docs/cube-migration-execution-plan.md` 的 M5.3.1 已把该问题列为下一步。

**建议：** 规划输入改为 `SurfaceAddress + chunk key + semantic neighbors + halo`，不要从 map rectangle 反推 cube 范围；feature anchor 和 op scope 都使用稳定 surface identity，最后统一 canonicalize/dedup。测试应覆盖每个相邻面类别、四个角点和两个极区，并比较两次生成的特征集合和最终 cell 集合。

### P1-1：首次世界同步重复构建地貌图谱

`GameSyncWorldSurfaceSnapshot` 先调用 `LandformRuntimePrepareSurface`，见 `src/app/game_environment.c:63-75`；随后 `SurfaceCubeContractCreate` 创建 `TerrainCubeContext`，见 `src/world/surface_cube_contract.c:566-589`；而 `TerrainCubeContextCreate` 在非 flat 且拥有 drainage capability 时直接调用 `LandformBuildWithInput` 构建自己的 atlas，见 `src/world/terrain_cube.c:73-104`。

这两份 atlas 使用同一个 surface strategy/profile/seed，但属于两个独立的堆对象。Homeworld 首次进入世界时会先构建 runtime atlas，再构建 cube context atlas；每次大约 98,304 个粗网格 cell。该路径还会调用 baseline 分形噪声，因此是同步启动的主要候选成本之一。

**建议：** 让 `TerrainCubeContext` 和 `LandformRuntime` 共享同一份 immutable atlas，或者明确取消其中一份消费者路径。共享时必须统一生命周期、版本捕获和 release 顺序；不能简单地让 worker 借用全局指针。加一次冷启动 profiling，分别记录 profile、atlas、context、首个 chunk、history cache 的耗时。

### P1-2：Homeworld 的规则实现已经分叉

Homeworld legacy 与 cube 都使用 `TerrainHomeBaselineAtDirection`，但 cube 自己复制了 biome 判定和方块分层：

- legacy 的 `SurfaceTerrainAt`/`Biome` 分类在 `src/world/terrain.c:393-441`；
- cube 的分类在 `src/world/terrain_cube.c:152-195`；
- legacy 表土/沉积/岩石在 `src/world/terrain_home_materials.c:122-304`；
- cube 自己处理 water、sediment、history、geology 和 cave，在 `src/world/terrain_cube.c:200-257`。

短期看，这让 cube 更快脱离 map row/column；长期看，它会形成“同一规则两份实现”，尤其是海平面、雪线、沼泽判定、海底材料和 cave flooding 规则。当前已有的 `TestCubeAndLegacyHeightsAgree` 只覆盖部分高度关系，不足以覆盖完整内容和装饰。

**建议：** 抽出 surface-neutral 的 `ColumnSample` 和 `ColumnMaterialize` 接口；legacy/cube 只负责拓扑适配和 section 调度，不复制生物群系、材料和水线策略。迁移阶段可以保留 legacy 适配器，但把规则表放在唯一的策略实现中。

### P1-3：CGA 每个 section 重新规划和展开

cube section job 调用 `TerrainCubeGenerateChunkSection`，其中先生成 section base，再调用 `GenerateCubeStructures(context, staging, key, sectionY)`。后者每次都会重新规划 chunk 范围、消解 conflict、扩展所有 feature 的全部 op，最后才用 `CgaVisitSurfaceCells` 过滤到目标 section。见 `src/world/terrain_cube_chunk.c:120-141`、`206-211`。

对于碰撞层附近的多个 section job，同一套 CGA 计划会被重复构建和扩展。结构复杂时这是明显的 CPU 浪费，也让 profile 更容易把 CGA 成本误算成“section base”成本。

**建议：** 按 `(generation, cubeKey, worldSeed, grammar)` 缓存不可变 CGA plan，或者把 plan 和 column job 一起打包；section job 只做 clip/visit。缓存需像 landform atlas 一样有明确版本和失效条件，不能跨 world generation 复用可变 plan。

### P1-4：冷 VolcanoHistory 构建与全局缓存锁

历史火山的首次构建是完整的沉积模型，文档记录样本为 0.62–1.39 秒；缓存查询在全局 mutex 内执行，见 `src/world/volcano_history_cache.c:170-208`。多个 worker 同时遇到未缓存山体时，会有一个构建、其余等待，随后才继续 chunk 生成。

**影响：** 在火山附近首次流式加载时容易产生尖峰；它与 chunk worker 的队列占用相互放大，而且当前 benchmark 不覆盖。

**建议：** 把历史山体的 profile/candidate 选择和 LRU 保护拆开，允许在提交 chunk job 前预热；或者为每个 generation/context 固定一个只读 history snapshot，避免 worker 在生成路径中触发重 IO/重计算。保持“拒绝边界结果而不是发布边缘堤坝”的语义。

### P2-1：规则复杂度集中在少数函数

静态指标显示以下函数是维护热点：

- `TerrainHomeBaseBlockFromSample`：复杂度 30，认知复杂度 128；
- `TerrainPlanetBaseAtGeographic`：复杂度 34，认知复杂度 63；
- `CgaPlanStructureFeatures`：复杂度 31，认知复杂度 77；
- `LandformSampleAt`：复杂度 20，认知复杂度 45；
- `TerrainCubeBlock`：复杂度 13，认知复杂度 52。

这些函数本身分区清楚、无条件跳转依赖，但规则嵌套和分支顺序是生成语义的一部分。任何“整理”都可能改变优先级和生成结果，因此应先补内容哈希和交叉表测试，再考虑数据驱动化。

### P2-2：跨平台 bitwise 稳定性的边界

生成主体使用整数哈希、固定 tie-break 和纯函数，worker 之间的输出顺序不会改变逻辑结果；landform 文档也明确不承诺不同数学库逐比特一致。cube 的 `sinf`/`cosf`、`atan2f`、`sqrtf` 等仍会引入平台差异，尤其在方向投影、噪声坐标和火山 history 网格边界。

**建议：** 把“同平台、同构建、同 seed、任意 worker 数量稳定”作为强测试；跨平台只承诺逻辑等价/容差，不承诺字节哈希。若未来需要跨平台存档可复现，应把关键采样坐标和带符号零规范化到固定点或明确量化点。

### P2-3：内容版本的组合语义仍偏隐式

目前“地形版本 11 + cube 基础 9 + topology/1 + CGA grammar 5/2/3/4/7”是五条独立序列。代码能在 cube context 创建时捕获 terrain version，但测试和文档仍容易出现“v11 等于所有内容版本”的误读。尤其是历史火山只对干燥固态行星启用，Homeworld 的 `!home` 分支不会叠加它；如果文档只写“v11 叠加历史层”，读者容易以为 Homeworld 也参与。

**建议：** 增加一张由代码生成或测试校验的 version capability 表，明确每种 surface/style 实际启用的生成器、装饰器、CGA 和 history 层；报告和文档引用组合名而不是单一数字。

## 7. 设计优点

在指出缺口的同时，当前系统有几点值得保留：

1. **哈希/纯函数优先。** 绝大多数随机性来自 seed、lane 和整数坐标，不依赖全局 RNG 状态，便于重放和并行。
2. **生成上下文不可变化。** cube 的 `TerrainCubeContext` 复制 snapshot，持有 atlas 和 subsurface 参数，worker 不需要读取活动世界；这比 legacy 全局快照路径更安全。
3. **结果提交有代次校验。** chunk generation、cube world generation、surface key、sectionY 和 slot incarnation 都参与匹配，过期结果不会覆盖新世界。
4. **地貌图谱有明确版本/参数隔离。** runtime cache 把 surface kind、body、seed、terrain version 和 physics/strategy fingerprint 一起纳入 key。
5. **水线和材料有阶段顺序。** planet 路径先完成 column sample，再填水，再做 shoreline/flora，避免 flora 看到半成品水线。
6. **CGA 有有限工作区和稳定排序。** feature/op 上限、conflict resolution、stage sorting 和 surface visitor 为跨 chunk 结构提供了可测试的基础。
7. **覆盖与 LOD 不再伪装成“局部 ready”。** `SurfaceCoverageEvidence` 和 M5.2 的 target-LOD readiness 已经把 coverage、mesh、draw 的语义分开。

## 8. 建议的实施顺序

### 阶段 A：先补齐正常 Homeworld cube 的内容闭环

1. 把 Homeworld 的 shoreline cover、ground cover、tree placement 和 ecology decoration 抽成 surface-neutral 装饰阶段；
2. 在 cube column/section 生成完成后、CGA 之前执行；
3. 对 tree/crown 使用 conservative halo，按 canonical `SurfaceAddress` 去重；
4. 在 face edge、corner、pole 场景验证树根、树干、树叶和 ground cover 都落入正确 section；
5. 将 legacy 与 cube 的结构数量、方块内容哈希和 reload 结果纳入同一测试。

### 阶段 B：完成 CGA 的 topology-native planning

把 map rectangle 替换为 cube chunk + semantic neighbor + halo 的规划范围；feature 存储 canonical surface identity；移除 seam/pole/global-wrap fallback；增加全部面边缘类别、四角和两极的 feature/cell 双向断言。该阶段完成前，不应把 terrain-only 的性能结果当作最终 workload 结果。

### 阶段 C：消除重复 atlas 与可量化冷启动

让 runtime 和 cube context 共享同一份 atlas，或删除不再需要的 runtime atlas 路径；为 profile、atlas、context、history cache、CGA plan、首个 chunk 和首个 mesh 分别记录冷/热耗时与内存。保持失败时明确 abort/诊断，不用空 atlas 掩盖错误。

### 阶段 D：建立完整内容确定性基线

建议为每个固定 seed 生成一个 content digest，输入至少包含：

- 每个 column 的 base block；
- `WorldCell.block` 与 `waterVolume`；
- landform kind/water level/sediment；
- CGA feature 集合与 op 集合摘要；
- flora structure kind/taxon/root/bounds；
- chunk section resolved 集合。

分别用 1 个和 8 个 worker、不同 chunk 到达顺序、卸载重载和保存重载运行，要求同一平台结果一致。该基线比当前“某几个局部 API 相等”更能防止内容回归。

### 阶段 E：再做规则数据化和性能优化

在内容哈希稳定后，再把高认知复杂度材料函数拆成表格/策略函数，并优化 CGA plan 缓存和 history 预热。不要在缺少内容基线时先做“看起来更整洁”的规则重排。

## 9. 结论

当前程序化生成的技术底座是扎实的：世界被明确拆成静态基线、地貌排水、体素材料、液体、结构和调度多个阶段；seed、版本、surface identity 和 worker generation 都有显式建模；landform、cave、CGA、火山和历史沉积也都已有独立模型与测试入口。

但当前 Homeworld 的主要运行路径已经切到 cube，而 cube 生成还没有继承 legacy 的植被和装饰内容；同时 CGA 在 seam/pole 仍有已知的内容缺失，M5.4 的性能证据也尚未覆盖真实完整 workload。下一步最有价值的不是增加更多噪声类型，而是把“完整内容生成”作为一等概念从 legacy chunk 函数中抽出来，补上 cube 的装饰阶段、topology-native CGA 和跨 worker 的完整内容哈希。

---

### 附：主要证据文件

- `src/world/terrain.c`
- `src/world/terrain_cube.c`
- `src/world/terrain_cube_chunk.c`
- `src/world/landform_model.c`
- `src/world/landform_runtime.c`
- `src/world/planet_landform_strategy.c`
- `src/world/subsurface.c`
- `src/world/terrain_home_materials.c`
- `src/world/terrain_geology.c`
- `src/world/terrain_planet_height.c`
- `src/world/terrain_planet_sample.c`
- `src/world/volcano_terrain.c`
- `src/world/volcano_history.c`
- `src/world/volcano_history_cache.c`
- `src/world/cga_voxel.c`
- `src/world/chunks_streaming.c`
- `src/world/chunks_cube.c`
- `src/world/surface_coverage.c`
- `src/world/surface_cube_contract.c`
- `src/ecology/ecology_flora.c`
- `src/space/planet_profile.c`
- `src/space/planet_surface.c`
- `src/app/game_environment.c`
- `src/app/game_biology.c`
- `tests/test_terrain_scale.c`
- `tests/test_cube_generation.c`
- `tests/test_planet_surface.c`
- `tests/test_landform.c`
- `tests/test_planet_landform_quality.c`
- `tests/test_surface_coverage.c`
- `tests/test_cga_voxel.c`
- `tests/benchmark_chunks.c`
