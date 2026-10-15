# 平台与外部依赖笔记（原 AGENTS §8；规范条文在 [invariants/window.md](invariants/window.md)）

> 从 AGENTS.md 搬出来（64 KB 指令预算）。这里是**只读参考**：查导出名、查 NVML 的加载缺陷时用。

## GDI+ 通过 `GetProcAddress` 动态绑定

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

**没有 arc 原语**：圆用 `AddPathArc` 两段闭合路径填充（`AGENTS.md` §8 的原话：圆用两段
`AddPathArc`）；环用 `GdipDrawEllipse`。

**没有 `min` / `max` 宏**（那是 MSVC 的）。用本文件的 `ClampInt` / `MinInt` / `MaxInt` / `MinDouble`。

**`pdh.h` 缺 `PDH_*` 状态常量**，已在本地补齐（如 `PDH_MORE_DATA 0x800007D2`）。

## NVML 与一个真实的驱动安装缺陷

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

## 头文件顺序（MinGW 特有，原 AGENTS §8）

`winsock2.h` → `netioapi.h` → `iphlpapi.h` → `windows.h`。
顺序错了会 `unknown type name 'PMIB_IF_TABLE2'`。

## 窗口侧的平台限制

**全屏独占（exclusive fullscreen）的 3D 应用无法被覆盖**（它绕过 DWM 合成），
以及**多显示器锚定规则**（`rect.top` 最小者，平手取主显示器；本机只验证过 2 台）——
这两条是改窗口行为时会撞上的约束，**规范条文与"何时读"在
[invariants/window.md](invariants/window.md) 第 2 节**（原 AGENTS §8：这是平台限制，
不是可以通过代码解决的问题）。

---
