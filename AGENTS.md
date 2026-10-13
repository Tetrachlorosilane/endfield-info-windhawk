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
├── endfield-info-windhawk.wh.cpp      唯一的交付物（约 226 KB）
├── README.md                          用户文档
├── AGENTS.md                          本文
├── docs/                              规划与设计文档（不是实现说明）
│   └── external-data-island-design.md 设计：外部数据（M1 数据面、M2 事件面均已实现）
│
├── 开发工具（不随 mod 交付）
├── build.ps1                          本地编译，不用开 Windhawk GUI
├── test-layout.cpp                    回归测试宿主（打桩 Windhawk API）
├── test-gdiplus-scale.cpp             GDI+ 缩放探针（独立 exe）
├── _settings.py                       校验 mod 头部的设置块结构
├── _preview.py                        渲染 assets/panel-preview.png
├── _extclient.py                      外部数据管道的冒烟客户端（Python）
├── _extclient.ps1                     第二客户端（.NET）——**真机验证过的是这一个**
└── assets/panel-preview.png           版面预览（README 引用）
```

`test-layout.cpp` 与 `test-gdiplus-scale.cpp` 是**开发工具**，不随 mod 交付。
`_settings.py` / `_preview.py` 需要 Pillow 与 Python。

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
（去掉堆分配后约 158 KB，静态链接 libc++ 会涨到约 590 KB），
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

**允许出现的两个警告**：

1. `winsock2.h:15:2: warning: Please include winsock2.h before windows.h` —
   **无法消除**：Windhawk 用 `-include windhawk_api.h` 强制先包含 `windows.h`，
   而该文件第一行就是 `#include <windows.h>`。这是头顺序问题，不是代码问题。
2. `unused function 'ApplyPanelTransform'` — 已知残留：该函数在主 mod 中未被调用，
   只服务于回归测试。可以消除（加 `__attribute__((unused))` 或挪进测试文件），
   见 §10。

**如果出现别的警告，要处理掉。**

> **本地编不出"安装版"**：`compile_flags.txt` 带 `-DWH_EDITING`，而在这个宏下
> `windhawk_api.h` 把 `Wh_Log` 定义成空的内联函数、`WH_MOD_ID` 变成占位符、
> 也不引入 `windhawk_api_internal.h`。所以本地 build **是语法/类型门，不是与安装版逐位一致的产物**：
> 日志调用连同它们的格式串在本地被编译掉，DLL 里搜不到任何日志字符串——我为此白排查过一轮
> （"DLL 里没有新日志"其实什么也没证明）。去掉 `-DWH_EDITING` 也救不了：那会引用
> `InternalWh_*`，而它们由 Windhawk 自己提供，本地链接不过。
> **要检查日志格式串，用测试宿主**（它打桩了一个真的 `Wh_Log`，会走 `-Wformat`）。

### 设置块校验（改过设置项就跑）

```powershell
python _settings.py
```

检查设置块结构并统一 `$name` / `$description` 的引号。期望输出含
`no structural problems found`。

注意：`$name` / `$description` 必须**单引号包裹**（裸的 `-` 会被 YAML 解析成序列，
导致 `bad indentation of a mapping entry`）；数值设置项**不要加引号**，否则
`Wh_GetIntSetting` 取不到。

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

期望最后一行 `PASSED (0 failure(s))`。

> **受限沙箱会跳过一条断言**：命名管道客户端在 DSH 的受限模式下被拒绝
> （连"完全不给安全描述符"的对照管道也打不开，`err=5`），所以 DACL 那条会打印
> `this environment refuses pipe clients ... the DACL check is skipped here`。
> **跳过不等于通过**——要验证 DACL 得在普通终端里跑一次。
> 实测边界：**只读**打开可以（`GENERIC_READ` → `opened=1`），而 `GENERIC_WRITE`、
> `FILE_WRITE_DATA`、`GENERIC_ALL` 全部 `err=5`。因此宿主**没法**做管道端到端测试
> （客户端要写），管道读写路径只能靠真机日志——§10 的"未验证"里也记着这条。

> **mod 的日志格式串是在这里被检查的**：宿主打桩了一个真的 `Wh_Log`，所以 mod 里每个
> `Wh_Log(L"...%s", ...)` 都会被真的编译并走 `-Wformat`。**不要给这条命令加
> `-Wno-format`**，那会把这项检查一起关掉（加过一次，等于把唯一的日志类型检查关了）。
> 宿主自己那 15 条 `%08X` 收 `DWORD` 的警告是它自己的 printf，与 mod 无关。

**宿主原理**：`test-layout.cpp` 把 Windhawk API（`Wh_GetIntSetting` / `Wh_GetStringSetting` /
`Wh_Log` / `Wh_SetFunctionHook`）打桩，然后 `#include "endfield-info-windhawk.wh.cpp"`。
因此它能直接调用 mod 的**内部静态函数**——这是本测试的价值所在，
也是为什么布局计算要**抽成独立纯函数**（见 §4）。

打桩的设置 API **按键名返回值**，所以 `ReadSettings()` 可以被真实驱动，
用于测试配置解析（主题优先级、图标路径裁剪）。

测试**最后一段会真启动 GDI+**（`g_gdiplus.Load()` + `Startup`），用真字体量
"这段文字会不会折行、会不会冲出预算"。这是唯一能守住 §5.1 那条
"文字不得折行"的办法：纯函数测不了 GDI+ 的排版行为。GDI+ 起不来时该段会打印
`GDI+ did not start; section skipped` 而不是失败，所以在没有 GDI+ 的环境里测试仍可用。

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
| `ExpandTokensAndRings` / `BuildRowLayout` | 把一行文本切分成「文字段 + 环段」 |
| `ComputeRowTextMetrics` | 一行文字可用的宽度 |
| `ComputeIconGeometry` / `RingRadiusAt` | 图标盘与其波纹的几何（**单一来源**） |
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
`ComputePanelTransform` · `ApplyPanelTransform` · `IconPlateDiameter` ·
`IconPlateCenterForRow` · `ComputeIconGeometry` · `RingRadiusAt` ·
`PlanDownscale` · `CopyUserPath` · `ResolveIconStyle` / `ResolveIconPath` ·
`EvaluateIntro` · `FitCharacterCount` / `FitCharsWithEllipsis`（测量以函数指针注入） ·
`ExtParseLine` · `ExtSanitize` · `ExtParseNumber` · `ExtLookup` · `ExtNextDeadline` ·
`ExtEvictIndex` · `ExtProviderAllowed`

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
  压在后面的环上。所以每段在 `DrawString` 前要按自己的预算
  （内联段 = 实测 advance，右停靠段 = `metrics.textWidth`）调用
  `FitTextToWidth()` 截断。`FitTextToWidth` / `FitCharacterCount` /
  `FitCharsWithEllipsis` 是纯函数或注入测量的函数，都有断言。
- **`DrawString` 的框宽下限是 `kMinLayoutWidth`（1.0）**，不能是 0：
  0 宽会让 GDI+ 把整段裁掉，这与第一条是同一个坑的另一面。
  截断与框用的是同一个下限，两者不能各写一个数字。

### 5.2 配色

- **命名主题完全胜出，颜色字段被忽略。** 只有 `Theme = Custom` 才读颜色字段。
  曾做成"预设打底、字段覆盖"，结果预设永远看不到。
- **进入命名主题分支时要显式清 `ringColorUserSet`**，而不是仅仅不读它——
  否则残留字段可能从某条路径泄漏进预设配色。
- **环色优先级**：`:COLOR` > `Ring colour`（仅 Custom）> 主题边框色。
- **`{gpu.temp}` 等"已知但取不到"的变量渲染为 `--`**，未知变量保持字面文本。
  二者必须区分（见 §6 第 3 条）。

### 5.3 图标

- **图标盘每行各画一个**，位置在该行垂直中心；**波纹从各自图标发出**。
  曾只画一个盘在面板中心 → 多行时"涟漪从中间行弹出"。
- **盘与环共享同一个圆心，尺寸不同。** 由 `ComputeIconGeometry()` 一并产出，
  两个绘制函数都从它取值，不允许各自算一遍。
- **环半径从盘半径起步**向外扩张（`RingRadiusAt`）。
  曾用 `盘半径 × 0.5` 起步，环比盘小、压在字形上，看起来像"不同心"。
- **环必须画在入场世界变换之内**，否则与盘处于不同坐标系。
- **自定义图片：小而宽高不等的源图，两轴不能分别钳位。** 曾导致 2:1 的图被压成 1.35:1。
- **用户路径必须裁剪引号与前后空白**（`CopyUserPath`）。
  曾把 `"C:\...\img.png"` 的引号一起传给文件 API → "文件明明在却 could not load"。
- **图片载入失败必须回退到内置字形。** 否则只剩空盘，看起来像"图标变纯白"，
  会把人引向完全错误的排查方向。
- **自定义图片在载入时按盘径重采样一次**，不在每帧缩放。大图一步压到 23px 会有锯齿，
  且每帧重采样很贵。

### 5.4 动画

- **`g_displayHeight` 是静止几何**（`PlaceWindow` 与 `WM_NCHITTEST` 都用它），
  **`g_paintedHeight` 是当前帧绘制高度**。动画**不得**写回前者。
  曾混用 → 窗口与命中矩形在动画中途缩到标签尺寸 → "弹出后立刻收起"。
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

- **只在有行引用 `{ext.*}` 且设置打开时才创建管道**：没有引用就是零成本
  （不建线程、不建表）。这条和 `RequiredGroups` 的按需加载是同一个设计。
- **展开行文本时持 `g_extLock`，且绝不给锁嵌套**：先放 `g_extLock` 再拿 `g_valuesLock`。
  HUD 线程**永不**触碰 `g_extLock`——它只读两个原子（`g_extDeadlineMs`、`g_extDirty`），
  这是锁图能保持无环的原因。
- **值过期不能只重绘**：`g_rowState` 存的是**已展开的文本**，token 早就不在了，
  只有 sampler 再展开一次，`--` 才会出现。所以 deadline 由 sampler 发布（`ExtNextDeadline`），
  40 ms 的 tick 到期时只是**再要一次采样**。
- **外部文本不被二次展开**（注入不成立）。这条由结构保证——`ExpandTokens` 只对模板做单向扫描，
  替换出来的文本不会被重新扫描——并且有断言钉住：把展开改成递归，
  `test-layout.cpp` 里 "a pushed value that looks like a token stays literal" 必须变红（已实测）。
- **停机顺序：IPC 先于 sampler**（写者先于读者），最后才 `DeleteCriticalSection(&g_extLock)`。
- **`CreateNamedPipe` 的 `nMaxInstances` 必须等于实例线程数**（每线程建一个实例）。
  传 1 会让第一个线程占满额度、其余线程以 `ERROR_PIPE_BUSY (231)` 退出——表面上"serving 4
  instances"，实际只有一个客户端能连。**231 是瞬态**（实例都被占着），要等一会儿重试，
  不能把线程退掉；其他错误码才是永久的。
- **I/O 等待必须有上界，且超时不是错误。** 读与连接都**不**用 `INFINITE` 等完成事件：
  一旦完成事件丢失，线程会永久停在等待里，而它**连"我卡住了"都打不出来**（所有出口日志
  都在那行等待之后），从外面看就是"客户端连上后彻底静默"。现在的规则（`kExtIoTimeoutMs`）：
  — 缓冲为空 → 继续等**同一笔**操作（客户端安静是正常的，取消重投只会白烧 CPU）；
  — 缓冲里**有数据却没收到完成** → 记一行 `read completion never arrived ...; re-posting`，
     取消并重投（取消不消费数据，字节还在）；
  — 连接等待超时同理：没客户端就继续听，不算失败。
- **传输必须由 mod 自己证明。** 开机自检 `ExtSelfTest()` 用本进程连自己的管道、发
  `HELLO 1 selftest` + `SET selftest ok - pipe self-test`：宿主与沙箱都验证不了管道读写
  （见 §3），于是让**跑 mod 的那台机器**验证。两帧若没出现在日志里，问题在读路径本身；
  出现了，缺值就是客户端没写。同时每连接打一行
  `pipe flags=… curInstances=… maxInstances=…` —— **同名管道若被第二个进程也建了实例，
  客户端会被抢走**（本进程只会一直等一个永远不来的字节），这一行会把它暴露出来。
- **事件队列有自己的一把锁**（`g_islandLock`）：管道线程入队、HUD 取事件，**永不与
  `g_extLock` 嵌套**（HUD 从不碰值表）。唤醒 HUD 用 `PostThreadMessage(g_hudThreadId, …)`，
  绝不用窗口句柄——句柄可能已经没了（设计 §5.6.3 R5）。
- **卡片宽度是静态的，不做动画。** `g_restWidth` 是静止宽度（窗口与命中都读它）：卡片在时等于
  `ComputeIslandCardWidth` 的结果，否则等于面板宽度，**同一状态下它不逐帧变化**。曾经的宽度动画
  （以及"先把窗口改成目标宽、再插值"）已移除——**只要窗口宽度会在帧间变化，就必然存在
  "窗口已改、位图未提"的那一帧**，这与 §5.4 的收起闪烁是同一类问题，表现为极短闪烁且无法靠
  调参数消除。现在宽度由 `PaintPanel` 在**创建位图之前**设定
  （`if (width != g_restWidth) { g_restWidth = width; PlaceWindow(hwnd); }`），
  所以任一帧内窗口与提交的位图尺寸永远一致。
- **`PlaceWindow` 在静止宽度还是 0 时回落到面板宽**，否则会得到一个 0 宽的窗口。
- **卡片宽度必须钳到屏幕**（`ComputeIslandCardWidth` 同时受用户配置的
  `Island minimum width` 与屏幕宽度约束，上限取"屏宽 − 24"与 640 中较小者）。
- **卡片几何 / 区域 / 命中是"单一来源"**（M3 第一片已落地）：`ComputeIslandRect` 给出卡片矩形
  （宽度、屏幕钳位、居中），`ComputeIslandCloseRect` 由它派生出关闭按钮的方块，
  `BuildIslandRegions` 按"后写在上"的顺序产出区域表，`IslandHitTest` 返回**最后一个**命中的区域——
  **这个顺序本身就是"看得见的都能点"那条规则**（断言逐像素验证：卡片内不存在 `IR_NONE` 像素）。
  绘制侧调用同一批函数（画片的位图**就是**卡片，所以只取宽度，局部原点恒为 (0,0)），
  点击侧以窗口矩形作屏幕坐标；两侧不许各算一套几何。
- **逐帧成本点只有这些**（性能纪律：**没东西要用就不开通路**，和 `RequiredGroups` 的按需加载、
  "没有 `{ext.*}` 引用就不建管道"是同一条）：定时器是 40 ms 的廉价轮询（`kCursorPollMs`），
  **只有面板高度动画或岛上某个相位在跑时才是 16 ms**（`DesiredTickMs` / `ApplyTickRate`，
  而且只在答案变化时才调 `SetTimer`）。悬停**不占定时器**：由 `WM_MOUSEMOVE` 驱动、
  用 `TrackMouseEvent` 注册 `WM_MOUSELEAVE` 收尾，**并且只在热态翻转的那一次重绘**——
  指针静止时连一条消息都不会来。岛上真正的每拍成本是：一次 `GetTickCount`、一次状态机步进，
  以及状态非 Idle 时在锁内复制一份队列快照；**关掉岛时只剩一个比较与返回**。
  顺带修好的一处：卡片自己的 Pop/Collapse 相位以前跑在 40 ms 轮询上（只有 `StartIntro` 会提到
  16 ms），所以淡出只有两三帧；现在相位与高度动画共用同一个决策点。
- **关闭卡片与 ttl 到期走同一条路**（`IslandDismiss` → `COLLAPSING`），所以淡出与状态机不会分叉；
  它**幂等**，第二次点击不会重启淡出。
- **"动画期间取两态并集"这条规则仍然不在代码里**：它服务于已移除的宽度动画。现在没有动画窗口期，
  区域表本身就是完整的可点范围；等 M3 的悬停态落地时再回来看是否需要它。

---

## 6. 已修复的缺陷（回归测试清单）

改相关代码前先看对应断言。表右列是"如果有人改错会看到什么"。

| 缺陷 | 症状 | 关键断言 |
| :-- | :-- | :-- |
| 无环行的可用宽度算成 0 | 第一行之后都不显示 | 无环行拿到完整内宽 |
| 内联环前的尾随空格未计入 | 环前面的空格失效 | 展开文本保留空格 |
| 自定义颜色压过命名主题 | 主题被自定义配置覆盖 | 命名主题完全胜出 |
| 环颜色来源混乱 | 环的颜色盖不住 | `:COLOR` > Custom 的 `Ring colour` > 边框色 |
| 已知变量取不到值被当未知 | 屏幕出现字面 `{gpu.temp}°C` | 已知不可用渲染为 `--` |
| 高度关键帧带收回段 + 过冲过大 | 弹出→收回→突然弹出 | 高度不得出现可见回落 |
| 图标盘只画一个且居中 | 涟漪从中间行弹出 | 每行一盘、点位正确 |
| 波纹未随世界变换 | 涟漪与图标不同坐标系 | 盘心环心逐位相同 |
| 最内圈半径小于盘半径 | 涟漪与图标不同心 | 环从盘边缘起步向外扩张 |
| 文字淡入窗口过短且只有一段 | 文字像啪地出现 | 淡入经多个中间透明度且单调不回头 |
| 大图一步压到图标尺寸 | 图标锯齿 | 逐级折半；各阶段只缩不放 |
| 缩放时两轴分别钳位 | 非方图被拉伸 | 比例漂移 <12% |
| 路径引号被当作路径一部分 | 文件明明在却 could not load | 引号/空格被裁剪且不残留 |
| 图片载入失败不回退字形 | 只剩空盘，像"图标变纯白" | 失败必须回退字形 |
| 行 `StringFormat` 少了 `NoWrap` | **第一行文字不在一条直线上**（该段首行上浮） | 超宽文本仍是 1 行；真 GDI+ 断言 |
| 关掉换行后文本不截断 | 文字压到后面的环上 | 每段都落在自己的预算内 |
| 管道 DACL 写成 `CO`（creator owner） | **每个客户端都被拒绝**（`PermissionError` / `ERROR_ACCESS_DENIED`），连启动 mod 的用户自己都连不上 | 用同一个 SD 建管道、让当前用户连上：`test-layout.cpp` 的 "the pipe DACL admits the user running the mod" |
| 提供者名从帧行拷贝时未按长度截断 | 客户端连上、`SET` 被接受，行**永远显示 `--`**；且每次重连占一个新槽位，8 次后 `HELLO` 开始回 403 | 用**带换行**的 `HELLO`/`SET` 驱动 handler，断言 `{ext.provider.key}` 查得到（旧代码下红两条） |
| `CreateNamedPipe` 的 `nMaxInstances` 传 1 而线程数是 4 | 日志写着 `serving 4 pipe instance(s)`，实际 3 个线程以 `CreateNamedPipe failed: 231` 当场退出，只支持 1 个并发客户端 | **没有断言能覆盖**（这是 API 调用点的常量错，宿主里没有真管道）；判别依据是错误码语义——**断言覆盖不到的地方，日志就是防线** |
| 读用 `INFINITE` 等完成事件，完成事件一旦丢失线程就永久停住 | 客户端**连上了**（`ExtLogClient` 有），之后日志**完全静默**——连 `client done` 都没有，行永远 `--` | 等待加 500 ms 上界 + `PeekNamedPipe` 判定；缓冲有数据却没完成就重投。判别日志：`EXT read#1 result=… got=…`、`read completion never arrived with N bytes queued`、`no frame for 2s and the pipe buffer is empty` |
| 收起时先把窗口缩到标签高度、再长回来画（`SetExpanded` 里那次 `PlaceWindow`） | 收起瞬间闪出一条"切片"，极短、难复现；鼠标离开检测区延迟后触发 | **无法断言**（这是分层窗口的帧内顺序，不是数值）。判据是流程：窗口尺寸必须与**同一帧提交的位图**一致；收起方向由绘制侧逐帧跟随内容（`if (expanded) PlaceWindow(hwnd);`） |

### 排查心法：先分类，再定位

本项目缺陷的症状高度重叠（"某处空白"可能是采样、布局、变换、配色、载入五层中的任意一层）。
**按下面顺序分类，不要跳步**：

1. **采样层**：`Paint` 日志里的 `published` vs `visibleRows`。
   `published` 小 → 采样端问题，与绘制无关。
2. **布局层**：`rowN segs= textLen= inline=` 与逐段 `draw row= seg= x= y= w= adv=`。
   **`w=1.0 adv=0.0` 就是"文本被裁没"**，这是 5.1 第一条的指纹。
3. **几何层**：`ComputeIconGeometry` 的输出（盘心、盘半径、环半径）。
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
2. **`ApplyPanelTransform` / 变换类断言要避开不动点。** 我第一版断言比较的是
   缩放变换的**不动点**（面板中心），映射前后必然相同，断言恒真、什么也证明不了。
   要用**偏离中心的点**，并给出"错误做法"的对照数值。
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

---

## 8. 外部依赖与平台约束

### GDI+ 通过 `GetProcAddress` 动态绑定

**不包含 `gdiplus.h`**：它的 API 全是 `static inline` 的 `Gdiplus::*` 包装（不可链接），
且它的 `RectF` 会与本文件的定义冲突。所有函数与常量都手动 `RESOLVE`。

**已有导出名陷阱**（都踩过）：

| 你要的 | 真实导出名 |
| :-- | :-- |
| 启动/关闭 | `GdiplusStartup` / `GdiplusShutdown`（**没有 `Gdip` 前缀**） |
| 世界变换 | `GdipMultiplyWorldTransform`（不是 `GdipMultiplyTransform`） |
| 画线 | `GdipDrawLines`（`GdipDrawLine` 在本工具链是 5 参的非常规导出） |
| 矩形填充 | `GdipFillRectangle`（需把 `GpSolidFill*` 转成 `GpBrush*`） |

**常量**：`PixelFormat32bppPARGB = 0x000E200B`（已核对）、`InterpolationModeHighQualityBicubic = 7`。

**没有 arc 原语**：圆用 `AddPathArc` 两段闭合路径填充；环用 `GdipDrawEllipse`。

**没有 `min` / `max` 宏**（那是 MSVC 的）。用本文件的 `ClampInt` / `MinInt` / `MaxInt` / `MinDouble`。

**`pdh.h` 缺 `PDH_*` 状态常量**，已在本地补齐（如 `PDH_MORE_DATA 0x800007D2`）。

### 头文件顺序（MinGW 特有）

`winsock2.h` → `netioapi.h` → `iphlpapi.h` → `windows.h`。
顺序错了会 `unknown type name 'PMIB_IF_TABLE2'`。

### 平台限制

- **全屏独占（exclusive fullscreen）的 3D 应用无法被覆盖**——它绕过 DWM 合成，
  任何窗口都不可能叠在上面。这是平台限制，不是可以通过代码解决的问题。
- **多显示器**：锚定"最上方"的显示器（`rect.top` 最小者，平手时取主显示器）。
  本机只验证过 2 台。

### NVML 与一个真实的驱动安装缺陷

某些 NVIDIA 驱动安装会把 `nvml.dll` 放进 `System32` 却**不把它的依赖
`nvml_loader.dll` 一起放过去**，于是 `LoadLibrary("nvml.dll")` 返回
`ERROR_MOD_NOT_FOUND (126)`——**而文件本身存在**。

因此 `LoadNvmlWithDependencies()` 会先在驱动仓库里搜到 `nvml_loader.dll` 预载，
再加载 `nvml.dll`。搜索是有限广度搜索（深度 ≤3、队列 ≤64、只跑一次、在采样线程上）。

回退到 PDH 时**没有 GPU 温度**，所以 `{gpu.temp}` 显示 `--`。
另外 `PdhAddEnglishCounter` **首次调用约 550 ms**（所以它在采样线程、不在 UI 线程），
且 `PdhCloseQuery` **不归还内存**——采集器一旦启动就常驻到重载。

**日志里的判别依据**：`error=126, already-in-system32=1` 就是上述缺陷；
`error=2` 才是文件真的不存在。

---

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
4. 在 `test-layout.cpp` 的 token 覆盖列表里加一行——
   **有测试保证"32 个变量全部能解析"**，漏了会被测出来。
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

### 报告与调试

- mod 的日志走 `OutputDebugString`，Windhawk 会自动加 `[行号:函数名]` 前缀。
  这个前缀可以用来**核对部署的是哪一版源码**（与本地源码行号对比）。
- 诊断日志要用**有界计数器**（如 `g_iconDrawLogs`），不要每帧刷屏。
- 日志要打印**判别性数值**（坐标、尺寸、错误码、颜色），不要只打印"失败了"。
  一个 `image=none` 比十行"画完了"有用。

---

## 10. 验证状态

**已验证**：

- 用 Windhawk 自带工具链编译链接通过，`-Wall -Wextra` 下只剩 §3 记的两个已知警告
  （头顺序 + `ApplyPanelTransform` 未使用）。
- 7 个导出入口齐全。
- 依赖仅系统 DLL：`USER32 / GDI32 / PDH / IPHLPAPI / PSAPI / ADVAPI32 / msvcrt / KERNEL32`。
- 回归测试全绿（295 条断言，含一段**真启动 GDI+** 的排版断言：
  文字不折行、且在预算内）。
- GDI+ 缩放管线用**已知内容**的图验证过（含透明底、纯 RGB、索引色、灰度+alpha、
  16 位、隔行 PNG）。
- 外部数据（M1）：编译只剩上面那两条已知警告；回归 **295 条断言**全绿（M1、M2 与 M3 第一片共新增 144 条）；
  并做了**负对照**——把展开改成递归后，注入断言确实变红（不是"看着像在守"）。
- 设置块 46 项 / 10 个选项组，`_settings.py` 报 `no structural problems found`。
- **mod 的日志格式串**用测试宿主查过：`-Wformat` 指向 `endfield-info-windhawk.wh.cpp`
  的警告为 0（本地 mod build 检查不了这项，见 §3）。
- 外部数据的**服务端路径**（`ExtHandleLine`：HELLO → SET → 存储 → 查找、换行框架与边缘空格裁剪、
  允许名单拒绝）有断言覆盖（总数见上文，不再重复一个会过期的数字）。
- **真机整链验证（用户环境，2026-10-13）**：mod 的开机自检两帧被服务端完整收到
  （`self-test sent 2 frames` → `recv bytes=17/33` → `hello provider=selftest` → `set … key=ok`），
  且自检值在行里**显示了出来**——传输、存储、展开、绘制四段都成立；随后用
  `_extclient.ps1`（.NET）发布的值也**在面板上显示**。管道参数同时被记录：
  `flags=0xd`（服务端 + message 类型 + 拒绝远程）、`curInstances=4`（4 个实例都是本进程的，
  没有第二个进程抢同名管道）。
- **`_extclient.py` 尚未在真机上被观察到写入**：那个环境里它连上后同一毫秒即断开、零帧，
  而脚本自身的时序（写完两行后至少 1 秒才关闭）与之不符——怀疑当时跑的并非本仓库这一份。
  要用客户端验证时**优先用 `_extclient.ps1`**，或先确认 `python -c "import os;print(os.path.abspath('_extclient.py'))"`
  指向本仓库的文件。

**未验证**（需要真实运行环境）：

- 点击/热区的实际手感（M3 的本地按钮落地后才会真正被考验）。
- 100 ms 重申是否足以消除"被短暂盖住"。
- 多于 2 台显示器的定位。
- 长时间运行稳定性（线程退出、采集器生命周期）。
- `{gpu.temp}` 在非 NVIDIA 机器上的表现。
- **外部数据的端到端行为**（无法在本机自动验证，必须真跑 Windhawk）：客户端连接/断开/重连、
  多个提供者并发、50 ms 唤醒的实际延迟、到期 → `--` 的实测时间、非管理员账户下管道 DACL 的行为、
  卸载时有在途消息时的收尾。
- **管道读写路径本身**：本机沙箱拒绝一切带写权限的管道打开（见 §3），所以宿主连不上真管道；
  这块只能靠 mod 的开机自检（`ExtSelfTest` 的两帧是否被服务端记录）与真机日志来验证。
- **事件面（M2）**：队列规则（替换/合并/优先级/丢弃）、状态机（`StepIsland`）、
  卡片内容模型（`PlanIslandCard`）、severity 配色（`ResolveSeverityColor`）、
  卡片宽度（`ComputeIslandCardWidth`，含最小宽度与屏幕钳位）都有断言，共 295 条；
  **卡片绘制本身与宽度改动的观感**只能在机器上看，仍列在未验证之内。
- **M3 第一片（卡片交互）**：`ComputeIslandRect`（矩形与宽度同源）、`ComputeIslandCloseRect`、
  `BuildIslandRegions`、`IslandHitTest`、`IslandDismiss` 都有断言，其中包括一条**整体性质**：
  逐像素遍历卡片，要求没有任何像素落在 `IR_NONE`（"看得见的都能点"）。**悬停态尚未实现**；
  关闭按钮在真机上的点击手感（含圆角外穿透是否照旧）仍列在未验证之内。
- **收起闪烁已真机确认修复（2026-10-13）**：原因是 `SetExpanded(false)` 先把窗口缩到标签高度、
  再由绘制帧长回来（§5.4 / §6 / §7.12）。修复方式是不再预缩，由绘制侧逐帧跟随；用户在真机上
  确认闪烁消失。这条**没有断言能守**（是分层窗口的帧内顺序），只能靠这条不变量与注释。

**已知的残留警告**：`ApplyPanelTransform` 在主 mod 中未被调用（只服务于测试），
会报 `unused function`。要么给它加 `__attribute__((unused))`，要么把它挪进测试文件。

---

## 11. 参考

- 原项目（视觉与设计语言的来源）：
  `Customization/`、`Styles/HudTheme.axaml`、`Animations/HudAnimations.cs`、`Views/`
- Windhawk tool mod 样板：
  <https://github.com/ramensoftware/windhawk/wiki/Mods-as-tools:-Running-mods-in-a-dedicated-process>
- 本地工具链：`D:\Program Files\Windhawk\Compiler\`（clang 20.1.3 + MinGW）
- 真实编译参数：`C:\ProgramData\Windhawk\EditorWorkspace\compile_flags.txt`
- 本仓库的设计文档（**M1 与 M2 已实现；M3 进行中——几何/区域/命中/关闭按钮已落地，悬停态待做；
  M4 仍是提案**，不要当成现有行为）：
  [docs/external-data-island-design.md](docs/external-data-island-design.md)
