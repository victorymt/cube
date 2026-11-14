# Frame Save Boundary P8b（2026-11-14）

- 日期：2026-11-14
- 性质：阶段记录；收口 [World Frame Cross-Reference P6c](36-world-frame-cross-p6c-2026-11-13.md) 的 caller 采用边界
- 范围：V30 Player save/load 的 frame identity、renderer 坐标系定位和不采用的通用相机相对方案
- 当前状态：见 [STATUS.md](../../STATUS.md)

## 1. Save 边界采用

Player save 写路径现在显式执行：

```text
player local WorldFramePoint
  -> WorldFrameSaveId(active surface bodyId)
  -> WorldFrameTransformPoint()
  -> V30 local save fields
```

load 路径执行反向转换，并在写入 Player 前验证有限值、body identity 和目标 frame。这里仍然是 local -> save-local 的恒等位置转换，因为 V30 当前只保存 local pose；采用 frame API 的目的不是改变数值，而是防止 save boundary 继续无条件假设“当前 local 坐标就是永久地址”。

V30 编码没有变化：仍写 local position/velocity/yaw/pitch/floating 和可选 canonical `SurfacePose`。速度、朝向和角速度的 frame 转换尚未定义，本轮不伪造这些语义。

## 2. Renderer 边界

renderer 不改成以 `WorldFrameTransformPointToCamera()` 返回的坐标直接提交 raylib。原因是当前 `BeginMode3D(camera)` 已经使用 `camera.position` 建立视图变换，而 surface/chunk 的 topology-native transform 负责把 section-local surface geometry 投影到当前 local-world/tangent frame。再显式减去相机位置会重复平移。

因此 renderer 的契约保持为：

- 渲染输入是当前 active local-world frame；
- surface、cube 和 planet 的特殊方向/曲率由各自的 topology-native transform 处理；
- `WorldFrameTransformPointToCamera()` 只用于明确需要 camera-relative 数值的跨 frame 计算，不作为所有 draw call 的统一坐标格式；
- camera 本身仍只消费 `CameraTargetView`，不成为模拟状态写入者。

这比“所有 caller 都必须调用 camera frame API”更严格：只有语义确实进入 camera frame 的 caller 才调用，普通 local-world draw 不制造重复变换。

## 3. 验证

- `test_game_save_spherical`：验证 local -> save frame -> local 的 Player 位置 round trip，并拒绝非有限位置。
- `test_player_collision`：保留 local/body/system/render-camera frame 转换和 unsupported pair 回归。
- `test_game_save_spherical` 的 source closure 增加 `world/world_frame.c`，确保 save caller 不是只在 mock 中验证。
- 完整 `make test`、architecture、headers、Werror、docs gate 和完整程序构建作为阶段门禁。

## 4. 已知边界

- V30 仍不记录 frame kind、body id 或 system anchor；save-local 身份由当前 topology snapshot 和 optional `SurfacePose.bodyId` 约束。
- 速度、加速度、朝向和角速度尚无跨 frame 契约。
- system frame 仍是 game-distance 近似；不是完整惯性/自转参考系。
- 具体的 camera-relative 消费者应显式声明源/目标 frame，不能依赖 renderer 默认转换。

## 5. 代码索引

- `src/app/game_save.[ch]`：Player local -> save-local 及反向转换、测试 seam。
- `src/world/world_frame.[ch]`：save id、frame identity 和位置转换。
- `src/presentation/surface_render_transform.c`：topology-native local-world render transform。
- `tests/test_game_save_spherical.c`：save boundary round trip 与非法输入。
- `tests/test_player_collision.c`：通用 frame pair 回归。
