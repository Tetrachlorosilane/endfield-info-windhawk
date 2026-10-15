# build.ps1

一句话：用 Windhawk 自己的编译器和参数在命令行里编译 mod，省掉"每试一次都要点一下 GUI"。

## 怎么跑

```powershell
pwsh -ExecutionPolicy Bypass -File build.ps1
```

没有参数。它自己找 `endfield-info-windhawk.wh.cpp`（与本脚本同目录），
产物写到 `$env:TEMP\endfield-info-windhawk.dll`。

## 需要什么

- Windhawk 工具链：`D:\Program Files\Windhawk\Compiler`（缺了直接 throw）。
- 与之匹配的真实编译参数：`C:\ProgramData\Windhawk\EditorWorkspace\compile_flags.txt`。
  这个文件不存在时脚本回落到**内联的那一份**（同样的 `-std=c++23` / `-target x86_64-w64-mingw32`
  / `-DUNICODE` / `-D__USE_MINGW_ANSI_STDIO=0` / `-DWH_MOD` / `-DWH_EDITING` / `-Wall -Wextra` …），
  所以干净安装上也能用。
- 可选：`D:\Program Files\LLVM\bin\llvm-objdump.exe`，有才会打印导出符号表。
- 不需要管理员权限。

## 输出与判读

- 开头一行 `compiling …`，随后是编译器自己的输出。
- **期望恰好一条警告**：`winsock2.h:15:2: warning: Please include winsock2.h before windows.h`。
  这是 Windhawk 自己的头文件顺序问题，不是本仓库的代码问题；**出现别的警告要处理掉**。
- 成功那一行形如 `OK  <字节数> bytes -> <完整路径>`，后接 `exports:` 与
  `Wh_Mod*` / `WhTool_*` 的符号列表。字节数量级是 18 万（2026-10-14 实测 182272），
  只当参考，不要当成断言。
- 失败打印 `FAILED (exit N)` 并 `exit 1`。成功路径没有显式退出码（即 0）。
  判据是两个条件同时成立：编译器退出码为 0 **且** 产物文件真的存在。
- 链接的库写死在脚本里：`-shared -O2 -luser32 -lgdi32 -lpdh -liphlpapi -lpsapi -ladvapi32`。
- **产物不是逐字节可复现的**（实测 2026-10-16）：同一份源码连编两次，大小相同
  （203776 B）、sha256 却不同，二进制差 **4 个字节**（PE 头的时间戳）。所以
  **不要拿"两次构建的哈希不同"当"代码变了"的证据**——它每次都不同。反过来，
  **文件大小也不必变**：PE 段按对齐切分，改几行代码完全可能落在同一个字节数上。
  要证明改了东西，看源码 diff 与断言，不要看哈希或体积。

## 坑

- **本地编不出"安装版"。** 参数里带 `-DWH_EDITING`，在该宏下 `Wh_Log` 是空的内联函数，
  日志调用连同格式串一起被编译掉。所以**在 DLL 里搜不到某条日志字符串，什么也证明不了**
  （去掉 `-DWH_EDITING` 也救不了：那会引用只有 Windhawk 自己提供的 `InternalWh_*`，本地链接不过）。
  要检查日志格式串用 `python .tool/checks/_logfmt.py`。
- 脚本刻意不用 `new` / `delete` / `std::*` / 异常 —— 这是**体积**决定，不是"堆分配不可用"：
  Windhawk 会自带并注入 `libc++.whl`。避开堆分配让 DLL 停在约 178 KB，
  换成 `-static-libstdc++ -static-libgcc` 会涨到约 578 KB。
