# 架构健康度审查（2026-10-23）

- 基线：`a9f4f1b`
- 方法：依赖矩阵实测、公共头面宽度量、耦合热点统计、边界规则核对、god-object 检测
- 性质：**审查报告，不含改动**。所有数字均为本次实测，可复现

## 0. 结论

架构**基本健康**，不是需要推倒重来的状态。七个组件的划分成立，边界检查（约 400 行规则）真实生效且全部通过，126 个测试全覆盖。

但有 **4 个具体问题**值得处理，按严重度排列：

| # | 问题 | 严重度 | 状态 |
|---|---|---|---|
| 1 | `flora_taxa` 整个模块放错组件，造成 world→ecology 反向边 | **中** | **已修复**（见 §9） |
| 2 | `presentation/ui_types.h` 被 gameplay 反向依赖，且规则有漏洞 | **中** | **已修复**（见 §9） |
| 3 | `GameRuntime` 137 字段，app 内部耦合中心 | 低 | 未处理 |
| 4 | `world` 59 个公共头，接口面过宽 | 低 | 未处理 |

---

## 1. 依赖矩阵（实测）

行 = 依赖方，列 = 被依赖方。数字为 include 语句数。

```
         core world space eco game pres app
core:      -     0     0    0    0    0    0
world:    11     -     9    7    0    0    0
space:     8     9     -    1    1    0    0
ecology:   2    39     6    -    1    0    0
gameplay:  7    40    11    0    -    1    0
presentation: 10 61   45   20   21    -    0
app:      69   204    47   26   48   51    -
```

**符合预期的部分**：

- `core` 依赖为 0——它确实是最底层，没有任何向上依赖。
- `app` 只被自己依赖（28 个文件用 `game_runtime.h`，全部在 `app/` 内），组合根没有泄漏。
- `presentation` 不依赖 `app`，方向正确。
- 依赖总量集中在 `app → world`（204），符合“app 是组合根”的设计。

**需要关注的部分**：矩阵不是严格 DAG，存在 4 处反向边。逐一分析见下。

---

## 2. 问题 1：`flora_taxa` 整个模块放错了组件（中等）

> 本节在开始修复时被修正过一次。初版诊断说"只有一个枚举归属错位"，实测后
> 发现范围大得多：world 依赖的是整个物种目录，不是单个枚举。

### 现象

`src/ecology/flora_taxa.{c,h}` 在 ecology 组件下，但实测：

| 检查项 | 结果 |
|---|---|
| ecology 内部有谁使用它 | **只有它自己**（`flora_taxa.c` 和 `.h`） |
| world 使用 | 7 处，含公共头 `world/terrain.h`、`home_tree_shape.h` |
| app 使用 | 2 处（`game_debug_flora.c`、`game_runtime.h`） |
| `flora_taxa.c` 的外部依赖 | 只有 `world/world_types.h` 和 `math.h` |

`flora_taxa.h` 依赖 world 的类型：

```c
typedef struct FloraTaxon {
    BlockType primaryBlock;   // world 的类型
    BlockType accentBlock;
} FloraTaxon;
typedef struct FloraHabitat {
    Biome biome;              // world 的类型
    BlockType substrate;
    ...
} FloraHabitat;
FloraTaxonId FloraTaxonIdForBlock(BlockType block);
```

### 为什么这是问题

这不是"一个枚举放错位置"，而是**模块归属错误**：

- 它的全部输入类型来自 world（`BlockType`、`Biome`）
- 它的全部消费者是 world 和 app
- ecology 内部零使用

也就是说 ecology 在为别人保管一个模块，并且因此制造了 `world → ecology` 的反向边。这才是矩阵里那 7 条边的真正来源。

初版建议（只把 `FloraTaxonId` 提到 core）是**错的**：world 同时要 `FloraTaxon`、`FloraHabitat`、`FloraTaxonAt`、`FloraSelectTaxon`、`FloraTaxonSuitability` 等，只搬枚举并不能消除依赖，反而会把一个内聚的数据模块拆散。

### 建议

把 `flora_taxa.{c,h}` 整体迁移到 `world/`：

- 依赖方向变成 `world → world`（自身），反向边消失
- ecology 不再为别的组件保管数据，`ecology → world` 的 39 条边保持不变（那部分是合法的：生态确实要读世界状态）
- 影响面：2 个文件移动 + 15 处引用改路径 + `mk/modules.mk`、`mk/tests.mk` 各一处

**待确认的设计判断**：迁到 world 后，`world` 就拥有了"有哪些树、各自适宜什么气候"的知识。另一种选择是把它留在 ecology、改由 `app` 在启动时把目录注入给 world（沿用 `WorldEcologyHooks` 的既有模式）。两种都能消除反向边；前者简单，后者概念更纯但要加一层注入接口。**本次按前者执行**，理由是 `flora_taxa` 已经在用 world 的类型，且 world 的地形生成必须同步地知道树种才能生成地形，注入式接口反而会引入生命周期问题。

---

## 3. 问题 2：`gameplay` 反向依赖 `presentation`（中等）

### 现象

```
src/gameplay/interaction.h:6:#include "presentation/ui_types.h"
```

用于 `ImportDialog`：

```c
void OpenImportDialog(ImportDialog *dialog);
void AppendImportText(ImportDialog *dialog, const char *text);
void UpdateImportDialog(ImportDialog *dialog, const Player *player, bool *cursorReleased);
```

### 为什么这是问题

这是**明确的分层违规**：gameplay 是领域层，presentation 是表现层。`tests/test_architecture.sh` 已经专门为 album/discovery 写了同样的禁令：

```sh
if grep -n '#include "presentation/' src/gameplay/album.c ...; then
    fail "gameplay album and discovery must not depend on presentation"
```

`interaction.h` 逃过了检查——**规则覆盖不完整，不是设计允许**。

`ImportDialog` 本体（`ui_types.h`）只有 11 行，是纯数据（bool + int + char[1024]），不含任何渲染调用。所以：

- 它被放在 `presentation/` 是**放错了位置**
- gameplay 依赖它**本身合理**（交互需要对话框状态），只是依赖了错误的组件

### 建议

把 `ui_types.h` 的纯数据类型下沉到 `core`（或 `gameplay/ui_types.h`）。同一份规则应扩展到 `interaction.h`，避免继续漏网。

---

## 4. 问题 3：`GameRuntime` 137 字段（低）

### 数据

```
字段数 : 137
bool   : 52
int    : 11
unsigned: 9
uint64_t: 9
char    : 7
```

结构体本体 145 行。但**只在 `app/` 内使用**（28 个文件），没有跨组件泄漏。

### 判断

它没有破坏分层——`app` 是组合根，持有全局运行状态是它的职责。但 137 个平铺字段有两个实际代价：

1. **任何新增功能都要改这个结构体**，它是 app 内部所有改动的交汇点
2. 52 个 bool 里大概率有成组的状态（如 `xxxActive` / `xxxOpen` / `xxxVisible`），本可以是子结构

### 建议

**不急于改**。若将来 app 继续膨胀，按领域分组成子结构（如 `GameUiState`、`GameDebugState`、`GameEcologyState`），是低风险的机械重构。当前优先级低于问题 1、2。

---

## 5. 问题 4：`world` 公共头过多（低）

### 数据

| 组件 | 公共头 | .c 文件 | .c 行数 | 含头文件总计 |
|---|---:|---:|---:|---:|
| core | 15 | 13 | 5,305 | 6,539 |
| **world** | **59** | 80 | **33,010** | **38,252** |
| space | 27 | 26 | 10,366 | 11,911 |
| ecology | 11 | 19 | 10,634 | 12,194 |
| gameplay | 15 | 16 | 6,112 | 6,925 |
| presentation | 31 | 37 | 15,172 | 16,424 |
| app | 38 | 42 | 14,864 | 16,202 |

`world` 的公共头/BUILD 单位比最差：59 个头对应 80 个 .c，而 ecology 是 11 个公共头对应 19 个 .c。

`world` 有 59 个公共头 = 平均每个 .c 文件对应 0.74 个公共头。对比 ecology（11 个公共头 / 19 个 .c）明显偏多。

耦合热点排名：

| 被引用 | 次数 |
|---|---:|
| `world/world.h` | 64 |
| `world/world_environment.h` | 50 |
| `world/terrain.h` | 50 |
| `world/chunks.h` | 43 |
| `world/world_types.h` | 39 |

`world.h` 公开 56 个函数、被 64 处引用——它是事实上的 world 总入口。

### 判断

这还不是病。平均文件 412 行，最大 2,097 行，都在架构测试的限额内（1500 行热点上限、2100 行全局上限），且 `world` 内部已经拆得比较细（`chunks_storage` / `chunks_streaming` / `chunks_mesh` / `chunks_runtime` / `chunks_water` 分列）。

风险在于**趋势**：cube 激活工作（W3 消费者统一）会继续往 world 加东西，公共头只会更多。

### 建议

在 W3 大规模改造前，考虑把 `world` 按子域拆成更明确的公共契约（如 `world/terrain_*` 与 `world/chunks_*` 各收敛到一个入口），而不是让 59 个头各自平等地对外。这是准备性工作，不是紧急缺陷。

---

## 6. 做得好的地方（不是客套，有数据支撑）

1. **边界检查真实有效**。`tests/test_architecture.sh` 约 400 行，不是摆设——它检查了 `core` 零向上依赖、`space` 只能经 `space_dependencies.h` 消费 ecology、`world` 必须用 `WorldSurfaceSnapshot` 而非 `space_state.h`、领域层不得调用 `AudioPlay*` / `ParticlesEmit*` 等。这些规则在代码中确实成立。

2. **`core` 的零依赖是真的**。实测 `core` 对六个组件的 include 全为 0。这在 10 万行项目里很少见。

3. **组合根纪律良好**。`GameRuntime` 虽大但没外泄，`app` 独占。

4. **声明的例外是显式的**。`space_dependencies.h` 作为聚合头，被架构测试**专门白名单**（`grep -v 'src/space/space_dependencies.h:'`），说明这是有意识的设计，不是失控。

5. **文件粒度控制严格**。每个 .c 平均 353–559 行，最大 2,097 行，没有出现单文件黑洞。架构测试里连 `main.c` 必须 ≤8 行、facade 必须 ≤10 行都钉住了。

---

## 7. 建议的处理顺序

| 顺序 | 动作 | 理由 | 工作量 |
|---|---|---|---|
| 1 | 把 `FloraTaxonId` 下沉到 core | 消除真实概念环，影响 7 处 | 小 |
| 2 | `ui_types.h` 下沉，并把架构规则扩展到 `interaction.h` | 修分层违规 + 补规则漏洞 | 小 |
| 3 | （准备）收敛 world 公共头 | 在 W3 往 world 继续加东西之前做 | 中 |
| 4 | （可选）`GameRuntime` 分组 | 纯内部重构，可延后 | 中 |

第 1、2 项都是小改动且能实际降低耦合，建议在开始 W3（消费者统一）之前完成——W3 会大量触碰 `world` 和 `gameplay`，先把边界理顺能少走弯路。

---

## 8. 复现命令

```sh
# 依赖矩阵
for from in core world space ecology gameplay presentation app; do
  for to in core world space ecology gameplay presentation app; do
    [ "$from" != "$to" ] && printf '%s ' \
      "$(grep -rh "#include \"$to/" src/$from --include=*.c --include=*.h 2>/dev/null | wc -l)"
  done; echo "$from"
done

# 耦合热点
for h in $(ls src/*/*.h | sed 's|^src/||'); do
  echo "$(grep -rl "\"$h\"" src --include=*.c --include=*.h | wc -l) $h"
done | sort -rn | head -20

# 边界检查
sh tests/test_architecture.sh
```

---

## 9. 修复记录（2026-10-24）

问题 1、2 已修复并提交。矩阵前后对比：

```
修复前                                   修复后
world:    11  -  9  7  0  0  0     →   world:    11  -  9  0  0  0  0
gameplay:  7 40 11  0  -  1  0     →   gameplay:  7 40 11  0  -  0  0
```

剩余两条反向边未经改动，它们是**受控例外**，不是缺陷：

- `space → ecology`（1）和 `space → gameplay`（1）都位于 `space/space_dependencies.h`。
  该文件是声明式聚合头，`tests/test_architecture.sh` 对它做了显式白名单：
  `grep -v 'src/space/space_dependencies.h:'`。其余 `space` 代码不得直接依赖。

### 9.1 问题 1：`flora_taxa` 迁移到 world

`src/ecology/flora_taxa.{c,h}` → `src/world/flora_taxa.{c,h}`。

依据（修复前一节已列出，此处复述结论）：该模块只依赖 `world/world_types.h` 和
`math.h`，ecology 内部零使用，消费者全部在 world 和 app。它不是"借放"在
ecology 的公共数据，而是放错了位置的 world 模块。

改动范围：2 个文件移动 + 12 处 include 路径 + `mk/modules.mk` 1 处 +
`mk/tests.mk` 2 处。

结果：`world → ecology` 从 7 降至 **0**。

### 9.2 问题 2：`ui_types.h` 迁移到 gameplay，并补上规则漏洞

`src/presentation/ui_types.h` → `src/gameplay/ui_types.h`。

依据：`ImportDialog` 由 gameplay 的 `interaction.c` 定义和修改
（`OpenImportDialog` / `AppendImportText` / `UpdateImportDialog`），presentation
只负责把它画出来（`DrawImportDialog`）。数据的所有者不是绘制方。

规则漏洞：原检查只点名 `album.{c,h}` 和 `discovery.{c,h}`，因此
`interaction.h` 的违规一直没被发现。已改为覆盖整个 `gameplay` 组件：
`grep -R '#include "presentation/' src/gameplay`。

**变异验证**：临时把 `interaction.h` 改回 `presentation/ui_types.h` 后，
检查输出 `architecture check failed: gameplay must not depend on presentation`，
确认新规则会真正拦截，而不是形同虚设。

### 9.3 验证

- `make -j8`：无 error、无 warning
- `make test`：**126 passed, 0 failed**，architecture checks passed
- `grep -rn '#include "ecology/' src/world`：无输出

### 9.4 本次未做

问题 3（`GameRuntime` 137 字段）与问题 4（world 59 个公共头）保持未处理。
前者是 app 内部重构、风险低但收益也低；后者应在 W3 大规模改造之前再做，
否则做完立刻又被新代码覆盖。
