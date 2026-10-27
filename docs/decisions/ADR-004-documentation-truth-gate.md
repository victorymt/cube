# ADR-004: 文档用登记表、tier 与陈旧检测来保证可核查

## 状态
已采纳

## 日期
2026-10-27（记录日期）

## 背景
散文门禁 `scripts/check-docs.mjs` 只能发现文本层的毛病：相对链接指向不存在的文件、`#fragment` 没有对应标题、非 `STATUS.md` 的文档另立一个状态章节、段落逐字重复、写死会变化的数字。这些检查全绿的时候，文档仍可能整体描述一个已经不存在的软件，`docs/architecture/23-doc-truth-gate-2026-10-24.md` 记录了同一批发现的三个例子：ready 行少列了命令组、一张表被正文切断、某条性能断言指向一个链接都通不过的 make 目标。第二种失效是可达性：没有任何索引链过去的文档不会被任何流程重新读到。硬约束是门禁必须能放进 pre-commit 又不必启动游戏：`tools/git-hooks/pre-commit` 对每次提交无条件跑 `make check-docs`，只有暂存区里有 `src/`、`mk/` 或 `Makefile` 时才考虑 `make docs-probe`。

## 决策
把“文档是否仍然成立”从记忆搬到命令：`docs/doc-claims.json` 为每份受门禁约束的文档登记一条记录，字段是 `tier`、`trackCode`、`anchors`、`recipes`、`pins`、`probe`、`verifiedAt`、`entryPoint`；`scripts/check-docs-truth.mjs` 是通用检查器，加文档只是加数据。`tier` 决定文档可以断言什么，`contract` 不得引用本仓库的提交标识而 `record` 可以。陈旧检测不是启发式：对 `trackCode` 文档，检查器把每个 `anchors` 的最后一次提交时间与 `verifiedAt` 指向版本的提交时间比较，代码动了而文档没动就报 `stale document`，并在消息里直接打印要重跑的 `recipes`。运行时名字另走一层：`scripts/docs-probe.mjs` 把 `docs/debug-dsl.md` 运行时命名空间表里的名字逐个喂给真实二进制。

## 备选方案
### 备选 1：给每份文档配一个专用校验脚本
- 优点：每份文档可以检查自己特有的断言，表达力最强。
- 缺点：检查逻辑随文档数量线性增长，规则会在脚本之间漂移；新增文档要同时写文档和写脚本。
- 未采用原因：同一条规则写两遍就会有两种解释，登记表让检查器保持通用，新增文档变成纯粹的登记动作。

### 备选 2：只保留散文门禁，把真值交给人工评审
- 优点：没有维护成本，不需要 git 历史，pre-commit 永远在亚秒级。
- 缺点：`docs/documentation-rules.md` 里那些“没有脚本检查、属于评审”的条目正是失效发生的地方；散文门禁对“文档描述的东西已经不在了”结构上不可见。
- 未采用原因：三个真实缺陷都在散文门禁全绿时存在，说明这一层不足以支撑文档可信度。

### 备选 3：从源码静态分析推导文档里的运行时名字
- 优点：不依赖图形会话，可以在任何 runner 上跑，还能发现文档漏写的名字。
- 缺点：若干命名空间靠前缀匹配分发，正则会把可推导的名字读得远比文档实际声明的宽，`strcmp` 与 `LOD_DSL_NUMBER` 两种写法混在一起时无法区分哪个是字段。
- 未采用原因：推导出的名字集合显著宽于文档声明的集合，比较就失去了判别力；把文档里的名字喂给真实进程只回答一个问题，即这个名字还解析得出来吗，而这正是唯一需要回答的问题。

## 后果
- 加文档是加数据：`scripts/check-docs-truth.mjs` 的 `makeTargets` 会核验 `recipes` 里的 make 目标真实存在，`pins` 的正则会核验被引用的常量还在源码里，`anchors` 路径消失会报 `missing anchor`。
- 孤儿文档会失败：既没有从 `indexes` 链过来、也没有 `entryPoint` 说明谁读它的文档报 `orphan document`。
- 两层扫描范围必须一致：`docs-check.config.json` 的 `include` 与登记表的 `roots` 不同时直接报错，避免一份文档只过一层检查而读起来像通过。
- 代价：`pins` 是字面正则，只能抓常量变化，抓不到语义变化；`verifiedAt` 记录的是“有人跑过 `recipes`”，`make docs-record` 无法验证这句话是否属实，这是为了让门禁可满足而接受的取舍。
- 代价：`docs-probe` 需要真实帧循环，没有 `WAYLAND_DISPLAY` 与 `XDG_RUNTIME_DIR` 时打印 `skipped` 并退出 0，只有 `--require-display` 才把它变成失败；探测范围只覆盖 flat homeworld 游玩时可达的名字，planet 或特定切换后才成立的字段不在断言范围内。
- 代价：CI 的 docs job 必须 `fetch-depth: 0`，因为它要真实提交历史而不是浅克隆。
- 验证方式：`make check-docs`（散文加真值）、`make docs-probe`、`make docs-record`。

## 证据
- `scripts/check-docs.mjs`：`run`、`proseBlocks`、`relativeLinks`、`volatileCountPattern`
- `scripts/check-docs-truth.mjs`：`TIERS`、`makeTargets`、`dirtyPaths`、`lastCommitTime`、`revisionTime`、`record`
- `scripts/check-docs-truth.mjs`：`stale document`、`orphan document`、`missing anchor`、`stale pin`、`unknown recipe target`
- `scripts/docs-probe.mjs`：`documentedFields`、`sceneFor`、`finish`、`--require-display`
- `docs/doc-claims.json`：`tier`、`trackCode`、`anchors`、`recipes`、`pins`、`verifiedAt`、`entryPoint`
- `docs-check.config.json`：`include`、`statusOwners`、`volatileCountIgnore`、`volatileCounts`
- `scripts/check-docs.mjs`：`DEFAULT_CONFIG.banCommitIds` 为 `tiered` 时把提交标识判定交给真值层
- `tools/git-hooks/pre-commit`：无条件 `make check-docs`，`SKIP_DOCS_PROBE` 与无 Wayland 时显式跳过 `docs-probe`
- `.github/workflows/ci.yml`：`docs` job 的 `fetch-depth: 0` 与失败时的 truth 明细步骤
- `src/core/debug_control.c`：`DebugControlCommandGroups`（ready 行由它生成，探测逐字比较）
- [文档门禁记录](../architecture/23-doc-truth-gate-2026-10-24.md)
- [文档规则](../documentation-rules.md)
