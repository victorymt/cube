---
name: voxelcraft-debug
description: >-
  在 Voxelcraft 里跑场景、看截图、判断视觉回归、复现用户提交的游戏画面或 bug。
  覆盖 scripts/run-scene.sh 与 voxelcraft_run 工具、PNG+TXT 报告字段、隔离运行目录、
  复现失败时该说什么。Use when running a scene, capturing a screenshot, comparing
  two visual states, or triaging a bug screenshot the user reported.
metadata:
  version: "2"
---

# 跑场景与复现画面

## 前置条件

真实 Hyprland/Wayland 会话，`WAYLAND_DISPLAY` 指向 `XDG_RUNTIME_DIR` 下的 socket，有可用 GPU。没有 Xvfb、没有软件渲染回退。先 `make` 出 `build/normal/voxelcraft`。

## 两种跑法，都走同一个脚本

- `scripts/run-scene.sh <scene>`：人和 CI 的入口。
- `voxelcraft_run`（DSH 工具）：同样的脚本，返回 `ok`、`runDir`、`captures[]`、`logTail`，用 `read_image` 看 `captures[].png`。

```sh
scripts/run-scene.sh cga_house                    # 单个场景
scripts/run-scene.sh --run-dir .cubetest/run1 cga_house
scripts/run-scene.sh --timeout 300 volcano_live
scripts/run-scene.sh --interactive cga_house      # 不追加 exit，留着窗口接手
scripts/run-scene.sh --scene-file /tmp/repro.dsl  # 跑 tests/scenes 之外的临时场景
```

`--scene-file` 是复现用户 bug 和跑生成场景用的：文件从任何位置读取，拷进隔离目录后执行，目录本身不被修改。不用为了跑一次性场景往 `tests/scenes/` 里塞文件——那里放的是长期回归场景。

**永远在临时运行目录跑。** 游戏把 `voxelcraft_save.txt`、`voxelcraft_settings.cfg`、`screenshots/` 写到 cwd，在仓库根直接跑会覆盖用户存档。`run-scene.sh` 已经替你做了隔离：它只软链 `assets/`。

场景本身以 `input manual` 结尾时，批处理需要 `exit`；脚本会自动追加 `exit 0`，**但只在场景自己不以 `exit` 结尾时**才追加。不要手工再追加一次——两个 `exit` 会变成语法错误（`exit must be the final top-level statement`）。

## 判断一次运行是否成功

看 `run-scene.sh` 的退出码和 `manifest.txt`，不要只看有没有报错日志。manifest 里：

| 字段 | 含义 |
| --- | --- |
| `ok` | 脚本自己的判定，`voxelcraft_run` 也以它为准 |
| `failure` | 失败原因：`timeout after Ns`、`game exited with status N`、`DSL error`、`scene issues screenshot but produced no capture` |
| `status` | 游戏进程退出码，`124` 是 timeout，`3` 是 DSL 错误 |
| `capture_count` / `report_count` | 截图和报告数量 |
| `expects_capture` | 场景里有 `screenshot` 语句才为 true |
| `git_head` / `git_dirty` | 这次运行对应的提交；`git_dirty=true` 时结果不能当回归基线 |

`expects_capture=0` 的场景（benchmark、history replay、诊断）不产生截图是正常的，不是失败。

`screenshot` 是**延迟捕获**：先出 `DEBUG_CONTROL screenshot scheduled`，之后才是 `DEBUG_CONTROL capture ok png=... report=...`。读文件前先等 `capture ok`。

## 读报告

PNG 和同名 `.txt` 报告在同一目录。报告是 `key=value`，先看 `format=voxelcraft-screenshot-debug` 和 `format.version` 再judge 画面。

| 字段 | 用途 |
| --- | --- |
| `world.seed` | 世界 seed；默认新世界 `1448040515` |
| `world.dimension` / `world.surface_id` | home / planet、表面身份 |
| `world.surface_topology` | `legacy-map` 或 `cube-grid` |
| `world.day_time` / `world.daylight` | 昼夜与光照 |
| `player.position` / `player.yaw_radians` / `player.pitch_radians` | 复现位置与朝向 |
| `camera.position` / `camera.target` / `camera.fov_y_degrees` | 实际相机 |
| `weather.*` | 天气、云、雾、风，直接影响画面 |
| `streaming.surface_ready` | 流式是否就绪（报告字段） |
| `capture.frame_id` / `capture.simulation_tick` | 捕获帧/tick |

字段会随开发变化。检查 `format.version`，容忍未知字段，不要把某个字段名当成唯一判据。

报告字段和 DSL 运行时字段是两套命名：报告写 `streaming.surface_ready`，DSL 里写 `stream.surface_ready`；`stream.audit_complete` 只在 DSL 里存在，不进报告。写脚本时别混用。

**比较两张截图前先对齐 seed、位置、相机、天气、day time**，否则差异不代表回归。

## 复现用户提交的画面/bug

1. **先要 F10 生成的同名 `.txt` 报告**，不要只看 PNG。里面有 seed、位置、朝向、dimension、天气、拓扑。
2. 用报告里的位置和朝向生成临时场景：

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

  第二次 `teleport` 是必要的：等待期间重力会移动玩家。

3. 分辨率、view distance、chunk workers 尽量与报告/用户描述一致。
4. **seed 不一致时**：用 `world seed <报告里的 seed>`，它只在开始界面有效，必须在 `start` 之前。

```text
world seed 1235
start
wait until game.screen == "playing" timeout 7200
assert world.seed == 1235
```

   行星表面另有 seed：`surface debug planet temperate|desert|ice|lava|crater SEED`。

5. 复现不了就**直说**，并列出可能原因：不同 seed、用户存档里的既有编辑、不同分辨率/设置/view distance、天气或 day time 不同、GPU/驱动差异、画面其实不在你假设的位置、场景里那次捕获不是用户看到的那一帧。不要假装修复。
6. 存档/读档 bug：只在临时目录跑，绝不覆盖用户存档。当前只接受 V30，V29 及更旧会被拒绝；fresh process 可从 V30 恢复 cube backend。

## 常见坑

- 仓库根直接跑游戏 → 覆盖用户存档/设置/截图。
- 没设 `WAYLAND_DISPLAY` 或不在 Hyprland → e2e 直接失败；不要试图用 Xvfb。
- 场景以 `input manual` 结尾 → 进程不退出；批处理要 `exit 0`，或明确这是交互运行。
- `stream wait` 只保证附近表面。远景观测要先 `input 0 0 0 0 180` 再 `wait until player.input_frames == 0 timeout 1200`。
- 没等到 `capture ok` 就读文件 → 读到旧图或空目录。
- `make test` 末尾的 architecture 失败是边界违规，不是 flaky。
- `scripts/run-scene.sh` 失败时会打印失败原因、前几条 DSL 错误和最后若干行日志；先读那段，再去翻 `game.log`。

## 确定性与性能对比

debug 运行固定 60 FPS 时钟并关闭 autosave。**只有 seed、位置、分辨率、worker 数、帧序都相同，两次运行才可比。**

- `--chunk-workers auto|1-8` 固定 worker 数，比较时记录实际值（`status` 回复末尾的 `chunk_workers=configured,started,active`）。
- `--debug-trace PATH` 写 JSONL，`--debug-trace-frames` 每帧采样。用 jq 比较 `stream.*`、`timing.*`。
- `--perf` 跑性能路线，`--perf-report` / `--perf-baseline` 做 5% 回归门禁；报告 schema 是 v3，旧 baseline 会被拒绝，需要重采。
- `make benchmark-chunks` 是 CI 的性能门禁；`make test-long-run` 覆盖 20,000 帧天气。

不要拿 `stream.surface_ready` 为 false、或 seed/位置不同的 run 做对比。

## 相关

- 语法/命令/字段：`docs/debug-dsl.md`（不要通读，先 grep 再读对应段）。
- 现在哪些东西能用、哪些还坏着：`STATUS.md`。
- 要加新命令或新字段：加载 `voxelcraft-dsl`。

## Cube geometry replay

`make test-cube-vertical-geometry-e2e` runs
`tests/scenes/cube_vertical_geometry_replay.dsl`. It places one flat vertical
wall, captures it through legacy and cube, and asserts cube activation before
the second screenshot. Use this route when changing section transforms, model
bounds or mesh coordinate conventions; the exhaustive per-vertex invariant is
in `test_cube_generation`.

V30 persistence routes:

- `make test-surface-topology-e2e` covers topology headers, V29 rejection,
  late-payload rollback and Homeworld/planet/space round trips.
- `make test-cube-v30-reload-e2e` saves cube Homeworld through the debug entry,
  then loads it in a fresh process without `surface debug cube`.
- `make test-v30-cell-reload-e2e` exercises plant+water complete cells and
  volume-aware undo/redo before a fresh load.
