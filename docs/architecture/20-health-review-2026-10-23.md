# 架构健康度审查（2026-10-23）

- 基线：`a9f4f1b`
- 方法：依赖矩阵实测、公共头面宽度量、耦合热点统计、边界规则核对、god-object 检测
- 性质：**审查报告，不含改动**。所有数字均为本次实测，可复现

## 0. 结论

架构**基本健康**，不是需要推倒重来的状态。七个组件的划分成立，边界检查（约 400 行规则）真实生效且全部通过，126 个测试全覆盖。

但有 **4 个具体问题**值得处理，按严重度排列：

| # | 问题 | 严重度 | 性质 |
|---|---|---|---|
| 1 | `FloraTaxonId` 归属错位，形成 world↔ecology 概念循环 | **中** | 真实的模型缺陷 |
| 2 | `presentation/ui_types.h` 被 gameplay 反向依赖 | **中** | 明确的分层违规 |
| 3 | `GameRuntime` 137 字段，app 内部耦合中心 | 低 | 可维护性负担，未外泄 |
| 4 | `world` 59 个公共头，接口面过宽 | 低 | 渐进劣化风险 |

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

## 2. 问题 1：`FloraTaxonId` 归属错位（中等）

### 现象

`FloraTaxonId` 枚举定义在 `src/ecology/flora_taxa.h`，但 `world` 组件有 7 处依赖它：

```
src/world/terrain.c
src/world/weather_impact.c
src/world/chunks_flora.c
src/world/terrain.h          ← 公共头
src/world/home_tree_shape.h
src/world/chunks_runtime.c
src/world/chunks_plant_mesh.c
```

同时 `ecology/flora_taxa.h` 第 4 行又 `#include "world/world_types.h"`。

### 为什么这是问题

这不是编译环（`world_types.h` 不反向 include ecology，已核实），所以构建不会失败。问题是**概念归属混乱**：

- 地形生成需要知道“这里长什么树”，所以 world 要认识 `FloraTaxonId`
- 但“有哪些树种”是生态学概念，定义在 ecology
- 结果是 world 无法独立于 ecology 理解和测试地形逻辑

`src/world/terrain.h` 是公共头，把 ecology 的类型写进 world 的公共契约，等于把耦合固化到接口层：

```c
FloraTaxonId TerrainHomeTreeTaxonAt(int x, int z, TerrainMode mode);
FloraTaxonId TerrainHomeGroundTaxonAt(int x, int z, TerrainMode mode, ...);
```

### 建议

`FloraTaxonId` 是纯标识符，按现有架构惯例应下沉到 `core` 或 `world/world_types.h`：

- **下沉到 core**：符合“中性值类型归 core”的既有规则（对照 `core/render_resources.h`、`core/game_notice.h` 的处理方式）
- 这样 world 依赖 core（合法方向），ecology 也依赖 core，反向边消失

**关键限制**：`flora_taxa.h` 是一个混合头，只有枚举适合下沉。同一个文件里还有：

```c
typedef struct FloraTaxon {        // 气温/湿度/光照范围、生物群系掩码……
    FloraTaxonId id;
    float temperatureMinK; ...
} FloraTaxon;
const FloraTaxon *FloraTaxonAt(FloraTaxonId id);   // 查询函数
```

这部分是**生态学数据与查询逻辑**，必须留在 ecology。所以动作是把 `FloraTaxonId` 枚举（约 25 行）单独提到 core 的一个头里，`flora_taxa.h` 反过来 include 它。不是整文件搬迁。

**影响面**：1 处枚举提取 + 7 处 include 调整，测试基本不用改。`home_tree_shape.h` 里可能有纯几何逻辑，也值得一并审视是否同样错位。

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
