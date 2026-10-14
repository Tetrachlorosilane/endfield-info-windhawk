# _netease-lyric-probe.ps1

一句话：探针——网易云「桌面歌词」那一行文字，能不能被正经 API 读到（**完全不读内存**）。

## 怎么跑

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File _netease-lyric-probe.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File _netease-lyric-probe.ps1 -All
```

参数：`-Class`（默认 `DesktopLyrics`）、`-Title`（默认 `桌面歌词`）；`-All` 顺带列出所有
**疑似**网易云的窗口（类名匹配 `lyric` 或进程名匹配 `cloudmusic|Netease`），便于类名/标题随版本变化时对照。
**前提**：网易云**正在播放**，且客户端的「桌面歌词」已开启——那是这一行的宿主窗口。

## 需要什么

**建议用 Windows PowerShell 5.1 运行**（UI Automation 与 `Add-Type` 在它下面最稳）。
脚本会 `Add-Type -AssemblyName UIAutomationClient / UIAutomationTypes` 并内联编译一小段 C#
（`EnumWindows` / `EnumChildWindows` / `GetWindowTextW` / `GetClassNameW` / `GetWindowLongW` /
`IsWindowVisible` / `GetWindowThreadProcessId`）。不改任何东西、不读任何进程内存。

## 输出与判读

- 每个候选一行 `=== 窗口 class=… title='…' pid=… (进程名) visible=… layered=…`，
  随后 `自身标题是否像歌词: True/False`（像歌词 = 含 CJK/假名/谚文，或形如 `[00:12.34]`）。
- 然后两节：`子控件 N 个：`（`class=… text='…' lyricLike=…`，最多 25 条）与
  `UIA 后代元素 N 个：`（`控件类型 类名 '名字|值' lyricLike=…`，最多 30 条；依次试
  `TextPattern` / `ValuePattern` / `LegacyIAccessiblePattern` / `RangeValuePattern`）。
- 脚本末尾自己写了判读口径：**任何一行的 text/名字里出现歌词内容**（`lyricLike=True` 且**不是**
  「桌面歌词」这四个字）→ 可以走正经 API，不碰内存；**全是空 / 只有窗口标题** → 这行是自绘的，
  正经 API 读不到。
- 退出码：0 = 找到了窗口并枚举完；2 = 没找到 `-Class` / `-Title` 匹配的窗口
  （没在播放或没开桌面歌词时这是正常的）。

## 坑

- 存在的理由：读客户端内存能拿到当前歌词行，但偏移随版本变化，还要 `OpenProcess` /
  `ReadProcessMemory`。而「桌面歌词」本身是个真 Win32 窗口（类名 `DesktopLyrics`、标题 `桌面歌词`），
  所以先问一句"它的文字是不是已经通过窗口标题 / 子控件文本 / UI Automation 暴露出来了"
  ——会，就零风险解决；不会，再谈内存。**本脚本只负责给出这个事实，不下结论。**
- 找窗口时**不能叫 `$pid`**：那是 PowerShell 的只读自动变量（本项目踩过同类坑），脚本里用的是 `$procId`。
- 开着播放也找不到窗口时，用 `-All` 再跑一次并看输出：类名/标题可能随版本变了。
- UIA 枚举在某些窗口上会抛异常，脚本会打印 `UIA 枚举失败：…` 并继续——这只说明 UIA 这条路不通，
  不代表窗口里没有文字。
