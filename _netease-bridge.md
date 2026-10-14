# `_netease-bridge.ps1`

**示例发布器**：把网易云音乐"正在播放什么"送进 mod 的面板与灵动岛。它不是 mod 的一部分，
只是一个**用公开接口的普通客户端**——不注入、不挂钩、不读写客户端的内存（唯一例外是
`-PositionSource memory` 这条可选路径，默认关闭）。

它发布这些 key（面板里用 `{ext.netease.*}` 引用）：

| key | 内容 |
| :-- | :-- |
| `title` | `歌名 - 歌手` |
| `lyric` | **当前**歌词行 |
| `translation` | 当前行的翻译（客户端给了才有） |
| `pos` | 当前歌词用的**位置来源**与秒数，如 `dom 90.0s +1000ms`（排错用） |

## 怎么跑

```powershell
# 0) 离线自检：不联网、不连管道，只跑纯函数（46 项）
pwsh -ExecutionPolicy Bypass -File _netease-bridge.ps1 -SelfTest

# 1) 先看这台机器有什么：媒体会话 / CDP 调试端口
pwsh -ExecutionPolicy Bypass -File _netease-bridge.ps1 -ProbePosition
pwsh -ExecutionPolicy Bypass -File _netease-bridge.ps1 -ProbeCdp

# 2) 先看不发：把将要发送的帧打印出来（一次迭代）
pwsh -ExecutionPolicy Bypass -File _netease-bridge.ps1 -DryRun -Once

# 3) 真发（面板里要有一行引用 {ext.netease.title} 或 {ext.netease.lyric}）
pwsh -ExecutionPolicy Bypass -File _netease-bridge.ps1 -Ttl 600000

# 4) 让 mod 托管它（推荐）：设置 → External publisher command 填
#    pwsh -NoProfile -File C:\...\_netease-bridge.ps1
#    只有某行真的引用了 {ext.*} 且 External data 打开时才会启动，卸载时被作业对象收走
```

常用参数：

| 参数 | 作用 |
| :-- | :-- |
| `-TtlMs` | 值的寿命，默认 600000（10 分钟）。歌在播就给长一点，别让行里闪 `--` |
| `-Event` | 换歌时发一条 `EVENT`，让灵动岛弹一次 |
| `-EventTtlMs` | **卡片**在屏时长，默认 6000 ms。⚠️ 不要用 `-TtlMs`：那是**值的**寿命，把它当卡片时长用，一张卡就占住岛十分钟 |
| `-EventMinIntervalMs` | 两条岛事件之间的最小间隔，默认 4000 ms；被节流掉的那条下一拍还有机会发 |
| `-LyricSource` | `auto`（默认）/ `store`（只读客户端页面，需要 CDP 端口）/ `lrc`（只读歌词接口） |
| `-PositionSource` | `auto`（默认）/ `memory` / `media` / `cdp` / `clock` |
| `-LeadMs` | **负延迟**：换行提早多少毫秒。`-1`（默认）按来源自动，`0` 关掉 |
| `-ProcessName` | 进程名匹配，默认 `cloudmusic` |
| `-SongId` | 手工指定歌曲 id，跳过搜索 |
| `-ProbePosition` / `-ProbeCdp` | 只探测并打印结论，不发布 |
| `-DryRun` / `-Once` / `-SelfTest` | 不发送 / 只跑一次 / 离线自检 |

## 需要什么

* **pwsh 7**（脚本用了 `Add-Type` 的委托回调；Windows PowerShell 5.1 只用于位置助手）。
* **网易云 PC 版在运行**，且它的某个**顶层窗口**标题里有「歌名 - 歌手」。
* 想要歌词行精确到亚秒 / 跟随拖动进度条：给它加 `--remote-debugging-port=9229`
  （脚本的 `-ProbeCdp` 会打印改快捷方式的步骤）。没有调试端口时退回"接口 LRC + 推算时钟"，
  歌词仍会走，但位置会漂。
* 可选：Windows 媒体会话（SMTC）。有它时位置是**真**位置。用 `-ProbePosition` 看有没有。

## 输出与判读

* 自检最后一行 `SELFTEST PASSED`（46 项）。任何一项 FAIL 都是真问题，不是环境噪声。
* 启动时会说明位置来源：`位置：媒体会话（真实位置…）` / `…退回推算时钟（拖动进度条后会失准…）`。
  ⚠️ 这句**只说明 SMTC 这一条**；整条链到底用了哪个来源，看 `pos` key，不要看这句话。
* 面板上写 `{ext.netease.pos}` 就能看到 `dom 90.0s +1000ms` 这类的值：
  `dom` = 迷你条 DOM 的真 currentTime（**跟随拖动与暂停**）、`storeline` = store 当前行的起始
  时间（按行跳变）、`mem` / `cdp` = 内存差分 / CDP 其它位置、`media` = SMTC、
  **`clock` = 推算时钟（已经开始漂）**。

## 坑（都踩过）

* **不要把"值的寿命"当"卡片时长"发。** 协议里 `EVENT` 的 ttl 是**卡片在屏多久**；`SET` 的 ttl
  才是值活多久。两者默认差了三个数量级（6 秒 vs 10 分钟），而早先这里两处都发 `$TtlMs`——
  于是每张卡片占住灵动岛十分钟，队列只有 8 格，两张卡就能把它堵住几十分钟，**歌词密集的歌
  看起来就像脚本坏了**。现在事件走 `-EventTtlMs`（默认 6 秒）✓；mod 侧同时把卡片时长封顶
  到 60 秒、并会丢弃等太久的旧事件（10 秒 / critical 40 秒）作为兜底 ✓。
* **歌词密的时候不要每条都弹。** 卡片展示一张要几秒，事件来得比这快只会积压。`-EventMinIntervalMs`
  （默认 4000）是桥这一侧的节流闸门；被它挡下的那条不算"已播报"，所以下一拍还会重试 ✓。

* **曲名必须枚举顶层窗口，不能看 `Process.MainWindowTitle`。** 真机实测：开着「桌面歌词」时
  `cloudmusic` 的 `MainWindowTitle` **就是「桌面歌词」**，带歌名的窗口是同一个 pid 的另一个
  顶层窗口。按主窗口标题取 → 永远没有「 - 」→ 桥一直停在"没有可用窗口"的等待循环里、
  **从不连接管道**，面板两行永远 `--`。现在按窗口所属 pid 反查进程名，再挑标题里带「 - 」的。
* **进程列表在受限环境里可能只给一部分**（实测：沙箱里 `Get-Process` / `tasklist` / Toolhelp
  都只看到 13 个进程），所以判定走"窗口 → pid → 按 pid 查进程名"，那条路是可靠的。
* **播放器自己的"当前行号"（`playingLyricLineNumber`）会冻结**（实测 82 行歌词它一直报第 10 行）。
  所以当前行是**用位置在行时间戳上算出来的**，直接读只作为过渡一帧。
* **位置有两个系统性滞后**，这就是"歌词比声音晚约 1 秒"的来源：迷你条只给「分:秒」
  （向下取整到秒，平均落后 0.5 s）+ 桥每 `-IntervalMs`（默认 1000 ms）才轮询一次（再落后半个
  周期）。`-LeadMs` 默认按来源补掉这两项（`dom` 时合计 1000 ms），觉得抢拍就 `-LeadMs 500`，
  觉得还晚就 `-LeadMs 1500`，想回到旧行为就 `-LeadMs 0`。
* **连不上管道不是错误**：mod 没跑、或设置里不需要外部数据时，`Connect-PipeClient` 会重试到
  **60 秒**（`-WaitSeconds`）再干净退出（早先这里是裸 `Connect(3000)`，瞬时拒绝会变成未处理异常
  把脚本打死，mod 看门狗再把它拉起来，重启预算就这么烧掉了）。退出后由 mod 的重启策略接管：
  **连续**最多 5 次，跑满一分钟就把预算还回来 ✓。所以 mod 日志里那句
  `EXT publisher: gave up after 5 restarts` 才是真的放弃——它同时说明"跑满 60 s 会恢复"。
* **脚本必须带 UTF-8 BOM**：会被 Windows PowerShell 5.1 执行的部分（位置助手）不带 BOM 时
  按 GBK 读，中文会乱码甚至破坏引号。桥自身用 `pwsh` 跑，但改文件时别把 BOM 弄丢。
* 在线歌词**搜索**接口偶发 `The SSL connection could not be established`；歌词仍能从客户端自己的
  store 取到，所以那只影响"store 里没有歌词"的兜底路径。
