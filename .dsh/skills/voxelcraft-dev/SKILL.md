---
name: voxelcraft-dev
description: >-
  Voxelcraft（C99/raylib 体素游戏）开发工作流：构建、分层测试、用 debug DSL
  在真实 Hyprland 窗口跑场景、读取 PNG+TXT 截图报告、复现视觉 bug、扩展 DSL、
  按阶段提交。Use when working in the Voxelcraft repo on CGA structures,
  terrain/landform, volcano, rendering, or any change that needs an in-game
  capture; also when triaging a bug screenshot.
metadata:
  version: "1"
---

# Voxelcraft 开发与视觉验证

## 先看哪里

- 项目规则：`AGENTS.md`。要点：DSL 优先、不要用 hyprctl 调试；缺功能就扩展 DSL 并更新文档；每完成一个阶段要 commit；用户提交游戏画面/bug 时先复现，复现不了就说明可能原因；尝试失败要直说；开发期不用考虑旧兼容。
- DSL 语法、命令、字段：`docs/debug-dsl.md`。不要每次通读 1686 行；先用 grep 找当前命令/字段，扩展时再读对应段。
- 阶段记录：`docs/architecture/README.md`；当前工作：`todo/00-roadmap.md`。
- 构建与测试目标：`Makefile`、`mk/tests.mk`。
- 隔离运行场景的现成写法：`tests/test_cga_previews_e2e.sh`。

## 代码边界

| 目录 | 职责 |
| --- | --- |
| `src/core` | 中性基础设施、插件宿主、公共值类型；不能包含高层域头 |
| `src/world` | 地形、区块、网格、天气、水文、火山/灰烬、表面存档 |
| `src/space` | 行星、轨道、光照、空间坐标 |
| `src/ecology` | 生态、种群、演化 |
| `src/gameplay` | 玩家、交互、物品、载具 |
| `src/presentation` | 渲染、音频、视觉派生 |
| `src/app` | 组合根：游戏循环、debug DSL、持久化、回调接线 |

- 公共头是组件契约；实现细节放 `*_internal.h` / `*_dependencies.h`。
- `make test-architecture` 会拒绝绕过边界的 include；不要为了省事直接 include 别的组件私有头。
- `plugins/` 与 `src/core/plugin.h` 是游戏自己的 `.so` 插件，和 DSH 插件无关。

## 快循环

```sh
make -j"$(nproc)"          # 构建 build/normal/voxelcraft
make test-headers          # 每个公共头单独 -Werror 编译
```

- 先跑最接近改动的聚焦目标，例如 `make test-surface-cube`、`make test-cga-house-e2e`。
- 没有别名的 C 单测可以直接构建并运行：`make build/normal/tests/test_cga_voxel && ./build/normal/tests/test_cga_voxel`。
- 提交前至少 `make test-architecture` + `git diff --check`；时间允许再跑 `make test`。
- 内存/UB 敏感改动（区块、网格、存档、生态）加 `make test-sanitize`。
- CI 用 `make test-ci`（`-Werror`，gcc/clang 矩阵）。
- 长测试放后台 job，不要阻塞会话；`make test` 的默认单测超时 120s，`test_entity_ecology` 900s，可用 `TEST_TIMEOUT_SECONDS` / `TEST_ENTITY_ECOLOGY_TIMEOUT_SECONDS` 覆盖。

## 测试分层

| 目标 | 用途 | 备注 |
| --- | --- | --- |
| `test-headers` | 公共头独立编译 | 改头文件后先跑 |
| `test-modules` / `test-plugins` | 组件归档 / `.so` 插件 | 改构建或插件边界时 |
| `test-architecture` | 组件 include/依赖边界 | `make test` 末尾也会跑 |
| `test-<name>` | 聚焦 C 单测 | 例如 `test-surface-cube`；无别名时用二进制路径 |
| `test` | 全量单测 + debug CLI/stdin + architecture | 提交前主门禁 |
| `test-sanitize` | ASan+UBSan，`SANITIZER_LEAKS=1` | 内存敏感改动 |
| `test-ci` | CI 的 `-Werror` 配置 | 合并前 |
| `test-debug-cli` | DSL 参数/启动错误，无窗口 | 改 debug 参数时 |
| `test-debug-stdin-protocol` | stdin block 顺序/错误，无窗口 | 改 DSL 执行器时 |
| `test-e2e` | 完整图形路线 + debug 图形 + chunk diagnostics | 需要 Hyprland |
| `test-cga-previews-e2e` | 5 个 CGA 预览场景，各 180s | 改 CGA 首选 |
| `test-cga-house-e2e` | 房屋预览 | |
| `test-polar-e2e` | 600 帧极区路线，90s wall | 改拓扑/极区 |
| `test-landform-quality-e2e` | 地貌质量截图 | 改地形/landform |
| `test-volcano-e2e` / `test-cloud-altitude-e2e` | 火山 / 云高 | |
| `test-chunk-diagnostics-e2e` | 诊断插件与 stream 快照 | 需要 diagnostics.so |
| `test-animal-pose-e2e` | 动物姿态 | |
| `test-long-run` | 20,000 帧天气长跑 | 改模拟时钟/天气 |
| `benchmark-chunks` | 区块性能门禁 | 改流式/网格 |

## 视觉调试循环（核心）

前置：真实 Hyprland/Wayland 会话、可用 GPU；`WAYLAND_DISPLAY` 必须指向 `XDG_RUNTIME_DIR` 下的 socket。没有 Xvfb/软件渲染回退。

**永远在临时运行目录跑**，不要在仓库根跑：游戏会把 `voxelcraft_save.txt`、`voxelcraft_settings.cfg`、`screenshots/` 写到 cwd，覆盖用户存档。

```sh
repo=$PWD
runtime=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-scene.XXXXXX")
ln -s "$repo/assets" "$runtime/assets"
cp "$repo/tests/scenes/cga_classical_hall.dsl" "$runtime/scene.dsl"
printf '\nexit 0\n' >> "$runtime/scene.dsl"   # 场景以 input manual 结尾时，批处理需要 exit

set +e
(cd "$runtime" && timeout 180 "$repo/build/normal/voxelcraft" \
  --debug-resolution 1280x720 \
  --debug-script "$runtime/scene.dsl" \
  --debug-trace "$runtime/trace.jsonl") > "$runtime/game.log" 2>&1
status=$?
set -e
printf 'status=%s runtime=%s\n' "$status" "$runtime"

rg 'DEBUG_CONTROL capture ok|DEBUG_SCRIPT (error|exit)' "$runtime/game.log"
report=$(find "$runtime/screenshots" -name '*.txt' | sort | tail -1)
sed -n '1,40p' "$report"
```

- 用 `read_image` 看 PNG；PNG 与同名 `.txt` 报告在同一目录。
- `screenshot` 是延迟捕获：先出现 `DEBUG_CONTROL screenshot scheduled`，之后才是 `DEBUG_CONTROL capture ok png=... report=...`。读文件前先等 `capture ok`。
- 场景需要更长的 stream 等待时，按场景调大 `timeout`；长跑放后台 job。
- `input manual` 会保留窗口供你接手；批处理要追加 `exit 0`。
- 不要用 hyprctl/桌面截图代替游戏内 `screenshot`。

## 读截图报告

报告是 `key=value` 文本，开头有 `format=voxelcraft-screenshot-debug` 和 `format.version`。先看这些再判断画面：

| 字段 | 用途 |
| --- | --- |
| `world.seed` | 世界 seed；默认新世界是 `1448040515`（`DEFAULT_WORLD_SEED`） |
| `world.dimension` / `world.surface_id` | home / planet、表面身份 |
| `world.surface_topology` | 当前拓扑；`legacy-map` 或 `none` |
| `world.day_time` / `world.daylight` | 昼夜与光照 |
| `player.position` / `player.yaw_radians` / `player.pitch_radians` | 复现位置与朝向 |
| `camera.position` / `camera.target` / `camera.fov_y_degrees` | 实际相机 |
| `weather.*` | 天气、云、雾、风；会影响画面 |
| `stream.surface_ready` / `stream.audit_complete` | 流式是否就绪 |
| `capture.frame_id` / `capture.simulation_tick` | 捕获帧/模拟 tick |

- 字段可能随开发变化；检查 `format.version`，容忍未知字段，不要把字段名硬编码成唯一判断。
- 比较两张截图前先确认 seed、位置、相机、天气、day time 一致；否则差异不代表回归。

## 复现用户提交的画面/bug

1. 先要 F10 生成的同名 `.txt` 报告，不要只看 PNG。报告里有 seed、位置、朝向、dimension、天气、拓扑。
2. 用报告里的 `player.position` + `yaw/pitch` 生成临时场景：

```text
terrain varied
start
wait until game.screen == "playing" timeout 7200
teleport <x> <y> <z> <yaw> <pitch>
wait until stream.surface_ready timeout 1800
stream wait 600
teleport <x> <y> <z> <yaw> <pitch>
screenshot
exit 0
```

3. 分辨率、view distance、chunk workers 尽量与报告/用户描述一致。
4. 默认 seed 是 `1448040515`。如果报告里的 `world.seed` 不同：DSL 目前没有直接设置 Homeworld seed 的命令；需要从开始界面输入 seed、加载用户存档，或扩展 DSL。行星表面可用 `surface debug planet temperate|desert|ice|lava|crater SEED`，但不能自动从报告映射到行星 profile。
5. 复现不了就直说，并列出可能原因：不同 seed、用户存档里的已有编辑、不同分辨率/设置/view distance、天气或 day time、GPU/驱动差异、画面不在你假设的位置。不要假装修复。
6. 存档/读档 bug：在临时目录跑，绝不覆盖用户存档；当前只支持 V29，V28 及更旧会被拒绝。

## 扩展 DSL

- 缺命令/字段时扩展项目 DSL，不要用 hyprctl。
- 入口通常在 `src/app/game_debug*.c`；共享解析/状态放 `src/core` 或对应的 `game_debug_*` 文件。保持 `DEBUG_CONTROL` / `DEBUG_SCRIPT` 的回复格式和确定性。
- 同步更新 `docs/debug-dsl.md`（命令表、字段表、生命周期说明）。
- 补测试：`tests/test_debug_dsl.c`、`tests/test_debug_cli.sh`、`tests/test_debug_stdin_protocol.sh` 按改动选择；无窗口先跑 `make test-debug-cli test-debug-stdin-protocol`。
- stdin 每个 block 的终端回复是 `DEBUG_SCRIPT complete source=stdin` / `DEBUG_SCRIPT error ...` / `DEBUG_SCRIPT exit code=...`；发送下一个 block 前先消费完上一个的终端回复。细节看 `tests/debug_stdin_protocol.sh`。

## 确定性与性能

- debug 运行使用固定 60 FPS 时钟并关闭 autosave；同样的 seed/位置/分辨率/worker 数/帧序才可比。
- `--chunk-workers auto|1-8` 固定 worker 数；比较时记录实际值。
- `--debug-trace PATH` 写 JSONL；`--debug-trace-frames` 每帧采样。用 jq 比较 `stream.*`、`timing.*`。
- `--perf` 跑性能路线，`--perf-report` / `--perf-baseline` 做 5% 回归门禁；报告 schema 是 v3，旧 baseline 会被拒绝，需要重采。
- `stream wait` 只保证附近表面；远景观测用 `input 0 0 0 0 180` + `wait until player.input_frames == 0 timeout 1200`，不要拿未就绪的 run 做对比。
- `make benchmark-chunks` 是 CI 的性能门禁；`make test-long-run` 覆盖 20,000 帧天气。

## 提交 checklist

- 一个阶段一个 commit；不要混入无关 WIP。
- `git diff --check`；`make test`（或聚焦测试 + `test-architecture`）；图形/流式改动跑对应 e2e；内存敏感改动跑 `test-sanitize`。
- 提交信息用 `area: lowercase imperative summary`，例如 `cga: harden rotated house geometry`、`world: connect cube coverage and projected mesh pipeline`；需要时用 `feat(scope): ...` / `fix(scope): ...`。
- 更新对应文档：`docs/architecture/` 的阶段记录、`todo/00-roadmap.md`、`docs/debug-dsl.md`（如果改了 DSL）。
- 不提交 `build/`、`screenshots/`、`debug-traces/`、save/settings；这些已在 `.gitignore`。
- 如果某个尝试失败，提交说明和回复里都要直说；不要用“应该可以”掩盖未验证。

## 常见坑

- 在仓库根跑游戏 → 覆盖 `voxelcraft_save.txt` / settings / screenshots；用临时目录 + assets symlink。
- 没有 `WAYLAND_DISPLAY`/不在 Hyprland → e2e 直接失败；不要试图用 Xvfb。
- 场景以 `input manual` 结尾 → 进程不退出；批处理追加 `exit 0`，或明确这是交互运行。
- `stream wait` 只保证附近；远景观测先 `input` 等玩家输入帧归零。
- `screenshot` 是延迟的；没等到 `capture ok` 就去读文件会读到旧图/空目录。
- 报告是 key=value；不要只按 PNG 文件名判断场景。
- `test_entity_ecology` 可能跑到 900s；用后台 job，别在会话里干等。
- `make test` 末尾的 architecture 失败是边界违规，不是 flaky。
- 开发期不要求旧兼容，但存档 V29 和既有 block ID 仍被测试钉住；不要随意重编号。

## 参考

- `docs/debug-dsl.md`：DSL 语法、命令、字段、生命周期。
- `docs/architecture/README.md`：阶段记录索引。
- `todo/00-roadmap.md`：当前工作与验收。
- `Makefile` / `mk/tests.mk`：目标与测试清单。
- `tests/test_cga_previews_e2e.sh`：临时目录 + assets symlink 的标准写法。
- `tests/scenes/*.dsl`：现有 47 个场景。
