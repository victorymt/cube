# 架构决策记录

这里记录已经生效、且改动代价较高的架构决策。每条 ADR 回答同一个形状的问题：当时的约束是什么、考虑过哪些方案、为什么选这个、选了以后接受了什么代价。

范围约定：

- 只记录“反悔要重写代码”的决定，不记录实现细节。函数怎么写看代码，系统怎么工作看 [架构总览](../architecture-overview.md)。
- 每条 ADR 写的是**决策**，不是**现状**。某个能力今天是否可用只看 [STATUS.md](../../STATUS.md)。
- 状态只有三种：`已采纳`、`已废弃`、`被 ADR-XXX 取代`。被取代的 ADR 不删除，保留原文与取代链接，因为推翻它的理由本身也是证据。
- 事实必须能在仓库里核对：文件路径、常量名、make 目标、脚本。读不到证据的判断不写。

| 编号 | 决策 | 状态 |
| --- | --- | --- |
| [ADR-001](ADR-001-vendored-raylib.md) | 内置固定版本的 raylib，不探测系统安装 | 已采纳 |
| [ADR-002](ADR-002-plugin-abi-and-services.md) | 插件走小型稳定 ABI 加版本化只读服务 | 已采纳 |
| [ADR-003](ADR-003-save-format-and-transactions.md) | 存档只接受 V30，写入走原子替换与事务读 | 已采纳 |
| [ADR-004](ADR-004-documentation-truth-gate.md) | 文档用登记表、tier 与陈旧检测保证可核查 | 已采纳 |
| [ADR-005](ADR-005-cube-grid-gated-topology.md) | cube-grid 以运行时门控引入，默认拓扑不变 | 已采纳 |
| [ADR-006](ADR-006-chunk-streaming-and-mesh-pipeline.md) | 区块在后台线程生成与建网格，主线程预算内上传 | 已采纳 |

## 编号规则

编号一旦分配就不再复用。新决策取下一个整数，不插入、不重排。新增一条 ADR 要做四件事：

1. 在 `docs/decisions/` 建 `ADR-NNN-短横线标题.md`。
2. 本文的表格加一行。
3. 在 [docs/doc-claims.json](../doc-claims.json) 登记：`tier` 用 `contract`，`anchors` 指向决策描述的那些源文件。
4. 跑 `make check-docs`。

## 与其他记录的分工

| 想找 | 去哪 |
| --- | --- |
| 组件边界、依赖方向、一帧的执行顺序 | [架构总览](../architecture-overview.md) |
| 某次改动的实现过程、测得的数据、验证命令 | [架构记录索引](../architecture/README.md) |
| 某个能力当前的实现边界与契约 | [cube 契约](../cube-contract.md)、[Debug DSL](../debug-dsl.md)、[文档规则](../documentation-rules.md) 等 `docs/` 下的系统文档 |
| 今天什么能用、什么坏着 | [STATUS.md](../../STATUS.md) |
