# _deadcode.py

一句话：找出编译器看不见的死代码——只写不读的结构体字段、`#if 0`、注释掉的代码，
以及脚本里定义了却没人引用的函数。

## 怎么跑

```powershell
python _deadcode.py                 # 默认扫本仓库的 .cpp / .ps1 / .py（跳过脚本自己）
python _deadcode.py test-layout.cpp _preview.py    # 也可以只扫指定文件
```

## 需要什么

Python 即可。本机 `python` 若是 Windows Store 占位程序，改用 DSH 运行时的解释器：
`C:\Users\HP\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe`。

## 输出与判读

- 首行 `dead-code scan: N C++ file(s), M PowerShell, K Python`。
- 每条发现一行：`  <类别>  <相对路径>:<行号>: <内容>`；类别有 `write-only field`、
  `#if 0`、`commented-out code`、`unreferenced function` / `unreferenced def`。
- 末行是判据：
  - `no dead code found (every field is read, no #if 0, every script function used)` —— 干净，退出码 0。
  - `<n> finding(s): <类别列表>` —— 退出码 1。
- 字段那类还会说清是 `is set Nx and never read` 还是 `is never used at all`。

编译器只管**符号**（`-Wunused-function` / `-Wunused-variable` / `-Wunused-macros` 已在 build.ps1 里），
字段级和"注释里的代码"是它管不到的部分。

## 坑

- **看到发现先别急着加豁免。** 第一次跑就是它找出本仓库自己的 5 处（`Values.valid`、
  `IntroFrame.rippleRise`、`ExtProvider.lastSeenAt`、`IconGeometry.ringInner` 只写不读，
  以及随之失去唯一调用者的几个函数）。判据是**先看它报什么，再决定是删还是加豁免**；
  豁免（`EXTERNAL_READERS` / `EXTERNAL_STRUCTS`）要写明理由——那几处是
  Windows / GDI+ 自己要读的结构体镜像。
- 三条刻意的限制，否则"干净"没有意义：读计数**跨所有 C++ 文件**（宿主 `#include` 了 mod，
  会读 mod 只写的字段，按单文件扫会误报）；**先剥注释再统计**（只在注释里出现的字段等于没人用）；
  **结构体体里有 `{` 就整个跳过**（成员函数可以不带 `this->` 引用字段，文本扫描看不见）。
- "注释掉的代码"要求注释以 `;` / `{` / `}` 结尾，否则散文会被误报；这是刻意的取舍。
