# 网易云歌词来源：黑胶页（CDP `li.line.current`）

**这份文档回答一个问题：桥的"当前歌词行"和"播放位置"到底从哪来，以及为什么只有这一条来源。**
读它的时候请同时读 [_netease-bridge.md](../_netease-bridge.md)（那个脚本怎么跑）与
[state-and-handover.md](state-and-handover.md) 的「未验证」一节（哪些是在真机上验证过的）。

写这份文档的直接原因是一个**从第一天就存在、用户报了很久**的缺陷：**把网易云的前台窗口
关掉（播放转到托盘）之后，歌词和曲名一起失效**。下面第一节是它的机制，第二节是现场证据，
第三节是最终的数据来源与选择器纪律，第四节是判据（怎么知道读到的东西还能用），
第五节是**删掉的东西**与"为什么要删而不是留着当降级链"，第六节是还没验证的部分。

---

## 1. 机制：缺陷不在 CDP，在"曲名只从窗口标题里读"

改前的数据流是这样的（`_netease-bridge.ps1`，改前版本）：

```
主循环  while ($true) {
          $now = Get-PlayingTrack        # ← 唯一入口
          if ($now) { …位置、歌词、发布… } # ← 整个发布块都在这个 if 里面
        }
```

而 `Get-PlayingTrack` → `Select-SongTitle (Get-NeteaseWindowTitles)`，也就是**枚举该进程
所有 `IsWindowVisible=1` 且有标题的顶层窗口，挑标题里带「 - 」的那个**。
托盘模式下**一个这样的窗口都没有**（真机实测，见 §2），于是：

* `Get-PlayingTrack` 返回 `$null`；
* `if ($now) { … }` 整段被跳过 → **`title`、`lyric`、`translation`、`pos` 四个 key 全都不发**；
* 面板上就是"歌词和曲名一起没了"。**这与 CDP 端口死没死完全无关** —— 用户手工验证过
  9229 一直活着、页面里的播放时间和当前歌词行也一直在更新。

所以这条缺陷的本质是**入口条件的形状错了**：一个"必须前台有窗口"的前置条件，
被放在了整条发布路径的最前面。它的失效不是降级，而是**整块静默**。

**改法**：入口不再依赖任何窗口。曲名/歌手/当前行/位置全部来自页面的 DOM（§3），
窗口标题只保留为**最后兜底**（没开调试端口的机器上，歌手只有那里有），并且当它空着时
（托盘模式的常态）**不再被当成错误**。

---

## 2. 现场证据（2026-10-15，本机）

### 2.1 `/json/list` 逐条（原文照抄）

```
$ Invoke-RestMethod http://127.0.0.1:9229/json/list
id                       : FFF09C63E1DDDD7AA40E784BED5BA8C1
type                     : page
title                    : 网易云音乐
url                      : orpheus://orpheus/pub/app.html
webSocketDebuggerUrl     : ws://127.0.0.1:9229/devtools/page/FFF09C63E1DDDD7AA40E784BED5BA8C1
devtoolsFrontendUrl      : /devtools/inspector.html?ws=127.0.0.1:9229/devtools/page/FFF09C63E1DDDD7AA40E784BED5BA8C1
```

`/json/version` → `Chrome/91.0.4472.169`（客户端自带的 Electron）。
`Invoke-CdpProbe` 报的另外两个端口（9223、9222）无响应，与预期一致。

### 2.2 那个状态下**窗口**是什么样（这就是缺陷的触发条件）

同一时刻枚举所有有标题的顶层窗口（`EnumWindows` + `GetWindowText` + `IsWindowVisible`），
属于 cloudmusic 进程（真机 pid 7220）的有 4 个，**全部 `IsWindowVisible=0`**：

```
hwnd      pid   vis    title
5246226   7220  False  桌面歌词
 724262   7220  False  铁花飞 - Mili/塞壬唱片-MSR
 461876   7220  False  铁花飞 - Mili/塞壬唱片-MSR
1837826   7220  False  迷你播放器
```

`Get-NeteaseWindowTitles` 返回 0 条 → 改前的 `Get-PlayingTrack` 必返回 `$null`。
**这就是用户报的"关掉窗口之后歌词和曲名一起没了"**，无需再猜。

### 2.3 同一时刻**页面**里有什么（DOM 摘录）

```html
<li class="line current" data-time="189.74" _nk="iArvpjX41">
  <p class="cmd-typography … TypographyWrapper_tca8bac" _nk="vpjX51">没有谁能迫我退悔</p>
  <p class="cmd-typography … TypographyWrapper_tca8bac" _nk="vpjX52"></p>
</li>
```

```html
<span class="cmd-typography title cmd-typography-primary cmd-typography-normal cmd-typography-userselect TypographyWrapper_tca8bac"
      title="铁花飞" _nk="MeGN63"><div class="label" _nk="MeGN54"></div>铁花飞<span class="label" _nk="MeGN71"></span></span>
```

歌手那一行（`li.info.artist`，**值在内层 span 的 `title` 属性上**）：

```html
<li class="info artist" data-testid="tid_tracktitle_artist" _nk="MeGN93" style="">
  歌手：<span title="Mili / 塞壬唱片-MSR" _nk="MeGN73">…<span class="link">Mili</span><span> / </span><span class="link">塞壬唱片-MSR</span></span>
</li>
```

原文与翻译**都在同一个 `li` 里**：第一个 `<p>` 是原文、第二个是翻译。逐行核对过
（64 行全部一致）：第二个 `<p>` 的 `textContent` 与 store 的 `tlyricLines[i].lyric`
**64/64 逐字相等**；`li[].data-time` 与 `lyricLines[i].time` **64/64 相等**（`agree=64,
disagree=[]`）。所以这不是"看起来像"，是同一个数组的两种呈现。

### 2.4 值确实在往前走（同一次会话，每 1.5 s 一次，共 6 拍）

```
i  at           curExists curTime  原文 | 翻译                      title(属性/文本)
1  01:04:01.492 True      195.92   四面楚歌，这颗心无惧无畏 |      铁花飞/铁花飞
2  01:04:03.019 True      202.91   铁花飞，飘逸不残灰 |            铁花飞/铁花飞
3  01:04:04.543 True      202.91   铁花飞，飘逸不残灰 |            铁花飞/铁花飞   ← 暂停
4  01:04:06.049 True      202.91   铁花飞，飘逸不残灰 |            铁花飞/铁花飞
5  01:04:07.563 True      202.91   铁花飞，飘逸不残灰 |            铁花飞/铁花飞
6  01:04:09.072 True      202.91   铁花飞，飘逸不残灰 |            铁花飞/铁花飞
```

第二次采样（播放恢复后）：`226.99 → 226.99 → 230.29 → 230.29 → 233.36 → 234.96`，
原文/翻译每一拍都在变（`就算能从过去找出差错 / 身世境遇不由人选择 / 有理解就足够 /
盼日夜细水长流`）。**同一台机器上同时**：

* `store.playingLyricLineNumber = 10`（**从 148 s 到 235 s 一直是 10** —— 这个数会冻结，
  与 [state-and-handover.md](state-and-handover.md) 2026-10-14 的两次实测一致）；
* `li.line.current` 的 `data-time` 从 148.14 走到 234.96。

这一条对比就是"为什么当前行不能从 store 的行号来、只能从 DOM 来"的直接证据。

### 2.5 保存下来的原始证据

`.tool/investigations/cdp-vinyl-evidence.txt`（整份探针输出，含 `/json/list`、
vinyl 根元素 outerHTML、`li.line.current` outerHTML、三条标题候选各自的
`title` 属性/文本/outerHTML、`audio,video` 数量、mm:ss 文本节点、store 的标量）与
`.tool/investigations/_live_*.ps1`（采样的脚本）。这一目录在 `.gitignore` 里
（机器相关、一次性的原始转储），**结论**写在本文里。

---

## 3. 数据来源与选择器纪律

### 3.1 一条来源，四个字段

| 字段 | 来源 | 真机取值 |
| :-- | :-- | :-- |
| 当前位置 | `li.line.current` 的 `data-time`（秒，十分之一秒） | `148.14` |
| 当前行原文 | 该元素的**第一个** `<p>` 的 `textContent` | `I know we're both right` |
| 当前行翻译 | 该元素的**第二个** `<p>` 的 `textContent`（可为空） | `我明白 我们都没错` |
| 曲名 | `span.title` 的 **`title` 属性** | `铁花飞` |
| 歌手 | `li.info.artist` 内层 `span[title]` 的 **`title` 属性** | `Mili / 塞壬唱片-MSR` |

位置与当前行是**同一次读**（在同一个元素上），所以两者天然自洽 —— 不需要"用位置去选行"
这种推算，也就不存在"位置漂一点、行选错一行"的形状。

### 3.2 候选表（顺序就是优先级）

**绝不锚带哈希的类名。** 客户端是打包过的 Electron 应用，类名里带构建哈希：
`ClassicPageContainer_c1h09pwc`、`VinylPageLayoutContainer_vwagppq`、
`TrackTitleContainer_t1s72en4`、`TypographyWrapper_tca8bac`、`IconStyle_ic272p3` ……
它们在**下一次客户端升级**时就会变。所以候选表按"稳定性"排序：

| 稳定性 | 依据 | 例 |
| :-- | :-- | :-- |
| ① 最稳 | **页面作者刻意留的稳定 id** | `VINYL_COVER_ELEMENT_ID`（名字就说明它是锚） |
| ② 稳 | `data-testid`（测试契约，改它等于改测试） | `tid_tracktitle_container` / `tid_tracktitle_artist` |
| ③ 稳 | **语义**类名与属性 | `li.line.current`、`span.title`、`data-time`、`title="…"` |
| ✗ 不稳 | 构建哈希类名 | `TrackTitleContainer_t1s72en4` |

实际候选表（`_netease-bridge.ps1` 的 `Get-Vinyl*Candidates`，`-SelfTest` 断言**顺序**）：

```
root   : #VINYL_COVER_ELEMENT_ID
       | [id=VINYL_COVER_ELEMENT_ID]
       | div.display

line   : #VINYL_COVER_ELEMENT_ID li.line.current
       | li.line.current                                       ← 不依赖 id 的第一条退路
       | li[class~="line"][class~="current"]                    ← 类名里多一个类也不怕
       | li[data-time].current

title  : [data-testid=tid_tracktitle_container] span.title
       | #VINYL_COVER_ELEMENT_ID span.title
       | span.cmd-typography.title                              ← 只有语义 token，无哈希

artist : [data-testid=tid_tracktitle_artist] span[title]
       | [data-testid=tid_tracktitle_artist]
       | #VINYL_COVER_ELEMENT_ID li.info.artist span[title]
```

**真机命中（`-ProbeVinyl` 输出，就是"报告哪一条候选命中"的落点）**：

```
root   → #VINYL_COVER_ELEMENT_ID
line   → #VINYL_COVER_ELEMENT_ID li.line.current
title  → [data-testid=tid_tracktitle_container] span.title
artist → [data-testid=tid_tracktitle_artist] span[title]
```

四条全部落在 ①/② 档。**为什么其余候选更不稳**：`li.line.current`（第 2 条）没有 id 约束，
页面上**别的列表**（搜索结果、播放列表）也可能有 `.line.current`，只是当前构建里没有 ——
它作为退路留着，但排在 id 限定之后；`span.cmd-typography.title`（标题第 3 条）会命中
侧栏的 `<span class="cmd-typography title">每日推荐</span>`（真机实测**第一遍就命中了**），
所以它必须在 `[data-testid=…]` 与 `#VINYL_COVER_ELEMENT_ID` **之后**；`div.display` 同理。

### 3.3 取值：属性优先，文本兜底

* **标题**：先取 `title` 属性（客户端自己写的干净值）；属性空/空白才回退到
  `textContent`。该 `span` 里嵌着两个 `<div class="label">`，它们的文本**当前是空的**
  （所以本机取到的文本恰好也干净），但这只描述现在；属性不依赖子节点长什么样。
  `-SelfTest` 有两条断言分别钉住"属性命中"与"属性空 → 文本兜底"，还有一条钉住
  "两个都空 → `$null`（宁可不发，也不发一个空标题）"。
* **歌手**：必须取**内层 span 的 `title` 属性** —— `li` 的 `textContent` 会带上
  「歌手：」前缀（而且那个前缀是本地化文案，会变）。没有属性时才回退到文本并**剥掉
  「xxx：」前缀**。
* **段切分**：第一个 `<p>` = 原文，第二个 = 翻译（§2.3 逐行核对过）。只有一个 `<p>` 时
  它就是原文、翻译为空（形状防御）。"首段空、次段非空"时次段当原文（真机没见过，
  但宁可把这一行当原文，也不要两个字段都空着丢一行歌词）。

### 3.4 曲名身份：一个键，三条来源

曲名既是**发出去的值**，也是**换歌检测与缓存失效的键**。改前它只有一个来源（窗口标题），
这是缺陷的根；现在按优先级取：

1. **黑胶页** `span.title` 的 `title` 属性（托盘模式走的就是这条）；
2. **store** `playing.resourceName` + `resourceArtists[].name` 用 `' / '` 拼；
3. **顶层窗口标题**（`Select-SongTitle`，只在完全没有调试端口时才可能命中）。

②的歌手列真机是 `['Mili','塞壬唱片-MSR']` → `Mili / 塞壬唱片-MSR`，与①的属性值**逐字
一致**，也与窗口标题里的歌手一致（后者只差一个空格）—— 所以换来源**不会**让面板上的
`title` 抖。三条都空时返回 `$null`：主循环**响亮地说读不到并置空**，不发任何值。

---

## 4. 判据：怎么知道读到的东西还能用

### 4.1 `Test-VinylLineUsable`（每拍都跑，纯函数，`-SelfTest` 覆盖）

这一条来源只有一级，所以"它失效"没有下级可以接手 —— 必须**当场判出来**。判据五条：

| # | 判据 | 为什么 |
| :-- | :-- | :-- |
| ① | 没有当前行（候选全落空 / 页面没挂载）→ 不可用 | 调用方在别处判（`$v.Line -eq $null`），这里看 `time` |
| ② | `data-time` 解不出（`< 0`）→ 不可用 | **0 s 是真值**，所以"不知道"必须用 −1 表示 |
| ③ | 往回走超过 `BackToleranceMs`（1 s）→ 不可用 | 真机：切歌当拍读到过 `data-time="1"`，而位置还在上一首的 240 s 上；用它当位置会把面板钉在新歌第 0 行 |
| ④ | 往前跳超过 `ForwardToleranceMs`（**30 s**）→ 不可用 | 挡的是"几百秒的跳变 = 读到了上一首/上一轮的陈旧节点"，不是"这句唱得久" |
| ⑤ | **连续 `RepeatLimit`（3）拍读到完全相同的数** → 不可用 | **这一条才是"冻结"的真正形状** |

关于④的阈值 —— 它是**实测定的**，不是拍的：这一首的相邻句间隔是 **2.6 s ~ 26.7 s**
（音乐段落之间有长间奏，实测 `148.14 → 150.2` 是 2 s，而 `195.92 → 226.99` 是 31 s、
`222.2 → 195.5` 之间有过 26.7 s 的一跳）。第一版阈值取了 5 s（"看起来谨慎"），
结果**真机序列里那一跳被自己的判据判死了** —— `-SelfTest` 的真机采样序列正是为此存在：
它当场变红。30 s 是"比这一首最长的间奏再宽一点"。

关于⑤ —— **"两拍相同"本身是正常的**（暂停时位置与行一起不动，那是自洽的），所以不判它；
判的是"一直同一个数"。真机反例：选择器命中了别处一个恒为 29.5 s 的 `data-time` 节点，
那时每一拍都"可用"，界面上就是**永远停在那一句** —— 这正是本项目两次真实缺陷的同一个形状
（`playingLyricLineNumber` 冻结在 10；`storeline` 把会前进的来源全遮住）。
连续相同达到上限时桥**不再发新的位置/歌词**（面板留 `--` 或停在上一句），
而不是把那个不动的数当成"新的位置"发出去。

### 4.2 `Test-StoreLyricUsable`（只在兜底路径上跑）

黑胶页那一拍没给出当前行时，用 store 的歌词数组 + 这个位置选一行（见 §5.2）。
准入判据只有两条硬事实：**store 报的曲目 id 必须与这一轮的身份一致**，且**行号在范围内**。
理由是真机上真实发生过的形状：`playing` 切片与 `async:lyric` 切片各自更新，切歌当拍
`playing` 已经是新歌、歌词数组还是旧的 —— 用它就会把上一句的翻译贴在这一句上。

### 4.3 响亮失败（不许静默）

读不到时打一行**有判别性数值**的诊断（本项目 §9「报告与调试」的规矩），
分别点名四件事的哪一件断了：

```
[HH:mm:ss] 读不到曲目（第 N 次）：没有可用的 CDP 目标：端口 9229/9223/9222；
           根候选='(miss)' 行候选='-' 标题候选='-'
[HH:mm:ss] data-time 不可用：prev=240000ms now=1000ms 连续相同=0（往回走/往前跳太多/一直不动 → 判陈旧）
```

`-ProbeVinyl` 每轮把"端口 / 目标 / 四条候选各自的命中 / `data-time` / 守卫结论 / 行数 /
标题与歌手 / 原文 | 翻译"全部打出来，一次运行就能回答"歌词为什么没了"。

---

## 5. 删掉的东西，以及为什么是删而不是留着当降级链

### 5.1 删除清单（都在这一个提交里）

| 删掉的 | 它原来干什么 | 为什么可以删 |
| :-- | :-- | :-- |
| `_netease-position.ps1` + `.md` | 起一个 5.1 助手进程读 **SMTC 媒体会话**的位置，写采样文件 | 本机 SMTC **实测不可用**（`PROBE none (no active media session)`）；且黑胶页的 `data-time` 比它准 |
| `_netease-lyric-scan.ps1` + `.md` | 扫**客户端内存**找 int32 毫秒的位置（两次采样差分） | 位置由客户端自己给了；内存地址随 ASLR 变、需要 PROCESS_VM_READ、被安全软件拦下就没用 |
| `-PositionSource`（`auto/memory/cdp/media/clock`） | 选位置来源 | 只剩一条来源，参数没有意义 |
| `-LyricSource`（`auto/store/lrc`） | 选歌词来源 | 同上 |
| `-ProbePosition` / `-PositionHelper` / `-PositionStaleMs` / `-MemoryGapSeconds` | 位置助手与 SMTC 的配套开关 | 助手脚本已删 |
| `-ScannerPath` / `-SongId` / `-DumpJson` / `-LyricRetryMs` | 内存扫描器与**歌词接口**的配套开关 | 两者都已删 |
| `Get-MemoryPosition` `Enable-MemReader` `Find-PosAddr` `Get-PosValue` `Read-PosCache` `Write-PosCache` `Test-PosAfterTrackChange` `Test-PosAddrAlive` `Get-NeteaseProcInfo` `ConvertFrom-ScanCandidate` `Test-Playing` | 内存位置那一整条 | 同上 |
| `ConvertFrom-PositionLine` `Test-PositionFresh` `Get-PositionMs` `Start-PositionHelper` `Stop-PositionHelper` `Get-PowerShell51` `Get-PositionSample` | SMTC 与推算时钟 | 同上 |
| `Get-CdpDomTime` | 读迷你条 `#page_pc_mini_bar` 的「分:秒 / 分:秒」 | 它是**向下取整到秒**的（平均落后 0.5 s），而 `data-time` 是十分之一秒 |
| `Get-CdpPositionMs` | 读页面里 `audio/video` 的 `currentTime` | 真机实测这个页面**没有 audio/video 元素**，它从未取到过值 |
| `Get-CdpStoreLyricLine` 的调用方（原"按位置选行"主路径） | 用 store 时间表 + 位置算当前行 | 客户端自己就给了当前行；但函数本身留着当兜底（§5.2） |
| `Test-StoreLineUsable` `Get-StoreTimesVerdict` `Test-StoreTimesStale` | store 行时间表的新鲜度判定 | 那套判定的存在理由是"store 的行时间会冻结"；现在当前行不来自它，判定就没有对象了 |
| **公开歌词接口那一整条**：`Get-SongId` `Get-LyricLines` `ConvertFrom-Lrc` `Select-LrcLine` `Write-Diag` `Get-Brief` + `$script:Headers` + `$script:LyricCache` | 用"歌名+歌手"查 id、取 LRC、按推算时钟选行 | 它是"客户端 store 里没有歌词"时的兜底；而 store 与黑胶页都在本地页面里，接口那条路既慢又会撞风控（`code=-460` / `abroad=true` / SSL 失败），且**没有位置就不能选行**（位置现在只来自黑胶页） |

`_netease-lyric-probe.ps1`（**保留**）：它是"歌词从哪来"的探路工具（只读、跑一次就退），
现在仍可用来核对这些字段在客户端升级后有没有变。

### 5.2 为什么留了 store 这条兜底

一条来源有一个必然的脆弱点：**选择器**。客户端升级后 `li.line.current` 改了名字，
黑胶页就整条读不到。那时客户端自己的歌词数组（`async:lyric.lyricLines`/`tlyricLines`）
**还在**，用 `data-time`（同一个位置）在它上面选一行，比"什么都不发"有信息量；
更重要的是它**不会引入冻结值** —— 它选出来的行与位置出自同一次读，是自洽的。
准入判据是 §4.2 的 `Test-StoreLyricUsable`（曲目 id 必须一致），并且这条路径一旦启用
会打一行 `（黑胶页没给当前行，用 store 兜底：id=… index=…）`。

**它不参与"位置"**：位置只来自 `data-time`。这正是把它与已删除的 `storeline` 区分开的地方 ——
`storeline` 当年是拿"冻结行号的起始时间"当**位置**用，于是把后面会前进的来源全遮住了。

### 5.3 不变的东西：管道协议

发布的 key **一个都没变**：`title`、`lyric`、`translation`、`pos`（顺序由
`Get-PublishPlan` 的有序字典保证，同一轮同一个 key 只出现一次）。产品侧读的
`{ext.netease.*}` 因此不需要任何改动（`.cpp` 这一轮没碰）。
`-SelfTest` 里"整轮恰好 title/lyric/translation/pos 四个 key"那条断言就是守着这件事的。

---

## 6. 还没验证的部分（真机上要做的确认）

**已在本机确认**（2026-10-15）：

* 端口 9229 活着，`/json/list` 只有一个 page 目标（§2.1）；
* **托盘模式**（4 个 cloudmusic 窗口全部 `IsWindowVisible=0`，§2.2）下，
  `li.line.current` 的原文/翻译与 `data-time` 都在、并且在往前走（§2.4）；
* `-ProbeVinyl` 四条候选全部命中 ①/② 档；`-DryRun` 打出四个 key 的正确值；
* `-SelfTest` 全绿。

**没验证 / 要你在真机上看的**：

1. **"关窗口"这个动作本身**。本机观察到的托盘状态是**已经处在**那个状态的快照，
   我没有真正执行"点 X 关掉前台窗口"再观察。要确认的就是这一条：
   关掉窗口后跑一次 `-ProbeVinyl`，`guard` 列应当继续出现 `ok` 且 `time` 在涨。
   ```powershell
   pwsh -NoProfile -ExecutionPolicy Bypass -File _netease-bridge.ps1 -ProbeVinyl -ProbeRounds 10
   ```
2. **播放停止 / 退出客户端时**页面还在不在（`data-time` 会不动 → 连读 3 拍后
   报 `data-time 不可用`，这是**预期**行为，不是缺陷；要确认的是它不会报成"换了首歌"
   或留下一句错的歌词）。
3. **迷你播放器 / 桌面歌词模式下**（用户点「迷你播放器」时黑胶页会不会被卸载）。
   真机实测过页面里同时有 `桌面歌词` 和 `迷你播放器` 窗口时字段仍在，但**没有**在
   迷你模式下单独采样过。
4. **客户端升级后**候选是否需要调整（`-ProbeVinyl` 会直接说哪条命中、哪条落空）。

`.tool/investigations/_live_advance.ps1` 与 `_live_shapes.ps1` 就是上面第 1、3 条用过的
采样脚本（连跑 6 拍，逐拍打印 `data-time`、原文/翻译、`li.line.current` 是否存在、
`document.visibilityState`、store 的行号），要复核时直接改一下跑。
