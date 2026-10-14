# AGENTS.md — 维护者与 LLM 指南

面向**修改本项目的人或模型**。用户向文档在 [README.md](README.md)；
本文只讲实现、不变量、踩过的坑与验证方式。

**读本文之前先读 README 的「功能」与「全部设置项」两节**，否则不知道这些代码在服务什么。

---

## 0. 三条铁律

在动任何代码之前先内化这三条。它们各自对应一次真实回归。

1. **不要"修一个、猜一个"。** 本项目大量缺陷的症状是**屏幕上某个东西消失或位置不对**，
   而原因可能在采样、布局、变换、配色解析、文件载入中的任意一层。
   已经有多次"我以为是 A，实际是 B"的记录（见 §6）。**先取证据，再改代码。**
2. **可断言的行为必须写进 `test-layout.cpp`。** 这个 mod 无法自动化 UI 测试，
   回归测试是唯一的自动防线。凡是"编译通过但屏幕上看得出错"的逻辑，都必须有断言。
3. **偏离原项目时，把偏离和理由写进注释与 README。** 本移植有意偏离原 Avalonia 实现的
   地方不止一处（动画时序、过冲强度、波纹升起）。静默偏离会在下次有人"对齐原项目"时被改回去。

---

## 1. 项目结构

```
endfield-info-windhawk/
├── endfield-info-windhawk.wh.cpp      唯一的交付物（含 Windhawk 元数据与注释内 Readme）
├── README.md                          用户文档（安装、设置、变量、排查）
├── AGENTS.md                          本文（实现与不变量）
├── CHANGELOG.md                       版本与变更（发布说明的来源）
├── LICENSE                            MIT
├── docs/                              规划、设计与交接文档
│   ├── external-data-island-design.md 设计：外部数据（M1 数据面、M2 事件面均已实现）
│   ├── font-and-row-metrics-design.md 设计：多字体回退 + 按行字号/高度（含"未配置零成本"闸门）
│   ├── platform-notes.md              GDI+ 导出名/常量、NVML 加载缺陷（原 AGENTS §8）
│   ├── inline-markup-design.md        设计：行内图标（**已实现**）+ 对齐/占位符（未实现）+ 一次性编译（**已实现**）
│   └── state-and-handover.md          验证状态、真机更正、犯错档案、待办
│
├── 开发工具（不随 mod 交付）——**每个都有自己的 .md，讲怎么跑/要什么/输出什么/坑在哪**
├── build.ps1                  + build.md
├── test-layout.cpp            + test-layout.md
├── test-gdiplus-scale.cpp     + test-gdiplus-scale.md
├── _settings.py               + _settings.md
├── _logfmt.py                 + _logfmt.md
├── _deadcode.py               + _deadcode.md
├── _preview.py                + _preview.md
├── _extclient.py              + _extclient.md
├── _extclient.ps1             + _extclient.md（与上面同一个文件：两个客户端一起讲）
├── _netease-bridge.ps1        + _netease-bridge.md
├── _netease-position.ps1      + _netease-position.md
├── _netease-lyric-scan.ps1    + _netease-lyric-scan.md
├── _netease-lyric-probe.ps1   + _netease-lyric-probe.md
└── assets/panel-preview.png           版面预览（README 引用）
```

分工：**README 讲用户怎么用**，**脚本的 `.md` 讲这个脚本怎么跑**，**AGENTS（本文）讲实现、
不变量与踩过的坑**。同一个事实不要在三处各写一遍——写在你改代码时需要看到的那一处。

`test-layout.cpp` 与 `test-gdiplus-scale.cpp` 是**开发工具**，不随 mod 交付。
`_settings.py` / `_logfmt.py` / `_deadcode.py` 只要 Python 标准库；**需要 Pillow 的是 `_preview.py`**
（它自己重画一遍版面）。

> 本机 `python` 解析到的是 Windows Store 的占位程序，不一定真的能执行。
> 如果 `python` 打开的是应用商店，用 DSH 运行时自带的解释器：
> `C:\Users\HP\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe`

---

## 2. 交付物：tool mod，不是注入式 mod

本 mod 是 Windhawk 的 **tool mod**：运行在**独立的 `windhawk.exe` 进程**里，
**不注入、不挂钩任何其他进程**。因此：

- 回调只有三个：`WhTool_ModInit` / `WhTool_ModSettingsChanged` / `WhTool_ModUninit`。
  文件尾部的其余导出（`Wh_ModInit` 等）是 Windhawk 的样板，保持原样。
- 所有绘制是**自绘**：一个 `WS_EX_LAYERED` 窗口 + `UpdateLayeredWindow` + GDI+ 画到
  32bpp PARGB DIB。没有子控件、没有 XAML。
- **没有动态注册设置项的 API**，所以设置项是 mod 头部
  `// ==WindhawkModSettings==` 块里的静态 YAML。字体、图标路径这类只能手输 + 校验。

**约束**：不使用 `new` / `delete` / `std::*` / 异常。原因是**体积**
（去掉堆分配后 DLL 约 178 KB，2026-10-14 实测 182272 B；静态链接 libc++ 会涨到约 590 KB），
**不是**因为运行时不可用——Windhawk 会自带并注入 `libc++.whl`，堆分配其实能用。
早先 AGENTS 里"堆分配不可用"的说法是错的，已更正。

---

## 3. 构建与测试

### 编译 mod

```powershell
pwsh -File build.ps1
```

脚本读取 Windhawk 自己的 `compile_flags.txt`，用它的 clang 20 工具链（MinGW x86_64 +
libc++）编译，并打印导出符号表。

**允许出现的唯一警告**：

1. `winsock2.h:15:2: warning: Please include winsock2.h before windows.h` —
   **无法消除**：Windhawk 用 `-include windhawk_api.h` 强制先包含 `windows.h`，
   而该文件第一行就是 `#include <windows.h>`。这是头顺序问题，不是代码问题。

> 2026-10-14 之前还有第二条（`unused function 'ApplyPanelTransform'`）。它不是"可以消除的噪声"，
> 而是**为测试而存在的产品函数**：主 mod 里没人调用它，只有 `test-layout.cpp` 调。
> 现已删除，断言改成直接打在产品**真正交给 GDI+ 的字段**上（`ComputePanelTransform` 的
> `active` / `scale` / `dx` / `dy`，以及"面板中心是不动点"这个由两个函数共同保证的关系）。
> **"只有测试在用"就是死代码**——它会让下一个人以为产品里有这么一条路径。见 §7.7。

**如果出现别的警告，要处理掉。**

> **本地编不出"安装版"**：`compile_flags.txt` 带 `-DWH_EDITING`，而在这个宏下
> `windhawk_api.h` 把 `Wh_Log` 定义成空的内联函数、`WH_MOD_ID` 变成占位符、
> 也不引入 `windhawk_api_internal.h`。所以本地 build **是语法/类型门，不是与安装版逐位一致的产物**：
> 日志调用连同它们的格式串在本地被编译掉，DLL 里搜不到任何日志字符串——我为此白排查过一轮
> （"DLL 里没有新日志"其实什么也没证明）。去掉 `-DWH_EDITING` 也救不了：那会引用
> `InternalWh_*`，而它们由 Windhawk 自己提供，本地链接不过。
> **要检查日志格式串，别指望编译器**——宽格式它一条都查不了（原因与实测见下面的方框），
> 用 `python _logfmt.py`。

### 设置块校验（改过设置项就跑）

```powershell
python _settings.py
```

检查设置块结构并统一 `$name` / `$description` 的引号。期望输出含
`no structural problems found`。

它查的结构项是**被 Windhawk 的解析器拒绝过、而它当时却说没事**的那些：

- **序列项的缩进**：顶层条目必须在第 0 列，`$options` 下的选项项必须正好 4 空格
  （5 空格 → js-yaml `bad indentation of a sequence entry`）；
- **每条目恰好一个 `$name` / 一个 `$description`**（把新条目插到上一条目的键与它自己的
  `$name` 之间 → `duplicated mapping key`）；
- **顶层键不重复**、**同一 `$options` 组内选项不重复**（YAML 会静默保留最后一个）；
- 选项项必须出现在它那条目的 `$options:` 之后。

注意：`$name` / `$description` 必须**单引号包裹**（裸的 `-` 会被 YAML 解析成序列，
导致 `bad indentation of a mapping entry`）；数值设置项**不要加引号**，否则
`Wh_GetIntSetting` 取不到。
`--write` 只重写这两种显示串，且是幂等的（实测字节不变）。

### 回归测试（改过任何绘制/布局/配色/动画逻辑就跑）

```powershell
$wc = 'D:\Program Files\Windhawk\Compiler'
# -D__USE_MINGW_ANSI_STDIO=0 与 Windhawk 的真实参数（compile_flags.txt）一致。
# 少了它，宿主用的是 ISO stdio：宽格式里的 %s 会被当成窄字符串，于是 mod 里
# 所有 Wh_Log(L"...%s", 宽字符串) 在宿主里都打印成垃圾——**宿主就成了另一套语义**，
# 测出来的结论不能代表产品。加界断言前就被这个坑咬过一次（管道 DACL 的 SDDL 被
# swprintf 截成了 "D"）。
& "$wc\bin\clang++.exe" -target x86_64-w64-mingw32 -std=c++23 -O0 -DUNICODE -D_UNICODE `
  -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000008 -D__USE_MINGW_ANSI_STDIO=0 `
  -Wno-pragma-pack `
  -luser32 -lgdi32 -lpdh -liphlpapi -lpsapi -ladvapi32 `
  -o "$env:TEMP\layouttest.exe" test-layout.cpp
& "$env:TEMP\layouttest.exe"
```

期望最后一行 `PASSED (0 failure(s))`，或者——在这个受限沙箱里**就是**——
`PASSED (0 failure(s), 1 skipped)`。**skipped 不为 0 时必须逐条解释**：每条跑不起来的检查
都打印成 `SKIP` 并计入这个数字，就是为了让"通过"和"根本没跑"不再长得一样
（宿主自己打印 `SKIPPED: n check(s) ...` 那行也在）。

> **受限沙箱会跳过管道 DACL 那条**：命名管道客户端在 DSH 的受限模式下被拒绝
> （连"完全不给安全描述符"的对照管道也打不开，`err=5`），于是那条打印
> `the pipe DACL admits the user running the mod (no pipe clients here)  SKIP`，
> 末行变成 `PASSED (0 failure(s), 1 skipped)`。**跳过不等于通过**——要验证 DACL 得在普通
> 终端里跑一次。实测边界：**只读**打开可以（`GENERIC_READ` → `opened=1`），而
> `GENERIC_WRITE`、`FILE_WRITE_DATA`、`GENERIC_ALL` 全部 `err=5`。因此宿主**没法**做管道
> 端到端测试（客户端要写），管道读写路径只能靠真机日志——§10 的"未验证"里也记着这条。

> 宿主还替产品守了三件它自己容易犯的事：**设置字符串的所有权**（桩按真实 API 每次发放新内存，
> 漏 free / double free / 泄漏都会在末段 `=== settings string ownership ===` 里报出来）、
> **变量注册表**（token 覆盖直接遍历 `kVariables[]`，注册了却不实现会被抓）、
> **宽度下限**（`LayoutWidthFloor` 由产品提供，测试不再抄一遍表达式）。
> 这三条原来都属于"看着有、其实没有"。

> **mod 的日志格式串不在这里检查，而且编译器一条都查不了**（`Wh_Log` 是自造变参函数，宽格式没有 format 属性；
> 四条路都实测堵死）。**替代检查是 `python _logfmt.py`**，实测记录与原因见 [_logfmt.md](_logfmt.md)。
> 宿主自己的 printf 已经能在 `-Wformat=2` 下零警告编译，多一条守得住的线。

### 死代码检查（改过结构体或脚本就跑）

```powershell
python _deadcode.py
```

编译器只管**符号**（`-Wunused-function` / `-Wunused-variable` / `-Wunused-macros` 都在 build.ps1 里，
mod 现在只剩 1 条已知警告）。它管不了的三类是：

- **只写不读的结构体字段**（每帧算一遍然后扔掉的量、重构留下的字段），以及**完全没被碰过**的字段；
- **`#if 0`**；
- **注释掉的代码**（要求注释以 `;` / `{` / `}` 结尾，否则散文会被误报）；
- **脚本里定义了却没人引用的函数**（PowerShell / Python 都没有编译器看它们）。

三条刻意的限制，否则一次"干净"没有意义：读计数**跨所有 C++ 文件**（宿主 `#include` 了 mod，
会读 mod 只写的字段——按单文件扫会误报 `DownscalePlan.steps`）；**先剥注释再统计**（只在注释里
出现的字段等于没人用）；少量**豁免清单**（`EXTERNAL_READERS` / `EXTERNAL_STRUCTS`）覆盖
"由 Windows / GDI+ 读，不是由我们读"的结构体镜像，另有"体里有 `{` 的结构体跳过"这条
（成员函数里的字段可以不带 `this->` 引用，文本扫描看不见）。

2026-10-14 第一次跑就是它找出我们自己的 5 处：`Values.valid`、`IntroFrame.rippleRise`、
`ExtProvider.lastSeenAt`、`IconGeometry.ringInner`（都只写不读），以及随之失去唯一调用者的
`Segment` / `EaseSmooth` / `kRippleOffsetY`（这几处编译器也报了）。**每次的判据都一样：
先看它报什么，再决定是删还是加豁免——豁免要写理由。**

**宿主原理**：`test-layout.cpp` 把 Windhawk API（`Wh_GetIntSetting` / `Wh_GetStringSetting` /
`Wh_Log` / `Wh_SetFunctionHook`）打桩，然后 `#include "endfield-info-windhawk.wh.cpp"`。
因此它能直接调用 mod 的**内部静态函数**——这是本测试的价值所在，
也是为什么布局计算要**抽成独立纯函数**（见 §4）。

打桩的设置 API **按键名返回值**，所以 `ReadSettings()` 可以被真实驱动，
用于测试配置解析（主题优先级、图标路径裁剪）。

测试**最后一段会真启动 GDI+**（`g_gdiplus.Load()` + `Startup`），用真字体量
"这段文字会不会折行、会不会冲出预算"。这是唯一能守住 §5.1 那条
"文字不得折行"的办法：纯函数测不了 GDI+ 的排版行为。GDI+ 起不来时该段打印一行
`SKIP` 并计入末行的 `skipped` 数（不再是一句容易被忽略的 "section skipped"），
所以没有 GDI+ 的环境里测试仍可用，但**不会被误读成全绿**。

> **踩过的坑**：脚本里要加 `if (-not (Test-Path $out)) { exit 1 }` 守卫。
> 曾出现编译失败但脚本仍执行了**上一次的旧 exe**，得出"改了没效果"的错误结论。
> 本仓库里这条坑**又踩了一次**：改动探针后编译失败，却把旧 exe 的结果当成了新结果。
> 任何"探针说没问题"的结论，都要先确认 exe 的时间戳比源码新。

### 预览图

```powershell
python _preview.py
```

`_preview.py` 是 GDI+ 绘制逻辑的**独立 Python 复刻**（PIL）。

> **重要**：预览与真实实现**曾经不一致**——预览每行画图标，而 C++ 当时只画一个，
> 导致我长期没发现问题。**改绘制逻辑后要同步改预览，或至少确认两者一致。**

### GDI+ 缩放探针

```powershell
& "$wc\bin\clang++.exe" -target x86_64-w64-mingw32 -std=c++23 -O0 -DUNICODE -D_UNICODE `
  -luser32 -lgdi32 -o "$env:TEMP\gdiplus-probe.exe" test-gdiplus-scale.cpp
& "$env:TEMP\gdiplus-probe.exe" <图片路径>
```

对一张**已知内容**的图跑完整的「载入 → 逐级折半 → 末级缩放」并输出均值 RGBA 与
不透明像素比例。用途是把「文件本身没颜色」和「渲染丢了颜色」分开——
这两种情况在浅色盘上**肉眼无法区分**。

---

## 4. 架构与职责

单文件，按段落组织。修改时**保持段落边界**，它们是有意义的：

| 段落 | 职责 |
| :-- | :-- |
| 设置块 → `ReadSettings` | 读 Windhawk 设置，解析主题/颜色/图标/动画 |
| 设计令牌常量 | 取自原项目 Avalonia 主题的调色板与不透明度层级 |
| `LookupRaw` / `LookupNumber` / `ResolveToken` / `ExpandTokens` | 变量与格式说明符求值 |
| `CompileRowText` → `InstantiateRowProgram` / `BuildRowLayout` | 把一行文本切分成「文字段 + 环段 + 图标段」 |
| `ComputeRowTextMetrics` | 一行文字可用的宽度 |
| `IconElementCenter` / `RingRadiusAt` | 行内图标的中心与其波纹半径（**单一来源**） |
| `PlanDownscale` / `DownscaleToFit` | 自定义图片的缩放计划与执行 |
| `PaintPanel` | 唯一的绘制入口 |
| `ForceTopmost` / `PlaceWindow` / `WindowProc` | 窗口生命周期、热区、命中测试 |
| `HudThreadProc` / `SamplerThread` | HUD 线程（消息循环）与采样线程 |
| `ExtLookup` / `ExtSanitize` / `ExtStoreValue` | 外部提供者推来的值：定长槽位表，零分配 |
| `ExtPipeThread` / `ExtStart` / `ExtStop` | 命名管道服务端：每实例一线程，被引用到才启动 |

### 关键设计：把逻辑抽成纯函数以便断言

本项目的**核心工程手法**：凡是"编译通过但屏幕上看得出错"的逻辑，
都抽成一个不依赖 GDI+ 状态的纯函数，然后由回归测试直接调用。已有：

`ComputeRowTextMetrics` · `ComputeRowVariables` · `ResolveRingColor` ·
`ComputePanelTransform` · `RingRadiusAt` · `IconElementCenter` ·
`IconDiameterForFontSize` / `ElementFontSizePx` / `IconElementRipples` / `IconStyleFromName` ·
`PlanDownscale` · `CopyUserPath` · `IconCacheFind` / `IconCachePick` ·
`ComputeIconImageDrawSize` · `LayoutWidthFloor` · `PdhValueUsable` ·
`EvaluateIntro` · `FitCharacterCount` / `FitCharsWithEllipsis`（测量以函数指针注入） ·
`ParseFontChainList` / `FontChainKey` / `RowNeedsFontPath` / `CollectRowFontTags` ·
`PickFontFamily`（覆盖判定以函数指针注入） ·
`ParseRowMetricTags` / `RowFontSizePx` / `RowHeightPx` / `RowTopPx` / `PanelRowsHeight` /
`MinimumRowHeightForFont` / `ParseInlineSize` / `MaxInlineRunSize` ·
`ExtParseLine` · `ExtSanitize` · `ExtParseNumber` · `ExtLookup` · `ExtNextDeadline` ·
`ExtEvictIndex` · `ExtProviderAllowed` · `ExtReplyTimeoutsAfter` · `IslandEnqueue` / `StepIsland` / `IslandHitTest` /
`IslandClickActionFor` · `Select-StoreLineIndex` / `Select-SongTitle` / `Get-LeadMs`（脚本侧）

**新增绘制逻辑时照此办理。** 如果一段逻辑无法在测试里调用，它就无法被守住。
反过来，**测试不应该自己复刻一份产品逻辑**（复刻出来的那份测不到产品）；
能共享就共享，`CreateRowStringFormat()` 就是为此而抽出来的（见 §7 第 8 条）。

### 数据流

```
SamplerThread ──(采样)──> g_values (临界区保护) ──> PaintPanel ──> 分层窗口
     │                                                              ▲
     └── 按 RequiredGroups(模板引用的变量) 决定加载哪些采集通路 ─────┘
```

**按需加载**是核心性能设计：`RequiredGroups()` 扫描所有行文本，
只有被引用的采集组才会初始化。GPU 通路代价最大（约 28 MB），所以这条不能退化。

---

## 5. 不变量（不要破坏）

每一条都对应一次真实回归，改动相关代码前先读它的测试。

### 5.1 布局

- **`ComputeRowTextMetrics` 对无环行必须返回完整内宽。** 曾返回 0，
  导致 GDI+ 把整行裁掉 → "第一行之后都不显示"。
- **`ComputeRowTextMetrics` 的 `textWidth` 不得为负**，下限为 0。
- **内联环的判进必须包含尾随空格。** GDI+ 的 `MeasureString` 故意不计尾随空格，
  所以用**哨兵字符 U+2502**：测 `文字+哨兵` 再减哨兵宽度。
  曾少算这一点，导致环压在 `%` 上。
- **行的 `StringFormat` 必须带 `StringFormatFlagsNoWrap`（`0x1000`），
  且只能由 `CreateRowStringFormat()` 建。** 这个 mod 的行是"一行读数"，
  没有第二行的位置。丢了 NoWrap 的后果不是"多出一行"，而是
  **该段首行上浮**：框高只有一行（`RowHeight`），而 `LineAlign = Center`
  会把折行后的多行整体垂直居中。一行由多段（文字/环/文字）拼成、
  每段各有自己的框，于是折行的那段与相邻段**基线不一致** →
  观感就是"第一行文字不在一条直线上"。断言用真 GDI+ 覆盖（见 §3）。
  **不要把这个函数拆回内联**：测试必须和绘制走同一个函数，
  否则测的是测试自己那份副本（这一点已实测踩到，见 §7 第 8 条）。
- **GDI+ 不按 layout 框裁剪文本。** 关掉换行后，超宽的文本会**直接画到框外**、
  压在后面的环上。所以每段在 `DrawString` 前要按自己的预算调用 `FitTextToWidth()` 截断：
  右停靠段 = `metrics.textWidth`，**内联段 = `ComputeInlineTextLimit(textRight, cursor,
  跟在它后面的环数, ringSize)`**。内联段曾经写成"实测 advance"，那是个**恒真的预算**
  （该函数第一句就是"已经放得下吗"），于是用了 `[[ring:…]]` 的行**从不截断**：文字切在
  位图外缘且没有 `…`，它自己的环被挤出面板。`FitTextToWidth` / `FitCharacterCount` /
  `FitCharsWithEllipsis` / `ComputeInlineTextLimit` 是纯函数或注入测量的函数，都有断言。
- **`DrawString` 的框宽下限是 `kMinLayoutWidth`（1.0）**，不能是 0：
  0 宽会让 GDI+ 把整段裁掉，这与第一条是同一个坑的另一面。
  截断与框用的是同一个下限（`LayoutWidthFloor`），两者不能各写一个数字。
- **`FitTextToWidth` 不缓存省略号宽度。** 它曾经"按进程缓存一次"，理由写着"字体终生固定"——
  **不成立**：绘制每帧按设置重建字体。于是改字号后缓存还是旧字号的宽度，被截断的行可以
  比预算宽出这个差值（实测 24 px 字体下某个预算超 6.1 px）。现在每次量一次（旁边本来就有
  一次二分测量，多这一次可以忽略）。断言是**扫一遍预算**（40..200）而不是试一个：
  预留被低估时只有"刚好填满预留"的那些预算会越界。
- **行不再是等高的**：每行有自己的字号与行高（`Row N font size` / `Row N height`，或行首的
  `[[size:NN]]` / `[[h:NN]]`，标签优先）。三个派生量只有一处实现，谁都不许再自己乘一遍：
  `RowFontSizePx`（0 = 继承全局）、`RowHeightPx`（**不小于 `MinimumRowHeightForFont`**）、
  `RowTopPx` / `PanelRowsHeight`（前缀和）。`ComputeGeometry` 的 `g_panelHeight` **必须**来自
  `PanelRowsHeight`——它同时是窗口高度、命中矩形高度与入场目标高度，**写成 `rowHeight × 行数`
  就会让面板高度与画面不一致**（这一条有断言：`ComputeGeometry` 算出的高度必须等于各行之和）。
- **行高下限是"字放得下"，不是美学选择**：`kRowHeightFontSlack = 15` 正是默认 `Row height 30`
  与 `Font size 15` 之差。去掉它，30 px 字号的行会溢出到相邻行上（GDI+ 不裁剪字形）。
  改这个常量会同时改变默认配置的外观，改前先看 §5.1 最后那条断言。
- **`[[size:NN]]` 有两副面孔，区别只在位置**：写在**行首**是**行属性**（`ReadSettings` 的
  `ParseRowMetricTags` 把它从 `rows[]` 里剥掉，同时决定该行的字号与高度下限）；写在**别处**是
  **段属性**（`InstantiateRowProgram` 换成 `kSizeMarkStart` + 两位十进制数字 + `kRingMarkEnd`，
  `[[/size]]` 换 `kSizeMarkEnd`），由 `BuildRowLayout` 记进 `TextSeg::runFontSize`。
  所以"段内 `[[size:20]]` 当普通文字"这条**只对行高成立**：它照样能改字号，但改的是那一段。
  绘制侧按段取字号（`AcquireRowFont(…, segFontSize)`），段不嵌套——`[[/size]]` 回到该行自己的字号，
  与 `[[/font]]` 回到主字体是同一条规矩。
- **行高下限用"这一行里最大的字"**（`ModSettings::rowMaxFontSize` = 本行字号与
  `MaxInlineRunSize` 的较大者），不是本行字号：否则 `[[size:30]]` 出现在小字行里会溢出到下一行。
- **图标跟随"它自己出现处的字号"，不跟随全局 `Font size`。** 图标是行里的**内容**
  （`[[icon:…]]` 元素），和它旁边的文字同一层：`[[size:24]]…[[icon:Memory]]…[[/size]]` 里就是
  24 px 那个图标，同一行里两个尺寸的图标是两个不同大小的盘子（`IconDiameterForFontSize` 是
  唯一乘系数的地方，尺寸取 `ElementFontSizePx` = 段字号或该行字号）。
  这条曾经**反过来**过：图标是行首的**盘**（面板元素，属于所有行共用的左侧一列），
  所以它只跟随全局字号，`Row N icon` / `Custom icon file` 九个设置项就是这么用的。
  那次用户报的是"改变行 size 后图标也跟着变大，应该不对"——在**行首盘**的模型下他是对的：
  盘不是那一行的内容，让它跟着行字号变还会把**所有行**共用的文字列一起挪动。
  改成行内元素后前提变了（盘不再存在、文字列也不再有共用的内缩），所以规则随之反转；
  两条规则各自都对，错的是拿旧模型的那条去套新模型。
  `RowMetrics` 里因此**没有**盘径字段：一个"每行一个盘径"的字段在新模型下必然是假的。
- **行首标签只认行首**：前缀解析在 `ReadSettings` 里完成、标签已从 `rows[]` 剥掉，所以绘制侧只看得到
  段标签；`[[h:…]]` 写在文本中间是**普通文字**，写错的一律留下原文。
- **两套"行号"必须分清**：设置按**设置里的行号**命名，绘制只知道**显示顺序**。
  留空的行会被跳过，两者因此会错位：`Row N font size` / `Row N height` 是**这一行自己的属性**，
  读取时按设置行号、存进被画出来的那一行（`rowSlotOf[]`），所以 `Row3Height` 落在第二个显示行上。
  （图标不再有这个坑：它写在行里，跟着那一行走。）
- **每帧的重活是"测量"，而它被缓存掉了**（`RowLayoutPlan` + `EnsureRowPlan`）：选族（覆盖查询）与
  拟合（`FitTextToWidth` 的二分、哨兵、省略号）只在**文本或几何变化时**跑一次，中间那些帧只
  `DrawString`。**注意前提**：标记解析（`InstantiateRowProgram` / `BuildRowLayout`）本来就只在
  `PublishRows` 里跑，不在每帧——所以"每帧解析文本"从来不是本项目的成本，别按那个去优化。
  新鲜度键的**每个字段都要有断言**（`RowPlanIsFresh` 的纪元/宽度/行高/字号/行数/段数；
  行内缩曾经也是键里的一项，它随行首图标盘一起消失、成了常量，那一项断言也随之删除）；
  纪元由 `ReleaseFontResources` 与 `PublishRows`（只在内容真的变了时）自增，**新增任何
  "改了就该重排"的状态都要加进这两处之一**，否则屏幕会留着上一份文本。证据链：
  `g_measureCalls`（产品计数器，`[perf]` 里的 `meas=`）——宿主断言"复用计划时测量次数为 0"。
- **岛上的卡片不受行高影响**：它有自己的行距，取的是全局 `Row height`。卡片不是"一行"，
  它盖在行上面；如果它的行距跟着下面的行变，一个正在淡出的卡片会在中途改尺寸。

### 5.2 配色

- **命名主题完全胜出，颜色字段被忽略。** 只有 `Theme = Custom` 才读颜色字段。
  曾做成"预设打底、字段覆盖"，结果预设永远看不到。
- **进入命名主题分支时要显式清 `ringColorUserSet`**，而不是仅仅不读它——
  否则残留字段可能从某条路径泄漏进预设配色。
- **环色优先级**：`:COLOR` > `Ring colour`（仅 Custom）> 主题边框色。
- **`{gpu.temp}` 等"已知但取不到"的变量渲染为 `--`**，未知变量保持字面文本。
  二者必须区分（见 §6 第 3 条）。**每个 NVML-only 读数都要有自己的有效性标志**
  （`gpuTempValid` / `gpuFanValid` / `gpuPowerValid` / `vramUsedValid` / `vramTotalValid`）：
  少一个标志，"这台机器读不到"就会被画成一个看着很像真读数的 `0`——温度、风扇、功耗、
  显存总量各栽过一次。
- **PDH 的"这个值能不能用"只有一个判据**：`PdhValueUsable()` 接受 `PDH_CSTATUS_VALID_DATA`
  **和** `PDH_CSTATUS_NEW_DATA`。CPU 时钟路径一直接受两者，而 GPU 的数组读取只接受
  `ERROR_SUCCESS`（即 VALID_DATA）——于是**计数器刚建好时的第一个采样被丢掉**，
  表现为 GPU 读数晚一拍才出现。三条路径现在问同一个函数。

### 5.3 行内图标

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
  见 §5.1 那条：这条规则与旧的"盘只跟随全局字号"是同一个问题的两个前提。
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

### 5.4 动画

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
- **展开与收回刻意不对称**：展开先生长定型、再让内容分层浮现；收回贯穿整段、时长为一半。
- **收回的关键帧 cue 是 `1.0`，不是 `kCollapseDurationRatio`。** 那个常量是**时长比例**；
  把它当 cue 用（`{kCollapseDurationRatio, end}`）会让收缩在半程就结束，剩下半程窗口停在
  标签高度、而帧时钟仍以 16 ms 重绘它——可见收回只有 `IntroMs/4`（README 写的是"一半"）。
  断言：`the shrink still has work left at the midpoint`。
- **方向判定用 `start = fromHeight`、`end = toHeight`**，方向由两者大小自然决定。
  曾按方向重排 start/end → 收回时高度反而在增长。
- **过冲强度 `kPanelBounceStrength = 0.04`**（原项目 0.275）。0.275 在 250px 面板上是
  弹过头 14px 再收回，正是"弹出又收回"的观感来源。

### 5.5 窗口与置顶

- 面板**永远不隐藏**，收起只是缩成 8px 标签。否则收起后无法点回来。
- `WS_EX_NOACTIVATE` 始终保留（不抢焦点）。
- 只有 `WM_NCHITTEST` 在圆角外返回 `HTTRANSPARENT`，因此圆角外点击会穿透。
- 置顶用三重手段：创建时样式位 + `SetWindowPos` 显式写位 + 每 100ms 重申。
  **周期性重申换来的不是正确性而是延迟**（已用独立探针验证），所以不要因为
  "反正会重申"就省掉创建时的样式位。

### 5.6 外部数据（命名管道）

- 这一节的**全部不变量**（锁序、I/O 超时、实例数、开机自检、事件队列、卡片几何/命中）已搬到
  [docs/external-data-island-design.md](docs/external-data-island-design.md) 的「命名管道不变量」一节；
  **动管道、岛、卡片或 tick 的代码前先读那里**（搬出去是因为 AGENTS 会被静默截断）。

### 5.7 字体回退与逐段覆盖

- **成本闸门（这条是这个功能的形状）**：`Fallback fonts` 为空**且**该行没有 `[[font:…]]` 标签时，
  走的就是**单字体那条老路**——一行一个 `GpFont`、零覆盖查询、零额外分段。闸门由
  `RowNeedsFontPath(rowHasTag, hasGlobalChain)` 判定，**在配置期**（`ReadSettings` 扫原文，结果存进
  `rowFontTag[]`），不在每帧判定。不要说"反正有缓存"就把它去掉。
- **纯 ASCII 的段不查覆盖**（`PickFontFamily` 的第一条规则）。全英文面板即使配了回退链也不付代价，
  这是性能承诺的另一半；`[perf]` 的 `glyphq=` 就是量它的。
- **逐段覆盖的语义有两档**：`[[font:族名]]` 里的**第一个**族名在 ASCII 段上直接生效（零查询）；
  非 ASCII 段按顺序问覆盖（一个都不覆盖时继续问全局链，最后才是主字体）。所以"给一段指定字体"
  不会在中文段上强行画出豆腐块。回退决策是**逐段**的：混排的一段整段交给第一个覆盖全段的候选。
- **标记机制与 `[[ring:]]` 是同一个实现**：展开期把标签换成 `kFontMarkStart` + 链序号 + `kRingMarkEnd`，
  `BuildRowLayout` 剥标记并给每个 `TextSeg` 带上 `tagChain`。**剥标记只有这一处**，绘制侧从不解析标记。
  标签 → 链序号用的是**规范文本**（`FontChainKey`），所以行里写 `A ; B` 与 `A;B` 命中同一条链。
- **坏标签绝不吞文字。** 族名写错/没装/超过 6 条链的 `[[font:…]]`，以及**画不出来的 `[[ring:…]]`**，
  都**原样留在文本里**。曾经的实现是"消费掉但不产出"：屏幕上少一段读数，而删掉的正是唯一线索
  （`ParseRing` 的注释从第一天起就承诺"调用方回退到原文"，代码却没有）。两条都有断言。
- **覆盖查询缓存 256 槽**（键 = 族哈希 + 字符，无堆分配），**字体对象缓存 8 槽**（键 = 族 + 字号）。
  字体缓存里**空槽优先于 LRU**——否则两个字号交替绘制时每帧重建对象，缓存等于没有（实测断言抓到过）。
  设置变更时在 HUD 线程整体释放（`ReleaseFontResources`），卸载时在 `GDI+ Shutdown` **之前**释放。
- **回退族名与标签族名都要过 `FontIsInstalled`**（与主字体同一个列表）。`CreateFontW` 对未知族名会
  **替换**成某个字体，而那个替身会照常回答覆盖查询——于是"回退生效了"是假的，画出来的也不是任何人都
  指定的字体。没装的因此被丢弃（标签随之原文显示，日志一条汇总）。
- **行内标记在读取设置时编译一次**（`CompileRowText` → `ModSettings::rowProgram[]`，与 `rows[]` 同一份数据），
  发布路径只展开 `{变量}` 与环的数值字段（`InstantiateRowProgram`），发布期不再解析标签、也不再查链序号。

---

## 6. 已修复的缺陷（总表已移出本文）

那份"缺陷 → 症状 → 关键断言"的总表（47 行）搬到了
[docs/state-and-handover.md](docs/state-and-handover.md) 的 §13，原因与 §10 相同：AGENTS 受工作区
**64 KB 指令预算**限制，超出部分会被**静默截断**（实测 65,381 B 被切到 65,244 B）。改相关代码前
先去那里查对应断言；本文只保留**不变量**（§5）——两者是同一件事的两个视角。

### 排查心法：先分类，再定位

本项目缺陷的症状高度重叠（"某处空白"可能是采样、布局、变换、配色、载入五层中的任意一层）。
**按下面顺序分类，不要跳步**：

1. **采样层**：`Paint` 日志里的 `published` vs `visibleRows`。
   `published` 小 → 采样端问题，与绘制无关。
2. **布局层**：`rowN segs= textLen= inline=` 与逐段 `draw row= seg= x= y= w= adv=`。
   **`w=1.0 adv=0.0` 就是"文本被裁没"**，这是 5.1 第一条的指纹。
3. **几何层**：`BuildRowPlan` 给每个元素算出的 `x` / 直径（行内图标是不是落在它该在的地方、
   大小跟着谁），以及逐段 `draw row= seg= x= y= w= adv=`。
4. **配色层**：`Theme=... bg=...` 那行确认谁赢了。
5. **资源层**：`Icon...: loaded / could not load` 那几行。

**不要用"看起来像"来选层。** 已经有三次我猜错了层（详见 §7）。

---

## 7. 方法论教训（都是真实弯路）

这些不是泛泛的建议，是**在这个项目里真实浪费时间的方式**。

1. **先要日志，再动手。** 我曾在"图标显示为纯白"上排查了多轮：写探针、验证
   GDI+ 管线、测四种 PNG 编码……最后一行日志显示
   `could not load ""C:\...\image.png""` ——**是路径里的引号**。
   一行日志能省掉全部弯路。
2. **变换类断言要避开不动点。** 我第一版断言比较的是
   缩放变换的**不动点**（面板中心），映射前后必然相同，断言恒真、什么也证明不了。
   要用**偏离中心的点**，并给出"错误做法"的对照数值。现在的写法是断言产品**交给 GDI+ 的字段**：
   `active` / `scale` / `dx = (1-scale)*W/2`，以及"面板中心在映射下不动"这条把
   `ComputePanelTransform` 与"GDI+ 怎么映射一个点"绑在一起的关系——它仍然会红
   （`dx` 少一个 `/2`，或符号反了，都会被它抓住），但不再依赖一个只有测试在用的函数。
3. **PARGB 下透明像素的 RGB 是 0。** 因此"彩色图案偏淡"与"整片纯白"在浅色盘上
   肉眼无法区分。要区分就必须**测量像素**（`GdipBitmapGetPixel` 采样）。
4. **测试脚本要防旧产物。** 编译失败时若仍执行上一次的 exe，会得出"改了没效果"的
   错误结论。见 §3 的 `Test-Path` 守卫。
5. **预览与实现会漂移。** `_preview.py` 每行画图标、C++ 当时只画一个，
   预览让我以为实现是对的。**两者必须同步核对。**
6. **断言要断言"关系"，不是"数字"。** 例如收回时长按 `kCollapseDurationRatio` 断言
   而非硬编码 600ms，这样改默认时长后断言依然成立。
7. **删掉"为文档而存在但未使用"的声明。** 曾声明一组设计令牌常量却全部未使用，
   编译器报 9 个 `unused-variable`。能用的接进渲染路径，不能用的写进注释。
   **同样适用于"只有测试在用"的函数**：`ApplyPanelTransform` 在主 mod 里没有调用者，
   只为让宿主能断言变换而存在——它让下一个人以为产品里有这条路径。删掉之后，
   断言改打在产品真正交给 GDI+ 的字段上（见 §3 的方框），mod 的已知警告从 2 条降到 1 条。
   **字段级的同类问题编译器看不见**（只写不读、从没被碰过），那是 `python _deadcode.py`
   的活；2026-10-14 它一次找出 5 处，其中 4 处是纯粹的"算完就扔"。
8. **断言必须打在"产品代码"上，不是打在测试自己复刻的那份上。** 修"第一行不在一条直线"
   时，我第一版断言在测试里自己调 `CreateStringFormat(kStringFormatNoWrap, …)` 建格式，
   于是把 mod 的 flag 改回 `0` 做负对照时，测试**照样全绿**——它测的是自己。
   修法是把构造抽成 `CreateRowStringFormat()`，绘制与测试都调它，负对照才终于变红。
   **判据：把 bug 改回去，测试必须失败。**（与第 2 条是同一件事的两个面：
   前者说要点在真东西上，后者说别拿不动点当靶子。）
9. **断言要用真实输入的形状。** 测 `ExtHandleLine` 时我喂的是 `L"HELLO 1 probe"`——**没有换行**，
   而真实客户端发的是 `"HELLO 1 music\n"`。于是"提供者名"这条从帧行切出来的切片被按**指针**拷贝、
   把换行一起吞进去，表里存成 `"music\n"`（6 字符）而行里问的是 `"music"`（5 字符）；
   `ExtNameEq` 比长度，永远不等，行**永远显示 `--`**——**而我的断言全绿**。
   修法是把框架按客户端的形状写进断言（带 `\n`），旧代码下立刻红两条。
   **判据：测试的输入要和真实调用的输入同形**。这是第 8 条的第三面：
   打在真东西上、喂真输入、改回去要变红。
10. **没有上界的等待会把诊断能力一起吃掉。** 管道读用 `INFINITE` 等完成事件，
    于是"完成事件丢失"这个故障的表现是**整个线程静默停住**：我加的所有日志
    （`recv`、`err`、`client done`）全都在那行等待的**后面**，一条也打不出来——
    从外面看，"卡住了"和"什么都没发生"完全一样，而这两者的排查方向相反。
    **判据：凡是可能永远等下去的地方，都要有超时和一个能说出"我还在等什么"的出口**
    （这里是 `PeekNamedPipe`），否则你连该看哪一行都不知道。
11. **不要用 `Sleep` 去"等过期"。** 到期断言原本写成 `ExtStoreValue(ttl=1ms)` + `Sleep(10)`，
    而 `GetTickCount` 的粒度约 15.6 ms——**10 ms 的睡眠不一定跨过一个 tick**，
    于是"是否已过期"取决于这次调度的运气。这条断言长期随机变红；我插入几行日志改变了
    时序，它连红两次才暴露出来（当时看起来像是我改坏了）。修法是直接把条目的
    `updatedAt` 挪到过去：**构造"时间过去了"这个状态，而不是等它发生**。
    判据：断言里的 `Sleep` 都是可疑的，除非你断言的就是"睡眠会持续"。
12. **分层窗口的「尺寸—位图」一致性是逐帧不变量，而补偿代码是它出问题的气味。** 收起闪烁我先后在
    **宽度**上改了四轮（宽度动画 → 撤销动画 → 跳变 → 干脆固定宽度），全都没用；真正的原因在**高度**
    路径的一次既有往返：先缩窗口、再长回来才画。识别它的线索正是绘制分支里那句补偿注释
    （*"SetExpanded already shrank the window to the resting (tab) height, so the frame has to grow
    it back"*）——**当你需要一段代码去撤销上一段代码刚做的事，顺序就是错的**。
    定位方法也不是按层猜，而是**把事件流程读出来**：谁在什么时候调 `PlaceWindow`、谁调
    `PaintPanel`、一拍之内两者的先后。用户一句"检查鼠标离开检测区后的渲染事件流程"比我自己
    猜四轮都有效——**症状说不清层的时候，去读调用顺序，而不是继续调参数**。
13. **恒真断言、"抄一份表达式"和"静默跳过"是同一类失效：看着在守，其实没有。**
    2026-10-14 一次清掉四处，每一处都用"把它要防的改动回去、必须变红"验证过：
    — `if (firstY == panelH / 2.0f) Check(rowCount == 1, …)`：条件与结论代数等价；
    — "盘心 = 环心"：拿同一个结构体的字段和自己比；
    — 测试里又抄一遍 `width > kMinLayoutWidth ? … : …`：测的是那份副本
      （产品现在提供 `LayoutWidthFloor`，绘制与测试都调它）；
    — 三处 `printf("… section skipped")`：**"跑不起来"和"通过"长得一模一样**——
      `CreateRowStringFormat` 一旦返回 null，GDI+ 段那些 NoWrap/预算断言全部消失，
      而末行照样写 `PASSED`。
    现在的规矩：跑不起来的检查打印 `SKIP` 并**计入末行的 skipped 数**；产品自身的前置条件
    （格式建不出来）算**失败**；字符串所有权、变量注册表这类"桩替产品守"的东西要真的能报错。
    判据与第 8、9 条相同——**断言的价值只等于"改坏它会不会红"**，与字数无关。

---

## 8. 外部依赖与平台约束

### GDI+ 通过 `GetProcAddress` 动态绑定

- **不包含 `gdiplus.h`**，所有函数与常量手动 `RESOLVE`。**导出名陷阱**（`GdipMultiplyWorldTransform`、
  `GdipDrawLines`、`GdiplusStartup` 没有 `Gdip` 前缀…）与常量值见 [docs/platform-notes.md](docs/platform-notes.md)。
- 没有 arc 原语（圆用两段 `AddPathArc`）；没有 `min`/`max` 宏，用 `ClampInt`/`MinInt`/`MaxInt`/`MinDouble`。

### 头文件顺序（MinGW 特有）

`winsock2.h` → `netioapi.h` → `iphlpapi.h` → `windows.h`。
顺序错了会 `unknown type name 'PMIB_IF_TABLE2'`。

### 平台限制

- **全屏独占（exclusive fullscreen）的 3D 应用无法被覆盖**——它绕过 DWM 合成，
  任何窗口都不可能叠在上面。这是平台限制，不是可以通过代码解决的问题。
- **多显示器**：锚定"最上方"的显示器（`rect.top` 最小者，平手时取主显示器）。
  本机只验证过 2 台。

### NVML 与一个真实的驱动安装缺陷

- 某些驱动把 `nvml.dll` 放进 `System32` 却不放依赖 `nvml_loader.dll` → `LoadLibrary` 报 `126` **而文件存在**；
  `LoadNvmlWithDependencies()` 先预载 loader。判别依据：`error=126, already-in-system32=1` 是它，
  `error=2` 才是文件真的不存在。细节见 [docs/platform-notes.md](docs/platform-notes.md)。

## 9. 修改指南

### 新增一个设置项

1. 在 `// ==WindhawkModSettings==` 块里加条目。
   **`$name` / `$description` 用单引号**；数值项**不加引号**。
2. 在 `ModSettings` 结构体加字段（给默认值）。
3. 在 `ReadSettings()` 里读取并 `ClampInt` / 校验。
4. 跑 `python _settings.py`，确认 `no structural problems found`。
5. 更新 README 的「全部设置项」表。

### 新增一个变量

1. 在 `kVariables[]` 注册（这决定 `RequiredGroups()` 会加载哪些采集通路——
   **漏注册会导致该变量悄悄不工作**）。
2. 在 `LookupRaw`（格式化字符串）或 `LookupNumber`（数值）里实现。
3. 若是数值变量，加进 `IsKnownNumberVariable`，这样"本机取不到"时会渲染 `--`
   而不是字面文本。
4. 更新 `test-layout.cpp` 时**不需要**再维护第二份 token 列表：那一节现在直接遍历
   `kVariables[]`，注册了却不实现（或没给采集组）会当场变红。README 的「变量速查」表
   仍要手改——它是给用户看的，没有任何东西比对它。
5. 更新 README 的「变量速查」表。

> `{ext.*}` 是**运行期**变量，**不要**注册进 `kVariables[]`：那张表是编译期的采集通路清单，
> 外部变量的组由 `RequiredGroups()` 里的前缀判断给出（[1343]），注册进去只会误导下一个人。
> 它的断言写在 `test-layout.cpp` 末尾的 "external data" 一节。

### 改动绘制/布局/几何

1. **先把要断言的关系抽成纯函数**（见 §4）。
2. 在 `test-layout.cpp` 加断言，并**先确认它在旧代码下会失败**——
   否则你不知道这个断言有没有意义。
3. 改实现，跑到 `PASSED (0 failure(s))`。
4. 同步 `_preview.py`（若涉及版面）。
5. 若偏离了原项目，把偏离与理由写进注释。

### 收尾检查（任何改动之后）

改完跑这三条，它们各自守一类编译器看不见的东西：

```powershell
python _settings.py     # 设置块结构（改了设置项就必须跑）
python _logfmt.py       # 宽格式日志的元数与错型（编译器一条都查不了）
python _deadcode.py     # 死代码：只写不读的字段、#if 0、注释掉的代码、脚本死函数
```

**改了结构体字段尤其要跑第三条**：删掉一个字段的写者之后，它可能变成"从没被碰过"，
而那正是它该被删掉的证据（`Values.valid` 就是这么走完最后一步的）。

### 报告与调试

- mod 的日志走 `OutputDebugString`，Windhawk 会自动加 `[行号:函数名]` 前缀。
  这个前缀可以用来**核对部署的是哪一版源码**（与本地源码行号对比）。
- 诊断日志要用**有界计数器**（如 `g_iconDrawLogs`），不要每帧刷屏。
- 日志要打印**判别性数值**（坐标、尺寸、错误码、颜色），不要只打印"失败了"。
  一个 `image=none` 比十行"画完了"有用。

---

## 10. 验证状态

**已移到 [docs/state-and-handover.md](docs/state-and-handover.md)** —— 原因：AGENTS 受工作区
指令预算 **64 KB** 限制，超过会被**静默截断**（实测 68,564 B 被截到 65,244 B，交接说明正被切在中间 ✗）。
这一节是**只读参考**（不必每次改代码都读），所以拆出去；AGENTS 只保留需要频繁查阅的 §0–§9 与 §11。

拆出去的内容包含：已验证清单、未验证清单、真机更正（`playingLyricLineNumber` 不可用、
`playingState = 2` 是播放中、真 currentTime 在迷你条 DOM）、犯错档案、以及**待办清单**。

## 11. 参考

- 原项目（视觉与设计语言的来源）：
  `Customization/`、`Styles/HudTheme.axaml`、`Animations/HudAnimations.cs`、`Views/`
- Windhawk tool mod 样板：
  <https://github.com/ramensoftware/windhawk/wiki/Mods-as-tools:-Running-mods-in-a-dedicated-process>
- 本地工具链：`D:\Program Files\Windhawk\Compiler\`（clang 20.1.3 + MinGW）
- 真实编译参数：`C:\ProgramData\Windhawk\EditorWorkspace\compile_flags.txt`
- 本仓库的设计文档（**M1 与 M2 已实现；M3 进行中——几何/区域/命中/关闭按钮/悬停/帧率决策均已落地，
  剩下 `ComputeIslandRect` 接管窗口定位；M4 仍是提案**，不要当成现有行为）：
  [docs/external-data-island-design.md](docs/external-data-island-design.md)
