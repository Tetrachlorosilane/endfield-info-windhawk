# 窗口与置顶不变量（原 AGENTS.md §5.5 + §8 的平台约束）

**何时读这个文件**：改窗口样式、`WS_EX_*`、`WM_NCHITTEST`、圆角穿透、置顶策略、
多显示器锚定，或碰"全屏独占应用"这类平台限制之前。

**同读**：[animation.md](animation.md)（谁在什么时候 `PlaceWindow`）、
[layout.md](layout.md)（窗口矩形与位图必须同宽）。

管道/岛的窗口侧（区域表、命中顺序、关闭按钮）在
[../external-data-island-design.md](../external-data-island-design.md) 的「命名管道不变量」一节。

---

## 1. 窗口与置顶（原 AGENTS §5.5）

- 面板**永远不隐藏**，收起只是缩成 8px 标签。否则收起后无法点回来。
- `WS_EX_NOACTIVATE` 始终保留（不抢焦点）。
- 只有 `WM_NCHITTEST` 在圆角外返回 `HTTRANSPARENT`，因此圆角外点击会穿透。
- 置顶用三重手段：创建时样式位 + `SetWindowPos` 显式写位 + 每 100ms 重申。
  **周期性重申换来的不是正确性而是延迟**（已用独立探针验证），所以不要因为
  "反正会重申"就省掉创建时的样式位。

## 2. 平台限制（原 AGENTS §8）

- **全屏独占（exclusive fullscreen）的 3D 应用无法被覆盖**——它绕过 DWM 合成，
  任何窗口都不可能叠在上面。这是平台限制，不是可以通过代码解决的问题。
- **多显示器**：锚定"最上方"的显示器（`rect.top` 最小者，平手时取主显示器）。
  本机只验证过 2 台。
