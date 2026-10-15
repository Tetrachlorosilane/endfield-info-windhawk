# AGENTS.md — 维护者与 LLM 指南

面向**修改本项目的人或模型**。用户向文档在 [README.md](README.md)；
本文只讲实现、不变量、踩过的坑与验证方式。

**读本文之前先读 README 的「功能」与「全部设置项」两节**，否则不知道这些代码在服务什么。

**各子系统的规范条文在 [docs/invariants/](docs/invariants/)**：§5 是那张索引表——
每条只写"什么时候必须读它"和最要命的 1–3 条，全文（含断言名与实测数字）在对应文件里。
改动相关代码前先读索引指到的那一个。

---

## 0. 三条铁律

在动任何代码之前先内化这三条。它们各自对应一次真实回归。

1. **不要"修一个、猜一个"。** 本项目的缺陷症状**高度重叠**——"屏幕上某个东西消失或位置不对"
   可能是采样、布局、变换、配色解析、文件载入中的任意一层。已经有多次"我以为是 A，实际是 B"
   的记录。**先取证据，再改代码**：§6 给了按层分类的顺序，§7 是真实弯路的清单。
2. **可断言的行为必须写进 `.tool/regress/test-layout.cpp`。** 这个 mod 无法自动化 UI 测试，
   回归测试是唯一的自动防线。凡是"编译通过但屏幕上看得出错"的逻辑，都必须有断言。
3. **偏离原项目时，把偏离和理由写进注释与 README。** 本移植有意偏离原 Avalonia 实现的
   地方不止一处（动画时序、过冲强度、波纹升起）。静默偏离会在下次有人"对齐原项目"时被改回去。

---

## 1. 项目结构

```
endfield-info-windhawk/
├── endfield-info-windhawk.wh.cpp      唯一的交付物（含 Windhawk 元数据与注释内 Readme）
├── README.md                          用户文档（安装、设置、变量、排查）
├── AGENTS.md                          本文（实现与不变量索引）
├── CHANGELOG.md                       版本与变更（发布说明的来源）
├── LICENSE                            MIT
├── docs/                              规划、设计与规范条文
│   ├── invariants/                    按子系统拆的规范条文（原 AGENTS §5 的正文）
│   │   ├── layout.md           行高/前缀和/面板宽度/静态数组 0 默认值 · 改布局前读
│   │   ├── text-measurement.md 笔进 vs 墨迹、FitTextToWidth、NoWrap · 改截断/测量前读
│   │   ├── markup.md           size/font/gap/对齐组/[[/]] 的编译与实例化 · 加标记前读
│   │   ├── icons.md            行内图标的几何、图片与缓存 · 碰图标前读
│   │   ├── animation.md        展开/收起、高度与窗口的同帧一致 · 碰动画前读
│   │   ├── theme.md            配色优先级、-- 与 PDH 有效性（原 §5.2）· 碰配色前读
│   │   ├── window.md           窗口样式/置顶/平台限制（原 §5.5 + §8 的窗口部分）· 碰窗口前读
│   │   └── fonts.md            回退链与逐段覆盖的成本闸门（原 §5.7）· 碰字体前读
│   ├── external-data-island-design.md 设计：外部数据（M1 数据面、M2 事件面均已实现）；§5.6 的不变量也在这里
│   ├── font-and-row-metrics-design.md 设计：多字体回退 + 按行字号/高度（含"未配置零成本"闸门）
│   ├── platform-notes.md              GDI+ 导出名/常量、头文件顺序、NVML 加载缺陷（原 AGENTS §8）
│   ├── panel-width-design.md          设计：面板宽度（Fixed / Fit content、三条不变量、负对照清单）
│   ├── inline-markup-design.md        设计：行内图标 + 留白/对齐（**§7 第 3、4、5 步均已实现**）+ 一次性编译（**已实现**）
│   ├── netease-lyric-source-design.md 设计：桥的歌词/位置只有一条来源（黑胶页 CDP）· 改桥前读
│   └── state-and-handover.md          验证状态、真机更正、犯错档案、待办
│
├── 开发工具（不随 mod 交付）——**每个都有自己的 .md**（怎么跑/要什么/坑在哪）
│   ├── build.ps1              + build.md              编译（唯一"怎么构建"入口，故留在根）
│   ├── _netease-bridge.ps1    + .md                   发布器（桥）本体
│   ├── _netease-lyric-probe.ps1 + .md                 只读探路：歌词字段在客户端升级后有没有变
│   └── .tool/                 其余工具按用途分组；都从**自身位置**解析仓库根
│       ├── checks/   _settings.py（设置块）· _logfmt.py（宽格式日志）· _deadcode.py（死代码）
│       ├── regress/  test-layout.cpp                  回归宿主：打桩 Windhawk API + `#include "../../产品"`
│       ├── preview/  _preview.py                      版面预览（**需要 Pillow**），写 assets/panel-preview.png
│       ├── probe/    test-gdiplus-scale.cpp           缩放探针：`<图片路径>` 为参数
│       ├── investigations/  .gitignore 掉的一次性转储（CDP 页面摘录、采样脚本）
│       └── pipe/     _extclient.py + _extclient.ps1   外部管道冒烟客户端（共用一个 .md）
│       （例：`python .tool/checks/_settings.py`；三个 checks 只要 Python 标准库）
└── assets/panel-preview.png           版面预览（README 引用）
```

删工具前先给两个证据：`git grep -n <文件名>` 在**被跟踪**文件里全无引用，且功能有**替代者**；
缺一就留着。删除用 `git mv` 进 `.Trash/`。
（`_netease-position.ps1` / `_netease-lyric-scan.ps1` 曾经是"运行期被桥调用"的例外，
**2026-10-15 起不再是**：位置与歌词改由黑胶页一条来源给出，它们已按上面这条规则进 `.Trash/`。
判断依据仍然是"被跟踪文件里全无引用" —— `git grep -n _netease-position` 一把就能复核。）

分工：**README 讲用户怎么用**，**脚本的 `.md` 讲这个脚本怎么跑**，**AGENTS（本文）讲实现、
不变量与踩过的坑**，**`docs/invariants/` 讲某个子系统的规范条文**。同一个事实不要在多处各写一遍
——写在你改代码时需要看到的那一处。

`.tool/` 下的工具都是**开发工具**，不随 mod 交付。

> 本机 `python` 可能是 Windows Store 的占位程序；真跑不了就用 DSH 运行时的解释器：
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
> 而是**为测试而存在的产品函数**：主 mod 里没人调用它，只有 `.tool/regress/test-layout.cpp` 调。
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
> 用 `python .tool/checks/_logfmt.py`。完整推导见 [.tool/checks/_logfmt.md](.tool/checks/_logfmt.md)
> 与 [.tool/regress/test-layout.md](.tool/regress/test-layout.md) 的「坑」一节。

### 设置块校验（改过设置项就跑）

```powershell
python .tool/checks/_settings.py
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
  -o "$env:TEMP\layouttest.exe" .tool/regress/test-layout.cpp
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
> 端到端测试（客户端要写），管道读写路径只能靠真机日志——[docs/state-and-handover.md](docs/state-and-handover.md)
> 的「未验证」里也记着这条。

> 宿主还替产品守了三件它自己容易犯的事：**设置字符串的所有权**（桩按真实 API 每次发放新内存，
> 漏 free / double free / 泄漏都会在末段 `=== settings string ownership ===` 里报出来）、
> **变量注册表**（token 覆盖直接遍历 `kVariables[]`，注册了却不实现会被抓）、
> **宽度下限**（`LayoutWidthFloor` 由产品提供，测试不再抄一遍表达式）。
> 这三条原来都属于"看着有、其实没有"。

> **mod 的日志格式串不在这里检查，而且编译器一条都查不了**（`Wh_Log` 是自造变参函数，宽格式没有 format 属性；
> 四条路都实测堵死）。**替代检查是 `python .tool/checks/_logfmt.py`**，实测记录与原因见 [_logfmt.md](.tool/checks/_logfmt.md)。
> 宿主自己的 printf 已经能在 `-Wformat=2` 下零警告编译，多一条守得住的线。

### 死代码检查（改过结构体或脚本就跑）

```powershell
python .tool/checks/_deadcode.py
```

编译器只管**符号**（`build.ps1` 已开 `-Wunused-function` 等）。它管不了的四类是：

- **只写不读的结构体字段**（每帧算一遍然后扔掉的量、重构留下的字段），以及**完全没被碰过**的字段；
- **`#if 0`**；
- **注释掉的代码**（要求注释以 `;` / `{` / `}` 结尾，否则散文会被误报）；
- **脚本里定义了却没人引用的函数**（PowerShell / Python 都没有编译器看它们），
  以及**同一文件里定义了两次的名字**——两份都"有人引用"（调用方绑到后面那份），
  所以前面那份永不执行却看不出来；`_netease-bridge.ps1` 的 `Test-VinylJumpPlausible`
  就是这样（`e141514` 删掉第一份，而旧检查器在**它修好之前**也说"没有死代码"）。
  这一条只认第 0 列的 PowerShell 定义与"同缩进同作用域链"的 Python 定义，
  条件分支里的合法重定义（导入回退 / `if`-`else`）刻意放行，详见 [_deadcode.md](.tool/checks/_deadcode.md)。

三条刻意的限制，否则一次"干净"没有意义：读计数**跨所有 C++ 文件**（宿主 `#include` 了 mod，
会读 mod 只写的字段——按单文件扫会误报 `DownscalePlan.steps`）；**先剥注释再统计**（只在注释里
出现的字段等于没人用）；少量**豁免清单**（`EXTERNAL_READERS` / `EXTERNAL_STRUCTS`）覆盖
"由 Windows / GDI+ 读，不是由我们读"的结构体镜像，另有"体里有 `{` 的结构体跳过"这条
（成员函数里的字段可以不带 `this->` 引用，文本扫描看不见）。

2026-10-14 第一次跑就是它找出我们自己的 5 处：`Values.valid`、`IntroFrame.rippleRise`、
`ExtProvider.lastSeenAt`、`IconGeometry.ringInner`（都只写不读），以及随之失去唯一调用者的
`Segment` / `EaseSmooth` / `kRippleOffsetY`（这几处编译器也报了）。**每次的判据都一样：
先看它报什么，再决定是删还是加豁免——豁免要写理由。**

**宿主原理**：`.tool/regress/test-layout.cpp` 把 Windhawk API（`Wh_GetIntSetting` / `Wh_GetStringSetting` /
`Wh_Log` / `Wh_SetFunctionHook`）打桩，然后 `#include "../../endfield-info-windhawk.wh.cpp"`。
因此它能直接调用 mod 的**内部静态函数**——这是本测试的价值所在，
也是为什么布局计算要**抽成独立纯函数**（见 §4）。

打桩的设置 API **按键名返回值**，所以 `ReadSettings()` 可以被真实驱动，
用于测试配置解析（主题优先级、图标路径裁剪）。

测试**最后一段会真启动 GDI+**（`g_gdiplus.Load()` + `Startup`），用真字体量
"这段文字会不会折行、会不会冲出预算"。这是唯一能守住 [text-measurement.md](docs/invariants/text-measurement.md)
那条"文字不得折行"的办法：纯函数测不了 GDI+ 的排版行为。GDI+ 起不来时该段打印一行
`SKIP` 并计入末行的 `skipped` 数（不再是一句容易被忽略的 "section skipped"），
所以没有 GDI+ 的环境里测试仍可用，但**不会被误读成全绿**。

> **踩过的坑**：脚本里要加 `if (-not (Test-Path $out)) { exit 1 }` 守卫。
> 曾出现编译失败但脚本仍执行了**上一次的旧 exe**，得出"改了没效果"的错误结论。
> 本仓库里这条坑**又踩了一次**：改动探针后编译失败，却把旧 exe 的结果当成了新结果。
> 任何"探针说没问题"的结论，都要先确认 exe 的时间戳比源码新。

### 预览图

```powershell
python .tool/preview/_preview.py
```

`.tool/preview/_preview.py` 是 GDI+ 绘制逻辑的**独立 Python 复刻**（PIL）。

> **重要**：预览与真实实现**曾经不一致**——预览每行画图标，而 C++ 当时只画一个，
> 导致我长期没发现问题。**改绘制逻辑后要同步改预览，或至少确认两者一致。**

### GDI+ 缩放探针

```powershell
& "$wc\bin\clang++.exe" -target x86_64-w64-mingw32 -std=c++23 -O0 -DUNICODE -D_UNICODE `
  -luser32 -lgdi32 -o "$env:TEMP\gdiplus-probe.exe" .tool/probe/test-gdiplus-scale.cpp
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
`IslandClickActionFor` · `Select-StoreLineIndex` / `Select-SongTitle` / `Get-LeadMs` /
`Split-VinylParagraphs` / `ConvertFrom-VinylSeconds` / `Test-VinylJumpPlausible` /
`Test-VinylValueFrozen` / `Resolve-VinylFrame` / `Resolve-TrackIdentity` /
`Get-CdpReplyVerdict` / `New-VinylExpression` / `Test-StoreLyricUsable` /
`Get-Vinyl*Candidates`（脚本侧）

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

## 5. 不变量（不要破坏）——按子系统拆到 `docs/invariants/`

每一条都对应一次真实回归。**这里是索引，不是全文**：每节只写触发条件、
最要命的几条（写在下面，读一眼就够），以及规范条文的落点。
**改相关代码前读指到的那个文件**——断言名、实测数字、容错清单都在那里。

### 5.1 布局

**改行高、行顶、前缀和、面板宽度、`ComputeGeometry`、绘制预算或静态数组字段前 →
读 [docs/invariants/layout.md](docs/invariants/layout.md)**
（同时读 [text-measurement.md](docs/invariants/text-measurement.md) 与
[markup.md](docs/invariants/markup.md)）。

- **行高与面板高度只有一个来源**：`RowFontSizePx` / `RowHeightPx` / `RowTopPx` /
  `PanelRowsHeight`。`ComputeGeometry` 的 `g_panelHeight` **必须**来自 `PanelRowsHeight`，
  写成 `rowHeight × 行数` 就会让面板高度与画面不一致。
- **面板宽度只有一个所有者：`EnsurePanelWidth`**（Fit content = `ResolvePanelWidth`，
  Fixed = `Panel width`），解析出的宽度对一个状态恒定、且在位图与 `PlaceWindow` **之前**确定；
  `ComputeGeometry` 只**播种**（`if (g_panelWidth <= 0)`），不覆盖。完整规则见
  [docs/panel-width-design.md](docs/panel-width-design.md)（**改宽度代码前先读它**）。
- **会进静态数组的结构体，新增字段的默认值只能是 0**：一个非零默认值会把**整个对象**的镜像
  拖进 `.data`（`RowLayoutPlan::segments[12]` × `g_rowPlans[4]` 实测多出 **93,696 字节**）。

全文另有：行高下限 `kRowHeightFontSlack = 15`、`rowMaxFontSize` 取"这一行最大的字"、
`[[size:]]` 行首 vs 段内、行首标签只认行首、两套"行号"（`rowSlotOf[]`）、
卡片行距取全局 `Row height`、每帧测量缓存与 `RowPlanIsFresh` 的新鲜度键（纪元/宽度/行高/字号/行数/段数）。

### 5.2 配色

**改主题解析、颜色设置项、环色优先级，或"取不到的读数"（`--`）/ PDH 有效性判定前 →
读 [docs/invariants/theme.md](docs/invariants/theme.md)**。

- **命名主题完全胜出，颜色字段被忽略**（只有 `Theme = Custom` 才读颜色字段），
  进入命名主题分支要**显式清 `ringColorUserSet`**；环色优先级 `:COLOR` > `Ring colour` > 主题边框色。
- **"已知但取不到"渲染为 `--`，未知变量保持字面文本**——每个 NVML-only 读数都要有自己的有效性
  标志（`gpuTempValid` / `gpuFanValid` / `gpuPowerValid` / `vramUsedValid` / `vramTotalValid`）。
- **PDH 只有一个判据 `PdhValueUsable()`**：接受 `PDH_CSTATUS_VALID_DATA` **和**
  `PDH_CSTATUS_NEW_DATA`（GPU 曾只认前者，于是第一个采样被丢掉）。

### 5.3 行内图标

**加图标字形、改 `[[icon:]]` 解析、动图标直径/圆心/涟漪、碰图片载入/重采样/缓存前 →
读 [docs/invariants/icons.md](docs/invariants/icons.md)**。

- **图标跟随"它自己出现处的字号"，不跟随全局 `Font size`**（直径 = 该处字号 ×
  `kIconPlateRatio`，`IconDiameterForFontSize` 是唯一乘系数的地方）。
- **环半径从盘半径起步向外扩张**（`RingRadiusAt(plateRadius, …)`），
  且环必须画在入场世界变换之内，否则与盘不同坐标系。
- **图片载入失败必须回退到内置字形**（`kIconImageFallbackGlyph` = `Memory`），
  否则只剩空盘、看起来像"图标变纯白"。

全文另有：`ParseIconTag` 的容错（拼错留原文）、元素占自己的盒子（`kIconContentGap` /
`kRowTextLeftInset`）、涟漪只从布局里第一个图标段发出、`CopyUserPath` 裁引号、
按路径缓存（`kIconCacheSlots` / `kIconVariants` / LRU）、`PlanDownscale` 逐级折半的不动点、
`ComputeIconImageDrawSize` 的入场缩放。

### 5.4 动画

**改展开/收起、入场关键帧、`g_displayHeight` / `g_paintedHeight`、`PlaceWindow` 的调用时机、
热区，或让宽度随时间变化前 → 读 [docs/invariants/animation.md](docs/invariants/animation.md)**。

- **窗口尺寸的变化必须与它提交的位图在同一帧内一致**：展开可以先定位，收起必须由绘制侧逐帧
  跟随内容（`if (expanded) PlaceWindow(hwnd);`）。**补偿代码存在，说明顺序错了。**
- **`g_displayHeight` 是静止几何**（`PlaceWindow` 与 `WM_NCHITTEST` 都用它），
  **`g_paintedHeight` 是当前帧绘制高度**——展开方向不得写回前者，收起方向的跟随是刻意的例外。
- **宽度不是动画通道**，也不许变成动画通道（每帧重算就是被移除过四次的宽度动画）。

全文另有：热区起点 `kPanelTopOffset`、每帧只采样一次进度、`kCollapseDurationRatio = 0.25`
与"收回 cue 是 `1.0` 不是这个常量"、`kCueContentFull` vs `kCueContentReady`、
方向判定 `start = fromHeight` / `end = toHeight`、`kPanelBounceStrength = 0.04`。

### 5.5 窗口与置顶

**改窗口样式、`WS_EX_*`、`WM_NCHITTEST`、圆角穿透、置顶策略或多显示器锚定前 →
读 [docs/invariants/window.md](docs/invariants/window.md)**。

- 面板**永远不隐藏**（收起只是缩成 8px 标签），`WS_EX_NOACTIVATE` 始终保留；
  只有 `WM_NCHITTEST` 在圆角外返回 `HTTRANSPARENT`（圆角外点击穿透）。
- **置顶用三重手段**（创建时样式位 + `SetWindowPos` 显式写位 + 每 100ms 重申）：
  周期性重申换来的是延迟而不是正确性，所以不要省掉创建时的样式位。
- **全屏独占的 3D 应用无法被覆盖**——平台限制，不是代码问题。

### 5.6 外部数据（命名管道）

**动管道、岛、卡片或 tick 的代码前 → 读
[docs/external-data-island-design.md](docs/external-data-island-design.md) 的「命名管道不变量」一节。**

这一节的**全部不变量**（锁序、I/O 超时、实例数、开机自检、事件队列、卡片几何/命中、
卡片宽度静态不动画、点击归属）都在那里，本文只留这个指针（搬出去是因为 AGENTS 会被静默截断）。

### 5.7 字体回退与逐段覆盖

**动 `Fallback fonts` / `[[font:…]]` 的解析与容错、覆盖查询、字体对象缓存，或改
`ReadSettings` 里的标记编译前 → 读 [docs/invariants/fonts.md](docs/invariants/fonts.md)**。

- **成本闸门（这条是这个功能的形状）**：`Fallback fonts` 为空**且**该行没有 `[[font:…]]` 标签时，
  走的就是**单字体那条老路**——一行一个 `GpFont`、零覆盖查询、零额外分段
  （`RowNeedsFontPath(rowHasTag, hasGlobalChain)`，**在配置期**判定）。
- **纯 ASCII 的段不查覆盖**：全英文面板即使配了回退链也不付代价（`[perf]` 的 `glyphq=` 量它）。
- **坏标签绝不吞文字**：族名拼错/没装/超过 6 条链，以及画不出来的 `[[ring:…]]`，一律**原样留在文本里**。

### 5.8 网易云桥（`_netease-bridge.ps1`）

**改桥的取数路径前 → 读 [docs/netease-lyric-source-design.md](docs/netease-lyric-source-design.md)。**

- **入口不许依赖"前台有窗口"。** 早先曲名只从可见顶层窗口标题里读，于是"关掉前台窗口"
  （托盘模式，真机实测 4 个 cloudmusic 窗口全部 `IsWindowVisible=0`）让 `$now` 为空、
  整个发布块被跳过 → 歌词和曲名**一起静默消失**。现在身份/位置/歌词都来自 CDP 页面。
- **一条来源只给一次值；失值时置空并响亮上报，绝不拿冻结值顶上。** 这来源的守卫是
  `Test-VinylJumpPlausible`（往回走 / 往前跳太多 / 解不出值）+ `Test-VinylValueFrozen`
  （连读同一个数到顶），两者在 `Resolve-VinylFrame` 里合成一句"发还是不发"。

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
   **`w=1.0 adv=0.0` 就是"文本被裁没"**，这是 §5.1 第一条的指纹。
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
5. **预览与实现会漂移。** 预览每行画图标、C++ 当时只画一个，
   预览让我以为实现是对的。**两者必须同步核对。**
6. **断言要断言"关系"，不是"数字"。** 例如收回时长按 `kCollapseDurationRatio` 断言
   而非硬编码 600ms，这样改默认时长后断言依然成立。
7. **删掉"为文档而存在但未使用"的声明。** 曾声明一组设计令牌常量却全部未使用，
   编译器报 9 个 `unused-variable`。能用的接进渲染路径，不能用的写进注释。
   **同样适用于"只有测试在用"的函数**：`ApplyPanelTransform` 在主 mod 里没有调用者，
   只为让宿主能断言变换而存在——它让下一个人以为产品里有这条路径。删掉之后，
   断言改打在产品真正交给 GDI+ 的字段上（见 §3 的方框），mod 的已知警告从 2 条降到 1 条。
   **字段级的同类问题编译器看不见**（只写不读），那是 `.tool/checks/_deadcode.py` 的活。
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

**GDI+ 的导出名陷阱与常量、头文件顺序、NVML 加载缺陷、窗口侧的平台限制**（原 §5.5 的窗口部分
在 §5.5）都搬到 [docs/platform-notes.md](docs/platform-notes.md) 了（**只读参考**：
查导出名、查加载缺陷、查头文件顺序时用）。

- **不包含 `gdiplus.h`**：所有函数与常量手动 `RESOLVE` 并 `GetProcAddress` 绑定；
  没有 arc 原语（圆用两段 `AddPathArc`）；没有 `min`/`max` 宏，用 `ClampInt`/`MinInt`/`MaxInt`/`MinDouble`。
- **头文件顺序（MinGW 特有）**：`winsock2.h` → `netioapi.h` → `iphlpapi.h` → `windows.h`。
  顺序错了会 `unknown type name 'PMIB_IF_TABLE2'`。
- **一个真实的驱动安装缺陷**：某些驱动把 `nvml.dll` 放进 `System32` 却不放依赖 `nvml_loader.dll`
  → `LoadLibrary` 报 `126` **而文件存在**；判别依据 `error=126, already-in-system32=1` 是它，
  `error=2` 才是文件真的不存在。

---

## 9. 修改指南

### 新增一个设置项

1. 在 `// ==WindhawkModSettings==` 块里加条目。
   **`$name` / `$description` 用单引号**；数值项**不加引号**。
2. 在 `ModSettings` 结构体加字段（给默认值）。
3. 在 `ReadSettings()` 里读取并 `ClampInt` / 校验。
4. 跑 `python .tool/checks/_settings.py`，确认 `no structural problems found`。
5. 更新 README 的「全部设置项」表。

### 新增一个变量

1. 在 `kVariables[]` 注册（这决定 `RequiredGroups()` 会加载哪些采集通路——
   **漏注册会导致该变量悄悄不工作**）。
2. 在 `LookupRaw`（格式化字符串）或 `LookupNumber`（数值）里实现。
3. 若是数值变量，加进 `IsKnownNumberVariable`，这样"本机取不到"时会渲染 `--`
   而不是字面文本。
4. 更新 `.tool/regress/test-layout.cpp` 时**不需要**再维护第二份 token 列表：那一节现在直接遍历
   `kVariables[]`，注册了却不实现（或没给采集组）会当场变红。README 的「变量速查」表
   仍要手改——它是给用户看的，没有任何东西比对它。
5. 更新 README 的「变量速查」表。

> `{ext.*}` 是**运行期**变量，**不要**注册进 `kVariables[]`：那张表是编译期的采集通路清单，
> 外部变量的组由 `RequiredGroups()` 里的前缀判断给出（[1343]），注册进去只会误导下一个人。
> 它的断言写在 `.tool/regress/test-layout.cpp` 末尾的 "external data" 一节。

### 改动绘制/布局/几何

1. **先把要断言的关系抽成纯函数**（见 §4）。
2. 在 `.tool/regress/test-layout.cpp` 加断言，并**先确认它在旧代码下会失败**——
   否则你不知道这个断言有没有意义。
3. 改实现，跑到 `PASSED (0 failure(s))`。
4. 同步 `.tool/preview/_preview.py`（若涉及版面）。
5. 若偏离了原项目，把偏离与理由写进注释。
6. **改动落在哪个子系统，就先读 [`docs/invariants/`](docs/invariants/) 里对应的那一个**（§5 的索引）。

### 收尾检查（任何改动之后）

改完跑这三条，它们各自守一类编译器看不见的东西：

```powershell
python .tool/checks/_settings.py     # 设置块结构（改了设置项就必须跑）
python .tool/checks/_logfmt.py       # 宽格式日志的元数与错型（编译器一条都查不了）
python .tool/checks/_deadcode.py     # 死代码：只写不读的字段、#if 0、注释掉的代码、脚本死函数、同文件重复定义
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
- 本仓库的规范条文（**按子系统**，§5 的正文）：
  [docs/invariants/](docs/invariants/)——
  [layout](docs/invariants/layout.md) · [text-measurement](docs/invariants/text-measurement.md) ·
  [markup](docs/invariants/markup.md) · [icons](docs/invariants/icons.md) ·
  [animation](docs/invariants/animation.md) · [theme](docs/invariants/theme.md) ·
  [window](docs/invariants/window.md) · [fonts](docs/invariants/fonts.md)
- 本仓库的设计文档（**M1 与 M2 已实现；M3 进行中——几何/区域/命中/关闭按钮/悬停/帧率决策均已落地，
  剩下 `ComputeIslandRect` 接管窗口定位；M4 仍是提案**，不要当成现有行为）：
  [docs/external-data-island-design.md](docs/external-data-island-design.md)
