# 动画不变量（原 AGENTS.md §5.4）

**何时读这个文件**：改展开/收起、入场关键帧、`g_displayHeight` / `g_paintedHeight`、
`PlaceWindow` 的调用时机、热区、宽度随时间变化之前。

**同读**：[window.md](window.md)（窗口几何与命中）、[layout.md](layout.md)（面板宽度的缓存键）、
[../panel-width-design.md](../panel-width-design.md)（宽度为什么不是动画通道）。

真机症状与修复史：[../state-and-handover.md](../state-and-handover.md)（收起闪烁、犯错档案）。

---

- **`g_displayHeight` 是静止几何**（`PlaceWindow` 与 `WM_NCHITTEST` 都用它），
  **`g_paintedHeight` 是当前帧绘制高度**。**展开**方向动画不得写回前者
  （曾混用 → 窗口与命中矩形在动画中途缩到标签尺寸 → "弹出后立刻收起"）。
  **例外是收起方向**：`PaintPanel` 的收缩分支逐帧把 `g_displayHeight` 跟到绘制高度并
  `PlaceWindow`——这正是修掉收起闪烁的手段（见下一条），此刻窗口与位图同高、命中矩形
  跟着窗口，仍然自洽；动画结束时 `UpdateIntro` 把静止值写回。**不要"恢复"成展开时那样
  不写它**，那会让展开高度的位图被裁进 8px 窗口——这一条与下一条是一体的。
- **热区起点用 `kPanelTopOffset`（窗口实际顶部）**，不是显示器顶边。
  曾用显示器顶边 → 面板最上面 6px 落在自己的热区之外。
- **动画进度每帧只采样一次。** `IntroCue()` 每次都重算 `GetTickCount()`，
  调用方若各自再算一次，两次采样可能对"动画是否结束"给出不同答案 → 最后一帧高度回跳。
  以 `g_intro.active` 为唯一状态。
- **收回时窗口必须逐帧跟随收缩**（收缩路径每帧调 `PlaceWindow`）。
  否则展开高度的位图会被裁进 8px 窗口。
- **但绝不能在收起开始时先把窗口缩到标签高度。** 曾经的顺序是「`SetExpanded(false)` 先把窗口缩到
  8px → `PaintPanel` 再把它长回动画高度才画」，同一拍内 168→8→168 的往返里存在
  「窗口 8px 高、却没有任何位图与之匹配」的一瞬间，合成器只能给出一条切片——**那就是收起时的
  闪烁**。规矩：**窗口尺寸的变化必须与它提交的位图在同一帧内一致**。展开方向可以先定位
  （面板长进一片本来什么都没有的空间），收起方向必须由绘制侧逐帧跟随内容
  （`if (expanded) PlaceWindow(hwnd);`）。绘制分支里那句 *"SetExpanded already shrank the window
  to the resting (tab) height, so the frame has to grow it back"* 正是这个往返的补偿——
  **补偿存在，说明顺序错了**。
- **宽度不是动画通道，也不许变成动画通道。** `EnsurePanelWidth` 只在一个**状态**改变时
  重新解析宽度（缓存键见 [layout.md](layout.md)），解析发生在位图与 `PlaceWindow` **之前**，
  所以"窗口尺寸的变化必须与它提交的位图在同一帧内一致"这条在宽度方向上是靠**顺序**满足的，
  不是靠补偿：`FrameWidthFor` → `PlaceWindow` → `CreateDIBSection(width)`。Fit content 模式下
  展开/收起**不改宽度**（行没变，宽度就没变），卡片来了又走才让它换到卡片自己的规则再换回来。
  **不要因为"宽度现在会变了"就把逐帧过渡加回来**——那正是 §"方法论教训"第 12 条里改了四轮的那个闪烁。
- **展开与收回刻意不对称**：展开先生长定型、再让内容分层浮现；收回贯穿整段、时长为
  **四分之一**（`kCollapseDurationRatio = 0.25`）。
- **收回的关键帧 cue 是 `1.0`，不是 `kCollapseDurationRatio`。** 那个常量是**时长比例**；
  把它当 cue 用（`{kCollapseDurationRatio, end}`）会让收缩在半程就结束，剩下半程窗口停在
  标签高度、而帧时钟仍以 16 ms 重绘它——可见收回只有 `IntroMs × ratio²`（当年 ratio 还是
  1/2，所以是 `IntroMs/4`；现在 ratio 是 1/4，再犯就是 `IntroMs/16`）。
  断言：`the shrink still has work left at the midpoint`。
- **文字淡入的终点有自己的常量 `kCueContentFull`，不是共用的 `kCueContentReady`。**
  后者是 `Fade`（`introStyle == 1`）那条纯淡入分支的窗口末端，**那条分支没有提速**，
  所以改淡入速度时不能去挪它。阶梯淡入的四个中间级是**窗口内的分数**
  （`kCueTextFadeAt1..4`），窗口 = `kCueContentSpan / kCueTextFadeSpeed`——改速度只动
  后者一处，升上来的形状不变；起点 `kCueContentIn` 不动（必须晚于 `kCueSettle`，
  "先长定型、数字再浮现"）。
- **方向判定用 `start = fromHeight`、`end = toHeight`**，方向由两者大小自然决定。
  曾按方向重排 start/end → 收回时高度反而在增长。
- **过冲强度 `kPanelBounceStrength = 0.04`**（原项目 0.275）。0.275 在 250px 面板上是
  弹过头 14px 再收回，正是"弹出又收回"的观感来源。
- **岛上的卡片有自己的相位**（`Popping` / `Card` / `Collapsing`，由卡片状态机驱动，
  与面板的入场动画共用 16 ms 帧时钟）：卡片几何、时长与"关闭卡片和 ttl 到期走同一条路"
  见 [../external-data-island-design.md](../external-data-island-design.md) 的「命名管道不变量」一节，
  **动卡片动画前先读那里**。
