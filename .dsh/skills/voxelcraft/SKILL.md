---
name: voxelcraft
description: >-
  Voxelcraft 仓库的入口路由：C99/raylib 体素游戏，跑在 Hyprland 下，用项目内
  debug DSL 做确定性视觉验证。当任务涉及构建、跑场景、复现用户截图里的 bug、
  扩展 debug DSL、改地形/CGA/火山/渲染/生态、写或改文档、按阶段提交时先读这个，
  再按下面的指引加载对应的专项 skill。Use when working anywhere in the
  Voxelcraft repo and you are not sure which workflow applies yet.
metadata:
  version: "2"
---

# Voxelcraft：先判断这是哪类任务

这个仓库的每个任务都属于下面五类之一。先分类，再加载对应 skill，不要凭印象直接改代码。

| 任务 | 加载 |
| --- | --- |
| 跑一个场景、看截图、复现用户报的画面/bug、判断画面是否回归 | `voxelcraft-debug` |
| 给 debug DSL 加命令或字段、改 debug 执行器、改截图报告字段 | `voxelcraft-dsl` |
| 写/改文档、更新现状、清理重复陈述、提交前检查 | `voxelcraft-docs` |
| 新增/修改/评审测试，或测试名与断言不符 | `voxelcraft-tests` |
| 改玩法/地形/渲染/性能/测试矩阵本身 | 本文件 → `voxelcraft-debug`（验证方式）+ `voxelcraft-tests`（测试声明） |

## 任何任务都先确认的三件事

1. **项目规则在 `AGENTS.md`**：DSL 优先，不要用 hyprctl 调试；缺功能就扩展 DSL 并更新 `docs/debug-dsl.md`；每完成一个阶段要 commit；用户提交游戏画面/bug 时先复现，复现不了就直说可能原因；尝试失败要直说；开发期不考虑旧兼容。
2. **当前状态只在 `STATUS.md`**。`README.md`、`todo/`、`docs/architecture/` 都不再复述现状。判断"这个功能现在到底能不能用"只查 `STATUS.md`。
3. **文档规则在 `docs/documentation-rules.md`**，由 `make check-docs` 强制。改文档前先读，不要靠猜。

## 目录职责

| 目录 | 职责 |
| --- | --- |
| `src/core` | 中性基础设施、插件宿主、公共值类型 |
| `src/world` | 地形、区块、网格、天气、水文、火山、表面存档 |
| `src/space` | 行星、轨道、光照 |
| `src/ecology` | 生态、种群、演化 |
| `src/gameplay` | 玩家、交互、物品、载具 |
| `src/presentation` | 渲染、音频、视觉派生 |
| `src/app` | 组合根：游戏循环、debug DSL、持久化 |

`make test-architecture` 会拒绝跨边界 include。公共头是契约，实现细节放 `*_internal.h`。

## 快循环

```sh
make -j"$(nproc)"      # build/normal/voxelcraft
make test-headers      # 每个公共头独立 -Werror 编译
make test-architecture # include/依赖边界
make check-docs        # 文档门禁
```

改动越接近哪层，就跑那层的聚焦测试，例如 `make test-surface-cube`、`make test-cga-house-e2e`。没有别名的单测直接构建二进制跑：`make build/normal/tests/test_cga_voxel && ./build/normal/tests/test_cga_voxel`。

长测试（`make test`、`test-sanitize`、`*_e2e`）放后台 job，不要阻塞会话。

## 测试矩阵：按改动选目标

| 目标 | 用途 | 何时跑 |
| --- | --- | --- |
| `test-headers` | 每个公共头独立 `-Werror` 编译 | 改了头文件 |
| `test-modules` / `test-plugins` | 组件归档 / `.so` 插件 | 改了构建或插件边界 |
| `test-architecture` | 组件 include/依赖边界 | 总是（`make test` 末尾也跑） |
| `test-<name>` | 聚焦 C 单测 | 例如 `test-surface-cube` |
| `test` | 全量单测 + debug CLI/stdin + architecture | 提交前主门禁 |
| `test-sanitize` | ASan+UBSan（`SANITIZER_LEAKS=1`） | 内存敏感改动 |
| `test-ci` | CI 的 `-Werror` 配置 + sanitize + 冷构建根，一条命令对齐 CI | 合并前 |
| `test-clean-root` | 空 `BUILD_ROOT` 上重编 raylib 并跑 `make test`（≈30s 串行 raylib） | 改了 raylib/CFLAGS/构建规则 |
| `test-debug-cli` / `test-debug-stdin-protocol` | DSL 参数与 stdin 协议，无窗口 | 改 debug 参数/执行器 |
| `test-e2e` | 完整图形路线 + debug 图形 + chunk diagnostics | 需要 Hyprland |
| `test-cga-previews-e2e` / `test-cga-house-e2e` | CGA 预览 | 改 CGA 首选 |
| `test-polar-e2e` | 600 帧极区路线，90s wall | 改拓扑/极区 |
| `test-landform-quality-e2e` | 地貌质量截图 | 改地形/landform |
| `test-volcano-e2e` / `test-cloud-altitude-e2e` | 火山 / 云高 | 对应域 |
| `test-chunk-diagnostics-e2e` | 诊断插件与 stream 快照 | 需要 diagnostics.so |
| `test-animal-pose-e2e` | 动物姿态 | 对应域 |
| `test-long-run` | 20,000 帧天气长跑 | 改模拟时钟/天气 |
| `benchmark-chunks` | 区块性能门禁 | 改流式/网格 |

`test_entity_ecology` 可能跑到 900s，用后台 job。`make test` 末尾的 architecture 失败是边界违规，不是 flaky。

## 提交

一个阶段一个 commit。提交信息用 `area: lowercase imperative summary`，例如 `debug: make scene runs report failures and accept a seed`。

提交前至少：`git diff --check`、`make test-architecture`、`make check-docs`，加上改动对应的聚焦测试。图形/流式改动跑对应 e2e；内存敏感改动跑 `make test-sanitize`。

不要提交 `build/`、`screenshots/`、`debug-traces/`、存档和设置，它们已在 `.gitignore`。

## 开发期规则

开发阶段不做旧兼容。存档格式 V29、既有 block ID、`DEBUG_CONTROL` 回复前缀仍被测试钉住，改它们要同步改测试，但不能因为"旧存档读不了"而放弃改动。

如果某个尝试失败了，在提交说明和回复里都直说，不要用"应该可以"掩盖未验证的部分。
