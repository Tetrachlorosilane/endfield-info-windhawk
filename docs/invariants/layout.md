# 布局不变量（原 AGENTS.md §5.1 的布局部分）

**何时读这个文件**：改行高、行顶、前缀和、面板宽度、`ComputeGeometry`、绘制预算或静态数组字段之前。

**同读**：[text-measurement.md](text-measurement.md)（"文字有多宽"与截断）、
[markup.md](markup.md)（`[[size:]]` / `[[gap:]]` / 对齐组的编译侧）、
[icons.md](icons.md)（行内元素占的盒子）。

**这些不是设计文档，是规范条文**：每条都对应一次真实回归，请连同它点名的那条断言一起改。
设计与推导在 [../font-and-row-metrics-design.md](../font-and-row-metrics-design.md)、
[../panel-width-design.md](../panel-width-design.md)。

---

## 1. 行高、行顶与面板高度（原 AGENTS §5.1）

- **行不再是等高的**：每行有自己的字号与行高（`Row N font size` / `Row N height`，或行首的
  `[[size:NN]]` / `[[h:NN]]`，标签优先）。三个派生量只有一处实现，谁都不许再自己乘一遍：
  `RowFontSizePx`（0 = 继承全局）、`RowHeightPx`（**不小于 `MinimumRowHeightForFont`**）、
  `RowTopPx` / `PanelRowsHeight`（前缀和）。`ComputeGeometry` 的 `g_panelHeight` **必须**来自
  `PanelRowsHeight`——它同时是窗口高度、命中矩形高度与入场目标高度，**写成 `rowHeight × 行数`
  就会让面板高度与画面不一致**（这一条有断言：`ComputeGeometry` 算出的高度必须等于各行之和）。
- **行高下限是"字放得下"，不是美学选择**：`kRowHeightFontSlack = 15` 正是默认 `Row height 30`
  与 `Font size 15` 之差。去掉它，30 px 字号的行会溢出到相邻行上（GDI+ 不裁剪字形）。
  改这个常量会同时改变默认配置的外观，改前先看本节最后那条断言。
- **行高下限用"这一行里最大的字"**（`ModSettings::rowMaxFontSize` = 本行字号与
  `MaxInlineRunSize` 的较大者），不是本行字号：否则 `[[size:30]]` 出现在小字行里会溢出到下一行。
- **`[[size:NN]]` 有两副面孔，区别只在位置**：写在**行首**是**行属性**（`ReadSettings` 的
  `ParseRowMetricTags` 把它从 `rows[]` 里剥掉，同时决定该行的字号与高度下限）；写在**别处**是
  **段属性**（`InstantiateRowProgram` 换成 `kSizeMarkStart` + 两位十进制数字 + `kRingMarkEnd`，
  `[[/size]]` 换 `kSizeMarkEnd`），由 `BuildRowLayout` 记进 `TextSeg::runFontSize`。
  所以"段内 `[[size:20]]` 当普通文字"这条**只对行高成立**：它照样能改字号，但改的是那一段。
  绘制侧按段取字号（`AcquireRowFont(…, segFontSize)`），段不嵌套——`[[/size]]` 回到该行自己的字号，
  与 `[[/font]]` 回到主字体是同一条规矩。
- **行首标签只认行首**：前缀解析在 `ReadSettings` 里完成、标签已从 `rows[]` 剥掉，所以绘制侧只看得到
  段标签；`[[h:…]]` 写在文本中间是**普通文字**，写错的一律留下原文。
- **两套"行号"必须分清**：设置按**设置里的行号**命名，绘制只知道**显示顺序**。
  留空的行会被跳过，两者因此会错位：`Row N font size` / `Row N height` 是**这一行自己的属性**，
  读取时按设置行号、存进被画出来的那一行（`rowSlotOf[]`），所以 `Row3Height` 落在第二个显示行上。
  （图标不再有这个坑：它写在行里，跟着那一行走。）
- **岛上的卡片不受行高影响**：它有自己的行距，取的是全局 `Row height`。卡片不是"一行"，
  它盖在行上面；如果它的行距跟着下面的行变，一个正在淡出的卡片会在中途改尺寸。

## 2. 面板宽度（原 AGENTS §5.1 的宽度部分）

- **「面板有多宽」只有一个所有者：`EnsurePanelWidth`**（Fit content = `ResolvePanelWidth`，
  Fixed = `Panel width`）。宽度**在 HUD 侧从实测内容解析**（设置期量不出来：没有行程序、
  字号、`GpGraphics`），`ModSettings::panelWidth` 因此只是 Fixed 的宽度 + 测量基准宽度。
- **解析出的宽度对一个状态恒定，并在位图与 `PlaceWindow` 之前确定**：
  `EnsurePanelWidth` → `FrameWidthFor` → `PlaceWindow` → `CreateDIBSection`，
  同一帧窗口与位图是同一个数。**每帧重算就是被移除过四次的宽度动画**（见
  [animation.md](animation.md) 与 [../panel-width-design.md](../panel-width-design.md)）；
  缓存键（纪元 + 已发布行 + 卡片态 + 显示器宽）不能省。
- **`ComputeGeometry` 只「播种」宽度（`if (g_panelWidth <= 0)`），不覆盖已解析的值。**
  它还会在展开/收起/改设置时被调用，覆盖一次就是"窗口用解析值、几何用配置值"。
  第一版断言在它之后把解析值补写回去，于是**恒真**（见 §"方法论教训"第 13 条）——现在产品侧不覆盖、
  测试侧按同样的顺序走。上限过屏幕钳位（monitor − 24），下限**不**过；
  `min > max` 退化成常数。

> 宽度的完整规则、退化情形与全部断言已经在 [../panel-width-design.md](../panel-width-design.md)
> （三条不变量、`ResolvePanelWidth` 的真值表、负对照清单）——**那里是唯一的家，这里不再抄一遍**；
> 本节只补上"播种而不覆盖"这条 AGENTS 独有、且被测试负对照钉住的形状。**改宽度代码前先读它。**

## 3. 静态数组里的结构体：默认成员初始值必须是 0（原 AGENTS §5.1）

一个非零的默认值（`int group = -1;`）会让编译器把这**整个对象**的镜像放进 `.data`（文件里真实存在），
而不是留在 `.bss`（只有虚拟大小，不占文件字节）。`RowLayoutPlan::segments[12]` × `g_rowPlans[4]` 因此
一次把 **93,696 字节**的纯零内存写进了 DLL：191,488 → 290,304，而 `.text` 只涨了 5.2 KB
（**这条是量出来的**：`git worktree` 建父提交编译，再比 PE 段表）。

所以 `RowSegmentPlan::group` / `TextSeg::group` 的"没有组"是 **`kAlignSegFlow` = 0**
（标记里的组号是 1..3，0 空着），`AlignGroupPlan::lastSegment` 也不再需要 -1——
组就是"同号连续段的最大区间"，两端一扫描就知道。

**判据：会进静态数组的结构体，新增字段的默认值只能是 0**；真要非零，就只在对象本身
很小的静态量上用（或让该对象走运行期构造）。量法（标准库解析 PE 段表，看 `.data` 的 raw）：

```python
# .data raw 应保持 ~1 KB；涨到几十 KB 就是非零默认值把对象拖进了文件镜像
import struct, sys
d = open(sys.argv[1], 'rb').read()
e = struct.unpack_from('<I', d, 0x3C)[0]
n = struct.unpack_from('<H', d, e + 6)[0]
opt = struct.unpack_from('<H', d, e + 20)[0]
for i in range(n):
    o = e + 24 + opt + i * 40
    nm = d[o:o+8].rstrip(b'\0').decode()
    vs, va, raw, pra = struct.unpack_from('<IIII', d, o + 8)
    print(f"{nm:10} {vs:>9} {raw:>9}")
```

## 4. 每帧的"测量"缓存与新鲜度键（原 AGENTS §5.1）

- **每帧的重活是"测量"，而它被缓存掉了**（`RowLayoutPlan` + `EnsureRowPlan`）：选族（覆盖查询）与
  拟合（`FitTextToWidth` 的二分、哨兵、省略号）只在**文本或几何变化时**跑一次，中间那些帧只
  `DrawString`。**注意前提**：标记解析（`InstantiateRowProgram` / `BuildRowLayout`）本来就只在
  `PublishRows` 里跑，不在每帧——所以"每帧解析文本"从来不是本项目的成本，别按那个去优化。
  新鲜度键的**每个字段都要有断言**（`RowPlanIsFresh` 的纪元/宽度/行高/字号/行数/段数；
  行内缩曾经也是键里的一项，它随行首图标盘一起消失、成了常量，那一项断言也随之删除）；
  纪元由 `ReleaseFontResources` 与 `PublishRows`（只在内容真的变了时）自增，**新增任何
  "改了就该重排"的状态都要加进这两处之一**，否则屏幕会留着上一份文本。证据链：
  `g_measureCalls`（产品计数器，`[perf]` 里的 `meas=`）——宿主断言"复用计划时测量次数为 0"。
