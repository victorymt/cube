# World Frame Cross-Reference P6c（2026-11-13）

- 日期：2026-11-13
- 性质：阶段记录；实现 [Player、World Simulation 与参考系目标架构](29-player-world-simulation-target-2026-11-13.md) 的 P6c API 边界
- 范围：system point 转换、surface/body/system frame、camera-relative point 和 save-local frame

## 1. 实施结果

新增 `space/space_frame.[ch]`，定义 `SpaceSystemPoint`，提供 local game position ↔ system-anchor offset 的 double 精度转换。当前 system 坐标以 star system 为锚点，使用 game-distance 偏移；转换复用 `SpaceOriginX/Z`、系统锚点计算和 `STAR_SYSTEM_SPACING`。

`WorldFrameId` 增加 system anchor 字段和 `WORLD_FRAME_RENDER_CAMERA`，`WorldFramePoint` 继续使用 double 坐标。`WorldFrameTransformPoint()` 现在支持：

- identity；
- surface local ↔ body-centered；
- surface local/body ↔ system；
- 不匹配的 system anchor 或不存在 active surface 时显式失败。

新增 `WorldFrameTransformPointToCamera()` / `WorldFrameTransformPointFromCamera()`：先转换为 active local frame，再以调用者提供的 camera local position 做平移；该 API 明确只处理位置，不声称处理旋转、速度或相机投影矩阵。

新增 `WorldFrameSaveId()`，将当前 V30 的持久化位置明确标记为 local save frame。save 仍使用既有 local Vector3 字段，不改变 V30 编码；需要从 system/body 保存时，调用者先转换到 save-local frame。

## 2. 参考系约束

| 转换 | 当前能力 |
| --- | --- |
| local ↔ body-centered | active surface，已实现 |
| local/body ↔ system | system anchor + space origin，已实现 API |
| local ↔ render camera | camera local translation，已实现位置 API |
| save ↔ local | `WorldFrameSaveId()` 标记，格式不变 |
| body → render/system | 经 local frame 组合转换 |

system anchor、body id、frame kind 和 source/target 不匹配时返回 `false`；非有限输入同样失败。尚未实现相机旋转、轨道/非惯性速度转换或 V30 新 frame 字段。

## 3. 验证

- `test_player_collision`：local ↔ body、local ↔ system、system → save-local 和 local ↔ render-camera round trip；不同 system anchor 和不支持输入失败。
- `make test-architecture`、`make test-headers`、`make test-werror`：通过。
- `make -j4 voxelcraft`：通过。
- 非 entity 注册测试全部通过；`test_entity_ecology` 单独运行通过。

## 4. 已知边界

- 渲染器尚未全面改用 `WorldFrameTransformPointToCamera()`，本轮先提供可测试 API。
- system frame 仍是 game-distance 近似，不是完整的惯性/自转参考系；速度和加速度转换需要后续扩展。
- V30 仍只保存 local pose，没有新增 frame 字段。

## 5. 代码索引

- `src/space/space_frame.[ch]`：system anchor 与 local game position 转换。
- `src/world/world_frame.[ch]`：frame identity、surface/body/system 组合转换、camera-relative 和 save-local API。
- `tests/test_player_collision.c`：frame round-trip 和拒绝路径。
