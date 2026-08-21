调试时使用 项目内提供的选项：DSL(使用方法在 docs/debug-dsl.md 中)，--debug-*（比如 --debug-stdin，），如果缺少需要的功能就自行扩展DSL,而不是使用hyprctl,拓展后更新 debug-dsl.md

每完成一个阶段的改动都要commit

当前项目构建的软件运行在hyprland下

如果我提交了一个游戏里的画面和bug，要先复现，如果无法复现就告知用户可能的原因

如果尝试了什么东西却失败了要简单的告知用户
