调试时使用 项目内提供的选项：DSL(使用方法在 docs/debug-dsl.md 中)，--debug-*（比如 --debug-stdin，），如果缺少需要的功能就自行扩展DSL,而不是使用hyprctl,拓展后更新 debug-dsl.md

每完成一个阶段的改动都要commit

当前项目构建的软件运行在hyprland下

如果我提交了一个游戏里的画面和bug，要先复现，如果无法复现就告知用户可能的原因

如果尝试了什么东西却失败了要简单的告知用户

当前项目处于开发阶段，任何的修改都可以不用考虑和以前的兼容性

测试按影响范围分层，禁止把 `make test` 当作每次小改动的默认命令：

- 单模块/局部修复：运行直接覆盖问题的 `make test-one TEST=<test_name>`；涉及玩家可见行为时再跑对应 E2E。
- 公共头、构建图、调度、存档格式、跨模块契约：运行 `make test`。
- 合并前或发布前：运行 `make test-clean-root`；需要完整 CI 矩阵时才运行 `make test-ci`。
- 文档-only 改动不跑 C 测试；只跑 `make check-docs` 和必要的 `make docs-probe`。
- 如果没有合适的定向测试，先补测试，不要用完整测试代替测试设计。
