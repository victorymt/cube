# ADR-002: 插件走小型稳定 ABI 加版本化只读服务

## 状态
已采纳

## 日期
2026-10-27（记录日期）

## 背景
遥测、诊断这类工具要能单独构建、单独缺席，又不能直接摸领域模块的可变状态，这两条要求把扩展从主链接路径里赶出去。现有构建把源码归进七个组件归档（`mk/components.mk` 的 `define_component`），功能一旦编进去就自动进入 `build/normal/voxelcraft` 的链接，`make -j` 的并行粒度也随之退化。另一条硬约束来自 `tests/test_architecture.sh`：非 app 模块不得 include `app/` 头，`core` 不得依赖更高层模块，所以扩展边界上不能出现 `GameRuntime *` 这类组合根类型。raylib 的类型同样不进边界：`src/core/plugin.h` 不 include raylib，否则一个 `.so` 就和主程序的 raylib 布局绑死。边界因此被压到最小：`VOXELCRAFT_PLUGIN_ABI_VERSION` 为 `2u`，宿主只认一个导出符号 `VoxelcraftPluginGetDescriptor`，插件最多 `VOXELCRAFT_PLUGIN_MAX_COUNT` 个，服务最多 `VOXELCRAFT_PLUGIN_MAX_SERVICE_COUNT` 条。

## 决策
扩展编成独立 `.so`，只依赖 `src/core/plugin.h`：描述符提供标识、语义版本、优先级与可选的生命周期回调，上下文提供不透明宿主指针、日志回调和两个服务钩子，跨插件通讯只走带 ID 与 ABI 版本的只读服务表。边界是只读的：服务接口必须是不可变函数表或值，宿主在激活前校验描述符 ABI 与 `struct_size`，重复插件 ID 与重复服务 ID 都直接拒绝。加载只在 `--plugin-dir` 给出目录时发生，`GamePluginsStart` 先以宿主身份发布 `voxelcraft.game`、`voxelcraft.world`、`voxelcraft.stream`、`voxelcraft.stream.v2` 与 `voxelcraft.hydrology`，再激活插件。任何一步失败都由 `PluginHostClear` 逆序调用 `on_unload` 并 `dlclose`，不留半加载状态。

## 备选方案
### 备选 1：把扩展直接编译进组件归档
- 优点：没有 ABI 版本负担，扩展可以调用任何已链接的函数，调试只有一条链接路径。
- 缺点：扩展与主程序同生共死，无法单独构建、单独缺席；`define_component` 的归档划分会被新依赖搅乱，并行编译退化成一次全量链接。
- 未采用原因：诊断与遥测属于应当可缺席的工具，未给 `--plugin-dir` 时进程里就不该有它们。

### 备选 2：让插件直接拿 GameRuntime 指针或领域全局
- 优点：实现最快，插件能读到一切状态，不必在 `core` 里重复定义快照结构。
- 缺点：`src/core/plugin.h` 属于 `core`，而 `tests/test_architecture.sh` 禁止 `core` 依赖更高层模块，该头因此无法声明 `GameRuntime *` 或任何 `world/`、`app/` 类型；绕开这条规则就得让插件自己 include 主程序内部头，`.so` 与主程序布局绑死。可写全局还让“只读观察”无法保证。
- 未采用原因：`docs/architecture/12-plugin-boundary.md` 要求不削弱公共状态边界，因此只暴露 `VoxelcraftPluginGameService` 这类中性只读表。

### 备选 3：服务接口直接用领域类型，不设独立 ABI 结构
- 优点：不必在 `core` 里重复定义维度、表面类型和快照字段。
- 缺点：`WorldSurfaceContext`、chunk 指针一旦进了 ABI，插件就依赖 world 模块的内部布局；服务升级会连带主程序结构体布局变化。
- 未采用原因：`plugin.h` 用 `VoxelcraftPluginWorldDimension`、`VoxelcraftPluginWorldSurfaceKind` 与定宽标量表达同一信息；需要新布局时另开服务 ID（如 `voxelcraft.stream.v2`），保住 v1 的二进制布局。

## 后果
- 主可执行不链接插件：`mk/plugins.mk` 的 `PLUGIN_HEADERS` 只包含 `src/core/plugin.h`，`plugins/manifest.mk` 每个条目展开成一个独立 `.so` 目标，`make -j plugins` 可以并行编译无关扩展。
- 加载失败是一致的失败：`game_plugins.c` 在失败时把 `processExitCode` 置 2 并关闭宿主，已加载的库全部卸载。
- 代价：ABI 版本要手工维护，描述符的 `abi_version` 不等于 `VOXELCRAFT_PLUGIN_ABI_VERSION` 会被直接拒载；`active` 之后宿主拒绝再注册或再加载，所以没有热重载。
- 代价：插件只能读快照，新增一个可观察量要改 `src/core/plugin.h` 并重编全部插件；服务表与插件表都是固定容量，超限即拒绝而不是扩容。
- 验证方式：`make test-plugins`（构建 `.so` 并把插件目录传给 `tests/test_plugin.c`）、`make plugins`、`make test-architecture`。

## 证据
- `src/core/plugin.h`：`VOXELCRAFT_PLUGIN_ABI_VERSION`、`VOXELCRAFT_PLUGIN_MAX_COUNT`、`VOXELCRAFT_PLUGIN_MAX_SERVICE_COUNT`
- `src/core/plugin.h`：`VoxelcraftPluginDescriptor`、`VoxelcraftPluginContext`、`VoxelcraftPluginService`
- `src/core/plugin.c`：`PluginDescriptorIsValid`、`PluginHostAdd`、`PluginInstanceCompare`
- `src/core/plugin.c`：`VoxelcraftPluginHostLoadDirectory`、`VoxelcraftPluginHostActivate`、`PluginHostClear`
- `src/core/plugin.c`：`PluginServiceIsValid`、`VoxelcraftPluginHostLookup`
- `src/app/game_plugins.c`：`GamePluginsStart`、`GamePluginWorldSnapshot`、`GamePluginHydrologyFeedbackAt`
- `mk/plugins.mk`：`define_plugin`、`PLUGIN_CFLAGS`、`PLUGIN_HEADERS`
- `plugins/manifest.mk`：`define_plugin(telemetry, plugins/telemetry/plugin.c, )`
- `plugins/telemetry/plugin.c`：`TelemetryLoad`、`TELEMETRY_DESCRIPTOR`
- `tests/test_plugin.c`：注册校验、优先级顺序、服务发布与查找、生命周期回调、目录加载分支
- [插件边界记录](../architecture/12-plugin-boundary.md)
- [plugins 目录说明](../../plugins/README.md)
