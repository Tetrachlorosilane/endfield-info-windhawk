# _extclient.py / _extclient.ps1

一句话：`{ext.*}` 外部数据管道的冒烟客户端。Python 版走 CPython 的 `open()`，PowerShell 版是
**不共享任何代码的第二客户端**——两个一起用才能把「客户端没写」和「传输坏了」分开。

## 怎么跑

PowerShell（**真机验证过的是这一个**）与 Python 版（参数同义：`--provider` / `--key` / `--ttl`，
`-` 用设置里的值、`0` 表示删除，另有 `--event` / `--raw` / `--quiet`）：

```powershell
pwsh -ExecutionPolicy Bypass -File .tool/pipe/_extclient.ps1
pwsh -ExecutionPolicy Bypass -File .tool/pipe/_extclient.ps1 -Key progress -Ttl 60000 -Text 0.42
pwsh -ExecutionPolicy Bypass -File .tool/pipe/_extclient.ps1 -Event -Text 'Blue Hour' -Artist 'Mili'
pwsh -ExecutionPolicy Bypass -File .tool/pipe/_extclient.ps1 -Raw 'GARBAGE'   # 看错误码
python .tool/pipe/_extclient.py "Now Playing: Blue Hour"
python .tool/pipe/_extclient.py --key progress --ttl 60000 0.42
python .tool/pipe/_extclient.py --event --artist Mili "Blue Hour"
```

参数：`-Provider`（默认 `music`）、`-Key`（`title`）、`-Ttl`（`600000`，`[int]`）、`-Text`、`-Raw`、
`-Event`、`-Artist`、`-Severity`（`info` / `warn` / `critical`）；事件模式下 `-Key` 不用，
`-Text` 是卡片**第一行**、`-Artist` 是**第二行**（留空 = 卡片只画一行）。
帧的形状是 `EVENT <provider> <severity> <ttl> <title>  <artist>`：**两个文本字段之间是两个空格**
（一个空格是文本的一部分——歌名里本来就有空格），协议正文见
[external-data-island-design.md](../../docs/external-data-island-design.md) §4.7。
卡片要真的出现，还需要设置里 `Notification island` 为 On（事件本身照样会被 `OK` 接收）。

## 需要什么

- **两个前置条件，缺一不可**：设置里 `External data` 为 On，且**至少有一行**含
  `{ext.provider.key}` token。否则 mod 根本不建管道，客户端会 `FileNotFoundError`
  ——这是按需加载生效，不是 bug。
- PowerShell 与/或 Python；管道名写死 `\\.\pipe\endfield-info-windhawk`。

## 输出与判读

- **服务端的回答在 Windhawk 日志里**，不在终端：`EXT hello ...` / `EXT set ...` / `EXT err code=...`。
  PowerShell 版只写不读，末行提示 `sent N frame(s); check the Windhawk log for EXT hello / EXT set / EXT err`。
- Python 版每行打印**实际写出的字节数**（`-> <内容>  (N/M bytes)`）、回包 `<- ...` 与总耗时 `(N ms)`。
- 退出码：Python —— 0 正常，1 是管道不存在或打开被拒；PowerShell —— 0 正常，连不上时打印原因并
  `exit 1`（`Connect(2000)` 带超时）。`--raw` / `-Raw` 是唯一能看错误码的路子（例如发 `GARBAGE`）。

## 坑

- **"打不开管道"和"DACL 不让这个用户连"从客户端看是同一个现象。** Python 版在 `PermissionError` 时把
  两句都写出来：可能是沙箱禁止任何管道客户端（受限模式连没有安全描述符的对照管道都拒），也可能真是
  DACL。**判别要用 mod 的日志**：`EXT: pipe DACL grants SYSTEM, Administrators and <sid>`，拿那个
  SID 和 `whoami /user` 比对；沙箱场景换普通终端再跑。
- PowerShell 版每行一次 `WriteLine`（协议是 message 模式），写完 `Start-Sleep -Milliseconds 300` 再
  关闭——这点等待留给服务端把收到的内容写进日志；Python 版把回复放在线程里读、用 `os.write` 直接写
  原始 fd，所以不读也不会卡住，而那个字节数正是区分"客户端没写"和"传输丢了"的证据。
