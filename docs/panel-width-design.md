# 面板宽度：由内容决定，或固定

**状态**：已实现（2026-10-15）。设置项 `Panel sizing` / `Panel minimum width` /
`Panel maximum width`；`Panel width` 保留原义，成为 **Fixed 模式**的宽度。

---

## 1. 决策

**面板（不是卡片）的宽度可以跟随内容**，并且**只能落在配置的两个界之间**。

- 「跟随内容」= 让**最宽的一行**决定面板宽度。四行是**一列**，不是四块：
  面板宽度只有一个，所以取各行自然宽度的最大值。
- 「两界」= `Panel minimum width`（下限）与 `Panel maximum width`（上限），
  上限还要再**过一遍屏幕钳位**（见不变量 iii）。
- **默认行为一个字都没变**：`Panel sizing = Fixed`（默认）时面板就是 `Panel width`，
  和这个功能之前逐像素一致——那是默认路径，也是「默认配置零成本」的意思。

**为什么是面板的宽度、而不是卡片的宽度。** 灵动岛卡片有它自己的宽度规则
（`ComputeIslandCardWidth` / `ComputeIslandRect`，见
[external-data-island-design.md](external-data-island-design.md) 的 §8），
那是一条**独立**的规则：卡片要放下一段标题和一段正文。这次改的是**面板**宽度
（窗口矩形、位图、命中矩形、文字列），卡片在屏幕上时仍然用它自己的那条规则。
把两者合成一条会让「卡片压住行文字」时说不清是谁让步——所以**两条规则各自拥有自己的问题**，
交汇点只有一个（`FrameWidthFor` 的 `cardUp` 分支）。

**这不是「宽度动画」。** 这个项目里宽度动画被移除过**四次**
（AGENTS §7.12：每次都以「某一帧窗口已改、位图还没跟上」的闪烁告终）。
内容自适应**不是**动画：同一个状态**永远**得到同一个宽度（不变量 ii），
只有状态变了才重新解析。一个每帧重算的宽度就是那个动画，只是换了个理由。

---

## 2. 三条不变量

### (i) 宽度在 HUD 侧从**实测内容**解析，不在设置期决定

宽度是 `ResolvePanelWidth()` 算出来的，它住在 HUD 线程里、紧挨着行布局测量
（`MeasurePanelContentWidth` / `MeasureRowNaturalWidth` / `NaturalSegmentSpan`）。
原因很直接：**设置期不知道宽度**。要量一行需要

- 解析过的行程序（`CompileRowText`，发布期产物）；
- 该行的字号、行高（`RowFontSizePx` / `RowHeightPx`）；
- 一个能 `MeasureString` 的 `GpGraphics`（`MeasureGraphics()` 的常驻 16×16 暂存面）。

这三样东西在 `ReadSettings()` 里都不存在。所以 `ModSettings::panelWidth` 只有一个含义：
**Fixed 模式的宽度**，外加 Fit content 模式下**测量基准宽度**（见 §4 的最后一条）。
`EnsurePanelWidth()` 是唯一写宽度的地方，其他所有地方都**读**它。

### (ii) 对一个状态，宽度跨帧恒定，并且在位图与 `PlaceWindow` **之前**确定

一个「状态」= 同一份设置（`g_layoutEpoch`）+ 同一批已发布行（`g_publishedRows`）
+ 同一张卡片状态 + 同一台显示器宽度。这四项组成的键**全等**时 `EnsurePanelWidth()`
直接返回缓存，一次测量都不做（断言 `and the next two frames resolve it zero more times`）。

同一帧内的**顺序**是这条不变量的一半：

```
PaintPanel:  EnsurePanelWidth()  ->  g_panelWidth     （先解析）
             FrameWidthFor()     ->  width            （再取本帧宽度）
             if (width != g_restWidth) PlaceWindow()  （窗口在位图之前）
             CreateDIBSection(width)                  （位图用同一个数）
```

`g_lastBitmapWidth` 记的就是最后那个数，断言 `and the window rectangle is the width of the
bitmap being submitted` 拿它和窗口宽度比。**没有任何一帧允许先改窗口、后交位图**——
这正是 AGENTS §7.12 那条教训，也是宽度动画被移除四次的原因。

### (iii) 上限也要过屏幕钳位

`PanelWidthCap()` = `min(Panel maximum width, 屏幕宽 − 24)`。
24 px 是两侧各 12 px 的边距，与卡片自己的屏幕钳位同源。
`screenWidth == 0` 表示「还不知道是哪台显示器」，此时**只认配置的上限**，
不会把上限当成 0 而一路塌到下限。

> 下限**不**过屏幕钳位：用户手写的 `Panel minimum width` 是他明确的意愿，
> 屏幕钳位是拦「内容无限长」的，不是拦用户。超出的文字由逐段截断收掉（§5）。

---

## 3. `ResolvePanelWidth` 的规则（唯一决定宽度的地方）

```c
PanelWidth ResolvePanelWidth(int contentWidth, int configuredMin,
                             int configuredMax, int screenWidth);
```

**纯函数、全定义**：任何输入都有答案，没有任何输入能产出 < 24 或 > 1600 的宽度
（断言 `no input produces a width outside the chrome..ceiling band`，
在 −200..4000 上扫一遍）。

| 条件 | 结果 | `clamp` |
| :-- | :-- | :-- |
| `cap < floor`（配置的 min > max） | `cap` | `PWC_MAX` |
| `content < floor` | `floor` | `PWC_MIN` |
| `content > cap` | `cap` | `PWC_SCREEN`（屏幕赢了）或 `PWC_MAX` |
| 其余（含两端相等） | `content` | `PWC_NONE` |

其中

- `floor = PanelWidthFloor(configuredMin) = max(configuredMin, kPanelMinAbsoluteWidth)`
- `cap   = PanelWidthCap(configuredMax, screenWidth)`
- `kPanelMinAbsoluteWidth = 2 × kPanelSidePadding = 24`——**比它自己的内边距还窄的面板不是面板**。
  这是下限设置项 200 之后**再一道**兜底：即使有人绕过设置（屏幕钳位、直接调用）也不会得到
  0 宽或负宽的位图/窗口。
- 两端**包含**：`content == floor` 与 `content == cap` 都算「内容就是宽度」，不是钳位。
- `contentWidth < 0` 一律当 0。

`clamp` 是**哪一个项赢了**的记录（不是裸 int），因为「上限赢了」和「下限赢了」正是这个功能
全部要说清的两件事，两者都有断言。它出现在宽度日志里
（`Panel width: fit content=…px -> …px clamp=…`，0 无 / 1 下限 / 2 上限 / 3 屏幕）。

### `min > max` 的退化规则

`cap` 赢，面板退化成**常数 `cap`**（也就是宽度变成固定值），
断言 `an inverted minimum/maximum resolves to the maximum, not to the minimum`。

理由：一个把上限写得比下限小的用户，最可能的意图是「这就是它最宽能到的样子」。
两种选择都说得通，所以**选一个并写下来**——不定行为才是缺陷。

### `PanelWidthForContent`

```c
int PanelWidthForContent(int contentWidth);   // contentWidth + 2 * kPanelSidePadding
```

`ResolvePanelWidth` 的输入是**面板宽度**（内容 + 两侧 chrome），不是纯内容宽度；
两者分开是为了让 padding 与钳位各自能被断言（`kPanelSidePadding = 12`；
断言 `the width the content needs is the content plus the chrome on both sides`）。

这个 padding 必须与行文字列**精确互补**：
`ComputeRowTextMetrics(resolved, …).textWidth == contentWidth`
（断言 `and the resulting row column is exactly the content width it was sized for`）。
否则一个刚按内容定好宽的面板会**截断它刚刚判定放得下的文字**。

---

## 4. 「自然宽度」是怎么量的

`MeasurePanelContentWidth` → `MeasureRowNaturalWidth` → `NaturalFlowingAdvance`
→ `NaturalSegmentSpan`，逐段累加，**不截断**：

- **元素**（环 / 图标 / `[[gap:…]]`）走 `RowElementAdvance`，那是 `BuildRowPlan`
  同一个函数。两条路径共用它，所以「面板按多宽定的」与「这一行实际排成多宽」
  **不可能漂移**——某个图标在一条路径里拿到不同间隙，就会按一个它并不在其中绘制的宽度去量。
- **文字段**用 `MeasureAdvance`（真实 GDI+ 测量）× 该段**自己的字号**，
  再加上它后面每一道环的 `ringSize + 4`——与 `FlowTextLimit` 从预算里减掉的是同一笔账。
- 元素一律按**参与流**（`inlineFlow = true`）计。停靠的环在**自己的坐标系**里从右边缘出发，
  问它「这一行内容多宽」需要先知道宽度——正是这个函数要产出的东西，循环。

**代价只在状态变化时付**：`EnsurePanelWidth` 的缓存键（不变量 ii）确保中间那些帧
一次测量都不做。Fixed 模式**根本不问内容宽度**
（`PanelWidthIsContentDriven()` 是唯一闸门，断言 `Fixed mode does not ask for a content width
at all` 与 `and three Fixed frames measure nothing`，实测 `measure calls 1599 -> 1599 over 3
frames`）。

**测量基准宽度**：Fit content 模式下 `Panel width` 仍被用来做**行文本列的基准**
（`ComputeRowTextMetrics` 的入参之一）。第一帧、以及 GDI+ 还没起来的那一帧，
面板就按它绘制；下一帧起由解析值接管。所以它**不是**废设置项，只是**降级为基准**。

---

## 5. 卡片在屏幕上时用哪个宽度

**卡片自己的规则赢**（`FrameWidthFor(..., cardUp = true)` →
`ComputeIslandRect(...).w`，与 `ComputeIslandCardWidth` 一致）。
断言 `and while a card is up the card's own width rule still wins`。

这条不只是「保持一致」：卡片的宽度也是**命中测试矩形的来源**，
所以由同一个函数给出，卡片就不能「画成一个尺寸、点击按另一个尺寸接受」。

卡片离开后宽度立刻回到面板自己的解析值，并且**在同一帧内**先改窗口再画
（`width` 已经回到解析值 → `PlaceWindow` → 位图）。这是 AGENTS §5.4 那条
「窗口尺寸的变化必须与它提交的位图在同一帧内一致」在宽度方向上的同一句话。

---

## 6. 超过上限之后

上限是**界，不是承诺**。一行要的比上限多时：

1. 面板停在 `cap`，不再变宽（断言 `the panel stops at the configured maximum, not at what the
   row wants`）；
2. 该行由**既有的逐段截断**接管（`FitTextToWidth` → `…`），
   所以没有任何东西被画到面板外（README「一行装不下会怎样」）。

断言 `past the cap the row still needs more than the panel can give it` 就是把这两件事接起来：
内容宽度 > 面板宽度，而面板宽度 == 上限。

---

## 7. 断言清单（`.tool/regress/test-layout.cpp`）

按节列出。**括号里是负对照**——把对应的改动回去各红几条，2026-10-15 实测。

**纯函数段**（不需要 GDI+）：

- 区间内 / 边界包含：`inside the range the content is the width`、
  `and nothing clamped it`、`exactly the minimum is not a clamp (the boundary is inclusive)`、
  `exactly the maximum is not a clamp either`
- 下限：`below the minimum the floor is the width`、`and the floor is what clamped it`
- 上限：`above the maximum the cap is the width`、`and the configured maximum is what clamped it`
- **屏幕钳位**：`the cap is lowered to the screen minus 24 px`、
  `so a row that wants 2000 px on a 1280 px screen is capped by the screen`、
  `and the clamp is reported as the screen's, not the setting's`、
  `a maximum already inside the screen is left alone`、
  `an unknown screen leaves the configured maximum in charge`、
  `and content inside the maximum is in charge of itself`、
  `an unknown screen is not treated as a zero-width one`
  （**去掉屏幕钳位 → 红 4 条**，见下）
- chrome 下限：`a screen narrower than the chrome raises the cap to the chrome floor`、
  `a screen still wider than the chrome is the screen minus 24`、
  `a minimum below the chrome floor is raised to it`、`and the floor still reports itself as the floor`、
  `the floor is never below the chrome`、`not even one pixel below it`、
  `and exactly the chrome is the chrome`、`one pixel above it is the caller's own minimum`、
  `even with every setting at zero the panel is at least its own chrome`
- 退化输入：`a negative content width is treated as no content`、
  `no content at all falls back to the minimum, not to zero`
- `min > max`：`an inverted minimum/maximum resolves to the maximum, not to the minimum`、
  `and it is reported as the cap winning`、
  `an inverted pair does not move with the content (it is a fixed width)`、
  `nor does a row wider than either end`
- 全输入扫描：`no input produces a width outside the chrome..ceiling band`、
  `and the sweep would have named the bad input`
- padding 与列宽互补：`the width the content needs is the content plus the chrome on both sides`、
  `a negative content width still leaves the chrome`、
  `and the resulting row column is exactly the content width it was sized for`

**真文字段**（真 GDI+；GDI+ 或暂存面起不来时按**计数跳过**，不静默）：

- 文字量得出来：`the real font really does measure those two runs differently`、
  `a text-only row needs exactly its own text, as the plan measures it`、
  `and a longer row needs more, by the text's own width`、
  `which is a difference big enough to move the panel`
- 元素按计划的盒子计：`an icon-bearing row needs its icon's own box, as the plan places it`、
  `a fixed 20 px gap widens the row the measurement reports`
- 字号：`the same text at a larger size needs a wider panel`
- 跟随内容、止于上限：`the longer row reports a larger natural content width`、
  `a short row's panel is the floor, not a degenerate width`、
  `and a longer row yields a wider panel, up to the cap`、
  `the panel stops at the configured maximum, not at what the row wants`、
  `and the cap is recorded as the reason`、
  `past the cap the row still needs more than the panel can give it`
- 每状态恒定：`the same state measures the same content width every time`、
  `so it resolves to the same panel width every time (constant per state)`
- 计划的新鲜度键带着宽度：`the plan built at the resolved width is fresh at that width`、
  `and a single pixel of width makes it stale, so the key really carries it`、
  `so does a new publish generation`、
  `and the runs the plan fitted at that width stay inside the panel`
- 解析次数（缓存的证据）：`the first frame of a state resolves the panel width once`、
  `and the next two frames resolve it zero more times (constant per state)`、
  `so all three frames of that state have the same width`、
  `and that width is the resolved one, not the configured Panel width`、
  `a new publish generation resolves it once more, and only once`、
  `and the same state still produces the same width`
- 帧内一致：`ComputeGeometry leaves a resolved width alone (it only seeds an unset one)`、
  `the resolved width is not the configured one, so the next check can tell them apart`、
  `the window and the bitmap agree, and the width is the resolved one`、
  `which is not the configured Panel width (900)`、
  `and the window rectangle is the width of the bitmap being submitted`、
  `and while a card is up the card's own width rule still wins`
- Fixed 免费：`Fixed mode does not ask for a content width at all`、
  `and three Fixed frames measure nothing (the measurement counter does not move)`、
  `the Fixed width is the configured width, unchanged`、
  `and a new publish does not make a Fixed panel measure either`、
  `while Fit content does (so the gate is really what was tested)`

### 三条负对照（实测）

| 负对照 | 结果 |
| :-- | :-- |
| `ComputeGeometry` 用配置宽度覆盖解析值（`g_panelWidth = ClampInt(panelWidth, …)` 无条件执行），并且帧宽度取 `g_settings.panelWidth` | **3 条红**：`ComputeGeometry leaves a resolved width alone (it only seeds an unset one)`、`the window and the bitmap agree, and the width is the resolved one`、`which is not the configured Panel width (900)` |
| 去掉 `PanelWidthCap` 的屏幕钳位 | **4 条红**：`the cap is lowered to the screen minus 24 px`、`so a row that wants 2000 px on a 1280 px screen is capped by the screen`、`a screen narrower than the chrome raises the cap to the chrome floor`、`a screen still wider than the chrome is the screen minus 24` |
| 去掉缓存（每帧重算） | 1 条红：`and the next two frames resolve it zero more times (constant per state)`。**再让宽度真的逐帧变化**（把解析次数掺进宽度）才红到 `so all three frames of that state have the same width` 与 `and the same state still produces the same width`（共 3 条）——**「每帧重算」本身只花掉缓存，不产生不同的宽度**，所以后两条要的是「变化」，不是「重算」 |

> 版本警告：**第一版帧内断言是恒真的。** 它在 `ComputeGeometry()` 之后把解析宽度
> 重新写回 `g_panelWidth`（「恢复」一下），于是无论 `ComputeGeometry` 干了什么，
> 后面比的都是它自己刚写进去的值——把绘制路径改成读 `Panel width` 也**照样全绿**。
> 修法有两半，缺一不可：产品侧 `ComputeGeometry` 只在**未设置**时播种宽度
> （`if (g_panelWidth <= 0)`，它会在展开/收起/改设置时被调到，覆盖解析值就是
> 「窗口与位图不同宽」的来源），测试侧**按产品的顺序**走一遍，中间不补写。
> 教训与 AGENTS §7.8/§7.13 是同一条：**判据是「改坏它会不会红」，不是断言的字数。**
