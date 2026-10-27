# ADR-006: 区块流式在后台线程生成，主线程接管上传

## 状态
已采纳

## 日期
2026-10-27（记录日期）

## 背景

表面地形按 16×16 的水平列切块，每个列再按 `SURFACE_SECTION_HEIGHT`（16）切成垂直 section，渲染距离上限 `MAX_RENDER_DISTANCE_CHUNKS` 为 12，槽位池固定 `MAX_ACTIVE_CHUNKS` 个（等于 12×2+1 的平方）。玩家移动一列，焦点附近就有成批的列与 section 需要生成、需要建网格。

生成和建网格都是纯 CPU 计算，但它们必须看到一致的输入。一列被驱逐时，指向它的指针会失效；一个 section 在网格构建期间被编辑过时，用它算出的网格就是旧内容。如果这些工作在渲染帧里做完，帧时间由最坏的那一列决定；如果把它丢给线程却不隔离输入，就会在驱逐与编辑时读到悬空或撕裂的数据。

另有一条结构限制：`InitWindow` 在 `src/app/game.c` 的主循环里调用，OpenGL 上下文归主线程。worker 线程拿不到可用的上下文，因此不能直接上传网格。

## 决策

地形生成、网格构建与贪心合并全部在后台 worker 池里做纯 CPU 计算，主线程只做三件事：提交带快照的任务、消费完成结果、在预算内上传网格。

worker 池的实现是 `src/world/chunks_streaming.c` 的 `ChunkGenWorker`，线程数由 `ChunkWorkerCountForCpu` 决定：显式请求为正时截到 `MAX_CHUNK_WORKER_THREADS`（8）；自动模式下在线 CPU 数不超过 2 就只开 1 个，否则取一半、下限 2、上限 8。同一个池既跑生成又跑网格，worker 每次取活时在 `preferMesh` 之间交替，优先级网格任务永远先取。

输入在提交时拷贝成完整快照。`MeshJob` 自带 `blocks`、`waterVolumes` 与 `boundary` 三块数据，`PrepareMeshJob` 在锁外的主线程里 `memcpy` 出 section 内容，再用 `CaptureSurfaceBoundary` 或 `CaptureCubeBoundary` 采样一圈 halo。`chunks_cube_mesh.c` 的注释写明 halo 只采样一次、worker 不追指针，所以驱逐列不会让任务输入失效。

完成结果要过三道身份检查才允许生效：`chunk->generation`、`section->dirtyStamp` 与 cube 世界代次 `cubeWorldGeneration`。`UploadMeshJob` 逐项比对任务快照与当前槽位，不匹配就丢弃网格并把 section 留在 dirty 状态，等下一次调度。`dirtyStamp` 在每次 `MarkChunkDirty*` 时自增，避免“任务构建期间又被编辑”的丢改动竞态。

上传有明确预算。`ProcessFinishedMeshJobs(double uploadBudgetMs)` 由 `GameUpdateWorldJobs` 每帧以 2.0 ms 调用，循环里最多处理 `MAX_MESH_REBUILDS_PER_FRAME`（10）个任务；超过预算就记 `uploadBudgetDeferrals`、写诊断原因 `upload_budget_exhausted`，并把任务放回完成队列。`ReplaceChunkModel` 是表面区块调用 `UploadMesh` 与 `LoadModelFromMesh` 的唯一位置，它有两个调用方：异步路径的 `UploadMeshJob`，和 worker 不可用时的同步回退 `RebuildChunkSectionMeshSync`。

贪心网格按块类型分层处理。`GREEDY_MESH_MAX_SPAN` 等于 `CHUNK_SIZE`（16），球面 grid 用 `GREEDY_MESH_SPHERICAL_MAX_SPAN`（4），cube-grid 传 1，也就是不合并、逐面输出。`BlockUsesGreedyCubeMesh` 把半透明方块、植物、非 cube 渲染形状以及 torch、album、spaceship、slab、door、stairs、fence、glass_pane 排除在合并之外；`GreedyFaceCellEqual` 只在类型、贴图、角 AO 与局部光照都相同时允许合并，因此合并不改变光照结果。合并后的矩形用 `AtlasRepeatedUVs` 重复贴图，`GREEDY_MESH_UV_STRIDE`（32）是图集的 UV 步长。

## 备选方案

### 备选 1：在主线程同步生成并同步建网格
- 优点：没有共享状态，也没有身份校验；读到的就是当前世界，代码路径最短。
- 缺点：一列的生成与建网格都要算进当帧，移动时帧时间被最坏列决定；大渲染距离下几乎必然掉帧。
- 未采用原因：它作为回退保留而不是主路径。`RebuildDirtyChunkMeshAt` 在 worker 未启动或网格队列饱和时调用 `RebuildChunkSectionMeshSync`，并自增 `syncRebuilds`；`perf.c` 把 `syncRebuilds == 0` 作为报告通过条件，把同步重建当作异常而不是常态。

### 备选 2：主线程分帧摊开做，不用 worker 池
- 优点：只有主线程，不需要互斥与队列；每帧固定做一点，帧时间可控。
- 缺点：摊开的单位是整列或整 section，任务粒度粗，一帧里做一个就到点上；在没有 worker 的机器上，生成吞吐只取决于帧率，暂停或掉帧就直接停摆。
- 未采用原因：预算只解决“每帧做多少”，不解决“每秒算多少”。仓库的做法是把预算放在上传这一端（2.0 ms），把计算放到 worker 端，两者用完成队列解耦，暂停模拟也不影响 worker 继续生成。

### 备选 3：worker 直接上传网格（当时的候选方案）
- 优点：省掉完成队列与主线程上传预算，结果一算完就能画；主线程完全不碰网格数据。
- 缺点：上传要当前 OpenGL 上下文，而上下文由主线程的 `InitWindow` 创建并绑定，worker 里没有可用上下文；即便加上 `glfwMakeContextCurrent` 切换，也会引入 GPU 驱动的线程争用，且与“主线程独占 raylib”的边界冲突。
- 未采用原因：仓库里没有 worker 侧的上传代码，也没有任何上下文切换调用。表面区块的上传链是 `ProcessFinishedMeshJobs` → `UploadMeshJob` → `ReplaceChunkModel` → `UploadMesh`，前两个都在 `src/world/chunks_runtime.c`，`ProcessFinishedMeshJobs` 的唯一调用点是主循环里的 `GameUpdateWorldJobs`；同步回退 `RebuildChunkSectionMeshSync` 也只在主线程被调用。Nether 与 Space 区块另有各自的同步上传路径（`src/world/nether.c` 的区块构建与 `RebuildSpaceChunkMesh`），它们同样只在主线程被调用。

### 备选 4：不做贪心合并，逐面输出
- 优点：实现直白，一个方块可见面就是一个四边形，没有合并条件要维护；调试时面数与方块数一一对应。
- 缺点：面数随体积增长，顶点缓冲与绘制开销都大；同样的地形要多传几倍数据。
- 未采用原因：逐面路径保留成对照，主路径改走贪心。`tests/benchmark_chunks.c` 用 `solidRequest.greedyMaxSpan = 0` 再跑一遍旧路径并输出 `legacy_solid_vertices_per_chunk`，让削减比例由测量得出而不是断言；`tests/test_benchmark_invariants.sh` 钉住该比例。

## 后果

- 生成与建网格不在渲染帧内完成，帧时间由上传预算决定而不是最坏列决定。
- 核数少的机器仍能跑：`ChunkWorkerCountForCpu` 在在线 CPU 数不超过 2 时只开 1 个 worker，其余情况取一半核数，上限 8。
- 任务输入是拷贝，驱逐列不会让在途任务读到悬空数据；代价是每个网格任务持有一份 section 快照与 halo，`UpdateQueuePeaksLocked` 用 `pendingMeshSnapshotBytes` 跟踪这份内存，并有峰值字段供诊断。
- 身份校验让陈旧结果自动作废而不是覆盖新内容；代价是可能白算一遍，`streamingStats.meshCanceled` 与 `generationCanceled` 记录这类浪费。
- worker 侧复用线程本地网格缓冲：`ChunkMeshWorkerScratchBegin` 与 `ChunkMeshWorkerScratchEnd` 包住 `BuildMeshJobPayload`，`MESH_SCRATCH_POOL_SLOTS` 个槽在任务之间保留容量，避免每个任务重新 malloc。
- 网格按优先级分道：`MeshQueueLane` 给优先级且 exact LOD 的任务 0 道，普通 exact 走 1 道，LOD 走 2 道，preload 走 3 道。
- LOD 用滞回避免边界抖动：`ChunkLodSelect` 在 `CHUNK_LOD_EXACT_MAX_DISTANCE`（4）与 `CHUNK_LOD_HALF_MAX_DISTANCE`（8）两侧留 `CHUNK_LOD_HYSTERESIS`（1）的余量。
- 性能不变量由脚本钉住，不由文档口述：`make test-benchmark-invariants` 检查网格快照字节、每列实体顶点数、顶点削减比例与 `sync_rebuilds`，其中 `sync_rebuilds` 从 `ChunksGetStreamingStats().syncRebuilds` 读出，脚本还会拒绝改回字面量。基准的时间数字是带日期的主机快照，见 [terrain-performance.md](../terrain-performance.md)。
- 代价是每帧上传上限固定：队列里堆积的任务要跨帧消化，`upload_budget_deferrals` 会随负载上升，远处区块就绪会变慢。
- 仓库仍容忍的一点：基准只跑网格构建，不跑完整移动路线，所以它不能替代流式与调度测试。范围说明见 [perf 记录](../architecture/09-performance-optimization.md)。
- 现状见 [STATUS.md](../../STATUS.md)。
- 验证方式：`make test-benchmark-invariants`、`make benchmark-chunks`、`make test-chunk-diagnostics-e2e`；单测直接跑 `make build/normal/tests/test_chunk_streaming && ./build/normal/tests/test_chunk_streaming`。

## 证据
- `src/core/config.h`：`CHUNK_SIZE`、`SURFACE_SECTION_HEIGHT`、`MAX_RENDER_DISTANCE_CHUNKS`、`MAX_ACTIVE_CHUNKS`、`CHUNK_GEN_SUBMISSIONS_PER_FRAME`、`MAX_CHUNK_GEN_JOBS`、`MAX_CHUNK_MESH_JOBS`、`MAX_CHUNK_WORKER_THREADS`、`MAX_BACKGROUND_WORKER_THREADS`、`MAX_MESH_REBUILDS_PER_FRAME`、`GREEDY_MESH_MAX_SPAN`、`GREEDY_MESH_SPHERICAL_MAX_SPAN`、`GREEDY_MESH_UV_STRIDE`
- `src/world/chunks_streaming.c`：`ChunkGenWorker`、`ChunkWorkerCountForCpu`、`ChunksConfigureWorkerCount`、`ChunksStartGenThread`、`ChunksRestartGenThreads`、`ProcessFinishedChunkJobs`、`DrainChunkGen`、`UpdateChunks`、`SECTION_GEN_SUBMISSIONS_PER_FRAME` 与 `CHUNK_PRELOAD_SUBMISSIONS_PER_FRAME` 的使用点
- `src/world/chunks_runtime.c`：`PrepareMeshJob`、`SubmitMeshJob`、`UploadMeshJob`、`ProcessFinishedMeshJobs`、`MeshQueueLane`、`RebuildDirtyChunkMeshAt`、`UpdateQueuePeaksLocked`、`RebuildDirtyChunkMeshes`
- `src/world/chunks_mesh_job.c`：`BuildMeshJobPayload`（`GREEDY_MESH_SPHERICAL_MAX_SPAN` 与 cube-grid 的 1 之间选择 span）
- `src/world/chunks_greedy_mesh.c`：`GreedyFaceCellAt`、`GreedyFaceCellEqual`、`BlockUsesGreedyCubeMesh`、`EmitGreedyCubeFaces`、`AcquireGreedyMask`、`ChunkGreedyMeshWorkerScratchShutdown`
- `src/world/chunks_mesh.c`：`ChunkMeshBlockPassesFilter`、`EmitChunkBlocksFiltered` 里的 `useGreedy` 分支
- `src/world/chunks_cube_mesh.c`：`CaptureCubeBoundary` 的 halo 注释
- `src/world/chunks_mesh_scratch.c`：`ChunkMeshWorkerScratchBegin`、`ChunkMeshWorkerScratchEnd`、`MESH_SCRATCH_POOL_SLOTS`、`ChunkMeshWorkerScratchAcquireMesh`
- `src/world/chunks_storage.c`：`ReplaceChunkModel`
- `src/world/chunk_types.h`：`ChunkLodLevel`、`ChunkSection` 的 `dirtyStamp` 注释、`ChunkGenJob` 的 `columnResult` 与 section 暂存注释
- `src/world/chunks.h`：`ChunkPipelineStage`、`ChunkDiagnostics`、`ProcessFinishedMeshJobs`、`ChunkLodSelect` 声明
- `src/core/render_resources.h`：`RenderResourceSnapshot` 与 worker 计数字段
- `src/core/perf_metrics.h`：`PerfStreamingStats`（`meshSnapshotBytes`、`syncRebuilds`、`uploadBudgetDeferrals`、`maxUploadCpuMs`）
- `src/core/perf.c`：`syncPass`、`queuePass`、`threadPass`、`uploadPass` 的判定
- `src/app/game.c`：`GameUpdateWorldJobs` 中的 `ProcessFinishedMeshJobs(2.0)`、`ChunksConfigureWorkerCount`、`InitWindow`
- `tests/benchmark_chunks.c`：`MeasureRound`、`snapshotBytes`、`legacySamples`、`sync_rebuilds` 取自 `ChunksGetStreamingStats()`
- `tests/test_benchmark_invariants.sh`：四项语义不变量与“拒绝字面量”检查
- `docs/terrain-performance.md`：带日期的基准样本与度量范围
- `docs/architecture/09-performance-optimization.md`：三个性能提交建立的边界（规范块索引、优先级队列、worker 本地缓冲）
- `docs/cube-contract.md`：容量契约（625 槽位、饱和是合法状态）
