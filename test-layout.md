# test-layout.cpp

一句话：把 Windhawk 运行时的 API 打桩、再 `#include` mod 源码，直接调用 mod 的内部函数跑回归断言。

## 怎么跑

```powershell
$wc = 'D:\Program Files\Windhawk\Compiler'
& "$wc\bin\clang++.exe" -target x86_64-w64-mingw32 -std=c++23 -O0 -DUNICODE -D_UNICODE `
  -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000008 -D__USE_MINGW_ANSI_STDIO=0 `
  -Wno-pragma-pack `
  -luser32 -lgdi32 -lpdh -liphlpapi -lpsapi -ladvapi32 `
  -o "$env:TEMP\layouttest.exe" test-layout.cpp
& "$env:TEMP\layouttest.exe"
```

产物名由调用方决定（头注释里写的是 `_layouttest.exe`）。

## 需要什么

Windhawk 的 clang++ 与 Windows，不需要管理员权限。最后一段会真启动 GDI+
（`g_gdiplus.Load()` + `GdiplusStartup`）用真字体量文本排版，起不来时那一段降级成 `SKIP`。

## 输出与判读

- 每个断言一行：`<标签>  OK` / `FAIL` / `SKIP`（标签一栏宽 58 字符）。
- 末行是全绿判据：
  - `PASSED (0 failure(s))` —— 全部检查都跑了。
  - `PASSED (0 failure(s), 1 skipped)` —— 受限沙箱里就是这一种，跳过的是
    `the pipe DACL admits the user running the mod (no pipe clients here)`
    （沙箱拒绝一切管道客户端，连"完全不给安全描述符"的对照管道也打不开）。**跳过不等于通过**。
  - 有失败就是 `FAILED (n failure(s)[, m skipped])`，退出码 1；全绿退出码 0。
- 结尾一段 `=== settings string ownership ===`：`handed out N string(s) … M still outstanding`，
  M 必须为 0（漏 free / double free / 泄漏都在这里报出来）。

## 坑

- **`-D__USE_MINGW_ANSI_STDIO=0` 不能省。** 它与 Windhawk 的真实参数一致；少了它宿主用的是 ISO stdio，
  宽格式里的 `%s` 会被当成窄字符串，mod 里每条 `Wh_Log(L"...%s", 宽字符串)` 在宿主里都打印成垃圾
  ——宿主就成了另一套语义，测出来的结论不能代表产品（曾把管道 DACL 的 SDDL 截成 `"D"`）。
- **当心旧 exe。** 编译失败却仍执行上一次的产物，会得出"改了没效果"的错误结论（本仓库踩过两次）；
  任何"探针说没问题"的结论，先确认 exe 的时间戳比源码新，命令行里加个
  `if (-not (Test-Path $out)) { exit 1 }` 守卫。
- 宿主**测不了**管道读写（客户端要写，沙箱/宿主都做不到）。这里的 `Wh_Log` 桩也**不检查格式串**：
  clang 只查带 format 属性的函数，而这个工具链上宽字符 format 属性是硬错误、`ms_printf` / `wprintf`
  又不受支持——宽格式要用 `python _logfmt.py` 查。
