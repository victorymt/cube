---
name: voxelcraft-docs
description: >-
  写或改 Voxelcraft 的文档：STATUS.md 单一真相源、README/architecture/todo 各写什么、
  make check-docs 的静态与真值两类检查、docs/doc-claims.json 登记表、文档陈旧怎么发现和修、
  make docs-probe 跑运行时字段。当要更新现状、记录一个阶段、发现文档之间互相矛盾、
  提交前被文档门禁挡住、或被报 stale document 时使用。Use when writing or fixing
  Voxelcraft docs, updating current state, registering a document, resolving a
  check-docs or check-docs-truth failure, or handling a stale-document report.
metadata:
  version: "3"
---

# 写文档

规则全文在 `docs/documentation-rules.md`。这里只讲怎么落地和怎么修错。

## 一句话版本

**现状只有 `STATUS.md` 一个家。** 别的文档都不许复述"现在能不能用"，只能链接过去。写实现原理的写原理，写计划的写计划，写历史的写历史（带日期，且不声称今天仍成立）。

## 这份内容该放哪

| 你要写 | 放这 | 不该出现 |
| --- | --- | --- |
| 今天什么能用、什么坏着、什么验证过 | `STATUS.md` | 实现细节 |
| 这是什么项目、怎么构建、有哪些特性 | `README.md` | 现状、blocker |
| 某个系统怎么工作、契约、边界 | `docs/*.md` | 哪部分现在坏了 |
| 阶段记录：边界、测试、带日期的证据、已知限制 | `docs/architecture/NN-*.md` | 对今天状态的断言 |
| 计划/验收标准/已完成范围 | `todo/*.md` | 重述 blocker |
| 某类任务怎么做 | `.dsh/skills/*` | 产品文档 |

带日期的记录可以写"2026-09-19 那次跑通了"，但不能写"现在跑得通"。

## STATUS.md 的维护

- 每条一行，`Updated <日期>` 记得改。
- `## Verified` 只放**跑过命令**证明的事，写清日期和命令。没跑过的不要写进去。
- `## Not verified` 写清哪些"看起来完成"的东西其实没验证。这一段比 Verified 更有价值。
- 一个 blocker 关闭时从 `## Open blockers` 删掉，而不是标记成"已关闭"——历史在 git 里。
- 别的文档需要提到 blocker，写一句链接，例如"见 [STATUS.md](../../../STATUS.md)"。`.dsh/skills/<name>/` 到仓库根是三层，`docs/*.md` 是一层，别写错。

## 数字不要写死

"`docs/debug-dsl.md` 有 N 行"、"有 N 个场景"这类话会在下一次编辑时过期。写成怎么查：

```text
wc -l docs/debug-dsl.md
ls tests/scenes/*.dsl | wc -l
```

真的需要写数字（一份以测量为主题的审计记录）时，在 `docs-check.config.json` 的 `volatileCountIgnore` 里按路径豁免，或在那一行末尾加 `<!-- volatile-counts:allow -->`。

## 写完必跑

```sh
make check-docs     # 静态：散文 + 真值
make docs-probe     # 运行时：DSL 字段真的能解析
```

`make check-docs` 现在是两个检查器串起来跑。

### 第一层：散文检查（`check-docs.mjs`）

| 报错 | 原因 | 怎么修 |
| --- | --- | --- |
| `dead link` | 相对链接指向不存在的文件 | 改路径，或补上文件 |
| `dead anchor` | `#fragment` 找不到同名标题 | 改锚点；改标题时同步改所有引用 |
| `second home for current state` | 非 STATUS.md 的文档里有个 status 标题，正文超过 40 词 | 删掉正文，改成一句链接 |
| `duplicated paragraph` | 25 词以上的段落逐字出现在两个文件 | 删掉一份，改成交叉引用 |
| `volatile count` | 写死了会变的数字 | 改成"怎么查"，或按上面的方式豁免 |
| `over budget` | 文件超过 `budgets` 里的词数上限 | 拆文档 |

`commit identifier in prose` 这一条已经移到真值检查里按 tier 判定，不再由散文检查报。

`wrap` 在本仓库是 `off`：段落仍建议一个物理行（diff 更可读），但不要为了这个去重排你没有在改的段落。

### 第二层：真值检查（`check-docs-truth.mjs`）

对 `docs/doc-claims.json` 里的每条登记做七类判定：

| 报错 | 原因 | 怎么修 |
| --- | --- | --- |
| `unregistered` | 门禁范围内有 md 没登记 | 在 `documents` 里加一条 |
| `stale registration` | 登记了但文件没了 | 删掉那条 |
| `orphan document` | 没有索引链过来，也没声明 `entryPoint` | 从索引链过去；或被 skill/工具读取时加 `entryPoint` 说明谁读 |
| `missing anchor` | `anchors` 里的路径不存在 | 改锚点，或确认文档描述的模块是否改名 |
| `unknown recipe target` | `recipes` 里的 make 目标不存在 | 改成真目标 |
| `stale pin` | `pins` 的正则不再命中 | 常量变了：改文档里的数字，或改 pin |
| `commit identifier in a <tier> document` | 非 record/plan 的文档写了 hash | 改写它证明了什么 |
| `stale document` | 锚点的最后一次提交晚于 `verifiedAt` | **跑它给你的 recipes，然后 `make docs-record`** |

`stale document` 是这一层存在的理由：它说明"代码动了，文档没跟上"，并且在报错里直接给出重跑命令。

### 修一份 stale document

```sh
make docs-probe        # 或者报错里打印的其他 recipe
make docs-record       # 用 HEAD 重新盖 verifiedAt
```

`docs-record` 只写 `verifiedAt`，不改正文。它记录的是"有人跑过 recipes"。不跑就直接 record，等于把门禁关掉。

## 加一份新文档

1. 照上表确定归属。
2. 从索引链过去：阶段记录 → `docs/architecture/README.md`，计划 → `todo/00-roadmap.md`，用户向 → `README.md`。如果它是被工具按名字加载的（skill、`AGENTS.md`），在登记表里写 `entryPoint`。
3. 在 `docs/doc-claims.json` 里登记：`tier`、`anchors`、`recipes`、以及它引用的常量对应的 `pins`。
4. `make check-docs`；文档写了运行时字段就再跑 `make docs-probe`。

没人链、也没人读的文档活不过一个月。

## 提交前

```sh
make check-docs && make docs-probe && git diff --check
```

文档改动和代码改动**分开提交**：`docs: give current state one home and gate the rules` 这样的信息，而不是塞进一个 feature commit。
