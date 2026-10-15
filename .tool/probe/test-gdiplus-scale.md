# test-gdiplus-scale.cpp

一句话：独立探针——对一张已知内容的图跑完整的「载入 → 逐级折半 → 末级缩放」，
把「文件本身没颜色」和「渲染丢了颜色」分开。

## 怎么跑

```powershell
& "$wc\bin\clang++.exe" -target x86_64-w64-mingw32 -std=c++23 -O0 -DUNICODE -D_UNICODE `
  -luser32 -lgdi32 -o "$env:TEMP\gdiplus-probe.exe" .tool/probe/test-gdiplus-scale.cpp
& "$env:TEMP\gdiplus-probe.exe" <图片路径>
```

**这是一次性探针，必须给一张图**（`argc < 2` 时打印 `usage: gdiplus-probe.exe <image>` 并返回 2）。
`$wc` 是 Windhawk 工具链目录 `D:\Program Files\Windhawk\Compiler`。
编译失败时先确认 exe 的时间戳比源码新，别拿上一次的产物下结论。

## 需要什么

Windhawk 的 clang++ 与运行时的 `gdiplus.dll`；不需要管理员权限、不需要 mod 在跑。

## 输出与判读

- `load "<路径>" rc=<n>` 与 `source <W>x<H>`：文件有没有载进来、多大。
- 之后是四组 `mean RGBA = …  transparent=a/n  opaque-white=b/n`：
  `source`、`stride = 0 (what the mod used)`、`stride = width*4 (explicit)`、
  以及作对照的 `single-step squash straight to 23`。
- **关键判读**：`source` 就没颜色 → 是文件的问题；`source` 有颜色而缩放后
  平均值明显偏向白/透明 → 是渲染的问题。目标盘径在输出里写作 `target (plate diameter) = 23`。
- `Downscale returned NULL  <-- nothing to draw` 表示缩放链一步都没成功。
- 退出码：0 正常跑完；1 是 `gdiplus.dll` 载入失败、`GdiplusStartup` 失败或图片载入失败；2 是没给图片路径。

## 坑

- **PARGB 下透明像素的 RGB 是 0**，所以"彩色图案偏淡"和"整片纯白"在浅色盘上肉眼无法区分
  ——必须像本探针这样**测像素**，不能靠看。
- 两种 stride 都跑，是因为"客户端/渲染丢失"和"位图步长传错"是同一症状的两个原因；
  两条结果并排才能选边。
- 脚本流程上别忘了 `if (-not (Test-Path $out)) { exit 1 }` 这类守卫：
  曾出现编译失败却仍执行旧 exe，把旧结果当成新结果（本仓库踩过两次）。
