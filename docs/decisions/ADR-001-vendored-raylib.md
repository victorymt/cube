# ADR-001: 内置 raylib 而不是探测系统版本

## 状态
已采纳

## 日期
2026-10-27（记录日期）

## 背景

构建需要一份确定的 raylib，主机上装了什么不该参与这个决定。此前用 pkg-config 读系统包，`mk/raylib.mk` 的文件头注释把后果记成三条：pkg-config 报告的版本和编译器实际读到的头文件对不上；`/usr/local` 下手动构建的副本会悄悄遮住包管理器装的那一份；而 gcc 把 `/usr/include` 当系统目录，因而忽略 `-I/usr/include`，从构建侧改不回来。

约束有三条。第一，X11 会话和 Hyprland 要跑同一个二进制，不能被拆成两个构建变体。第二，`BUILD_VARIANT` 会切换 sanitizer 与 coverage 的编译选项，raylib 的编译选项却来自它自己的 Makefile，两者必须解耦。第三，构建的唯一外部输入要可校验，因为 tag 可以被上游移动。

## 决策

项目自己下载、编译并静态链接一份 raylib，版本由 `Makefile` 的 `RAYLIB_VERSION` 固定为 6.0，不再从系统读取。

`mk/raylib.mk` 从 codeload 取 tarball，解包到 `$(BUILD_ROOT)/raylib/raylib-$(RAYLIB_VERSION)`，用 `GLFW_LINUX_ENABLE_X11=TRUE` 与 `GLFW_LINUX_ENABLE_WAYLAND=TRUE` 同时打开两个后端，并以 `RAYLIB_LIBTYPE=STATIC`、`RAYLIB_BUILD_MODE=RELEASE` 编译。GLFW 在启动时自选后端，所以一份库服务两种会话。

解包后会应用 `third_party/raylib-wayland-framebuffer-scale.patch`。Raylib 6.0 只在“Wayland 已启用且 X11 未启用”时关闭 `GLFW_SCALE_FRAMEBUFFER`；本项目同时编译双后端，因此原条件在 Hyprland 的缩放显示器上不会成立。补丁把条件改为只检查 `_GLFW_WAYLAND`，使 Raylib 的窗口、framebuffer 与 Wayland 指针坐标保持同一尺度，避免 UI 绘制矩形和真实鼠标命中区域按不同比例计算。补丁摘要保存在 raylib 源码目录的 stamp 中；补丁变化会强制重建静态库。

库落在 `$(BUILD_ROOT)/raylib` 而不是 `$(BUILD_DIR)`：raylib 的编译选项与外层变体的 sanitizer、coverage 选项无关，一个 build root 只编一次库，各变体分别链接。

编译 raylib 的 recipe 用 `env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL` 剥掉外层的构建变量。命令行传入的 `CFLAGS` 会同时经环境变量和 `MAKEFLAGS` 到达递归 make，而 raylib 的 Makefile 用不带 `override` 的 `CFLAGS =` 赋值，会丢掉自己的 `-D_GLFW_X11` 等定义，最终在 `-Werror` 或 “Cannot disable Wayland and X11 at the same time” 处失败。

## 备选方案

### 备选 1：通过 pkg-config 读系统 raylib
- 优点：不用下载和编译，构建起步快；发行版的安全更新直接生效；工作区里少一份第三方源码。
- 缺点：pkg-config 报告的版本不能证明编译器读到的是同一份头文件；`/usr/local` 下的副本会经默认搜索路径遮住包管理器的版本，而 `-I/usr/include` 被 gcc 忽略，构建无法纠正这个偏差。
- 未采用原因：它把“哪一份 raylib”的答案留给机器，只可能事后报错，不能事前保证。`mk/raylib.mk` 的注释把这串失败记为 “Everything else was tried and failed”。

### 备选 2：保留纯版本探针（`raylib-version-check`）
- 优点：在编译全部目标之前就失败，报错带 pkg-config 路径和编译器实际解析到的头文件，便于定位。
- 缺点：探针只能宣布机器不对，它默认机器最终会被装对，而这一步不受仓库控制；探针自身还要维护一段编译片段，用来检查 `Mesh`、`Model` 的字段布局。
- 未采用原因：改为把 raylib 当构建产物。`Makefile` 现在写的是 `all: $(RAYLIB_LIB) $(TARGET) | raylib-check`，raylib 是前置产物，版本检查退化为系统包可用性提示。要区分的一点是：`raylib-check` 仍在，但它查的是开发包是否齐全，不再回答“是哪一版”。

### 备选 3：单独的 Wayland 构建变体（`make wayland`）
- 优点：主构建完全不动，出问题可以退回；两套产物路径互不覆盖。
- 缺点：要维护两套目录、两套 raylib 和两套链接结果，仍然要求本机装齐 Wayland 与 X11 的开发包。
- 未采用原因：一份 GLFW 可以同时编进两个后端并在启动时选一个，变体带来的隔离抵不上维护成本。该变体的 `mk/wayland.mk` 已随内置化删除，`make wayland` 现在没有对应规则。

## 后果

- 游戏链接的 raylib 由 `RAYLIB_VERSION` 唯一决定。换版本是一次显式选择，要同时改 `RAYLIB_VERSION` 和 `RAYLIB_SHA256`，否则新 tarball 会卡在校验上；源码面向 6.x API。
- 一份 `libraylib.a` 同时含 X11 与 Wayland 后端，X11 桌面和 Hyprland 共用 `build/normal/voxelcraft`。
- Wayland framebuffer 缩放由 tracked patch 修复；补丁内容变更会让 raylib 静态库失效并重新编译，热构建不会继续链接旧坐标行为。
- tarball 是构建唯一的网络输入，用 `RAYLIB_SHA256` 校验。tag 会移动，摘要不会；不匹配时打印实测值，`make RAYLIB_SHA256=` 可跳过校验一次并明确打印 “NOT verified”。
- 代价是首次构建需要网络，或者手工把 tarball 放到 `build/raylib/raylib-$(RAYLIB_VERSION).tar.gz`。
- 代价是 raylib 的编译不走外层 jobserver，串行约 30 秒，每个 build root 一次；`make clean` 会连它一起删掉，下次重新下载并编译。
- 代价是构建机器必须装齐 Wayland、X11 与 GL 的开发包，包含 GLFW 的 X11 后端会无条件 include 的扩展头。缺包由 `make raylib-check` 按名字报出，它遍历 `RAYLIB_PKG_CONFIG_DEPS` 与 `RAYLIB_REQUIRED_PROGRAMS`，并打印对应发行版的安装命令。
- 仓库仍容忍的一点：`raylib-check` 只确认这些包能被 pkg-config 解析、程序能在 PATH 里找到，不校验系统 raylib 的版本；版本由内置路径保证，系统包装的是哪一版无关紧要。
- 验证方式：`make test-clean-root` 在空 `BUILD_ROOT` 上重编 raylib 并跑 `make test`；`make test-build-prerequisites` 检查 README、CI 与 `mk/deps.mk` 的包清单一致；`make prerequisites` 打印本机缺失项。

## 证据
- `Makefile`：`RAYLIB_VERSION`、`RAYLIB_SHA256`、`BUILD_CONFIG_INPUTS`、`all: $(RAYLIB_LIB) $(TARGET) | raylib-check`、`test-clean-root`
- `mk/raylib.mk`：文件头注释（pkg-config 版本不符、`/usr/local` 遮蔽、`-I/usr/include` 被忽略三处失败）、`RAYLIB_URL`、`RAYLIB_TARBALL_STAMP`、`RAYLIB_PATCHES`、`RAYLIB_PATCH_STAMP`、`RAYLIB_HEADER_STAMP`、`$(RAYLIB_LIB)` 规则里的 `env -u MAKEFLAGS`
- `third_party/raylib-wayland-framebuffer-scale.patch`：双后端构建在 Wayland 下关闭 `GLFW_SCALE_FRAMEBUFFER`
- `mk/deps.mk`：`RAYLIB_PKG_CONFIG_DEPS`、`RAYLIB_REQUIRED_PROGRAMS`、`RAYLIB_ARCH_PACKAGES`、`RAYLIB_DEBIAN_PACKAGES`
- `mk/raylib.mk`：`raylib-check`、`prerequisites`
- `mk/tests.mk`：`ALLOW_DUP_LDFLAG`（`test_planet_texture_resources` 与静态库的同名符号）
- `tests/test_build_prerequisites.sh`

M2 的 `test-cube-vertical-geometry-e2e` 继续使用同一个 vendored-raylib
二进制：场景在 Hyprland 中依次渲染 legacy 与 cube 的同一面竖墙，用来验证
共享投影几何和 section transform，而不是引入 X11/平台分支。

M3 adds the V30 fresh-process reload targets to the Makefile. They run through
the same vendored dual-backend raylib binary under Hyprland; no save/reload path
introduces a platform-specific renderer or alternate raylib build.
