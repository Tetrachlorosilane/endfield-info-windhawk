# _preview.py

一句话：GDI+ 绘制逻辑的独立 Python 复刻（PIL），渲染出 `assets/panel-preview.png`
这张版面预览图。

## 怎么跑

```powershell
python _preview.py
```

没有参数。输出固定写到脚本同目录的 `assets\panel-preview.png`（会覆盖旧图）。
版面数据是写死的：三行 `ROWS`，每行给「**带 `[[icon:…]]` 标签的原文** + [(比例, 颜色)] + 是否内联」。

## 需要什么

Python 与 **Pillow**（`from PIL import Image, ImageDraw, ImageFont`）。
字体从 `C:\Windows\Fonts\segoeui.ttf` 载入（`FONTS = r"C:\Windows\Fonts"`），
所以这是 Windows 上的工具。本机 `python` 若是 Windows Store 占位程序，改用 DSH 运行时的解释器：
`C:\Users\HP\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe`。

## 输出与判读

一行 `wrote <路径>  <W>x<H>  (N rows, M rings, K icons)`。脚本把所有绘制合成好之后才保存，
所以**没有别的判据行**——图对不对要用眼睛看：底板是棋盘格，用来让面板的 alpha 可见；
图标盘、环的轨道与扇形、文字与图标的先后顺序都应该和 mod 画的一致。

## 行是"流"：预览和实现共用的那条规则

`ROWS` 里存的是**原文**（含 `[[icon:…]]`），`parse_row()` 按元素切开，然后像
`BuildRowPlan` 那样逐个摆放：**每个元素占自己的盒子，后面的元素从它结束的地方开始**。
文字用 PIL 量宽度、图标按 `字号 × ICON_RATIO` 算直径、图标后面留 `ICON_GAP`，
`TEXT_LEFT = 12` 对应 `kRowTextLeftInset`——也就是"没有行首图标盘，文字从行自己的内边距开始"。

三处刻意指向产品的镜像，语义漂移最容易看不出来：

- `parse_icon()` ↔ `ParseIconTag`：`[[icon:字形]]` / `[[icon:file=路径]]` / `:ripple` / `:noripple`，
  路径走 `CopyUserPath` 的裁剪（引号、前后空白），**拼错的标签当普通文字**（不是空盘子）。
- `parse_row()` ↔ `CompileRowText` 的扫描：坏标签只吃掉一个字符，从它后面继续找标签。
- `draw_icon_element()` ↔ `PaintIconElement` + `IconElementCenter`：盘心 = 元素左边界 + 半径，
  垂直居中在该行；`draw_ring()` ↔ `DrawRing`（整圈轨道 + 从 12 点顺时针的弧；
  PIL 的角度从 3 点起算，所以 `start=-90`）。

## 坑

- 这份预览**不是**产品的测量结果，是复刻；它用的几何与配色常量
  （`PanelWidth 420`、`RowHeight 30`、`CornerRadius 22`、`FontSize 15`、`RingSize 26`、
  各种 hex 颜色）是照 mod 默认值手抄的，`ROWS` 也是写死的示例配置。
  **改了 C++ 的绘制逻辑就要同步这份复刻，或至少确认两者一致。**
- 这条不是纸上规矩：预览曾经"每行都画图标"而 C++ 当时只画一个，
  正是这张图让人长期以为实现是对的。
- **这份预览不模拟多字体回退**（`Fallback fonts` 与 `[[font:…]]`，见
  [docs/font-and-row-metrics-design.md](docs/font-and-row-metrics-design.md)）：
  它的 `ROWS` 里既没有 token 也没有字体标签，所以不会出现"标签被当文字画出来"的
  假象；反过来说，预览里那三行永远只用 `segoeui.ttf` 一种字体。要检查回退是否生效，
  看真机面板与 `[perf]` 的 `glyphq=`，不要看这张图。
- **`[[size:…]]` 与按行字号/行高同样不在预览里**：预览的行高是写死的一个 `ROW_H`，
  也只建了一个字号的对象。所以**"图标跟随它自己所在的那一段字号"这条在图上看不出来**——
  预览里没有 `[[size:…]]`，每个图标都按全局 `FONT_SIZE` 算直径。
  这条关系由宿主的断言守着（`two icons in one row at two sizes have two diameters` /
  `and the second is the run's size`），要确认它请跑 `test-layout.cpp`，不要看这张图。
- **图片图标（`[[icon:file=…]]`）只解析路径、不载入文件**：预览画的是回退字形
  （`IMAGE_FALLBACK_GLYPH`，对应 `kIconImageFallbackGlyph`），因为载图需要真文件与 GDI+。
  想验证图片本身的缩放/重采样，用 `test-gdiplus-scale.cpp`。
- **预览不画动画**：没有入场缩放、没有涟漪，`:ripple` / `:noripple` 只被解析、不影响这张静态图。
