# _settings.py

一句话：校验 mod 头部 `// ==WindhawkModSettings==` 块的结构，并统一 `$name` / `$description`
的引号写法（可选择就地重写）。

## 怎么跑

```powershell
python _settings.py            # 只检查
python _settings.py --write    # 顺带把两个显示串规整成单引号
```

源文件按脚本自身位置定位（同目录的 `endfield-info-windhawk.wh.cpp`），因此在任何目录下跑都一样。

## 需要什么

Python 即可（脚本只 import `os` / `re` / `sys`）。本机 `python` 可能解析到 Windows Store 的占位程序，
打不开应用商店时改用 DSH 运行时的解释器：
`C:\Users\HP\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe`。

## 输出与判读

三行统计（`settings entries:` / `option groups:` / `display strings quoted:`），随后是判据行：

- **`no structural problems found`** —— 结构没问题，退出码 0。
- `PROBLEMS:` + 逐条 `行号: 说明` —— 退出码 1。判据是这一节是否为空，不是统计数字。
- `--write` 额外打印 `rewrote <完整路径>`。

它查的结构项：条目必须在第 0 列、`$options` 下的选项项**正好 4 空格**、每条目恰好一个
`$name` 与一个 `$description`、顶层键不重复、同一组内选项不重复、选项项必须出现在
该条目的 `$options:` 之后。

## 坑

- 上面这些检查是**被 Windhawk 的解析器真实拒绝过、而本脚本当时却说"没问题"**的那些：
  5 空格缩进 → js-yaml `bad indentation of a sequence entry`；
  条目插在别人的键与自己的 `$name` 之间 → `duplicated mapping key`。
  所以判据是"这一节有没有输出"，不要只看统计行。
- **`--write` 只重写 `$name` / `$description` 两种显示串**，其余行原样保留
  （早期版本会丢掉别的行、把整块毁掉，现在的逐行 pass-through 就是那次修复）。
  数值设置项**不加引号**，否则 `Wh_GetIntSetting` 取不到——脚本刻意不碰它们。
  重写是幂等的（实测字节不变）。
- 显示串用**单引号**是因为 YAML 的单引号没有转义序列，只有字面撇号需要写成两个；
  裸的 `-` 会被 YAML 当成序列，正是当年让 Windhawk 拒绝整个 schema 的东西。
