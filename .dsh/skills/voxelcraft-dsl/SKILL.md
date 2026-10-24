---
name: voxelcraft-dsl
description: >-
  给 Voxelcraft 的 debug DSL 加命令或运行时字段：解析器、dispatch、截图报告、
  文档与四层测试的完整改动路径。当缺少需要的调试能力、要读新状态、或要改
  DEBUG_CONTROL/DEBUG_SCRIPT 回复格式时使用。Use when extending the debug DSL
  with a new command or field, or when changing debug reply formats.
metadata:
  version: "2"
---

# 扩展 debug DSL

缺能力就扩展 DSL，**不要改用 hyprctl 或桌面截图**。扩完必须更新 `docs/debug-dsl.md`。

## 先决定你要加的是哪一类

| 你要的 | 改哪里 |
| --- | --- |
| 新命令（做动作、改状态） | `src/core/debug_control.{h,c}` 解析 → `src/app/game_debug*.c` 执行 → `DebugControlReply` 回复 |
| 新运行时字段（`assert`/`wait` 可读） | `src/app/game_debug.c` 的 resolver 表 → 文档命名空间表 |
| 截图报告新字段 | `src/app/screenshot.c` 的 `REPORT_LINE` → 升 `format.version` |
| 新语句/表达式语义 | `src/core/debug_dsl*.c`（很少需要，改动面最大） |

## 加一个命令：五步

1. **解析**：在 `src/core/debug_control.c` 找到对应域的函数（`DebugControlParseBasic`、`ParseMotion`、`ParseStreamAudit`、`ParseBlock`、`ParseFlora`、`ParseGeology`、`ParseWeather`、`ParseFluid`、`ParseView`、`ParseShip`、`ParseEvolution`、`ParseMarker`）。在 enum `src/core/debug_control.h` 里加 `DEBUG_CONTROL_COMMAND_*`，把参数存到 `DebugControl` 结构体的字段。
2. **校验要严**：`sscanf` 只数返回值不够。`%u` 会把 `-1` 包成 `4294967295`，`%c` 用来抓尾随字符。数字范围、枚举拼写、多余参数都要显式拒绝并返回 `DEBUG_CONTROL_COMMAND_INVALID`。
3. **执行**：在 `src/app/game_debug.c` 的 `GameDispatchDebugCommandValue` 分发链里接上。按域放进对应的 `GameDebug*Dispatch`。**注意 `game_debug.c` 有 2100 行上限**（`make test-architecture` 强制），接近上限时把新域拆成独立 `game_debug_<domain>.{h,c}`，像 `game_debug_world.c` 那样——由 `game_debug.c` 调一个 `bool GameDebug<Domain>Dispatch(game, command)`。 <!-- volatile-counts:allow -->
4. **回复**：用 `DebugControlReply`，格式 `DEBUG_CONTROL <域> <ok|error> key=value...`。命令失败时调用 `GameDebugMarkCommandError` 让 DSL 块中止，不要只打日志。
5. **文档 + 测试**：见下。

新模块要同时补 `tests/test_architecture.sh` 的归属断言，否则下一个人会把逻辑搬回 `game_debug.c`。

## 加一个运行时字段：两步

在 `src/app/game_debug.c` 的 `gameDebugDslResolvers[]` 里找到负责的 resolver（`GameDebugDslResolveCore`/`Player`/`Stream`/`Lod`/`Weather`/`Hydrology`/`Topology`/`Block`/`Flora`/`Evolution`/`EcologyStorage`/`Tail`，或 `<Domain>Resolve`）。加 `strcmp(name, "域.字段") == 0` 分支并返回 `GameDebugDslBool`/`Number`/`String`/`Vec3`。

- resolver 顺序是历史顺序，**新增字段不要插到别的域前面去抢名字**。
- 字段必须在游戏状态无效时有确定值（返回 `false` 表示不存在，或返回带默认含义的值），不要返回未初始化内存。
- 命名用 `域.字段`，域前缀必须已在文档命名空间表里。

## 截图报告字段

`src/app/screenshot.c` 的 `REPORT_LINE(...)`。加字段就升 `format.version`——读取方（`voxelcraft_run`、各个 e2e）可能按版本判断。报告是 `key=value` 文本，布尔写 `true`/`false`，向量写 `x,y,z`。

## 文档

`docs/debug-dsl.md` 同步更新，别只改代码：

- 命令加进 `## Command catalog` 下对应小节，写清参数范围、合法游戏状态、回复格式。
- 运行时字段加进 `## Runtime namespaces` 的表。
- 生命周期/退出语义变了就改 `## Process and stdin lifetime`。
- 新增错误码写进 `## Troubleshooting`。

先 grep 找到对应小节再改，不要通读整个文件。

## 测试四层，按改动选

```sh
make test-debug-cli            # 参数/启动错误，不开窗口
make test-debug-stdin-protocol # stdin block 顺序/错误，不开窗口
make build/normal/tests/test_debug_control && ./build/normal/tests/test_debug_control
make test-architecture         # 边界与归属
```

| 改动 | 必跑 |
| --- | --- |
| 解析器（新命令、新参数范围） | `tests/test_debug_control.c` 加用例 + `make test-debug-cli` |
| 执行/dispatch | `voxelcraft_run` 跑一个真实场景，或用 stdin 发一条命令 |
| DSL 语法/语句 | `tests/test_debug_dsl.c` |
| stdin 协议 | `tests/test_debug_stdin_protocol.sh` |
| 截图报告 | 对应 e2e（如 `make test-cga-previews-e2e`） |

解析器的负例要覆盖：缺参数、多参数、越界、错误大小写、只差一个后缀。`%u` 的负数回绕是真实踩过的坑。

## 别做的事

- 用 `hyprctl` 或外部截图工具代替 DSL。
- 只改代码不改 `docs/debug-dsl.md`。
- 在 `game_debug.c` 里继续堆到超过 2100 行再想怎么拆。 <!-- volatile-counts:allow -->
- 改 `DEBUG_CONTROL`/`DEBUG_SCRIPT` 的既有回复格式而不检查 `scripts/run-scene.sh` 和 `voxelcraft_run` 是否在解析它——`run-scene.sh` 依赖 `^DEBUG_CONTROL capture ok ` 和 `^DEBUG_SCRIPT error` 两个前缀，`voxelcraft_run` 依赖 `manifest.txt` 的字段。
