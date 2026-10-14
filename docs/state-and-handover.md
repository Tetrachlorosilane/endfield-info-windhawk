# 验证状态与交接说明

> 本文件从 `AGENTS.md` 拆出：AGENTS 受工作区**指令预算 64 KB** 限制，超长会被**静默截断**，
> 而这一节的体量最大、又是**只读参考**（不需要每次改代码都读）。改代码前仍以 AGENTS 的
> §0–§9 为准；想知道"什么已验证 / 什么还没验 / 这次踩了什么坑"再翻这里。

## 10. 验证状态

**已验证**：

- 用 Windhawk 自带工具链编译链接通过，`-Wall -Wextra` 下只剩 §3 记的两个已知警告
  （头顺序 + `ApplyPanelTransform` 未使用）。
- 7 个导出入口齐全。
- 依赖仅系统 DLL：`USER32 / GDI32 / PDH / IPHLPAPI / PSAPI / ADVAPI32 / msvcrt / KERNEL32`。
- 回归测试全绿（340 条断言，含一段**真启动 GDI+** 的排版断言：
  文字不折行、且在预算内）。
- GDI+ 缩放管线用**已知内容**的图验证过（含透明底、纯 RGB、索引色、灰度+alpha、
  16 位、隔行 PNG）。
- 外部数据（M1）：编译只剩上面那两条已知警告；回归 **340 条断言**全绿（M1、M2、M3 与 CPU 频率共新增 180 条）；
  并做了**负对照**——把展开改成递归后，注入断言确实变红（不是"看着像在守"）。
- 设置块 46 项 / 10 个选项组，`_settings.py` 报 `no structural problems found`。
- **mod 的日志格式串**：`-Wformat` **从来没有检查过它们**（2026-10-14 更正）✗。
  宿主打的 `Wh_Log` 桩是自造变参函数，没有 format 属性，clang 不会检查；实测同一文件里
  `printf` 的两处错型各报 1 条、自造桩 0 条。改用 `python _logfmt.py` 审计：105 个调用点、
  0 处元数/错型问题（负对照实测会红）。详见 AGENTS §3 的方框。
- 外部数据的**服务端路径**（`ExtHandleLine`：HELLO → SET → 存储 → 查找、换行框架与边缘空格裁剪、
  允许名单拒绝）有断言覆盖（总数见上文，不再重复一个会过期的数字）。
- **真机整链验证（用户环境，2026-10-13）**：mod 的开机自检两帧被服务端完整收到
  （`self-test sent 2 frames` → `recv bytes=17/33` → `hello provider=selftest` → `set … key=ok`），
  且自检值在行里**显示了出来**——传输、存储、展开、绘制四段都成立；随后用
  `_extclient.ps1`（.NET）发布的值也**在面板上显示**。管道参数同时被记录：
  `flags=0xd`（服务端 + message 类型 + 拒绝远程）、`curInstances=4`（4 个实例都是本进程的，
  没有第二个进程抢同名管道）。
- **`_extclient.py` 尚未在真机上被观察到写入**：那个环境里它连上后同一毫秒即断开、零帧，
  而脚本自身的时序（写完两行后至少 1 秒才关闭）与之不符——怀疑当时跑的并非本仓库这一份。
  要用客户端验证时**优先用 `_extclient.ps1`**，或先确认 `python -c "import os;print(os.path.abspath('_extclient.py'))"`
  指向本仓库的文件。
- **`_netease-bridge.ps1`（示例发布器）**：**只有离线部分被验证过**——LRC 解析（元信息行、纯时间戳行、
  `.xx` 与 `.xxx` 两种小数、一行多标签）与按位置选行，9 条自检全绿；参数解析干净；
  没有网易云窗口时给出可诊断的退出并列出所有有标题的进程。**未验证**：搜索/歌词两个网络接口的
  返回形状（本机无法访问）、窗口标题在真实客户端上的格式、以及"按看到它的时刻推算歌词行"这个
  时钟的漂移（暂停时会继续走，这是设计上已知的取舍，要精确同步得上 SMTC）。
- **`_netease-position.ps1`（位置助手，SMTC）**：与主脚本**分开成两个进程是有原因的**——SMTC 是
  WinRT，而 WinRT 投影只在 **Windows PowerShell 5.1** 里可用（pwsh 7 读不到）。所以位置由 5.1 宿主
  产生、写成一个每秒刷新的采样文件，主脚本读最后一行。**实测（本机）**：5.1 下 `-ProbePosition`
  返回 `PROBE none (no active media session)`（这台机器没有媒体会话 = 有效的负结果），
  pwsh 7 下返回 `PROBE unsupported-host`（正是分进程的理由）。**离线可断言的部分全绿**（17 项自检）：
  采样行解析（正常 / 暂停 / 无会话 / 垃圾 / 空）、**暂停不推进**、播放时按采样年龄外推、
  采样过旧视为不可用。**未验证**：真实机器上有会话时位置是否跟得上拖动与暂停——需要用户跑
  `-ProbePosition` 看有没有会话，再拖动进度条实测歌词是否立刻跟上。
- **`_netease-lyric-probe.ps1` / `_netease-lyric-scan.ps1`（歌词来源探路，均只读）**：真机探针结论——
  桌面歌词是 `class=DesktopLyrics` / `title=桌面歌词` 的**分层窗口、零子控件**（可见时也一样），
  即**正文由客户端自绘，正规 API 读不到** ✗，只剩读内存一条路。扫描脚本因此**不抄博客的硬编码偏移**
  （那是别人客户端版本的地址；第二篇博客就是第一篇两年后失效的实证 ✗），改成**特征扫描**：
  只扫 `MEM_PRIVATE` 可读提交区、找含 CJK 的 UTF-16LE 串，**隔几秒重采**，"文本变了"的那条即当前歌词行。
  **未验证**：`OpenProcess` 在本机受限沙箱里返回**错误码 5**（沙箱拒绝访问其他进程，同 §3 的管道边界），
  所以**只能在用户普通终端里跑**；若那里也报 5，说明客户端权限更高或有安全软件拦截，这条线就停。
- **一条踩过的坑**：桌面歌词**不是进程主窗口**，`Get-Process` 的 `MainWindowTitle` **看不到它**
  （实测：窗口存在但按进程名查不到），必须 `EnumWindows` 按标题找；扫描脚本的自动找 pid 因此失效过，
  需要 `-TargetPid`（pid 可从探针输出里读）。另外 **5.1 会把无 BOM 的 UTF-8 脚本按 GBK 读** ✗
  （中文乱码、甚至破坏引号）——所有会被 5.1 执行的 .ps1 必须带 **UTF-8 BOM**。
- **网易云「播放位置」可以从内存拿到（真机定论，2026-10-13；工具 `_netease-lyric-scan.ps1`）**：
  排查顺序本身就是结论——① 客户端**文本全程 UTF-8**（进程里 UTF-16 中文 **0 条**，UTF-8 中文约 4554 条）；
  ② **"当前歌词行"不是一条会变化的字符串**：整首歌词常驻，换行时文本一个字节都不变 ✗
  → **"文本差分"这条判据从原理上找不出当前行**（我一开始走错了这条路，记此以免重走）；
  ③ **播放位置是 int32 毫秒，可用差分扫描定位**：两次采样间隔 15 s，保留"值恰好增加 ≈15 s"的地址
  （判据极强：几百万个整数里只剩个位数；命中值与界面显示的毫秒吻合）。用观察模式逐地址验三条性质：

  | 地址 | 播放 | 暂停 | 切歌 | 结论 |
  | :-- | :-- | :-- | :-- | :-- |
  | `0x80014BFB08` | ✓ | ✓ | ✓ | **权威位置** |
  | `0x8001E3CA38` / `0x8001E3CB38` | ✓ | ✗ | ✓ | 只在播放时累加（暂停即停） |
  | `0x800C31EF10` | ✓ | ✓ | ✗ | 按歌曲缓存（切歌不刷新） |

  **地址是绝对的、受 ASLR 影响** → 客户端一重启就变 ✗ → 运行期只能
  **差分发现 + 缓存 + 自证 + 失败回退**，**绝不能写死地址**（这与两篇博客的"硬编码基址+偏移"是
  同一个脆弱点的两种程度）。另一个沙箱边界：`OpenProcess` / `CreateToolhelp32Snapshot` 在 DSH 受限
  沙箱里都返回**错误码 5** ✗，所以内存扫描**只能在普通终端里跑** ✓。
  扫描器的四种模式都是**只读**（`PROCESS_QUERY_INFORMATION | PROCESS_VM_READ`）：
  默认模块扫描 / `-WholeProcess` / `-FindClock`（差分找位置）/ `-WatchAddrs`（观察指定地址）。
- **内存位置的运行期策略（已实现于 `_netease-bridge.ps1`）**：实测**堆地址随客户端重启就失效**
  （重启后老地址不可访问 ✗，ASLR 生效 ✓；那些 `0x800…` 是堆地址，与 `0x7FF8…` 的映像区无关 ✗——
  "相对 DLL 基址的偏移"这个想法因此作废 ✓）。所以缓存以 **pid + 进程启动时间** 为键 ✓，实例一变即作废 ✓。
  **进程活着就绝不反复全局扫描** ✓（用户定的规矩）：每进程实例最多发现 **3 次** ✓、两次间隔 **≥60 s** ✓、
  **只在确实在播放时**做 ✓；**"值不再增长"不触发重扫** ✗，改用**切歌粗校验** ✓（曲名变了，位置应回落或
  明显跳变，否则判该地址失效 ✓）——失效只**禁用**内存来源、走降级链 ✓，重新发现推迟到客户端下次重启 ✓。
  稳态成本：每 0.5 s 读 4 字节 ✓。降级链 `-PositionSource auto`：**memory → media（SMTC）→ 推算时钟** ✓。
- **一条明确的设计约束：插件不启动第三方软件** ✓（用户要求）。CDP 只在客户端**已由用户**用带
  `--remote-debugging-port=9229` 的快捷方式启动时可用 ✓；插件负责**把改快捷方式的步骤讲清楚**
  （`Show-CdpShortcutGuidance` ✓，内存读取失败时自动打印 ✓）和 `-ProbeCdp` 探测 ✓，
  **不会**替用户启动或重启音乐软件 ✓（全文唯一的 `Start-Process` 是启动我们自己的 SMTC 位置助手 ✓）。
  **CDP 的实测与修正（2026-10-13，真机三次探测 + 精读本地 `cloudmusic-desktop-mcp` 的实现）**：
  端口 9229 确实是 CDP（`Chrome/91.0.4472.169` ✓）、`Runtime.evaluate` 可用 ✓ —— **但 DOM 路确实不通** ✗：
  页面里**没有 `audio/video` 元素** ✗、`m:ss` 文本全是**歌单每首的时长** ✗、`style` 里的宽度是列表行与
  缩略图（**没有进度条** ✗）、含中文的叶子节点是导航与歌单名（**没有歌词** ✗）。
  ⚠️ **但"store 不可达"是我下早了的结论 ✗**：我扫的是 `window` 上带 `getState` 的对象（结果为空 ✗），
  而 `cloudmusic-desktop-mcp/server.py:283-287` 的做法是**往 `webpackJsonp` 塞一个模块，把 webpack 的
  `require` 存到 window 上**，再 `require(12).a.getStore()` 拿 store ✓✓（`server.py:336-347` 读
  `s.playing.playingState/curPlaying`、`s.playingList.curPlayingList`、`s.setting.showLyric` ✓）。
  **教训**：探测要打在"实现真正使用的那条路上"，而不是想当然的 `window` 全局上 ✗；
  项目就在本地 `C:\Users\HP\source\gitclone\cloudmusic-desktop-mcp` ✓，**先读它再下结论** ✓。
  因此 `-ProbeCdp` 的表达式已改为**照同一条路**（自己的空闲模块号 987661 ✓、只列键与短值 ✓、全程 try/catch ✓），
  下一步据其输出固化"位置 + 当前歌词行" ✓。`Get-CdpPositionMs` 现有的通用锚点（`audio.currentTime`）
  在 DOM 路下取不到值 ✗ → 会安静返回空、继续降级 ✓（不误报 ✓）。自检 23 项 ✓。
- **store 路已打通（2026-10-13 真机实测，读过本地 `cloudmusic-desktop-mcp` 的实现之后）** ✓✓：
  ① **访问方式**：往 `webpackJsonp` 塞一个模块，把 webpack 的 `require` 存到 `window` ✓ → `getStore()` ✓。
  **不要调 `getState`** ✗ —— `server.py:339-346` 是拿到后**直接用 `s.playing` / `s.playingList` / `s.setting`** ✓；
  我多调了一次，报 "getState is not a function" ✗（这条弯路值得记 ✓）。
  ② **模块 id 随客户端版本变** ✗（这台是 **11** ✓，MCP 钉的是 **12** ✗，只对它测过的构建成立 ✗）→
  所以**运行期自寻址** ✓：扫 webpack 的**缓存** `req.c`（只含已实例化模块 ✓，`req(id)` 会执行初始化代码，
  所以**不调它** ✗）找导出（或其 `.a`/`.default`）里带 `getStore` 的那个 ✓，结果缓存在
  `window.__ei_store_id__` ✓，**客户端升级后自己重找** ✓；`-ProbeCdp` 走同一条自寻址路 ✓
  ——**这就是"规则化的探查"**：升级后重锚定就用它 ✓。
  ③ **当前行 = `async:lyric.lyricLines[playing.playingLyricLineNumber]`** ✓✓（真机实测：第 **10/64** 行，
  文本与界面显示一致 ✓，行对象形状 `{time: <秒>, lyric: "<文本>"}` ✓）→ **不需要位置、不需要时间推算** ✓，
  拖动进度条 / 暂停 / 切歌**按构造就是对的** ✓（那是客户端为自己界面维护的状态 ✓）。顺带可读
  `tlyricLines`（翻译 ✓，实测 64 行 ✓）、`yrcInfo`（逐字 ✓）、`offset`（歌词偏移 ✓）、
  `playingState`（播放状态 ✓）、`curTrack.artists/album` ✓。
  ④ **三种时间单位混在同一切片里，千万别混** ✗：`lyricLines[i].time` 是**秒** ✓（35.84）、
  `curTrack.duration` 是**毫秒** ✓（242613）、`playing.resourceDuration` 是**秒** ✓（242）。
  ⑤ **集成**：`-LyricSource auto|store|lrc` ✓，`auto` **首选 store** ✓（失败退回"LRC + 位置选行" ✓，
  无调试端口的机器行为不变 ✓）；位置仍走 `-PositionSource`（memory ✓ → media → clock）；
  `store` 只读 ✓ —— **只读状态、不派发任何 action** ✗（两个成熟项目里有写操作，我们不学 ✓）。
  ⑥ **仍未验证** ✗：客户端**升级之后** store 是否还能被自寻址到（要等下次升级实测 ✓）、
  以及 `store` 路在**暂停/拖动**时的实际表现（这次只验证了"与界面一致" ✓，尚无双性质对照 ✓）。
- **mod 侧「外部发布器」包装（M4 第一片，已实现）**：设置项 `External publisher command` 里写一条命令行，
  mod 就替你启动它 ✓。要点（每条都有理由）：
  · **只启动用户写的那条命令** ✓，**绝不启动/重启任何第三方应用**（含音乐客户端）✗——那属于改变用户
    软件的使用方式 ✓（用户明确要求）；
  · **与前缀同一个门**：只有"某行真的引用 `{ext.*}`"且"外部数据开关打开"才会启动 ✓（和建管道同一条
    惰性纪律：没东西要用就不开通路 ✓）；
  · **不套 `cmd /c`** ✓：命令行按 `CreateProcessW`（`lpApplicationName == nullptr`）的约定拆分
    （`ExtParsePublisherCommand` ✓ 纯函数、有 4 条断言 ✓：带引号可含空格 ✓、不带引号到第一个空格 ✓、
    其余逐字 ✓、空/纯空白 = 无 exe ✓）；
  · **`CREATE_NO_WINDOW`**（发布器不该闪黑框 ✓）+ **`CREATE_SUSPENDED`**（进作业对象之前不许跑 ✓）；
  · **Job Object + `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`** ✓：卸载 mod 时**整棵树**一起没 ✓
    （`TerminateProcess` 只能杀一个进程，管不住脚本自己拉起的孙进程 ✓）；
  · **有界重启** ≤5 次、间隔 5 s、放弃时只打一行日志 ✓（不刷屏、不无限重启 ✓）；
  · 设置每 **10 s** 重读一次 ✓：改了命令行会先收掉旧进程再起新的 ✓；清空即停 ✓；
  · 停机顺序：**发布器先于管道**（写者先于读者 ✓，`ExtStop` 第一句就是 `ExtPublisherStop()` ✓）。
  **未验证（需要真机）**：作业对象是否真的收掉了孙进程 ✓、`CREATE_NO_WINDOW` 是否确实不闪窗 ✓、
  重载 mod 时有无孤儿残留 ✓——这三条**没有断言能覆盖**，只能靠真机看进程列表 ✓。
- **看门狗：触发与信息收集分离（用户设计 2026-10-13，已实现）** ✓：设置
  `ExternalPublisherWatchProcess` 填一个进程名（如 `cloudmusic`），mod 就**只观察**它 —— 在运行才启动
  用户配置的发布器、退出就收掉并**重置重启计数**（下次启动干净）；留空 = 旧行为（有 `{ext.*}` 引用就启动，
  向后兼容）。**mod 绝不启动/重启那个程序本身** ✓✓ —— 这正是它比"无条件启动脚本"更合约束的地方。
  实现要点：① 进程遍历用 kernel32 toolhelp，但**不引 `<tlhelp32.h>`** —— 按本文件对 pdh/GDI+ 的做法
  **自己声明符号**（`CreateToolhelp32Snapshot` / `Process32FirstW/NextW`），不加库、不动包含顺序；
  ② 扫描**每 5 秒一次**（`kPubWatchScanMs`），**不每拍建快照**；③ `ExtProcessNameMatches` 是纯函数，
  **两个方向都要认** —— 第一次跑断言就抓到只做了单向（写 `cloudmusic` 能匹配 `cloudmusic.exe`，
  反过来不行 ✗），现在 5 条断言覆盖：无后缀 / 带 `.exe` / 大小写 / 更长名字不匹配 / 空名字不匹配。
- **来源优先级（用户实测后调整 2026-10-13）** ✓：用户实测**内存扫描极不稳定** ✗ → 主循环改为
  **store（当前行 + 行时间当位置）→ cdp → memory → media → 时钟** —— CDP/store 可用时**根本不扫内存**
  （省掉 12 秒差分发现，也不再受那份不稳定影响）。⚠️ 但 store 的 `lineTimeSec` 是**当前行的起始时间**
  （按行跳变），要连续毫秒仍须 memory/SMTC —— 代码注释与 README 都写了，别当进度条用 ✗。

**未验证**（需要真实运行环境）：

- 点击/热区的实际手感：关闭按钮与悬停都已落地，但"按下去、移上去是什么感觉"只能在真机上判断；
  另外要确认圆角外穿透没有被新增的鼠标消息处理破坏。
- 100 ms 重申是否足以消除"被短暂盖住"。
- 多于 2 台显示器的定位。
- 长时间运行稳定性（线程退出、采集器生命周期）。
- `{gpu.temp}` 在非 NVIDIA 机器上的表现。
- **外部数据的端到端行为**（无法在本机自动验证，必须真跑 Windhawk）：客户端连接/断开/重连、
  多个提供者并发、50 ms 唤醒的实际延迟、到期 → `--` 的实测时间、非管理员账户下管道 DACL 的行为、
  卸载时有在途消息时的收尾。
- **管道读写路径本身**：本机沙箱拒绝一切带写权限的管道打开（见 §3），所以宿主连不上真管道；
  这块只能靠 mod 的开机自检（`ExtSelfTest` 的两帧是否被服务端记录）与真机日志来验证。
- **事件面（M2）**：队列规则（替换/合并/优先级/丢弃）、状态机（`StepIsland`）、
  卡片内容模型（`PlanIslandCard`）、severity 配色（`ResolveSeverityColor`）、
  卡片宽度（`ComputeIslandCardWidth`，含最小宽度与屏幕钳位）都有断言（当时 302 条；当前总数见 §12.1）；
  **卡片绘制本身与宽度改动的观感**只能在机器上看，仍列在未验证之内。
- **M3 第一片（卡片交互）**：`ComputeIslandRect`（矩形与宽度同源）、`ComputeIslandCloseRect`、
  `BuildIslandRegions`、`IslandHitTest`、`IslandDismiss` 都有断言，其中包括一条**整体性质**：
  逐像素遍历卡片，要求没有任何像素落在 `IR_NONE`（"看得见的都能点"）。悬停不占定时器
  （`WM_MOUSEMOVE` 驱动、`WM_MOUSELEAVE` 收尾、只在热态翻转时重绘），帧率决策本身
  （`DesiredTickMs` / `ApplyTickRate`）也有断言；**关闭按钮与悬停在真机上的手感、以及圆角外
  穿透是否照旧**仍列在未验证之内。
- **CPU 实际频率（`{cpu.ghz}`）**：Windows 只给"相对标称的比值"（PDH `% Processor Performance`，
  100 = 标称、>100 = 睿频），所以换算是纯函数并有断言：标称 × 比值 ÷ 100，且把
  **0 / 负数 / 荒谬比值 / 无标称频率**四种情形都判为"不可用"（返回 0 → 调用方回退显示标称频率，
  不谎报动态值）。"只有引用了 `{cpu.ghz}` 的行才允许开这条 PDH 查询"按 token 扫描判定，
  同样有断言（`{cpu}`、`{cpu.name}`、`{cpu.ghz.max}` 都不得触发——`{cpu.ghz.max}` 是最容易
  被前缀误判的那个）。查询是**惰性创建、永不销毁**（`PdhCloseQuery` 不归还内存），
  与 GPU 后端一致；它**不能**搭在 GPU 那条查询上，否则 `{cpu.ghz}` 会依赖 `{gpu}` 是否在显示。
  **真机已验证（2026-10-13）** ✓：用户确认 `{cpu.ghz}` 显示**正确** ✓ —— 动态频率合理、标称取自电源
  API 的 **2.20 GHz** ✓（注册表 `~MHz` 的 2.42 已被正确忽略 ✓）。这条从"未验证"移到"已验证" ✓。
- **标称频率有两个来源，且会不一致**：注册表的 `~MHz` 是 ACPI/BIOS 报的值（实测把 2.20 GHz 的标称
  报成 2.42），电源 API 的 `PROCESSOR_POWER_INFORMATION.MaxMhz` 才是 Windows 自己当作标称的那个
  ——所以**API 优先、注册表兜底**。多核取值取**各处理器的最大 MaxMhz**（混合架构的效率核报得更低，
  单一"标称"不能取慢的那个），这条选择是纯函数并有断言。`powrprof.dll` 用
  `GetProcAddress` 按需加载（与 iphlpapi、nvml 同一种做法），**导入表因此不变**，仍然只有
  系统 DLL 那七个。整条路径只尝试一次，失败就打一行日志并保留注册表的值。
- **收起闪烁已真机确认修复（2026-10-13）**：原因是 `SetExpanded(false)` 先把窗口缩到标签高度、
  再由绘制帧长回来（§5.4 / §6 / §7.12）。修复方式是不再预缩，由绘制侧逐帧跟随；用户在真机上
  确认闪烁消失。这条**没有断言能守**（是分层窗口的帧内顺序），只能靠这条不变量与注释。

**已知的残留警告**：`ApplyPanelTransform` 在主 mod 中未被调用（只服务于测试），
会报 `unused function`。要么给它加 `__attribute__((unused))`，要么把它挪进测试文件。

---


### 10.x 真机更正与新增教训（2026-10-13，用户实测）

- **`playingLyricLineNumber` 不可用（真机更正）**：播放中、歌词页开着、两次采样相隔 20 秒，
  它**稳定停在 10**（行文本与 `time` 一字不差）。它是 **UI 状态**：随换歌重建、随播放**不推进**。
  → 当前行改为**用位置在 `async:lyric.lyricLines[].time`（秒）上选**（`Select-StoreLineIndex`）。
- **`playingState = 2` 是"播放中"**（用户确认当时在播）→ 此前"1=播放、2=暂停"的猜测作废；
  暂停时的取值仍未知，所以不拿它当暂停依据。
- **真 currentTime 在 DOM 里**：`#page_pc_mini_bar` 内 `span` 文本形如 `02:58 / 03:06`（用户从 DevTools 找到）。
  `Get-CdpDomTime` 读它并读进度柄 `left: xx%` 交叉校验。**早先的 DOM 探测只找 `audio/video`、`m:ss` 与
  `style.width`，漏掉了这一个** —— 教训：DOM 探测要**枚举可见文本形状**，而不是只找"我以为该有的标签"。
- **断言第一次跑就抓到真 bug**：`times` 若用 `[double[]]` 收，JSON 的 `null` 会被**静默变成 0**，
  那一行于是匹配任何位置 → 必须 `[object[]]` + 显式跳过 `null`。
- **不要把整文件行尾改掉**：`ReadAllText` 读、却 `split`+`join CRLF` 写回，使 20 行补丁在 diff 里
  变成 **1400 增 / 1295 删**；"修"时又整file翻成 LF → 1400/1400。写回前先确认原行尾。
- **辅助函数名先用 `Select-String` 确认**：自检里我写了 `Check ...`，而那份自检的失败计数是 `$bad`，
  于是整段自检中断、连 `SELFTEST PASSED` 都打不出来；而"移除"时只删掉一半，残留 20 行留了数轮。

---

### 10.y 全量源码复查、两处修复与遗留清单（2026-10-14）

一次覆盖 mod（6935 行）、宿主（1696 行）与全部开发脚本的复查。**四条头条**：

1. **CPU 标称频率回归（已修）**：`ReadCpuStatic()` 在每次设置版本变化时重跑，并无条件把注册表
   `~MHz`（本机 2.42）写进 `g_cpuGhzMax`，而 `CpuNominalEnsure()` 的一次性守卫保证电源 API 的
   2.20 只应用一次 → **保存过任何设置之后**，`{cpu.ghz}` / `{cpu.ghz.max}` 永久偏高约 10%。
   修法：新增 `g_cpuGhzFromApi` 锁存来源，注册表只在没有 API 答案时才写 `g_cpuGhzMax`。
   新增两条断言（`a later registry read does not undo the power API's nominal` 等）；
   **负对照实测：把修复改回去，恰好这一条变红**（当时 326 条断言 → 旧代码 FAILED 1）。
2. **"宿主会检查日志格式串"是错的（已更正文档 + 新工具）**：见上文与 AGENTS §3 的方框。
   属性 / `ms_printf` / `wprintf` / `-fms-compatibility` 四条路实测全堵，MinGW 的 `swprintf`
   同样不检查 → 新增 `_logfmt.py`（105 个调用点、0 问题；负对照会红）。
3. **bridge 把 DOM 位置清成 null（已修；很可能是待办 #1 的成因）**：`_netease-bridge.ps1` 里
   `$positionMs = $null` 出现两次，第二次紧跟在 DOM 取值之后 → 位置退回 store 的 `lineTimeSec`
   （= 冻结的 `playingLyricLineNumber` 那一行）→ 选行永远选回同一行 → **歌词不换行**。
   删掉那一行，并在原处留注释说明它必须留在 DOM 之前。⚠️ 该文件是 **CRLF + UTF-8 BOM**，
   编辑工具会**吃掉 BOM**（Windows PowerShell 5.1 按 GBK 读会乱码）——改完必须字节级检查补回。
4. **宽扁源图不折半**：`PlanDownscale` 的比例钳位会落进不动点（`200x20` → 12 次 1:1 空转 → `23x2`；
   `4000x20` 末级一次 173.91×），与注释 "final step within 2x" 和 §5.3「大图逐级折半」冲突。

**第二批已修（2026-10-14；六项，每项都做了负对照）**：

| 修复 | 断言（把修复改回旧写法后**实测变红**） |
| :-- | :-- |
| **内联环行的截断预算**：抽出 `ComputeInlineTextLimit`，内联段按"整行 − 光标 − 后面的环"给预算（此前用自身实测 advance，恒不触发截断 → 用 `[[ring:…]]` 的行永不截断、环被挤出面板） | `an inline run with no gauge after it may use the whole row`、`one gauge after the run is reserved out of its budget`、`the run plus the gauges after it exactly fill the row`、`a run already past the right inset gets nothing, never a negative budget` |
| **收回时间轴**：关键帧 cue 改为 `1.0`（此前把 `kCollapseDurationRatio` 当 cue → 收缩半程即止、后半个时间轴以 16 ms 空转；可见收回只有 `IntroMs/4`） | `the shrink still has work left at the midpoint`、`and it keeps shrinking past the midpoint without passing the tab` |
| **GPU 有效性标志**：`gpuFanValid` / `gpuPowerValid` / `vramUsedValid` / `vramTotalValid`（此前 PDH 回退机上 `{gpu.fan}`=0、`{gpu.power}`=0、`{gpu.vram.total}`=0.0） | 5 条 `{gpu.fan}` / `{gpu.power}` / `{gpu.vram.*}` → `--` |
| **`{ext.*}` 行内长度**：暂存缓冲改为 `kExtMaxValueChars + 1`（此前 128 → 静默截到 127 字符，而设置允许 512） | `a long pushed value reaches the row whole, not cut at 127`（399 字符整段到达） |
| **关掉岛丢相位**：`UpdateIsland` 在 `islandEnabled == false` 时把 `POPPING`/`COLLAPSING` 复位为 `IS_IDLE`（此前状态冻结 → tick 永久 16 ms、卡片也关不掉） | `turning the island off drops a phase that was in flight`、`and the tick falls back off the frame rate with it` |
| **发布器回收**：`extConsumed` 为假时 `ExtPublisherStop()`（此前关掉开关、或删掉最后一行 `{ext.*}` 后，发布器继续读内存/联网直到卸载） | **无断言**（真 `CreateProcess`）；判据是日志 `nothing consumes {ext.*} any more; stopping it` |

**其余已确认、尚未修**（按建议顺序）：

| 类别 | 问题（位置见当次复查报告） |
| :-- | :-- |
| 绘制 | `FitTextToWidth` 的省略号宽度缓存在函数 static，注释称"字体终生固定"，实际字体每次绘制按设置重建 → 改字号后预留宽度失效 |
| 外部数据 | `WaitForSingleObject(sampler, 2000)` 超时被忽略，随后仍 `DeleteCriticalSection` |
| 灵动岛 | 合并窗口只看 `(provider,title)` → 同标题的 `warn → critical` 升档被吞 |
| 灵动岛 | `ComputeIslandCloseRect` 在 `card.h < 22`（`rowHeight` 下限 18）时 y = −4，跑出卡片 |
| 灵动岛 | 淡出期间再点 × → `IslandDismiss` 幂等返回 false → 落到面板自己的点击行为 |
| 图标 | 图片-only 槽 + 别处有字形时，载入失败**没有**字形回退（注释声称会借用） |
| 图标 | 自定义图片不参与入场 `iconScale`（`dw > 2*plateRadius*1.01` 对缓存图恒假） |
| 图标 | `PlanDownscale` 的比例钳位不动点：短边 ≤ target 的宽扁源图一次都不折半（见上面头条 4） |
| GPU | PDH 数组读只认 `CStatus == ERROR_SUCCESS`，而 CPU 路径接受 `PDH_CSTATUS_NEW_DATA` |
| 脚本 | `_settings.py` 两条漏检仍在（同级缩进一致性、每条目单个 `$name`）——即待办 #6 |
| 脚本 | bridge 的 `$client.Connect(3000)` 无 try/catch → 瞬态管道失败即退出，烧掉 mod 的重启预算 |

**测试装置（这 340 条断言的可信度）**：`kMinLayoutWidth` 只断言了常量；收回时长由测试自己乘
`kCollapseDurationRatio`（从不调用 `StartIntro`）；"盘心 = 环心"是拿 `g.centerX` 和自己比的
**恒真**断言；token 覆盖是手抄的 32 个字面量，全文不引用 `kVariables[]`（**新增**变量不会被测到）；
`Wh_FreeStringSetting` 是空实现（漏 free / double free 都测不出）；GDI+ 段内三处
`section skipped` **不增加失败数**；尾随空格哨兵 U+2502 无断言。
"跳过 = 通过"与"改坏不会红"都已被变异实验证实。

---

## 12. 交接说明（本会话结束时写入）

### 12.1 现在是什么状态

**已验证（有证据）**：

- mod 编译：**恰好 2 条已知警告**，DLL **179712** 字节；回归 **340 断言**全绿（2026-10-14 两批修复后为 340）。
- 设置块：`_settings.py` → `no structural problems found`；独立结构检查 49 条目、顶层 dash 缩进唯一为 0、每条目恰好一个 `$name`/`$description`。
- 真实管道链：mod 开机自检两帧被服务端完整收到；`_extclient.ps1` 的值**显示在面板上**。
- `{cpu.ghz}`：用户确认显示**正确**，标称取电源 API 的 **2.20 GHz**（注册表 2.42 被正确忽略）。⚠️ 但**保存过任何设置之后**它曾回退到注册表值（2026-10-14 修，见 §10.y 头条 1）。
- 看门狗：日志出现 `EXT publisher: started pid=…`，命令行按 `CreateProcessW` 约定正确拆分。
- 歌词来源（部分打通）：`async:lyric.lyricLines`（实测 **64 / 67** 行）与 `tlyricLines`；**真 currentTime** 来自 `#page_pc_mini_bar` 的 `mm:ss / mm:ss`。
- bridge 自检 **30 项**（含 8 项 `Select-StoreLineIndex`，不假设长度）全绿。

**未验证 / 待办（按优先级）**：

1. **面板上的歌词是否真的随播放换行**（用户尚未回报）。排查顺序：bridge 手跑输出 → `Get-CdpDomTime` 是否拿到 `curS` → `times` 是否拉到 → 选行索引是否变化。
2. **发布器重启风暴是否消失**（已改成"等待窗口"而非 `exit 2`）；建议同时设 `External publisher watch process = cloudmusic`。
3. 作业对象是否收掉**孙进程**、`CREATE_NO_WINDOW` 是否不闪窗、重载无孤儿 —— 三条只能真机看进程列表。
4. 客户端**升级后** store 自寻址是否仍可用（跑一次 `-ProbeCdp`，它会自寻址并列出可见字段）。
5. `playingState` 在**暂停**时的取值仍未知（目前不拿它判暂停）。
6. `_settings.py` 补两条漏检：**同级条目缩进一致性**、**每条目单个 `$name`**（Windhawk 两次报错它都没抓到）。
7. `ApplyPanelTransform` 的 `unused function` 警告（加 `__attribute__((unused))` 或挪进测试文件）。
8. 设计文档里 M3 剩下的部分：`ComputeIslandRect` 接管窗口定位。
9. AGENTS 的章节顺序：`### 10.x` 那段是追加在 §11 之后的，若在意次序可挪进 §10 末尾。

### 12.2 犯错档案（本会话的错误 → 原因 → 防范）

| 错误 | 原因 | 防范 |
| :-- | :-- | :-- |
| 设置块缩进错，Windhawk 报 `bad indentation of a sequence entry` | 凭感觉写缩进，而 `_settings.py` 不查这一项 | 抄相邻条目的风格；给 `_settings.py` 补检查 |
| 条目插在"键"与它自己的 `$name` 之间 → `duplicated mapping key` | 锚点选在 `$name` 上，而它属于**上一行**的条目 | 条目边界是**下一条 `- Key:`**；结构检查保证每条目单 `$name` |
| 断言里凭空写 `Check` → 自检中断数轮 | 凭记忆用辅助函数名（真实失败计数是 `$bad`） | 插入前先 `Select-String` 找**真正的汇总行**；残留块要整段删除并复核 |
| 日志清理**误删活代码**（两次，共 6 个编译错误） | 日志与其专用计数器绑定；按**文本**删命中别的函数，按**行号**删又算错一位 | **静默优于删除**（`DBG` 宏保留语句）；删前先编译，只删编译器点名的行 |
| 20 行补丁在 diff 里变成 1400 行 | `ReadAllText` 读、却 `split` + `join CRLF` 写回 | 写回前确认原行尾；优先 `Replace` + `WriteAllText` |
| 把 `playingLyricLineNumber` 当"实时当前行"并写进文档 | 单次采样看着对，就当结论 | 至少**两次采样** + 与界面/位置交叉验证；未验证的结论必须标明 |
| 发布器遇到"暂时没有窗口"就 `exit 2` | 按"人手跑"的思路写，忽略了 mod 的重启预算 | 被托管的进程**不因暂态条件退出**；生命周期交给看门狗/作业对象 |
| DOM 探测漏掉迷你条时间 | 只找"我以为该有的标签"（`audio/video`、`m:ss`、`style.width`） | 枚举**可见文本形状**（`m:ss / m:ss`），并在**正确的页面**上测 |

### 12.3 接手时先读这几节

1. §0 三条铁律 → 2. §3 构建与测试（两条已知警告 + 沙箱边界）→ 3. §5.6 外部数据（管道 / 发布器 / 看门狗）→ 4. §10 与本节（分清哪些是实测、哪些还没验）。