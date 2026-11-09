# ADR-003: 存档只接受 V30，写入走原子替换与事务读

## 状态
已采纳

## 日期
2026-10-27（记录日期）

## 背景

存档是整世界的序列化结果，包含地形版本、种子、canonical `SurfacePose`、各维度完整 `WorldCell` 编辑、生态时钟、火山与熔岩运行时和库存余数。字段布局演进到 V30；`src/world/save_format.h` 保留 V18–V29 枚举值，只用于历史子系统夹具，游戏读取端只接受 V30。

两条硬约束决定了这个格式。第一，读端的每个字段都必须对应一个确定的语义，一旦字段布局变了，旧文件的字节流就无法再被同一套读取代码解释。第二，读过程会先动运行时状态再继续解析，中途失败时世界已经改了一半，不能只报个错就留在半成品状态。

写入侧还有一条：存档路径上已经有一份旧文件时，写失败不能毁掉它。《AGENTS.md》也写明当前是开发阶段，任何修改都不必考虑与旧行为的兼容。

## 决策

游戏存档只写 V30、只读 V30，读到 V29 及更早版本直接拒绝，不做迁移；写入用“临时文件 + fsync + 备份 + rename”原子替换，读取用“checkpoint + 失败重放”的事务。

`WorldSaveReadHeader` 先比对 `VOXELCRAFT_SAVE_V30` 这个 19 字节 magic，不匹配就返回 `"Load failed: unsupported save format. V29 and older are retired; start a new world."`。magic 之后是 24 字节的 `TOPOL002` 块，逐表面记录 home/planets topology。V30 读档不再依赖进程启动时打开的 cube debug gate；读取策略后由事务安装阶段打开对应后端，使 fresh process 可以直接恢复 cube Homeworld。

写头部的 `WorldSaveWriteHeader` 只要求策略可表达：home 与 planets 可以各自 legacy 或 cube，也可以混用。写档忠实记录当前策略；读档事务负责安装并打开所需后端。

`SaveIoWriteAtomic` 在目标路径旁 `mkstemp` 出 `<path>.tmp.XXXXXX`，写完后 `fflush`、`fsync`、`fclose`，再用同样方式把旧文件复制成备份，最后 `rename` 临时文件到目标并 `fsync` 父目录。`SaveIoWriteAtomic(path, backupPath, writer, context)` 的 writer 回调是 `GameSaveWriteFile`，`SaveIoReadTransactional(source, checkpointWriter, reader, context)` 的 checkpoint 就是同一个 writer。

`SaveIoReadTransactional` 先用 checkpointWriter 把当前世界写进 `tmpfile()`，再让 reader 读源文件。reader 失败时清掉错误标志、`rewind` checkpoint，再用同一个 reader 把快照读回去，于是“读失败”和“回滚失败”是两种不同的结果码。

## 备选方案

### 备选 1：读端接受 V18–V28 并在载入时迁移
- 优点：旧存档还能打开，玩家不必重开世界；版本升级对玩家不可见。
- 缺点：每多留一版，读取路径就多一条分支，而分支只有在旧文件真的存在时才被执行到，测试很难覆盖全部组合；`WorldSaveFormatHas*` 那组谓词已经是这种分支的形态，`tests/test_save_format.c` 要用一串断言才钉住哪个版本有哪些段。
- 未采用原因：字段布局、拓扑语义与地形版本可以独立演进，任何一条变化都会让旧字节流失去唯一解释。`docs/cube-contract.md` 的版本矩阵写明存档推进条件是“拓扑或字段布局改变即升到 V30”，读端只接受当前值。仓库保留 V18–V28 枚举值是为了给子系统测试造历史夹具，不是为了兼容。

### 备选 2：原地覆盖写目标文件
- 优点：实现最短，一次 `fopen(..., "wb")` 加一串 `fwrite`；不需要临时文件和备份。
- 缺点：写到一半失败，旧存档已经截断，磁盘上只剩半份文件；磁盘满、断电或进程被杀都会走到这一步。
- 未采用原因：`SaveIoWriteAtomic` 的整个结构就是为了让“写失败”退化为“什么都没发生”。`GameSaveMap` 在失败时只提示 `"Save failed: existing save was kept intact."`，这句话只有在旧文件真的没被动过时才成立。`tests/test_save_io.c` 的 `TestFailedWritePreservesDestination` 直接钉住这一点。

### 备选 3：保留备份，但读失败不回滚
- 优点：写侧仍有旧文件保护，读侧实现简单，失败了就提示玩家重开。
- 缺点：`GameLoadApplyFile` 会先 `DrainChunkGen`、安装拓扑、写地形版本、同步表面快照，再逐段读扩展载荷；中途某个段失败时，世界已经是新旧状态混合体，此时只报错会让玩家停在一个不属于任何存档的状态里。
- 未采用原因：把“失败后仍可用”当作必须项。`SaveIoTransactionResult` 把结果拆成 `SAVE_IO_TRANSACTION_OK`、`CHECKPOINT_FAILED`、`READ_FAILED`、`ROLLBACK_FAILED` 四种，`GameLoadMap` 对每一种都给出不同提示，其中 `ROLLBACK_FAILED` 明说 `"Load failed and the previous game state could not be restored."`，这条提示只有在尝试过回滚之后才有意义。

## 后果

- 存档格式版本与地形生成版本互不影响：V30 文件里另存一个地形版本字段，载入时校验它落在 `TERRAIN_GENERATION_MIN` 到 `TERRAIN_GENERATION_CURRENT` 之间，和存档版本不是同一个数。
- 载入被拒绝时不产生副作用：`GameLoadMap` 先查 magic 和拓扑，再 `fseek` 回 0，然后才取 checkpoint。头不对就直接关文件，运行时的瞬态状态没有被重置。
- 载入前先用 `fstat` 检查大小上限 `SAVE_MAX_FILE_BYTES`（256 MiB），超限直接报 `"Load failed: save file is too large or unreadable."`，避免为一个坏文件先付一次 checkpoint 的代价。
- 读取时 `GameLoadApplyFile` 把格式常量写死为 `WORLD_SAVE_FORMAT_V30`；扩展 payload 谓词在游戏路径上恒为真，只服务于历史夹具。
- 备份是一次完整复制，`CopyFileAtomic` 走临时文件加 rename，并且源文件不存在时返回真，因此首次存档没有旧文件也能成功。
- 代价是每次存档要写两份数据并 `fsync` 两次，存档越大越慢；这是换取“旧文件不被写坏”的成本。
- 代价是拒绝旧存档意味着旧世界直接打不开，只能看到那句提示并开新世界。开发阶段接受这个代价。
- 仓库仍容忍的一点：`save_format.h` 继续保留 V18–V28 枚举值与 `WorldSaveFormatHas*` 谓词，它们描述的是历史夹具而不是可读格式。现状见 [STATUS.md](../../STATUS.md)。
- 验证方式：`make test` 覆盖 `test_save_io`、`test_save_format`、`test_game_save_spherical`；`make test-cube-save-reload` 验证 codec 跨进程；`make test-cube-v30-reload-e2e` 验证 fresh process 从 V30 恢复 cube Homeworld；`make test-v30-cell-reload-e2e` 验证 plant/water 完整 cell 与 undo/redo。

## 证据
- `src/world/save_format.h`：`WorldSaveFormat` 枚举、`WORLD_SAVE_FORMAT_V30`、历史枚举值注释
- `src/world/save_format.c`：`SAVE_MAGIC_V30`、`TOPOL002`、逐表面 topology 校验、V29 及更早拒绝
- `src/world/cube_address_save.h/.c`：`CUBADDR2`、完整 `WorldCell` 记录、无效 volume/solid-water 拒绝
- `src/app/game_save.c`：canonical `SurfacePose` 编解码、fresh-process topology 安装、统一 cell edit block、volume-aware undo/redo
- `src/core/save_io.h`：`SaveIoWriter`、`SaveIoReader`、`SaveIoTransactionResult`、两个函数注释
- `src/core/save_io.c`：`SyncParentDirectory`、`CopyFileAtomic`、`SaveIoWriteAtomic`、`SaveIoReadTransactional`
- `src/app/game_save.c`：`SAVE_FILE_BAK`、`SAVE_MAX_FILE_BYTES`、`GameSaveMap`、`GameSaveWriteFile`、`GameSaveCheckpoint`、`GameLoadApplyFile` 的 `WORLD_SAVE_FORMAT_V30`、`GameLoadMap`
- `src/core/config.h`：`SAVE_FILE`
- `tests/test_save_io.c`：`TestAtomicReplacement`、`TestFailedWritePreservesDestination`、`TestTransactionalReadCommits`、`TestTransactionalReadRollsBackPartialMutation`、`TestTransactionalReadReportsRollbackFailure`
- `tests/test_save_format.c`：V29 与更早 magic 被拒绝，V30 TOPOL002 头通过
- `tests/test_cube_v30_reload_e2e.sh`、`tests/test_v30_cell_reload_e2e.sh`：fresh-process cube 与完整 cell/undo 回放
- `docs/cube-contract.md`：版本矩阵里的 V30 契约
