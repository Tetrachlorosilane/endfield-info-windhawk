# _preview.py

一句话：GDI+ 绘制逻辑的独立 Python 复刻（PIL），渲染出 `assets/panel-preview.png`
这张版面预览图。

## 怎么跑

```powershell
python _preview.py
```

没有参数。输出固定写到脚本同目录的 `assets\panel-preview.png`（会覆盖旧图）。
版面数据是写死的：三行 `ROWS`，每行给「去掉环 token 后的文本 + [(比例, 颜色)] + 是否内联」。

## 需要什么

Python 与 **Pillow**（`from PIL import Image, ImageDraw, ImageFont`）。
字体从 `C:\Windows\Fonts\segoeui.ttf` 载入（`FONTS = r"C:\Windows\Fonts"`），
所以这是 Windows 上的工具。本机 `python` 若是 Windows Store 占位程序，改用 DSH 运行时的解释器：
`C:\Users\HP\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe`。

## 输出与判读

一行 `wrote <路径>  <W>x<H>  (N rows, M rings)`。脚本把所有绘制合成好之后才保存，
所以**没有别的判据行**——图对不对要用眼睛看：底板是棋盘格，用来让面板的 alpha 可见；
图标盘、环的轨道与扇形、文字停靠位置都应该和 mod 画的一致。

## 坑

- 这份预览**不是**产品的测量结果，是复刻；它用的几何与配色常量
  （`PanelWidth 420`、`RowHeight 30`、`CornerRadius 22`、`FontSize 15`、`RingSize 26`、
  各种 hex 颜色）是照 mod 默认值手抄的，`ROWS` / `ICON_STYLES` 也是写死的示例配置。
  **改了 C++ 的绘制逻辑就要同步这份复刻，或至少确认两者一致。**
- 这条不是纸上规矩：预览曾经"每行都画图标"而 C++ 当时只画一个，
  正是这张图让人长期以为实现是对的。
- 脚本里有两行专门指向产品的注释：`resolved_style()` 对应 `ResolveIconStyle`（未设字形的行
  继承第一个已配置的字形），`draw_ring()` 对应 `DrawRing`（整圈轨道 + 从 12 点顺时针的弧；
  PIL 的角度从 3 点起算，所以 `start=-90`）。这两处语义漂移最容易看不出来。
- **这份预览不模拟多字体回退**（`Fallback fonts` 与 `[[font:…]]`，见
  [docs/font-and-row-metrics-design.md](docs/font-and-row-metrics-design.md)）：它的 `ROWS`
  存的是**已经展开好**的文本，里面既没有 token 也没有标签，所以不会出现"标签被当文字画出来"的
  假象；反过来说，预览里那三行永远只用 `segoeui.ttf` 一种字体。要检查回退是否生效，
  看真机面板与 `[perf]` 的 `glyphq=`，不要看这张图。
