# 文字测量与截断不变量（原 AGENTS.md §5.1 的测量部分）

**何时读这个文件**：动 `MeasureString` / `MeasureAdvance` / `MeasureDrawnWidth` /
`FitTextToWidth` / `FitCharacterCount` / `FitCharsWithEllipsis` / `ComputeInlineTextLimit` /
`LayoutWidthFloor` / `CreateRowStringFormat` 之前——也就是**任何"这段文字放得下吗"的判断**。

**同读**：[layout.md](layout.md)（行的框、组与面板宽度怎么定）、
[markup.md](markup.md)（内联段的预算由标记决定）、[icons.md](icons.md)（元素占的盒子）。

设计与实测记录：[../state-and-handover.md](../state-and-handover.md) §13 与真机更正一节；
预览复刻：[../../.tool/preview/_preview.md](../../.tool/preview/_preview.md)。

---

## 1. GDI+ 对放不下的字形不是溢出，是丢弃

**GDI+ 不按 layout 框"溢出"文本，它把放不下的字形丢掉。** 所以每段在 `DrawString` 前要按自己的预算调用
`FitTextToWidth()` 截断：右停靠段 = `metrics.textWidth`，**内联段 =
`ComputeInlineTextLimit(textRight, cursor, 跟在它后面的环数, ringSize)`**。内联段曾经写成"实测 advance"，
那是个**恒真的预算**（该函数第一句就是"已经放得下吗"），于是用了 `[[ring:…]]` 的行**从不截断**：
文字切在位图外缘且没有 `…`，它自己的环被挤出面板。`FitTextToWidth` / `FitCharacterCount` /
`FitCharsWithEllipsis` / `ComputeInlineTextLimit` 是纯函数或注入测量的函数，都有断言。

## 2. "文字有多宽"有两个数，不是一个

**`MeasureAdvance`（笔的终点）和 `MeasureDrawnWidth`（GDI+ 报的墨迹范围）。**
`GdipMeasureString` 报的是**墨迹范围**，最后一个字形的墨迹会超过笔的终点（右侧边距），
所以两者能差出字号的三分之一（15 px 的 `W`：笔进 14.43、报 19.43，`WW`/`WWW` 同样是 5.00）。
**GDI+ 对放不下的字形不是溢出，是丢弃**：`WWW` 放进"笔进那么宽"（43.29）的框只出来两个字，
48.29 才三个——这就是"最后一个字不画"。

用谁：**行流游标 / 组的定位 / 自然宽度 = `MeasureAdvance`；layout 框与组的宽度 =
`MeasureDrawnWidth`（两者取大）**。组的宽度必须用后者，因为它同时是相位 C 的预算：用笔进求和，
组内每段都被交给一个差一个右侧边距的预算，"一个字符偏移原位、多个字符少最后一个、末尾加空格就好"
三种症状都是这一个差。

**不要再把两个数合并**：合成笔进 → 框太窄丢末字；合成墨迹 → 续段预留过多，小字号下
`[[center]]W[[/center]]` 的预算会小于笔进、整段被截没（宿主实测 10 px 下 `lost=102`）。
`LayoutWidthFloor` 仍只是"框宽下限"（1.0），**不是**把宽度取整到更窄。

## 3. 框宽下限

**`DrawString` 的框宽下限是 `kMinLayoutWidth`（1.0）**，不能是 0：
0 宽会让 GDI+ 把整段裁掉，这与下面第 5 条是同一个坑的另一面。
截断与框用的是同一个下限（`LayoutWidthFloor`），两者不能各写一个数字。

## 4. `FitTextToWidth` 不缓存省略号宽度

它曾经"按进程缓存一次"，理由写着"字体终生固定"——**不成立**：绘制每帧按设置重建字体。
于是改字号后缓存还是旧字号的宽度，被截断的行可以比预算宽出这个差值（实测 24 px 字体下某个预算
超 6.1 px）。现在每次量一次（旁边本来就有一次二分测量，多这一次可以忽略）。
断言是**扫一遍预算**（40..200）而不是试一个：预留被低估时只有"刚好填满预留"的那些预算会越界。

## 5. 无环行必须拿到完整内宽

**`ComputeRowTextMetrics` 对无环行必须返回完整内宽。** 曾返回 0，
导致 GDI+ 把整行裁掉 → "第一行之后都不显示"。

**`ComputeRowTextMetrics` 的 `textWidth` 不得为负**，下限为 0。

## 6. 内联环的判进必须包含尾随空格

GDI+ 的 `MeasureString` 故意不计尾随空格，所以用**哨兵字符 U+2502**：测 `文字+哨兵` 再减哨兵宽度。
曾少算这一点，导致环压在 `%` 上。

## 7. 行的 `StringFormat`：必须 NoWrap，且只有一处构造

**行的 `StringFormat` 必须带 `StringFormatFlagsNoWrap`（`0x1000`），且只能由
`CreateRowStringFormat()` 建。** 这个 mod 的行是"一行读数"，没有第二行的位置。丢了 NoWrap 的后果
不是"多出一行"，而是**该段首行上浮**：框高只有一行（`RowHeight`），而 `LineAlign = Center`
会把折行后的多行整体垂直居中。一行由多段（文字/环/文字）拼成、每段各有自己的框，于是折行的那段
与相邻段**基线不一致** → 观感就是"第一行文字不在一条直线上"。断言用真 GDI+ 覆盖
（见 [../../.tool/regress/test-layout.md](../../.tool/regress/test-layout.md)）。

**不要把这个函数拆回内联**：测试必须和绘制走同一个函数，否则测的是测试自己那份副本
（这一点已实测踩到，见 AGENTS.md §7 第 8 条）。
