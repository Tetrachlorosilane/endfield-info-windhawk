# 行内标记语言：图标、对齐、占位符与一次性编译

面向**实现者**。用户向说明写在 [README.md](../README.md)，不变量写进 [AGENTS.md](../AGENTS.md)。
本文记录 2026-10-14 与用户确认后的设计决定（**§7 的第 3 步已完成：行内标记在读取设置时编译；
图标/对齐/占位符仍未实现**），以及为什么这么定。

背景：`Row N icon` 系列设置被用户判定为"失效"（真机原因见
[state-and-handover](state-and-handover.md) §13：为修错位把空行的图标槽丢了，继承源一起消失）。
用户的决定是**不再修这套设置**，改为把图标做成行内标签，与已有的 `[[ring:]]` / `[[font:]]` /
`[[size:]]` 同一套语法。

---

## 1. 删掉图标设置，图标只由标签给出

| 删除 | 原因 |
| :-- | :-- |
| `Row N icon`（4 项） | 行内标签更明确："哪个图标、放在哪、要不要涟漪"由文本本身表达 |
| `Row N icon file`（4 项） | 同上 |
| `Custom icon file` | 同上；要"每行都一样"就在每行写同一个标签 |

删除后 `ModSettings` 里 `IconSpec icons[]` / `iconCount` / `iconPath` / `iconFallbackStyle` /
`iconSlotOfRow[]` 与 `ResolveIconStyle` / `ResolveIconPath` / `ResolveIconSlot` /
`IconPlateDiameter` / `IconPlateCenterForRow` / `AnyIconConfigured` / `MaxIconPlateDiameter`
一并去掉（`ComputeIconGeometry` 保留骨架，改由行内图标驱动）。

**保留**的底层设施（它们与"图标是什么"无关）：图片解码与重采样
（`PlanDownscale` / `DownscaleToFit`）、`kIconPlateRatio` 与字形绘制。

设置块从 58 项降到约 46 项。

---

## 2. 标签语法

一行文本 = 文本 + 元素。元素由标签插入，标签不能跨越 `[[ring:]]` 之外的嵌套（见 §4）。

```
[[icon:字形]]                    一个图标，字形是内置名：CPU / Memory / Disk / Network / GPU
[[icon:file=C:\a.png]]           用图片（也可以给引号路径，走 CopyUserPath 的裁剪规则）
[[icon:Memory:ripple]]           从**这个图标中心**播涟漪
[[icon:Memory:noripple]]         明确不要涟漪（覆盖默认）
[[gap:1.5]]                      占位 1.5 × 该行字号（短占位符，跟着字号走）
[[gap:12px]]                     占位 12 px（长占位符，绝对像素）
[[left]]…[[/left]]               这一段内容整体靠左
[[center]]…[[/center]]           居中
[[right]]…[[/right]]             靠右
```

规则：

* **对齐标签修饰任何内容**：文字段、图标、占位符都可以包在里面（用户明确要求
  "居中/左/右可以像 font 一样修饰任何内容，包括文字段与图标"）。
* **涟漪默认**：一行里**第一个**图标默认播涟漪（与今天"每行一个盘子冒涟漪"的观感一致），
  其余图标默认不播；`:ripple` / `:noripple` 逐图标覆盖。
* **占位符单位由写法决定**：纯数字 = 字号倍数（`1.5` = 该行字号的 1.5 倍），
  带 `px` 后缀 = 绝对像素。这两个单位**只有一处换算**（§5）。
* **对齐与占位符可以组合**：`[[center]][[gap:2]]文字[[/center]]`。
* 容错沿用既有规矩：拼错、缺 `]]`、未知字形、越界像素**一律留下原文**并记一条有界日志，
  绝不让标签吃掉文字（AGENTS §5.7）。

---

## 3. 行内图标取代行首盘

今天的模型是"每行左端一个盘子 + 文字列按盘径让开"。新模型下图标是**行内元素**：

* 图标占用它自己的槽（宽度 = 盘径），后面的元素接着排；
* 一行的**第一个元素**通常就是 `[[icon:…]]`，于是默认版面与今天几乎一样；
* 一行的图标数量不再受 4 行限制（上限由每行的元素数封顶，见 §4）；
* **没有图标行不再有左内缩**：文字从行首开始（今天的 `kIconLeftMargin` 内缩随图标设置消失）。

这与用户"将 icon 不再放在行首"的要求一致：放哪里由标签在文本里的位置决定。

---

## 4. 一次性编译（把文本变成常驻 C++ 对象）

**先说清"编译"不是什么**：它不是静态渲染，不是把一帧的像素或最终几何冻结起来反复提交。
**绘制每帧照旧完整执行**——透明度、悬停、涟漪相位、入场世界变换全都每帧重算。
编译省掉的**只有文本解析**。

> **2026-10-14 实测更正（先量再改）**：**标记解析今天已经在发布时做，不在每帧做**。
> （当时的）`ExpandTokensAndRings` 与 `BuildRowLayout` 的唯一调用点是 `PublishRows`
> （采样线程，一次采样一次；`ExpandTokensAndRings` 后来被
> §7 第 3 步拆成了 `ReadSettings` 里的 `CompileRowText` + `PublishRows` 里的
> `InstantiateRowProgram`），
> `PaintPanel` 只读 `g_rowState`。所以"每帧重新解析文本"这个前提**不成立**，
> 编译化的收益比原先设想的小；每帧真正在付的是**测量**与**选族**：
> `FitTextToWidth` 每个文本段一次二分（多次 `MeasureString`）+ 哨兵/省略号测量，
> 加上每段一次 `PickFontFamily`（覆盖查询有 256 槽缓存，但调用链还在）。
> **因此计划顺序调整为：先把"测量 + 选族"的结果按发布缓存掉**（§7 新的第 2 步），
> 编译成常驻对象（第 3 步）仍然要做——它是 `[[icon:…]]` / 对齐 / 占位符能落地的**结构前提**
> （元素表本身就是这些标签的宿主），只是它带来的每帧收益主要来自顺带缓存掉的测量。

**编译是什么**：把每行的原文解析成一个**常驻的 C++ 对象**（`RowProgram`），
它的生命周期等于**设置版本**，跨帧、跨发布一直存在，每帧只是**读**它。

```cpp
// 常驻：设置变更时构造一次，之后只读。
struct RowPart {
    enum Kind { TEXT, ICON, RING, GAP } kind;
    // TEXT：模板（含 {变量} 占位），到实例化时才填上当前值
    WCHAR text[kRowTextCap];
    int  tagChain;        // [[font:…]] 的链序号，0 = 主字体
    int  runFontSize;     // [[size:NN]]，0 = 该行字号
    int  align;           // 所属对齐组：ALIGN_FLOW / LEFT / CENTER / RIGHT
    // ICON：字形或图片路径 + 是否从这个图标中心播涟漪
    int  glyph;  WCHAR path[MAX_PATH];  bool ripple;
    // GAP：已换算成像素（字号倍数的在这里就乘好，见 §5）
    float gapPx;
    // RING：环的下标（环参数仍来自行模板的 [[ring:…]]）
    int  ringIndex;
};

struct RowProgram {          // 每行一个，定长数组，零堆分配
    RowPart parts[kMaxParts];
    int  partCount;
    int  fontTagCount;       // 供 RowNeedsFontPath 的成本闸门用
    bool hasIcon, hasGap, hasAlign;
};
```

**分成两半，值的部分必须与模板分开**（这是这份设计的要点）：

| 对象 | 生命周期 | 内容 | 谁写 |
| :-- | :-- | :-- | :-- |
| `RowProgram g_programs[kMaxRows]` | **设置版本**（跨帧常驻） | 部件表 + 每个部件的静态属性（字体链、字号、对齐、字形、图片路径、涟漪开关、占位像素） | 设置线程（`ReadSettings`）构造，之后只读 |
| `RowLive g_rowLive[kMaxRows]` | 每次发布 | 每个 TEXT 部件的**当前值字符串**、每个 RING 的当前数值 | 采样线程（`PublishRows`） |
| `RowLayout g_rowLayout[kMaxRows]` | 每次发布（可选再细到几何变化） | 量出来的 advance、截断结果、每个部件的 x/y | HUD 线程首次绘制 |

三者**不许合并**：把值编译进部件表就等于"文本只在配置期有效"；把 `RowProgram` 建在发布里
就等于"每帧仍然解析文本"，编译就白做了。

**约束**（都是从"常驻"这个词直接推出来的，实现时不要绕）：

* **`RowProgram` 里不许持有任何 GDI+ 句柄**。字体对象与图片位图仍然走现有的缓存
  （`AcquireRowFont` / 字体 8 槽、图片按路径共享槽），部件表只存**键**（族名、字号、路径）。
  这样"设置变更时在 HUD 线程 `ReleaseFontResources`"那条规矩不用改，
  也不会出现"程序对象里的画笔活过了 GDI+ Shutdown"。
* **无堆分配**：定长数组 + `int` 下标，与 mod 现有风格一致（§2 的体积理由）。
  析构是平凡的，因此不需要登记进任何释放路径。
* **只读共享**：设置线程写完才发布（与 `g_settings` 同一套同步），HUD 与采样线程只读。

**测量缓存是可选的第二层加速，不是编译本身**。布局（advance / x / y / 截断）在发布后
由 HUD 线程算一次并挂在部件表旁边，缓存键 = 设置版本 + 行几何 + 发布代数
（`g_publishGeneration`，`PublishRows` 真的变了才 +1）。它缓存的**只是文本布局**，
不缓存像素、也不阻止任何每帧动画。

**必须断言**：
1. 编译是纯函数：`CompileRowText(raw) → RowProgram`，同输入两次产出逐字段相等，
   且 `RowProgramEqual` 能识别"只有一个字形变了"（否则该重编译时不会重编译）；
2. 部件表里**没有** `{}` 残留，也没有任何 GDI+ 句柄（后者由"结构体里只有 POD 字段"保证，
   `_deadcode.py` 的字段扫描顺带守住）；
3. 标签容错：坏标签留下的原文进的是 TEXT 部件（与今天的断言同形）；
4. **每帧解析次数为 0**：把解析入口计数器包住，连画三帧（期间只改动画相位）后计数不变；
   负对照——把 `PaintPanel` 改回每帧调用解析，断言必须变红；
5. 缓存失效：发布代数变了必须重算，几何变了必须重算，两者都没变时测量次数为 0
   （测量以函数指针注入，与 `g_coverQueryCount` 同一手法）；
6. 部件的 x 单调递增（除对齐组整体位移外），且最后一个部件的右边界 ≤ 内容宽。

---

## 5. 规范化像素计算

所有尺寸**只由两个输入**推导：该行字号与该行行高。集中在一处，
`RowMetrics`（结构体 + 一个构造函数），谁都不许再自己乘系数：

```cpp
struct RowMetrics {
    float fontSize;        // 该行字号
    float lineHeight;      // 该行行高
    float iconDiameter;    // = fontSize * kIconPlateRatio（图标盘）
    float iconGap;         // 图标与相邻文字的间距
    float emGap;           // [[gap:N]] 的 1 单位 = fontSize
    float ringSize;        // [[ring:]] 的外径（今天来自设置，改成随字号缩放？见下）
    float minRowHeight;    // = fontSize + kRowHeightFontSlack，喂给 RowHeightPx
};
```

* `[[gap:N]]` 与"按字号"的一切都用 `emGap`；`[[gap:Npx]]` 直接是像素；
* **仍然保留的绝对像素设置**：`Panel width`、`Ring size` / `Ring thickness`（用户没要求改）、
  `Ring right offset`、`Corner radius`、`Padding`。它们进 `RowMetrics` 的构造参数，
  但**不再散落在绘制代码里**；
* 断言：给一组 (fontSize, lineHeight) 断言 `RowMetrics` 的每个字段（表驱动），
  以及"改变行高不会改变图标盘径"（＝回归到用户上一次报的那个 bug）。

---

## 6. 影响面

| 位置 | 影响 |
| :-- | :-- |
| 设置块 | 删 9 项（§1） |
| `ReadSettings` | 不再读图标；改为**编译每行的部件程序** |
| `ExpandTokensAndRings` | 被 `InstantiateRowProgram` 取代（它今天既展开又编码标记） |
| `BuildRowLayout` | 被"实例化 + 测量"取代；`TextSeg` 扩成"元素"（文字/图标/环/占位） |
| `PaintPanel` | 行绘制改为"读常驻部件表 + 当前值"（仍然每帧完整绘制）；图标在行内画（不再有行首盘与共享内缩） |
| `_preview.py` | 要跟着改（它今天是"每行左端一个盘"的复刻） |
| README | 「左侧图标」一节重写为「行内图标与对齐」；设置表删 9 行 |
| 岛上的卡片 | **不受影响**（它有自己的几何） |

---

## 7. 迁移顺序（每一步都要能编译、能跑、有断言）

1. **`RowMetrics` + 断言**：只加不改，先把散落的系数集中（行为不变）。**已完成**（`cd3f06d`，
   745 条断言，4 个负对照：盘改回跟随行字号 / `lineHeight` 忽略行高 / `fontSize` 忽略行字号 /
   显式 `None` 仍给盘）。
2. **发布级布局缓存（先做这一步，理由见 §4 的实测更正）**：把每行每段的
   **选族结果 + 拟合结果（截断后的字符数、advance）+ x 位置**在 `PublishRows` 之后
   由 HUD 线程算一次并缓存，键 = 发布代数 + 行几何。之后每帧只 `DrawString`，
   `FitTextToWidth` / `PickFontFamily` 一次都不调。**已完成**（`RowLayoutPlan` /
   `EnsureRowPlan` / `BuildRowPlan`；763 条断言）。要害断言是
   **"复用计划时测量次数为 0"**（产品计数器 `g_measureCalls`，`[perf]` 里的 `meas=`），
   键的**七个字段各有一条**断言把它单独隔离出来（纪元 / 宽度 / 内缩 / 行高 / 字号 / 行数 / 段数），
   负对照实测：去掉缓存 → `reuse` 从 0 变 4 且该断言变红；忽略纪元 → 新样本重测 0 次、
   计划里留着上一份文本（"CPU 12% …" 画成新的那一帧）。
   两处刻意的设计：计划存**族名**而不是 `GpFont*`（字体对象在 8 槽缓存里会被后面的行挤掉，
   指针会悬空），`ReleaseFontResources` 是唯一释放点因而也是纪元的自增点之一。
3. **编译成常驻对象**（`RowProgram` × `RowLive`），先**不**加图标/对齐/占位符，
   只保持现有 `[[ring:]]` / `[[font:]]` / `[[size:]]` / `[[h:]]` 的行为。
   这一步改的是**数据来源**（发布期解析一次 → 读常驻对象），**不是**每帧的绘制流程：
   透明度、悬停、涟漪相位、入场变换照旧每帧算。
   判据：默认配置的断言**逐值不变**；这一步同时是第 4、5 步的结构前提。
   **已完成**（`CompileRowText` 在 `ReadSettings` 里跑一次，结果存进
   `ModSettings::rowProgram[]`——与 `rows[]` 同一份数据，因此不可能与它不一致；
   `InstantiateRowProgram` 在 `PublishRows` 里只展开 `{变量}` 与环的数值字段）。
   763 → 811 条断言。新断言里最重要的两类是**编译器的形状**（无标签行是单个 TEXT 元素、
   `tagChain` / `runFontSize` 逐段等于 `BuildRowLayout` 从标记推出来的值、模板里保留
   `{token}`、坏标签原样留在模板里）与**逐字写出的 marked 串期望表**（含环数）。
   另外做了一次旧实现 ↔ 新实现的差分对照：随机行（标签片段 + 随机字符）× 8 种输出缓冲区
   上界 × 3 种环数上界，4 个种子共 40 万条结果，**零差异**，其中 4431 条同时含"留在文字里的
   `[[`"与"字体标记"——也就是"环画不出来、回退扫描又认出了它里面的标签"这条最绕的路径。
   还没有做的：`RowLive` 与测量缓存合并（§4 的第 6 条），因为图标/对齐尚未落地。
4. **加 `[[icon:…]]`（含涟漪开关）**，同时删掉图标设置与行首盘。
5. **加 `[[gap:…]]` 与 `[[left/center/right]]…[[/…]]`**。
6. 文档、`_preview.py`、CHANGELOG、缺陷表。

第 3 步是风险最大的一步（它换掉行绘制的数据来源），也是唯一无法"只加不改"的一步；
它的验收标准就一条：**默认配置的屏幕输出与今天逐值一致**（用现有的 745 条断言当护栏，
外加 §4 的"每帧解析次数为 0"与测量缓存断言）。
