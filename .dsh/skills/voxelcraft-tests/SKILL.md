---
name: voxelcraft-tests
description: >-
  给 Voxelcraft 新增或修改测试时，必须先写清这份测试验什么、覆盖到哪、防住哪个风险、出自哪条需求或 bug。
  凡是新增测试文件、新增 Test 函数、改断言、评审测试、给测试改名，或发现测试名与断言不符时使用。
  Use when adding or editing a test, reviewing a test, renaming one, or when a test's name does not match its assertions.
metadata:
  version: "1"
---

# 写测试前先说清它在测什么

**核心规则：没有声明的测试不许进入 `make test`。** 测试的价值不在于它通过，而在于别人读得懂它守的是什么。

这条规则有一部分已经是机械检查：`tests/test_declarations.sh`（挂在 `make test` 的 `test-declarations`）要求每个 `tests/test_*.c` 在第一个 `#include` 之前有实质说明（≥ 8 词），并检查登记清单是否与实际欠账一致。它**不能**判断说明是否属实——那仍是评审问题，见 §4。

相机和 world-query 边界应优先用 fake backend 测：`test_camera` 不链接 Player、ship、terrain 或 interaction 实现，避免测试通过只是因为同一条耦合链仍被链接。

迁移 geometry/collision 时同时测试 neutral API 和兼容 facade；例如 `test_player_collision` 直接调用 `WorldActorResolveMotion`，再验证 Player 包装入口。
特殊 Player 状态入口要单独测试 transition helper；app 直接写 pose/velocity 字段应由架构检查拒绝。
Replay checkpoint 覆盖新状态类型时，同时验证完整重放、checkpoint 恢复后的续跑，以及改变输入后的首个 divergence lane。
环境查询迁移要同时覆盖 world sample、Player alias/adapter 和原 E2E 行为，不能只验证新接口可编译。

审计结果（2026-10-25）：126 个 C 测试中只有 6 个写了说明；979 个 `Test*` 函数只有 17 个带注释；`docs/documentation-rules.md` 从未提到测试注释。这个 skill 就是从"没有约定"到"有约定"的那一步。

**存量债务怎么处理**：规则只向前收紧，不追溯全罚。规则生效前就存在的无说明测试登记在 `tests/test_declarations_allowlist.txt` 里，每行是可偿还的债；给某个文件补了说明就把那行删掉，否则 `test_declarations.sh` 会报 stale 并失败。`make test-declarations-backlog` 打印剩余清单和字段缺失情况，用来数欠账，不阻断开发。

## 1. 适用背景：这个项目的测试怎么组织

四层，按成本从低到高：

| 层 | 位置 | 入口 | 说明要求 |
| --- | --- | --- | --- |
| 单元测试 | `tests/test_*.c`（128 个） | `mk/tests.mk` 的 `define_test` | 文件头 3–8 行 + 关键 `Test*` 函数上一行 |
| 门禁测试 | `tests/test_*.sh`（25 个） | `Makefile` 的 `test-*` 目标 | 文件头说明守什么不变量、违规时提示怎么修 |
| E2E | `tests/test_*_e2e.sh`（19 个）+ `tests/scenes/*.dsl`（52 个） | `test-e2e` / Hyprland CI job | 脚本头说明验证的画面/状态；场景靠 `assert` 自述 |
| Benchmark | `tests/benchmark_chunks.c` | `test-benchmark-invariants` | 输出的每个不变量必须来自测量，并注明来源 |

要点：

- `define_test` 自动排序源文件，所以**新增测试只需在 `mk/tests.mk` 加一行**，不要在 `Makefile` 里手写编译命令。
- `tests/diagnostics_plugin.c`、`tests/ecology_test_fixture.c` 是夹具，不是测试，不注册。**夹具不要用 `Test` 前缀命名**（见 §5 反例）。
- 断言两种风格都在用：裸 `assert()` 与自定义 `AssertXxx()` 辅助。失败信息要能定位，所以带循环/边界的检查优先用辅助函数（`grep -l 'static void Assert' tests/*.c` 可看现有写法）。
- 测试通过只打印一行 `puts` 成功消息。**这行消息就是该测试对外自称的覆盖范围**，必须与实际断言相符。

## 2. 何时必须触发

出现以下任一情况，本 skill 强制生效：

1. **新增**测试文件、`Test*` 函数、e2e 脚本、`.dsl` 场景、benchmark 不变量。
2. **修改**已有测试的断言、循环范围、输入数据、期望值——即覆盖范围变宽或变窄。
3. **重命名**测试文件、`Test*` 函数、或修改成功消息。
4. **评审**任何包含 `tests/` 改动的 diff。
5. **发现**测试名、文件头、成功消息与断言不一致——修断言或修说明，两者必须同步。

不在范围内：只改测试里的注释错别字、只调格式。

## 3. 每个测试必须显式声明的四件事

按"意图 / 范围 / 边界 / 来源"顺序写在文件头。合起来 3–8 行，超过 8 行说明这个测试该拆了。

```c
/* <意图>：验证什么行为，或防住哪个具体风险。
 * <范围>：覆盖哪些输入、分支、状态；调用哪个生产符号。
 * <不覆盖>：明确写出这次不测什么，以及为什么（不链接、需要 GPU、属于别的测试）。
 * <来源>：需求/记录/bug 的编号或路径；没有就写 "无对应 issue，用于守住 <X> 的不变量"。 */
```

字段含义：

- **意图**必须能回答"这个测试挂了代表什么坏了"。写"测试碰撞"不算；写"脚下的地面格必须已知且实心，未知地形不得当作空气"才算。
- **范围**要具体到可反驳：哪些面、哪些极点、哪些非法输入、多少样本。
- **不覆盖**是最容易漏也最有价值的一条。它让读者知道该去别处找覆盖，而不是误以为这里已经管了。
- **来源**证明这个测试不是凭空写的。项目里合法的来源：`docs/architecture/NN-*.md`、`docs/*-design.md`、`todo/NN-*.md`、用户报的 bug。都套不上时，诚实写"守住 X 的不变量"。

`Test*` 函数级：函数名必须自解释，且名实相符。一个函数只测一件事；测两件就拆开（见 §5 反例）。

**夹具/替身命名**：不用 `Test` 前缀。用 `Fake*`、`Stub*`、`Stub*Provider`，与真正的测试用例区分开。

## 4. 检查清单：这个测试是不是"知其作用、知其范围"

评审时逐条打勾，任一为"否"即不通过。

声明完整性：

- [ ] 文件头有 3–8 行说明，四个字段齐全。
- [ ] 说明里的每个动词都能在断言中找到对应，反之每个断言都能归到说明的某一项。
- [ ] "不覆盖"不是空话：写明了缺的是哪个能力、为什么这次不做。
- [ ] 来源可点击或在仓库内可搜到。

名实一致：

- [ ] 文件名主题词与所链接源文件、断言里的生产符号匹配。
- [ ] `Test*` 函数名与实际断言相符；名字声称 Rejected/Invalid 就必须有否定断言。
- [ ] 成功消息列出的方面不超过实际断言的方面。

范围可信：

- [ ] 边界是枚举出来的（面、极点、非法输入清单），不是"测了一些边界"。
- [ ] 单个 `Test*` 函数没有长到无法定位：一眼扫不完、或一个断言失败无法判断是哪项契约，就拆。
- [ ] 场景/脚本有驱动者：`.dsl` 必须被某个 `tests/*.sh` 或 make 目标执行（`test_scene_reachability.sh` 强制）。
- [ ] 不变量是测量出来的，不是打印出来的（`test_benchmark_invariants.sh` 强制）。

**不满足时怎么办**（按优先级）：

1. 说明缺失但断言扎实 → **补说明**，不改断言。这是最快也最常见的情形。
2. 名字与断言不符 → **改名**，或在说明中写清为什么保留现名。改比解释便宜。
3. 声称覆盖但没测 → **加断言**；做不到就在"不覆盖"里写明，并链到真正覆盖它的地方。
4. 一个函数测多件事 → **拆分**，各自保留原断言。
5. 完全说不清测什么的测试 → **删掉**。说不清的测试比没有测试更糟：它让 `make test` 通过这件事产生虚假信心。

## 5. 正例与反例

### 正例 1：单元测试文件头（现有代码，可直接照抄结构）

`tests/test_world_block_landing.c`

```c
/* Landing clearance in cube space: the ground cell must be known and solid,
 * and every occupied body cell must be known and clear. Unknown terrain is
 * never treated as air. */
```

好在哪：一句话说清行为（落地间隙判定）、覆盖对象（地面格 + 占用格）、以及最关键的边界（未知地形≠空气）。最后半句是真正防住风险的断言方向。

### 正例 2：主动声明不覆盖什么

`tests/test_cube_raycast.c`

```c
/* Cube selection ray: it must find the same cells collision uses, stop at the
 * first solid block, and respect the distance limit. The real cube edit store
 * supplies the solid cells, so this exercises production lookup too. */
/* Focused fixture: no resident chunks, so marking a section dirty is a no-op.
 * The production definition lives in the chunk runtime, which this test does
 * not link. */
```

好在哪：第二段说明为什么这里有桩、桩替换了什么、生产实现不在本测试的链接范围内。评审者不会误以为脏区标记被覆盖了。

### 正例 3：门禁脚本头（说明守什么 + 违规怎么修）

```sh
# Every scene in tests/scenes must be reachable from something.
# ...
# This test fails when no `tests/*.sh` driver and no make target runs a scene.
# The fix is either to wire it up or to delete it ...
```

好在哪：不但说守什么，还说**失败时该做什么**。门禁脚本的说明必须包含修法，否则红灯只带来困惑。

### 反例 1：无说明的测试

```c
#include "presentation/weather_optics.h"
#include <assert.h>
int main(void) { ... }
```

问题：读者无法知道 4 个断言覆盖了彩虹光学的哪一部分、边界在哪、为什么阈值是 `0.00001`。测试通过不代表任何人理解了它。

### 反例 2：用 `Test` 前缀命名夹具

```c
static bool TestBiomeSupportsFauna(int x, int z)   /* test_entity_ecology.c:232 */
{
    PlanetBiome biome = PlanetBiomeAt(x, z);
    return biome != PLANET_BIOME_OCEAN && ...;
}
```

问题：没有断言、返回 `bool`、被别的用例当参数用——它是替身，不是测试。但 `Test` 前缀会让"搜索 Test 函数"的人误以为找到了覆盖点。应命名为 `BiomeSupportsFaunaStub`。

### 反例 3：一个函数测两件不相干的事

```c
static void TestEmptyScriptAndErrorNames(void) {   /* test_debug_dsl.c:326 */
    ...assert(step == DEBUG_DSL_STEP_COMPLETE);          /* 空脚本执行 */
    assert(strcmp(DebugDslErrorCodeName(DEBUG_DSL_ERROR_TIMEOUT), "timeout") == 0);  /* 错误码命名 */
    assert(strcmp(DebugDslErrorCodeName((DebugDslErrorCode)999), "unknown") == 0);
}
```

问题：名字承诺"错误码名称"全表覆盖，实际只测了 1 个真实错误码 + 1 个越界值。应拆成两个测试，并把错误码清单枚举出来。

### 反例 4：成功消息超出实际覆盖

```c
puts("volcano history: deterministic eruptions, cooled deposits, foot, ledgers and bounds passed");
```

而 `test_volcano_history.c` 里只有 `TestMountains` 和 `TestBounds` 两个函数。问题：这行消息是测试对外的自我宣称，声称 5 个方面而实际 2 个函数。要么补齐 5 个方面，要么把消息改成它真正测的东西。

## 6. 提交前

- [ ] 新增/修改的测试满足 §3 的四字段声明（`make test-declarations` 会检查声明是否存在且非空话）。
- [ ] 若给某个存量测试补了说明，同步删掉 `tests/test_declarations_allowlist.txt` 里对应的行。
- [ ] 逐条走过 §4 清单。
- [ ] `make test` 通过，且新增检查在故意破坏时确实会失败（变异验证一次）。
- [ ] 若改的是门禁脚本，失败信息里包含"怎么修"。
- [ ] 若测试覆盖的行为变了，同步 `STATUS.md` 或对应 `docs/architecture/NN-*.md`。

M1 adds a mandatory architecture sweep via
`tests/test_surface_contract_boundaries.sh`. When a test or production change
touches surface consumers, run it with `make test-architecture`; new backend
tokens must use `SurfaceContract`, not extend the exact allowlist casually.

M2 adds `test-cube-vertical-geometry-e2e`: the scene captures the same vertical
wall through legacy and cube under Hyprland, while `test_cube_generation` owns
the exhaustive per-vertex transform and section-bounds invariant.

Cube selection tests must exercise `CubeRaycast` through exact cell boundaries,
not fixed-distance samples. Keep mask, entering-normal and grazing-hit cases in
`tests/test_cube_raycast.c`; its manifest closure includes `surface_geometry.c`
because those exact intersections are part of the production path.

`cube_vertical_geometry_replay.dsl` also checks the interaction identity path:
after `render pick`, it records the cube address, performs `mouse left`, and
requires a second pick to stop seeing that address. Keep this E2E check when
changing hit-result fields or block mutation.

The cube-native tree work keeps two layers separate: `test_cube_generation`
owns deterministic root/taxon/shape, column-section parity, cross-face ownership,
mesh/LOD partition and selective removal; `cube_tree_generation.dsl` owns the
real Hyprland generation/diagnostic/flora-draw path. When changing structure
records or flora diagnostics, update both layers rather than treating one green
test as coverage of the other.

Use `make test-one TEST=<test_name>` during the local edit loop. Run the complete
`make test` only for public contracts, build/test manifests, scheduling,
persistence, shared formats or other cross-component changes. `test-clean-root`
and `test-ci` stay at merge/release boundaries and must not become the default
response to every small fix.

Collision changes need two layers: enumerate the relevant block-catalog shapes
in the unit test (including zero-height decorations and a solid control), then
exercise the scripted movement path in the matching Hyprland scene. Do not infer
shape collisions only from a rendered mesh or from one hand-picked block.
