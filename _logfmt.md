# _logfmt.py

一句话：审计 mod 里宽格式日志调用点——编译器和工具链**一条都查不了**宽格式串，
这个脚本就是那个不存在的检查的替身。

## 怎么跑

```powershell
python _logfmt.py                                  # 默认查同目录的 endfield-info-windhawk.wh.cpp
python _logfmt.py path\to\other.wh.cpp             # 或指定文件
```

## 需要什么

Python 即可（只 import `re` / `sys` / `pathlib`）。本机 `python` 若是 Windows Store 占位程序，
改用 DSH 运行时的解释器：
`C:\Users\HP\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe`。

## 输出与判读

- 头部三行：`format call sites checked`（检查了多少个调用点）、`not decidable`（判定不了的，
  逐行列出 `line N <调用名>: <源码行>`），然后是两个小节：
  - `== mismatches ==` —— 判据。`(none)` 才是干净；否则每条形如
    `<文件>:<行>  <调用名>: <说明>`，并附上那一行源码。
  - `== 64-bit specifier given a 32-bit-looking argument ==` —— 单列的提示，不计入失败。
- **退出码：0 = 没发现问题，1 = 至少一处 mismatch。** 负对照（故意写错型）实测会红。

判定的三类：`%s`/`%S` 收到明显是数字的实参、`%d`/`%u`/`%x`/`%X`/`%c` 收到字符串字面量、
`%f`/`%e`/`%g` 收到明显是整数的实参；先按"说明符个数（含 `*` 宽/精度）对实参个数"过一遍。

## 坑

- 它检查的是 `Wh_Log` / `DBG` / `CpuClockLog` 三个调用名；`#define` 那一行与带 `...` 的定义会被跳过。
- **别指望编译器**：`format(printf,1,2)` 挂在 `wchar_t*` 格式参数上是硬错误
  （`format argument not a string type`）；`ms_printf` / `wprintf` 不被这个 target 支持；
  `-fms-compatibility` 会把编译器自己的头文件打崩；连 MinGW 的 `swprintf` 也不查。
  宿主里的 `Wh_Log` 桩同样**不**做格式检查（clang 只查带 format 属性的函数或 printf 家族）。
  所以"宿主全绿"对日志格式串什么也没证明，必须跑这个脚本。
- 它是语法级检查，不是编译器：判定不了就如实列在 `not decidable` 里，不猜。
  所以那个数字变大不等于出问题，但值得看一眼多出来的是什么。
