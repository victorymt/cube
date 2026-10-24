---
name: voxelcraft-docs
description: >-
  写或改 Voxelcraft 的文档：STATUS.md 单一真相源、README/architecture/todo 各写什么、
  make check-docs 的六类检查、怎么修 check-docs 报的错。当要更新现状、记录一个阶段、
  发现文档之间互相矛盾、或提交前被文档门禁挡住时使用。Use when writing or fixing
  Voxelcraft docs, updating current state, or resolving a check-docs failure.
metadata:
  version: "2"
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
make check-docs
```

六类检查，按报错文案对照修：

| 报错 | 原因 | 怎么修 |
| --- | --- | --- |
| `dead link` | 相对链接指向不存在的文件 | 改路径，或补上文件 |
| `dead anchor` | `#fragment` 找不到同名标题 | 改锚点；改标题时同步改所有引用 |
| `second home for current state` | 非 STATUS.md 的文档里有个 status 标题，正文超过 40 词 | 删掉正文，改成一句链接 |
| `duplicated paragraph` | 25 词以上的段落逐字出现在两个文件 | 删掉一份，改成交叉引用 |
| `volatile count` | 写死了会变的数字 | 改成"怎么查"，或按上面的方式豁免 |
| `commit identifier in prose` | 正文里写了本仓库存在的 commit id | 改写它证明了什么；历史归 git |
| `over budget` | 文件超过 `budgets` 里的词数上限 | 拆文档 |

`wrap` 在本仓库是 `off`：段落仍建议一个物理行（diff 更可读），但不要为了这个去重排你没有在改的段落。

## 加一份新文档

1. 照上表确定归属。
2. 从索引链过去：阶段记录 → `docs/architecture/README.md`，计划 → `todo/00-roadmap.md`，用户向 → `README.md`。
3. `make check-docs`。

没人链的文档没人读。

## 提交前

```sh
make check-docs && git diff --check
```

文档改动和代码改动**分开提交**：`docs: give current state one home and gate the rules` 这样的信息，而不是塞进一个 feature commit。
