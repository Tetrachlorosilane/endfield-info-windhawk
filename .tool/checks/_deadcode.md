# _deadcode.py

一句话：找出编译器看不见的死代码——只写不读的结构体字段、`#if 0`、注释掉的代码、
脚本里定义了却没人引用的函数，以及**同一个文件里定义了两次的名字**。

## 怎么跑

```powershell
python .tool/checks/_deadcode.py      # 默认递归扫本仓库的 .cpp / .ps1 / .py（跳过脚本自己）
python .tool/checks/_deadcode.py .tool/regress/test-layout.cpp   # 也可以只扫指定文件
```

从任何工作目录跑都一样：仓库根由脚本**自身位置**解析（`Path(__file__).resolve().parents[2]`），
不依赖 cwd。

## 需要什么

Python 即可。本机 `python` 若是 Windows Store 占位程序，改用 DSH 运行时的解释器：
`C:\Users\HP\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe`。

## 输出与判读

- 首行 `dead-code scan: N C++ file(s), M PowerShell, K Python`。
- 每条发现一行：`  <类别>  <相对路径>:<行号>: <内容>`；类别有 `write-only field`、
  `#if 0`、`commented-out code`、`unreferenced function` / `unreferenced def`、
  `duplicate definition`。
- 末行是判据：
  - `no dead code found (every field is read, no #if 0, every script function used,
    0 duplicate definitions among <n> script definition name(s))` —— 干净，退出码 0。
    末尾那个 `n` 是**被检查过的定义名总数**（不是函数个数），它就写在判据里，
    这样"扫了 0 个名字"和"扫了 71 个名字且都干净"不会再长得一样。
  - `<n> finding(s): <类别列表>` —— 退出码 1。
- 字段那类还会说清是 `is set Nx and never read` 还是 `is never used at all`。
- `duplicate definition` 占两行：第一行给出文件、**每一处行号**，第二行给出判决——
  `byte-identical bodies -- provably dead, only the last definition can run`
  （两份逐字节相同，前面那份**可证明**永不执行），或
  `bodies differ -- the earlier definition can never run, and the two have silently diverged`
  （两份已经悄悄分叉：前面那份仍不可达，而且两份行为不一致——比相同的那类更危险，不是更轻）。

编译器只管**符号**（`-Wunused-function` / `-Wunused-variable` / `-Wunused-macros` 已在 build.ps1 里），
字段级和"注释里的代码"是它管不到的部分。

## `duplicate definition` 查什么、刻意不查什么

**为什么单列一条**：原来那条规则问的是"这个名字有没有人引用"，而重复定义**两份都"有人引用"**
（调用方绑到后面那份），所以一个永不执行的定义对它是隐形的。`-Wunused-function` 也看不见脚本。
本仓库真出过：`_netease-bridge.ps1` 里 `Test-VinylJumpPlausible` 有两份，`e141514` 删掉了第一份——
**删除前**跑旧版检查器得到的是 `no dead code found`（实测：拿 `e141514^` 的旧检查器扫 `e141514^`
的桥，退出码 0）。这一条就是为那个形状加的。

**查**：

- PowerShell：**第 0 列**的 `function` / `filter` / `workflow`，同一文件里重名即报。
- Python：任意缩进的 `def` / `class`，同一缩进 + 同一作用域链下重名即报。
- 判定"是不是同一份"用**去掉公共缩进后的函数体文本**（注释不参与，缩进不参与）。

**刻意不查**（每一条都是为了不误报）：

- **C++**：重复定义在 C++ 里根本编译不过，编译器已经是裁判，这条规则不插手。
- **不在第 0 列的 PowerShell 定义**：嵌套 `function` 只有父函数跑过才可见，"后面那份覆盖前面那份"
  对它不成立；桥里那些 **here-string 内嵌的 JavaScript `function`** 也在此列（先按 PowerShell 自己
  的规则——结束标记必须从第 0 列开始——把 here-string 体遮掉，再找定义）。
- **Python 同一个条件语句的不同分支**（`try:`/`except`、`if`/`else`）：这正是"导入回退"的合法写法，
  一律放行；判据是**各自的外层条件行集合不同**（两个并排的 `if` 因此是两个条件，不是同一个条件的两臂）。
- **同一分支内的重名**（哪怕是 `if` 里面）照报不误：那正是不可达的形状。
- **不同文件**里的同名，以及 `Set-Alias` / `$x = { … }` / `x = lambda` 这类非定义语句绑的名字。

**遇到解析不确定时，方向是"宁可多报"**：写在多行的条件头（`if (` 单独一行）会被读成"不是条件头"，
于是这种配对会上报——即使它其实是合法的回退写法。这是刻意的取舍：漏掉重复定义才是这条规则存在的理由。

判据（§7 第 8/13 条）：**把它要防的改动回去，必须变红**。已实测两向——
把 `Test-VinylJumpPlausible` 的重复原样放回 `_netease-bridge.ps1`（第 771 / 778 行）当场报
`byte-identical bodies`；而 `try/except` 回退、`if/else` 选实现、函数体里**提到**别的函数名、
here-string 里的 JS `function` 这四种形状全部保持 `no dead code found`。

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
- **重复定义那条别用"引用计数"去想**。它和"脚本死函数"用的是同一份定义清单，但判据完全不同：
  死函数问"有没有人引用"，重复定义问"同一个名字在这个文件里出现了几次、哪一份能执行"。
  名字被引用**完全不能**证明两份定义里只有一份是活的——桥里那次两份都"有人引用"。
- 解析器本身的坑（都已踩过并修掉，改动这段代码时留意）：PowerShell 定义必须按 `re.M` 匹配行首
  （少了 `re.M` 会搜出 0 个定义，而输出仍然说"干净"）；条件头的行首词只取纯字母
  （`([A-Za-z_]\w*)` 会把 `if _HAS_FAST:` 读成 `_HAS_F`，于是所有条件都被当成"非条件"而误报）；
  行尾注释要先切掉再判"是不是以 `:` 结尾"。这些都是**静默**失效——判据仍是"改坏它会不会红"。
