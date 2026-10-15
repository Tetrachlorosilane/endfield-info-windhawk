# 行内图标不变量（原 AGENTS.md §5.3）

**何时读这个文件**：加图标字形、改 `[[icon:]]` 的解析、动图标直径/圆心/涟漪、碰图片载入、
重采样、图标缓存或 `ComputeIconImageDrawSize` 之前。

**同读**：[markup.md](markup.md)（`[[icon:]]` 是编译期变成段的元素）、
[layout.md](layout.md)（元素占的盒子与行高）、[theme.md](theme.md)（环的颜色来源）。

设计与语法：[../inline-markup-design.md](../inline-markup-design.md) §1–§3；
预览复刻（画同一套几何，必须同步）：[../../.tool/preview/_preview.md](../../.tool/preview/_preview.md)。

---

图标是**标签放进去的普通元素**（`[[icon:…]]`），不再是设置项，也不再是行首那个盘：

- **`[[icon:字形]]` / `[[icon:file=路径]]`，`ParseIconTag` 在编译期解析**：字形名走
  `IconStyleFromName`（`CPU` / `Memory`/`RAM` / `Disk` / `Network`/`Net` / `GPU`），
  路径是**行文本里的一段切片**（`RowPart::offset/length`）——盘径与图片路径都不进 `RowPart`
  的数组，否则每个 element 都要背一个 `MAX_PATH`。
  **拼错的一律留下原文**（未知字形、空 body、`file=` 后面什么都没有、缺 `]]`），
  与其它标签同一条容错规矩：一个空盘子会让人往完全错误的方向查。
- **它占自己的盒子，行是"流"。** `BuildRowPlan` 把每个图标变成 `x` + `diameter`，
  后面的元素从 `x + diameter + kIconContentGap` 开始；图标在**该行的垂直中心**
  （`IconElementCenter`，盘和它的涟漪都用这一个点算）。
  **没有行首盘，也没有"所有行共用的文字列内缩"**：文字从行自己的 `kRowTextLeftInset` 开始。
  所以"图标画在哪个 x"只有 `BuildRowPlan` 一个来源，绘制侧只读计划。
- **每个图标一个尺寸，没有全局图标尺寸。** 直径 = **该元素所在处**的字号 × `kIconPlateRatio`
  （`IconDiameterForFontSize`，唯一乘系数的地方），字号取 `ElementFontSizePx`（段字号或该行字号）。
  见 [layout.md](layout.md) 那条：这条规则与旧的"盘只跟随全局字号"是同一个问题的两个前提。
- **环半径从盘半径起步**向外扩张（`RingRadiusAt(plateRadius, …)`）。
  曾用 `盘半径 × 0.5` 起步，环比盘小、压在字形上，看起来像"不同心"。
- **环必须画在入场世界变换之内**，否则与盘处于不同坐标系。
- **涟漪默认只从一行的第一个图标发出**（那是"每行一个盘冒涟漪"的旧观感），
  其余默认安静；`:ripple` / `:noripple` 逐图标覆盖（`IconElementRipples`，"第一个"按**布局里
  第一个图标段**算，不是按编译顺序）。
- **自定义图片：小而宽高不等的源图，两轴不能分别钳位。** 曾导致 2:1 的图被压成 1.35:1。
- **用户路径必须裁剪引号与前后空白**（`CopyUserPath`，`[[icon:file="C:\a b.png"]]` 也走它）。
  曾把引号一起传给文件 API → "文件明明在却 could not load"。
- **图片载入失败必须回退到内置字形**。否则只剩空盘，看起来像"图标变纯白"，会把人引向
  完全错误的排查方向。行内模型里这条由 `kIconImageFallbackGlyph`（`Memory`）实现：
  图片图标的 `entry.glyph` 在 `BuildRowPlan` 里补成它，`PaintIconElement` 先试图片、
  失败才画这个字形。
- **图片按路径缓存，不再按行**（`g_iconCache`，`kIconCacheSlots` 个条目）：
  一行里同一个文件出现两次、或四行都写它，都只解码一次。
  **淘汰规则**：先占空条目，否则丢弃**最久未使用**的那个（条目级 LRU，`lastUsed` 时钟）；
  换路径时 `ReleaseIconEntry` 是唯一的释放点。
  每个条目里再留 `kIconVariants` 份重采样结果（变体级 LRU）：**同一个文件在两个字号下出现是
  两个重采样目标**，只留一份会让它在同一帧里反复重建位图。
  条目里**不放 GDI+ 句柄以外的东西**，`RowPart` / `RowProgram` 里则一个句柄都没有。
- **自定义图片在载入时按该图标直径重采样一次**，不在每帧缩放。大图一步压到 23px 会有锯齿，
  且每帧重采样很贵。**逐级折半对"短边已经小于目标箱"的横幅同样成立**：
  `PlanDownscale` 只按比例折半，不把短边钳回目标（那个钳位有不动点，会让宽扁图做十几次
  1:1 空转、然后一次性压 173 倍）；短边在中间步骤允许降到 1px，末级仍由长边定尺。
- **图片的绘制尺寸 = min(自然尺寸, 图标直径) × 入场缩放**（`ComputeIconImageDrawSize`）。
  "不放大超过自然尺寸"是给**基准**尺寸的天花板，不是第二次钳位。它曾经被写成
  "只有比盘还大的图才乘缩放"，而缓存图恰好**等于**盘径，于是自定义图片只会淡入、
  不随盘子长大（内置字形那条路径一直是乘缩放的）。
