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
- 回归测试全绿（478 条断言，含一段**真启动 GDI+** 的排版断言：
  文字不折行、且在预算内）。
- GDI+ 缩放管线用**已知内容**的图验证过（含透明底、纯 RGB、索引色、灰度+alpha、
  16 位、隔行 PNG）。
- 外部数据（M1）：编译只剩上面那两条已知警告；回归 **478 条断言**全绿（M1、M2、M3 与 CPU 频率共新增 318 条）；
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
| **收回时间轴**：关键帧 cue 改为 `1.0`（此前把 `kCollapseDurationRatio` 当 cue → 收缩半程即止、后半个时间轴以 16 ms 空转；可见收回只有 `IntroMs/4`）**(2026-10-15 补：当时 ratio 是 1/2，所以 1/4 是缺陷；现在 ratio 本身是 1/4，`IntroMs/4` 是设计值——这一行已经是历史记录)** | `the shrink still has work left at the midpoint`、`and it keeps shrinking past the midpoint without passing the tab` |
| **GPU 有效性标志**：`gpuFanValid` / `gpuPowerValid` / `vramUsedValid` / `vramTotalValid`（此前 PDH 回退机上 `{gpu.fan}`=0、`{gpu.power}`=0、`{gpu.vram.total}`=0.0） | 5 条 `{gpu.fan}` / `{gpu.power}` / `{gpu.vram.*}` → `--` |
| **`{ext.*}` 行内长度**：暂存缓冲改为 `kExtMaxValueChars + 1`（此前 128 → 静默截到 127 字符，而设置允许 512） | `a long pushed value reaches the row whole, not cut at 127`（399 字符整段到达） |
| **关掉岛丢相位**：`UpdateIsland` 在 `islandEnabled == false` 时把 `POPPING`/`COLLAPSING` 复位为 `IS_IDLE`（此前状态冻结 → tick 永久 16 ms、卡片也关不掉） | `turning the island off drops a phase that was in flight`、`and the tick falls back off the frame rate with it` |
| **发布器回收**：`extConsumed` 为假时 `ExtPublisherStop()`（此前关掉开关、或删掉最后一行 `{ext.*}` 后，发布器继续读内存/联网直到卸载） | **无断言**（真 `CreateProcess`）；判据是日志 `nothing consumes {ext.*} any more; stopping it` |

**第三批已修（2026-10-14；三项，每项都做了负对照）**：

| 修复 | 断言 / 负对照 |
| :-- | :-- |
| **`PlanDownscale` 的不动点**：去掉"把短边钳回目标"的比例钳位（它让 `200x20` 这类横幅做十几次 1:1 空转、随后一次性压 8.7 倍；`4000x20` 末级 173 倍），改为纯按比例折半、短边允许降到 1px | `no stage reduces the long edge by more than 2x`、`the short side is the exact fit, rounded up by at most a pixel`；把钳位改回去 → 红 |
| **图片-only 槽的字形回退**：借"第一个已配置的字形"（一个都没有则 `Memory`）现在对**任何**图片槽生效，不再只在"全工程都没有字形"时才借 | `an image-only slot borrows the configured glyph`、`with no glyph anywhere an image-only slot falls back to Memory`；把回退改回旧条件 → 红 |
| **`_settings.py` 的两条漏检（待办 #6）**：新增"序列项缩进一致性（顶层 0 列 / 选项 4 空格）"与"每条目恰好一个 `$name` / `$description`"，外加顶层键不重复、同组选项不重复、选项必须在 `$options:` 之后 | 逐条注入实测：5 空格缩进、重复 `$name`、重复键、重复选项各自报错并 exit 1；未改动的块仍 `no structural problems found`，`--write` 幂等（字节不变） |

**第四批（用户要求实现 `IconPath`，2026-10-14；含一个附带缺陷，五处负对照共红 12 条）**：

> **这一批已经作废（2026-10-14，同日稍后）**：图标设置项（`Row N icon` / `Row N icon file` /
> `Custom icon file`）被整体删除，图标改为行内标签 `[[icon:…]]`
> （见 [inline-markup-design.md](inline-markup-design.md) §7 第 4 步、CHANGELOG「删除的与不兼容的」）。
> 下表里的断言**已经随功能一起删除**，`ModSettings` 里也不再有任何图标字段——
> 不要照着它去找那些断言。行内图标的不变量与断言现在记在 AGENTS §5.3 与
> `test-layout.cpp` 的 `=== inline icons: the tag, the glyph and the ripple switch ===` /
> `=== inline icon images: one cache entry per file, bounded ===` 两节。
> 下面这段留着是因为其中的**教训**仍然成立：一个"有设置项、却从来没被读取"的死设置项
> （它当时的表现就是"填了没效果"），以及"空行的槽位被顺手丢掉"这类因继承规则而起的错位。
> 内联图标从结构上消掉了继承（图标跟着写它的那一行走），所以那两类缺陷不会再出现。

| 内容 | 断言 / 负对照 |
| :-- | :-- |
| **`Custom icon file` 真正生效**（此前是死设置项）：`ModSettings::iconPath`，一张图给所有行用；每行优先级 `Row N icon file` > 显式 `None` > 全局图片 > 内置字形 | `the global icon applies to a row that has no file of its own`、`a global icon on its own still means plates are drawn` |
| **`Row N icon = None` 现在真的表示"这一行不要图标"**（附带缺陷，探针实测 `Icon1=Memory`+`Icon3=None` → 第 3 行拿到 Memory）：`IconSpec::styleSet` 把"显式 None"与"没设置"分开；`None` 连全局图片一起挡 | `an explicit None leaves that row without a glyph`、`an unconfigured row inherits the first real glyph, not a None` |
| **相同路径只解码/缩放一次**（用户选择"按路径合并缓存、不加第五槽"）：`ResolveIconSlot` 让解析到同一文件的行共用最低那个槽位，每个文件仍只有一个所有者，按槽释放逻辑不变 | `every row shares one cache slot, so the file is decoded once`、`a row that names the global file shares row 1's slot` |
| **回退规则改为解析式**：`iconFallbackStyle` 由 `ReadSettings` 算一次，`ResolveIconStyle` 取用（不再把字形写进槽位——全局图片不属于任何槽）；`AnyIconConfigured` 改为问 `IconPlateDiameter`（单一来源），否则"全行 None"会被当成"要留图标边距" | `with no glyph anywhere an image-only row falls back to Memory`、`with nothing configured at all no row gets a plate` |
| **全局图片排在"继承别的行的文件"之前**：有全局图时单行文件是例外、不扩散；没有全局图时"只设 `Row 1 icon file` 就等于所有行都用它"照旧 | `the per-row file does not leak to the rows the global covers`、`one row's file still covers the rows after it when there is no global image` |

README 的图标一节与设置表、AGENTS §5.3/§6、以及 `IconPath` 的 `$description` 都已同步
（用户文档现在明确写了四级优先级）。

**第五批（修测试装置自身，2026-10-14；六处，逐处都有变异负对照）**：

| 缺口 | 修法 | 负对照（改回去 → 红） |
| :-- | :-- | :-- |
| 三处 `printf("… section skipped")` **不增加失败数** | 新增 `Skip()`：打印 `SKIP` 并计入末行的 `skipped`；**产品自身的前置条件算失败**（`CreateRowStringFormat` 返回 null → `Check(false, …)`）。末行现在是 `PASSED (0 failure(s), 1 skipped)` | 让 `CreateRowStringFormat` 早返回 → 那条变 FAIL，且受它影响没跑的检查显示 `Segoe UI is installed (real-text checks) SKIP`（旧写法下末行仍是 PASSED） |
| 两条**恒真**断言 | 删掉"盘心 = 环心"（同一结构体字段自比）与 `if (firstY == panelH/2) Check(rowCount == 1, …)`（条件与结论代数等价），换成**每行**：`each plate is centred in its own row`、`and the whole plate fits inside that row`、`the plate stays inside the panel's left edge` | `IconPlateCenterForRow` 的 Y 改回 `rowHeight / 2` → 7 条 FAIL（旧断言在第 2..4 行全部漏过） |
| `kMinLayoutWidth` 只断言了常量、GDI+ 段还**抄了一遍表达式** | 新增产品的 `LayoutWidthFloor()`，绘制（预算与 DrawString 框）与测试都调它；断言 0 / 负数 / 半格 / 正常值 / 恰为下限五例 | `LayoutWidthFloor` 改成 `return width;` → 3 条 FAIL |
| 尾随空格哨兵 U+2502 **无断言** | 在真 GDI+ 段用真字体量 `"CPU 12%"` 与 `"CPU 12%   "`（实测 62.4 vs 75.1 px） | 把哨兵改成 `L'\0'` → 两者都是 50.3 px，断言 FAIL |
| 桩的 `Wh_FreeStringSetting` 是空实现（漏 free / double free 都测不出） | 桩按真实 API **每次发放新内存**并记账：每笔发放可查、重复释放/释放非发放指针会打印并置错、末段 `=== settings string ownership ===` 报出未释放数（实测 351 次发放 / 0 未释放） | 删掉 mod 里一处 `Wh_FreeStringSetting(theme)` → `every settings string the run asked for was released` FAIL，并报 13 个未释放 |
| token 覆盖是**手抄**的 32 个字面量，与 `kVariables[]` 无对应 | 直接遍历 `kVariables[]`，并加"每个注册项都要有采集组"（`group != G_NONE`） | 往 `kVariables[]` 加一个未实现的 `{L"disk", G_SYS}` → `every registered variable resolves` FAIL |

断言数 380 → **434**（第五批，+54：列表改遍历后按注册项计，加上新增的行级几何与下限检查）→ **460**（第六批，+26：灵动岛三条）→ **478**（第七批，+18：剩余五项）。
**测试装置现在仍有的信任边界**（诚实记录）：`PaintPanel` 的**调用点**依旧不可断言（需要真窗口），
所以内联预算只测到 `ComputeInlineTextLimit`／`LayoutWidthFloor` 本身，绘制分支里怎么用它们靠读代码；
`StartIntro` 的关键帧接线**已不再是未驱动**（2026-10-15 更正）：`fdf5806` 起宿主直接调
`StartIntro(nullptr, from, to)` 两个方向各一次，收回时长由产品自己按 `kCollapseDurationRatio`
算出，测试不再自己乘一遍（断言见 `StartIntro: arriving … -> leaving …` 那一段与
"the shrink still has work left at the midpoint"）。**仍然**不可断言的是 `SetExpanded` /
`PaintPanel` 的**调用点**与调用顺序（需要真窗口）；GDI+ 段的像素阈值全是关系式，
换字体会让具体数值变但关系不变。

**第六批已修（2026-10-14；灵动岛三条，五处负对照共红 15 条）**：

| 修复 | 断言 / 负对照 |
| :-- | :-- |
| **合并窗口吞掉升级**：同 `(provider,title)` 在窗口内重复时**取更高的 severity**（并刷新为最新文本）；此前保留首次的级别 → 状态机看不到 `criticalWaiting`，卡片一直停在 warn | `an escalation inside the merge window raises the queued level`、`so a card already on screen stands down for it`（直接驱动 `StepIsland`）、`a repeat never lowers the level the queue already holds`、`the same title after the merge window is a new card, not a repeat`；去掉取高 → 红 4 条 |
| **关闭按钮比卡片高**：按钮按卡片尺寸收缩（`min(kIslandCloseSize, card.w, card.h)`）。此前固定 22 px 对单行 18 px 卡片时，上钳位先抬到卡片上沿、下钳位再把它推出卡片（y = card.y − 4） | `the close button fits inside a card of any height`（h = 1/8/18/21/22/30/90/168）、`and it shrinks to the card when the card is smaller`；恢复固定尺寸 → 红 8 条 |
| **淡出期点击落到面板**：抽出纯函数 `IslandClickActionFor(hit, dismissable)`——按钮命中时**能淡出就淡出、正在淡出也吃掉**；卡片命中一律吃掉；只有卡片之外归面板 | `and during the fade it is still swallowed, not handed to the panel`、`a click on the card is swallowed, never the panel's`、`so the button's two questions agree on both states`；改回"失败就落到面板" → 红 3 条 |

**第七批已修（2026-10-14；剩余五项一次清完，负对照共红 7 条 + 桥自检对照）**：

| 修复 | 断言 / 负对照 |
| :-- | :-- |
| **`FitTextToWidth` 不再按进程缓存省略号宽度**（理由写"字体终生固定"，而绘制每帧重建字体 → 改字号后预留宽度失效） | GDI+ 段扫 40..200 共 161 个预算：先用 15 px 字号"弄脏"旧缓存，再要求 24 px 下**每个**预算都不越界（实测陈旧缓存下预算 132 越界 6.1 px）→ 红 |
| **sampler join 的等待结果不再丢弃**（超时后仍删 `g_valuesLock` = 在另一条线程可能还握着它时删锁） | `ValuesLockCanBeReleased` 只认 `WAIT_OBJECT_0`：`a sampler that did not stop leaves its lock alone`、`and so does a failed wait`；恒真 → 红 2 条 |
| **自定义图片随入场缩放**：抽出 `ComputeIconImageDrawSize` = `min(自然尺寸, 盘径) × iconScale`（旧条件只在"比盘大"时乘缩放，而缓存图恰好等于盘径） | `and follows the intro as it scales the plate in`、`while the intro still scales it down`、`at zero scale it draws at zero size`；旧条件 → 红 3 条 |
| **PDH 状态口径统一**：`PdhValueUsable()` 接受 `VALID_DATA` 与 `NEW_DATA`，三条路径共用（GPU 数组读取此前只认 `ERROR_SUCCESS` → 计数器刚建好的第一个采样被丢） | `and so is a counter's first sample (new data)` 等 5 条；只认 `ERROR_SUCCESS` → 红 1 条 |
| **bridge 的管道连接加等待窗口**：`Connect-PipeClient` 失败重试、等不到返回 `$null`（此前裸 `Connect(3000)` 抛未处理异常、脚本当场死掉、烧掉 mod 的重启预算） | 自检新增 `连不上管道时返回空值（不抛异常）`；对照：换回裸 Connect → **未捕获异常、exit 1**，自检根本跑不完 |

**第八批（2026-10-14，用户真机报告"歌词与标题都为空横线"；**根因在示例发布器**，不是 mod）**：

| 内容 | 证据 / 断言 |
| :-- | :-- |
| **`Get-PlayingTrack` 改用窗口枚举，不再用 `Process.MainWindowTitle`**：真机上网易云开着「桌面歌词」时 `MainWindowTitle` **就是「桌面歌词」**，带歌名的窗口反而不是主窗口 → 标题里永远没有「 - 」→ 桥一直停在"没有可用窗口"的等待循环、**从不连接管道** → 两行永远 `--` | 现场取证：`Get-Process -Id 15824` → `cloudmusic / 桌面歌词`；`EnumWindows` 列出该 pid 的**两个**顶层窗口 `[桌面歌词] [下等马 - 洛天依Official/ChiliChill乐团]`。修后自检活体输出同两行，且 `-DryRun -Once` 打出 `-> SET netease title … 下等马`、`-> SET netease lyric … 练就这套拳叫` |
| **按窗口所属 pid 反查进程名**，而不是"先列出全部进程再筛 pid"：进程**列表**在受限环境里只给一部分（实测沙箱里 `Get-Process`/`tasklist`/Toolhelp 都只看到 13 个进程），而 `Get-Process -Id` 可靠——否则这段在受限环境里永远返回空 | 自检 `窗口枚举：网易云在跑时能看到它的顶层窗口`；活体输出 2 个窗口（正是靠这条路才在沙箱里也拿到证据） |
| 自检新增 5 项（4 项纯函数 + 1 项活体），并把窗口列表**打印出来**当活体证据 | `曲目标题：桌面歌词窗口不算曲目`、`…挑出带「 - 」的那个`、`…多个候选取最长`、`…空标题与空数组都不算`、`窗口枚举：…`；总计 **36 项**全绿 |

> 这条与交接文档 §12 早就记下的真机事实**同源**（"桌面歌词不是进程主窗口，`MainWindowTitle` 看不到它，
> 必须 `EnumWindows` 按标题找"）——脚本当初没照做。**教训：文档里的真机事实要当约束用**，
> 光记下来不等于用上了。
>
> 附带观察（不影响功能）：在线歌词**搜索**接口在本机报 `The SSL connection could not be established`
> （两个端点都失败），但歌词仍从播放器自己的 store 取到了（`-> SET netease lyric … 练就这套拳叫`），
> 所以搜索只是"store 里没有时的兜底"。要不要修这条另说。

**第九批（2026-10-14，回答"当前歌词是直接获取还是按时间点算的"）**：

| 结论 / 改动 | 真机证据 |
| :-- | :-- |
| **当前歌词是"算"的**：取播放**位置**，再在歌词行时间戳上选行（`Select-StoreLineIndex`，纯函数） | 播放器自己的 `playingLyricLineNumber` **冻结在 10**（82 行歌词、那句 `それを珍品として崇めて` = 29.5 s），而真实位置 **90.0 s**、算出来的行是 `つまらない生活の中`——直接读的行只作为过渡一帧，随即被算出来的覆盖 |
| **位置来源链**（每一步失败静默降级）：迷你条 DOM 的真 currentTime → store 当前行起始时间 → CDP 其它位置 → 内存差分 → SMTC 媒体会话 → 推算时钟 | 本机 SMTC **不可用**（`_netease-position.ps1 -Probe` → `PROBE none (no active media session)`）；CDP **可用**（9229，`playingState=2`）；实际生效 **DOM**：`-> SET netease pos 600000 dom 90.0s` |
| **新增 `pos` key**（`{ext.netease.pos}`）：把"位置是哪来的"发到面板上，不再只打在 mod 收走的 stdout | 自检新增 4 项 `位置来源标签：…`（36 → **40** 项）；实测 `pos` 帧内容为 `dom 90.0s` |
| 顺手澄清两处易误读：① 启动那句 `位置：媒体会话不可用，退回推算时钟` **只说明 SMTC 不可用**，不代表整条链退到时钟；② `-PositionSource auto` 的注释原写"memory → media → clock"，与实际顺序不符（已按主循环改写） | README 的发布器一节写明了顺序与 `clock` 的含义 |

**第十批（2026-10-14，用户报告"歌词滞后约 1 秒"，要一个"负延迟"）**：

| 内容 | 依据 / 断言 |
| :-- | :-- |
| **滞后的两个来源都已量化**：① 迷你条 DOM 只给 `mm:ss`，位置**向下取整到秒**（真实 90.9 s 报 90）→ 平均落后 0.5 s、最坏 1 s；② 桥每 `-IntervalMs`（默认 1000 ms）才轮询一次 → 平均再落后半周期。**mod 侧可忽略**：sampler 每 50 ms 查一次 `g_extDirty`，值一到就重采样（`[5901]`） | 代码 + 实测量级；`-LeadMs -1` 的自动值 = 半周期 +（`dom` 再 0.5 s）= **1000 ms** |
| **新增 `-LeadMs`**（"负延迟"）：把**选行用的位置**提早这么多；`-1` 自动、`0` 关掉、正数自定义。发出去的 `pos` 带出提早量（`dom 90.0s +1000ms`），过头了看得见 | 自检 40 → **46** 项：`负延迟：DOM 来源 = 半周期 + 0.5s 取整偏差`、`…精确来源只补半个周期`、`…周期变了自动值跟着变`、`…显式值覆盖自动值（0 = 关掉）`、`pos 标签带出提早量`，以及**组合效果**断言 `负延迟确实把换行提早（9.9s 仍是 0 行，+1s 后是 1 行）` |
| 实现踩点：**局部变量不能叫 `$leadMs`**——PowerShell 变量名大小写不敏感，会覆盖 `-LeadMs` 参数本身，自动值就永远停在第一次算出来的数（代码里已注明） | 活体输出 `-> SET netease pos 600000 dom 136.0s +1000ms` |
| 顺手修正 README 两处与实测不符：`-LyricSource store` 原写"精确到客户端自己的当前行"（实测**冻结**）；参数表里的位置顺序与下一行自相矛盾 | README 的发布器一节 |

**其余已确认、尚未修**：**本会话复查列出的项目已全部处理完**。剩下的都是真机项（见 §12.1）
与 AGENTS §7.13 末尾记的测试装置信任边界（`PaintPanel` 的**调用点**；`StartIntro` 接线自
`fdf5806` 起已由宿主驱动两个方向，2026-10-15 更正——见本文件前面的更正条）。

**观察记录（不是缺陷，留给真机）**：`FitTextToWidth` 每次多量一次省略号宽度；这发生在
"某段超预算"的帧里，与旁边的二分测量同量级。若真机上发现滚动文本的帧时间有变化，
第一个可回退的点是把它改成**按字号键控**的缓存——**不要**改回"按进程"。

---

## 12. 交接说明（本会话结束时写入）

### 12.1 现在是什么状态

**已验证（有证据）**：

- mod 编译：**恰好 2 条已知警告**，DLL **182272** 字节；回归 **478 断言**全绿（2026-10-14 七批修复后为 478，含 1 条环境性 SKIP）。
- 设置块：`_settings.py` → `no structural problems found`；独立结构检查 49 条目、顶层 dash 缩进唯一为 0、每条目恰好一个 `$name`/`$description`。
- 真实管道链：mod 开机自检两帧被服务端完整收到；`_extclient.ps1` 的值**显示在面板上**。
- `{cpu.ghz}`：用户确认显示**正确**，标称取电源 API 的 **2.20 GHz**（注册表 2.42 被正确忽略）。⚠️ 但**保存过任何设置之后**它曾回退到注册表值（2026-10-14 修，见 §10.y 头条 1）。
- 看门狗：日志出现 `EXT publisher: started pid=…`，命令行按 `CreateProcessW` 约定正确拆分。
- 歌词来源（部分打通）：`async:lyric.lyricLines`（实测 **64 / 67** 行）与 `tlyricLines`；**真 currentTime** 来自 `#page_pc_mini_bar` 的 `mm:ss / mm:ss`。
- bridge 自检 **74 项**（含 8 项 Select-StoreLineIndex、6 项行时间表缓存、9 项收敛判定、4 项 store 行时间当位置的判定，与 5 项窗口/曲名判定，都不假设长度或环境）全绿。
- 图标全局覆盖（Custom icon file）与 None 语义：2026-10-14 实现并断言（第四批，见 §10.y）。
- **曲名来源（真机定论，2026-10-14）**：网易云开着「桌面歌词」时 `cloudmusic` 的 `MainWindowTitle`
  **就是「桌面歌词」**，带歌名的窗口是同一 pid 的**另一个顶层窗口**
  （`Get-Process -Id 15824` → `cloudmusic / 桌面歌词`；`EnumWindows` 给出
  `[桌面歌词] [下等马 - 洛天依Official/ChiliChill乐团]`）。**取曲名只能枚举顶层窗口，不能看
  MainWindowTitle**——与 §12 里"桌面歌词不是主窗口"那条同源，示例发布器当初没照做，
  于是面板两行一直 `--`（第八批已修）。

**未验证 / 待办（按优先级）**：

0. ~~**对齐组吃掉最后一个字符**~~ —— **2026-10-15 已在宿主复现并修掉**（真机症状：组里一个
   字符时"偏离原位"、多个字符时**最后一个不画**（中英文皆然）、末尾**加一个空格**就好）。
   根因是两个数被当成一个：组的宽度与 `DrawString` 的 layout 框用的是 `MeasureAdvance`
   （**笔的推进**），而 GDI+ 需要的框宽是**墨迹范围**——最后一个字形的右侧边距会超出笔的终点
   （15 px `Segoe UI` 的 `W`：笔进 14.43 / GDI+ 报 19.43，`WW`/`WWW` 同为 5.00；
   把 `WWW` 画进 43.29 的框只出来**两个字**，48.29 才三个）。**GDI+ 不溢出框、它丢掉放不下的字形**，
   所以框窄一丁点儿就是"最后一个字不显示"；一个字符时表现为位置偏一点；末尾空格"有推进、无墨迹"，
   正好补上差值——三个症状同源。另外相位 C 有一处坐标架混用（元素仍在组的局部坐标、右边缘是面板
   坐标，续段预算算成负数、整段被截没）。
   修法：`MeasureAdvance` 仍是笔的推进（行流游标/定位/自然宽度），新增 `MeasureDrawnWidth`
   （两者取大）作为**框与组宽度**的唯一来源；相位 C 把组内元素一起平移到面板坐标。
   断言：`a run's layout box is at least as wide as the text GDI+ draws in it`、
   `text drawn in a box as wide as its advance keeps all of its glyphs`、
   `the group draws every character between its tags, at every font size and both scripts`、
   `and the group is as wide as the text it draws (so it is centred where that text is)`、
   `and no run is given a box narrower than the text GDI+ draws in it`、
   `and a centred group is centred on its own text`，外加用户原串
   `[[h:100]] [[center]]你好[[/center]]` 的直接断言（10..30 px × 三种对齐 × 三种位置 × 有无尾随空格
   × 中英文 × 1/2/多字符 × 行内有 `[[size:24]]` 的全矩阵，`lost/shallow/clipped/offCentre` 全 0）。
   负对照：① `MeasureDrawnWidth` 退回笔推进 → 5 条红（含 `a run's layout box is at least as wide...`
   与 `and a centred group is centred on its own text`）；② 组的相位 A 预算退回行流游标 →
   `a group's content survives a long flow run before it (all three alignments)` 等 6 条红。
   两条都已还原（SHA-256 与改动前一致）。
   ⚠️ 教训：上一版断言只扫 `'W' × 0..40` **一个字号**，而那个字号恰好没有这个差值——
   **断言要在真实的字号范围上扫，不能只扫一种字号**（与 §7 第 9 条"喂真输入"同源）。
1. **手动切歌之后歌词是否真的继续走**（2026-10-15 用户报"切歌后歌词不动"）。口径已改：
   把位置降级链里"会冻结的 store 行时间"这条来源加上前进判据（`Test-StoreLineUsable`）；
   `-ProbeStore` 每轮新增 `origin=` / `lineMs=` / `table=` 三列。
   **2026-10-15 拿到了用户跑出来的探针输出：`table=stale`** —— 那指向 `b97f403` 加的第二把尺子
   （行数），而它的**不收敛形状**已经改掉：旧判定拿**两次不同的读**去比相等（缓存行数来自
   读表那一拍、store 行数来自这一拍），只要对不上就每拍都判过期、每拍都重读，表永远"过期"、
   选行缓存被反复清掉。现在 `Get-StoreTimesVerdict` 把 **id 当"这份表属于谁"的判据、
   行数当"要不要再读一次"的触发器**（这个行数没读过 → 读一次并记下；同一个行数再来 → 就用
   这份表；行数再变 → 再允许读一次），所以每个 (id, 行数) 组合最多多读一次、**任何情况下都收敛**，
   而"上一首的表被打上新 id"仍然在第一格就被读掉。`-ProbeStore` 改成跟着主循环的判定走
   （判过期就重读一次）并**点名子条件**（`stale(Track)` / `stale(Lines)` / `stale(Empty)`），
   再加一行 `table-in:`（`cached=` / `store=` / 两个 id / `acceptedLines=`）与每个位置来源
   各自的新鲜度；bridge 自检 66 → **74 项**（含 9 项收敛判定的状态机断言）。
   负对照：把行数触发器去掉（行数对不上也放行）→ `时间表缓存：行数对不上 → 判 stale(Lines)，
   重读一拍后就 fresh（不再永远过期）` 等 4 条红；已还原（SHA-256 与改动前一致）。
   **还缺的证据**：真机跑一次
   `pwsh -ExecutionPolicy Bypass -File _netease-bridge.ps1 -ProbeStore -ProbeRounds 30`
   并手播一次歌，看 `table=stale(<子条件>)` 会不会在**下一轮**变成 `fresh`。
   （本机 2026-10-15 实测的一首歌是 `table=fresh`、`cached=2`、`store=2`、两个 id 相同——
   即这个形状在本机没有触发；探针开盘还打印 `times[0..3]` 便于核对那把尺子。）
   仍然可能的三条判读：
   · `pos=n/a` 且 `lineMs` 一直不变 → 迷你条 DOM 读不到、位置会退到 store 行时间（已修的那条）；
   · `table=stale(Lines)` 且下一轮还是它，而 `table-in:` 的 `cached=`/`store=` **卡住不动** →
     store 报的行数与表里记着的行数不是同一把尺子（例如歌词数组比 2000 行上限还长）；
   · `pos` 与 `byPosition` 都在走、而面板歌词不动 → 问题在取行文本那一环（`Get-CdpStoreLyricLine`），
     目前**没有**任何证据指向这一条。
   ⚠️ 2026-10-14：`$positionMs = $null` 被写了两遍、第二遍把 DOM 的真 currentTime 清掉（已修，见 §10.y 头条 3）。
2. **发布器重启风暴是否消失**（已改成"等待窗口"而非 `exit 2`）；建议同时设 `External publisher watch process = cloudmusic`。
3. 作业对象是否收掉**孙进程**、`CREATE_NO_WINDOW` 是否不闪窗、重载无孤儿 —— 三条只能真机看进程列表。
4. 客户端**升级后** store 自寻址是否仍可用（跑一次 `-ProbeCdp`，它会自寻址并列出可见字段）。
5. `playingState` 在**暂停**时的取值仍未知（目前不拿它判暂停）。
6. ~~`_settings.py` 补两条漏检~~ —— **2026-10-14 已补**（缩进一致性、每条目单个 `$name`，外加重复键/重复选项），四条检查都有注入式负对照，见 §10.y 第三批。
7. `ApplyPanelTransform` 的 `unused function` 警告（加 `__attribute__((unused))` 或挪进测试文件）。
8. 设计文档里 M3 剩下的部分：`ComputeIslandRect` 接管窗口定位。
9. AGENTS 的章节顺序：`### 10.x` 那段是追加在 §11 之后的，若在意次序可挪进 §10 末尾。
10. **测试装置自身的缺口**（§10.y 末尾列了一串）：三处 `section skipped` 仍报 `PASSED`、两条恒真断言、`kMinLayoutWidth` 只断言常量、尾随空格哨兵 U+2502 无断言、桩不模拟字符串所有权、token 列表与 `kVariables[]` 无对应。都能变成会红的断言，是下一批最省事的目标。

### 12.2 犯错档案（本会话的错误 → 原因 → 防范）

| 错误 | 原因 | 防范 |
| :-- | :-- | :-- |
| 设置块缩进错，Windhawk 报 `bad indentation of a sequence entry` | 凭感觉写缩进，而 `_settings.py` 不查这一项 | 抄相邻条目的风格；给 `_settings.py` 补检查（2026-10-14 已补：它现在查缩进一致性） |
| 条目插在"键"与它自己的 `$name` 之间 → `duplicated mapping key` | 锚点选在 `$name` 上，而它属于**上一行**的条目 | 条目边界是**下一条 `- Key:`**；结构检查保证每条目单 `$name`（已实现） |
| 断言里凭空写 `Check` → 自检中断数轮 | 凭记忆用辅助函数名（真实失败计数是 `$bad`） | 插入前先 `Select-String` 找**真正的汇总行**；残留块要整段删除并复核 |
| 日志清理**误删活代码**（两次，共 6 个编译错误） | 日志与其专用计数器绑定；按**文本**删命中别的函数，按**行号**删又算错一位 | **静默优于删除**（`DBG` 宏保留语句）；删前先编译，只删编译器点名的行 |
| 20 行补丁在 diff 里变成 1400 行 | `ReadAllText` 读、却 `split` + `join CRLF` 写回 | 写回前确认原行尾；优先 `Replace` + `WriteAllText` |
| 把 `playingLyricLineNumber` 当"实时当前行"并写进文档 | 单次采样看着对，就当结论 | 至少**两次采样** + 与界面/位置交叉验证；未验证的结论必须标明 |
| 发布器遇到"暂时没有窗口"就 `exit 2` | 按"人手跑"的思路写，忽略了 mod 的重启预算 | 被托管的进程**不因暂态条件退出**；生命周期交给看门狗/作业对象 |
| DOM 探测漏掉迷你条时间 | 只找"我以为该有的标签"（`audio/video`、`m:ss`、`style.width`） | 枚举**可见文本形状**（`m:ss / m:ss`），并在**正确的页面**上测 |

### 12.3 接手时先读这几节

1. §0 三条铁律 → 2. §3 构建与测试（两条已知警告 + 沙箱边界）→ 3. §5.6 外部数据（管道 / 发布器 / 看门狗）→ 4. §10 与本节（分清哪些是实测、哪些还没验）。
## 13. 已修复的缺陷总表（原 AGENTS §6）

## 6. 已修复的缺陷（回归测试清单）

改相关代码前先看对应断言。表右列是"如果有人改错会看到什么"。

| 缺陷 | 症状 | 关键断言 |
| :-- | :-- | :-- |
| 无环行的可用宽度算成 0 | 第一行之后都不显示 | 无环行拿到完整内宽 |
| 内联环前的尾随空格未计入 | 环前面的空格失效 | 展开文本保留空格 |
| 自定义颜色压过命名主题 | 主题被自定义配置覆盖 | 命名主题完全胜出 |
| 环颜色来源混乱 | 环的颜色盖不住 | `:COLOR` > Custom 的 `Ring colour` > 边框色 |
| 已知变量取不到值被当未知 | 屏幕出现字面 `{gpu.temp}°C` | 已知不可用渲染为 `--` |
| 高度关键帧带收回段 + 过冲过大 | 弹出→收回→突然弹出 | 高度不得出现可见回落 |
| 图标盘只画一个且居中 | 涟漪从中间行弹出 | 每行一盘、点位正确 |
| 波纹未随世界变换 | 涟漪与图标不同坐标系 | 盘心环心逐位相同 |
| 最内圈半径小于盘半径 | 涟漪与图标不同心 | 环从盘边缘起步向外扩张 |
| 文字淡入窗口过短且只有一段 | 文字像啪地出现 | 淡入经多个中间透明度且单调不回头 |
| 大图一步压到图标尺寸 | 图标锯齿 | 逐级折半；各阶段只缩不放 |
| 缩放时两轴分别钳位 | 非方图被拉伸 | 比例漂移 <12% |
| 路径引号被当作路径一部分 | 文件明明在却 could not load | 引号/空格被裁剪且不残留 |
| 图片载入失败不回退字形 | 只剩空盘，像"图标变纯白" | 失败必须回退字形 |
| 行 `StringFormat` 少了 `NoWrap` | **第一行文字不在一条直线上**（该段首行上浮） | 超宽文本仍是 1 行；真 GDI+ 断言 |
| 关掉换行后文本不截断 | 文字压到后面的环上 | 每段都落在自己的预算内 |
| 管道 DACL 写成 `CO`（creator owner） | **每个客户端都被拒绝**（`PermissionError` / `ERROR_ACCESS_DENIED`），连启动 mod 的用户自己都连不上 | 用同一个 SD 建管道、让当前用户连上：`test-layout.cpp` 的 "the pipe DACL admits the user running the mod" |
| 提供者名从帧行拷贝时未按长度截断 | 客户端连上、`SET` 被接受，行**永远显示 `--`**；且每次重连占一个新槽位，8 次后 `HELLO` 开始回 403 | 用**带换行**的 `HELLO`/`SET` 驱动 handler，断言 `{ext.provider.key}` 查得到（旧代码下红两条） |
| `CreateNamedPipe` 的 `nMaxInstances` 传 1 而线程数是 4 | 日志写着 `serving 4 pipe instance(s)`，实际 3 个线程以 `CreateNamedPipe failed: 231` 当场退出，只支持 1 个并发客户端 | **没有断言能覆盖**（这是 API 调用点的常量错，宿主里没有真管道）；判别依据是错误码语义——**断言覆盖不到的地方，日志就是防线** |
| 读用 `INFINITE` 等完成事件，完成事件一旦丢失线程就永久停住 | 客户端**连上了**（`ExtLogClient` 有），之后日志**完全静默**——连 `client done` 都没有，行永远 `--` | 等待加 500 ms 上界 + `PeekNamedPipe` 判定；缓冲有数据却没完成就重投。判别日志：`EXT read#1 result=… got=…`、`read completion never arrived with N bytes queued`、`no frame for 2s and the pipe buffer is empty` |
| 收起时先把窗口缩到标签高度、再长回来画（`SetExpanded` 里那次 `PlaceWindow`） | 收起瞬间闪出一条"切片"，极短、难复现；鼠标离开检测区延迟后触发 | **无法断言**（这是分层窗口的帧内顺序，不是数值）。判据是流程：窗口尺寸必须与**同一帧提交的位图**一致；收起方向由绘制侧逐帧跟随内容（`if (expanded) PlaceWindow(hwnd);`） |
| 内联环行的截断预算写成"实测 advance" | 用了 `[[ring:…]]` 的行**永不截断**：文字切在位图外缘（没有 `…`），它自己的环被挤出面板 | `ComputeInlineTextLimit` 的关系断言（run + 后面的环 = 整行） |
| 收回关键帧把**时长比例**当 **cue** 用 | 收缩在半程就完成、后半个时间轴是死区：可见收回只有 `IntroMs/4`（README 写"一半"），且那半程仍以 16 ms 重绘空转。**(2026-10-15 补：当时 `kCollapseDurationRatio` 是 1/2，所以 `IntroMs/4` 是缺陷；该常量现在是 1/4，`IntroMs/4` 就是设计值——这一行是历史记录，别按它去"修"收回时长)** | `the shrink still has work left at the midpoint` |
| GPU 的风扇/功耗/显存总量没有有效性标志 | 非 NVIDIA（PDH 回退）机器上 `{gpu.fan}`=0、`{gpu.power}`=0、`{gpu.vram.total}`=0.0——看着像真读数 | `{gpu.fan}` → `--` 等 5 条 unavailable 断言 |
| 行内展开用 128 字符暂存缓冲 | `{ext.*}` 的值被静默截到 127 字符（设置允许 512，且不带 `…`） | 399 字符的值整段到达行里 |
| 关掉岛不丢相位 | `POPPING`/`COLLAPSING` 冻结 → `IslandPhaseAnimating` 恒真 → tick 永久钉在 16 ms，卡片也关不掉 | `turning the island off drops a phase that was in flight` |
| 发布器没有回收路径 | 关掉 `External data`、或删掉最后一行 `{ext.*}` 后，mod 启动的发布器继续跑（读内存/联网）直到卸载 | **没有断言**（真 `CreateProcess`）；判据是日志 `nothing consumes {ext.*} any more; stopping it` |
| `PlanDownscale` 的比例钳位有不动点 | 短边已小于目标箱的宽扁图（横幅）**一次都不折半**：十几次 1:1 空转之后一次性压 173 倍——正是要修的锯齿形态 | `no stage reduces the long edge by more than 2x`、`the short side is the exact fit, rounded up by at most a pixel` |
| 图片-only 槽只在"全工程都没有字形"时才借字形 | 混合配置（第 1 行图片、第 2 行 Network）里图片载入失败仍是**空盘**，看着像"图标变纯白" | `an image-only row falls back to the configured glyph`、`with no glyph anywhere an image-only row falls back to Memory` |
| `Row N icon = None` 与"没设置"无法区分 | 设了 `None` 的行**照样得到继承来的字形**（实测 `Icon1=Memory`+`Icon3=None` → 第 3 行是 Memory），README 的"设成 None 就不要图标"不成立 | `an explicit None leaves that row without a glyph`、`an unconfigured row inherits the first real glyph, not a None` |
| 全局 `IconPath` 从未被读取（死设置项） | 界面上有"Custom icon file"，填了没有任何效果 | `the global icon applies to a row that has no file of its own`、`a global icon on its own still means plates are drawn` |
| 多行用同一张图时逐行解码 | 全局图片在大图上会被解码 N 次（峰值内存按行数翻倍） | `every row shares one cache slot, so the file is decoded once`、`a row that names the global file shares row 1's slot` |
| 合并窗口吞掉同一标题的升级 | 同标题从 warn 变 critical 时卡片仍停在 warn，升级永远看不见 | `an escalation inside the merge window raises the queued level`、`so a card already on screen stands down for it` |
| 关闭按钮比卡片还高 | 单行卡片（18 px）下按钮被下钳位顶出卡片上沿（y = card.y − 4）→ 卡片外多出一个可点区域 | `the close button fits inside a card of any height`、`and it shrinks to the card when the card is smaller` |
| 淡出期间点击 × 落到面板行为上 | 卡片正在淡出时再点 × → 面板被切换（看着像"× 关掉了面板"）；点卡片本身同样如此 | `and during the fade it is still swallowed, not handed to the panel`、`a click on the card is swallowed, never the panel's` |
| `FitTextToWidth` 按进程缓存省略号宽度 | 改字号后被截断的行比预算宽出省略号的差值（实测 24 px 下某个预算超 6.1 px），文字压到后面的环上 | `a trimmed run fits its budget under any second font size`（扫 40..200 全部预算） |
| sampler 的 join 超时被忽略 | 超时后仍 `DeleteCriticalSection(&g_valuesLock)`：另一条线程可能还握着它，未定义行为 | `a sampler that did not stop leaves its lock alone`、`and so does a failed wait` |
| 自定义图片不参与入场缩放 | 图片只淡入、不随盘子长大（内置字形那条路径一直会） | `and follows the intro as it scales the plate in`、`while the intro still scales it down` |
| PDH 的 GPU 数组读只认 `ERROR_SUCCESS` | 计数器刚建好时的第一个采样（`NEW_DATA`）被丢掉 → GPU 读数晚一拍 | `and so is a counter's first sample (new data)` |
| 对齐组里最后一个字符不显示 / 位置偏一点 | 组里一个字符时"偏离原位"、多个字符时**最后一个不画**（中英文皆然），末尾加一个空格就好 | 组宽度与 layout 框曾用"笔的推进"（`MeasureAdvance`），而 GDI+ 要的是**墨迹范围**：15 px `W` 笔进 14.43 / 报 19.43；`WWW` 放进 43.29 的框只出**两个字**。GDI+ **丢弃**放不下的字形。断言 `a run's layout box is at least as wide as the text GDI+ draws in it`、`the group draws every character between its tags, at every font size and both scripts`、`and a centred group is centred on its own text`（10..30 px 全扫，含用户原串） |
| 行时间表缓存"行数必须相等"把表判成永久过期 | 真机 `-ProbeStore` 报 `table=stale`，歌词不动：每拍都判过期 → 每拍都重读 → 选行缓存被反复清掉 | 缓存行数与 store 行数来自**两次不同的读**，比相等就永远不成立。现在 id 是"这份表属于谁"的判据、行数是"要不要再读一次"的触发器（这个行数没读过才读，读过就用；行数再变再读一次）。断言：`时间表缓存：行数对不上 → 判 stale(Lines)，重读一拍后就 fresh（不再永远过期）`、`…只换曲目 id → …`、`…两个切片互相追…最多两拍收敛`（照主循环跑状态机），探针点名 `stale(Track/Lines/Empty)` |
| bridge 的 `$client.Connect(3000)` 无保护 | 瞬时连不上变成未处理异常、脚本当场死掉，mod 看门狗再拉起它——重启预算烧在"再等一秒就能连上"上 | 自检 `连不上管道时返回空值（不抛异常）`；对照（裸 Connect）实测未捕获异常 + exit 1 |
| bridge 用 `Process.MainWindowTitle` 取曲名 | **开着「桌面歌词」时 `MainWindowTitle` 就是「桌面歌词」**（真机实测 2026-10-14：`Get-Process -Id 15824` → `cloudmusic / 桌面歌词`，而同一 pid 另一个顶层窗口才是「下等马 - 洛天依…」）→ 标题里永远没有「 - 」→ 桥一直停在"没有可用窗口"的等待循环里、**从不连接**，面板 title/lyric 两行永远 `--` | 自检 `曲目标题：桌面歌词窗口不算曲目`、`曲目标题：从多窗口里挑出带「 - 」的那个`、`曲目标题：多个候选取最长的`、`窗口枚举：网易云在跑时能看到它的顶层窗口`；活体输出 `网易云顶层窗口（2 个）：[桌面歌词] [下等马 - …]` |
| 推送把采样器整个叫醒 | **系统信息刷新跟着发布器节奏变快**（与配置的刷新间隔无关，NVML 也被拖到同一频率）；重复的推送值与没变的采样还会白重绘 | `SamplerWakeIsLight` 的 5 条策略断言、`a pushed value costs no system sample`（对照 `while a scheduled sample does re-read the counters`）、`publishing the same values again is not (so it is not repainted)`；现场证据是 `[perf]` 的 `samples=` / `republish=` |
| 发布器重启预算按"累计"计 | 5 次启动后**永久**不再拉起（日志 `gave up after 5 restarts`）：曲名与歌词停止更新，只有重载 mod 能救 | `PublisherEarnedBackRestarts` 的 5 条边界断言（61/60/59 s、已死、从未启动） |
| 卡片把"值的寿命"当"展示时长"用 | 桥把 `-TtlMs`（值的寿命，600 000 ms）当 `EVENT` 的 ttl 发出、mod 照收：一张卡占住岛十分钟，8 格队列堵几十分钟，**密集歌词看起来像坏了** | `IslandCardDuration` 的 4 条边界断言 + 那组 `StepIsland`（10 分钟 ttl 的卡片必须过了 `kIslandMaxCardMs` 就收） |
| 队列只有深度上限、没有时效 | 歌词每秒一行时队列里 8 条**都是过期歌词**，岛在讲一分钟前那句 | `IslandDropStale`：9 s 的 info 留、11 s 的 info 丢、11 s 的 critical 留（`IslandStaleBoundMs`）、41 s 的 critical 也丢 |
| 画不出来的 `[[ring:...]]` 被"消费掉但不产出" | 写错的环连同它周围的读数一起从屏幕上消失——而消失的那几个字符正是唯一能解释"为什么没有环"的线索（`ParseRing` 的注释一直写的是"调用方回退到原文"，代码却没有回退） | `T=[[ring:nonsense]]C` 必须原样显示（`expanded text, spaces preserved`） |
| 未注册的 `[[font:...]]` 标签同样被消费掉 | 族名写错/没装的标签会把它包住的文字起点一起吞掉，屏幕上留下一段没有提示的错位文本 | 六个标签容错用例：`[[font:]]`、`[[fonts:…]]`、缺 `]]`、未安装族名各留下原文，且每行"可见字符都归属于某个段" |
| `AcquireRowFont` 在有**空槽**时仍驱逐 LRU | 两个字号交替绘制时每帧重建字体对象——缓存等于没有，正是它要避免的开销（2026-10-14 由新断言 `a different size is a different, live object` 抓到，未发布） | `AcquireRowFont` 的空槽优先 + `releasing the caches really empties them` |
| 留空的行让后面的**图标**整体错位 | `Row 2` 留空时 `Icon3` 落到第二个显示出来的行（= `Row 3`）上，`Row 3` 以下每个图标都取自上一行的设置；README 的"`Row N icon` 配 `Row N`"因此不是真的（2026-10-14 加 `rowSlotOf[]` 时发现，未发布） | `the second drawn row (settings Row3) gets Icon3, not Icon2`、`Row3's metrics land on the second drawn row` |
| 修上一个错位时把**空行的图标槽**一起丢了 | 只设 `Icon1 = Memory`（`Row 1` 留空）时，继承源"第一个已设的图标"随槽位一起消失 → **整块面板没有图标**，真机上看起来就是"Row icon 选项失效"（2026-10-14 用户报，宿主断言复现：`styles = 0, 0`） | `an icon set on an empty row still covers the rows that are drawn`、`so the text column still leaves room for a plate`；图标槽始终按设置行号存（`IconSlotForRow`） |
| 桥对同一个 key 有两个发布者 | 标题在"歌名"（store 的 `resourceName`，**没有作者**）与"歌名 - 歌手"之间闪；歌词隔一拍跳回 store 那个**会冻结**的 `playingLyricLineNumber` 行（真机 DryRun 实测：两条 `SET title` 一轮） | `Get-PublishPlan` 的 6 条自检（有序字典 = 重复 key 不可能）+ `Publish` 的运行期点名 |
| 不读回复的客户端让服务端每帧白等 500 ms | out 缓冲约 1300 条回复，按每秒 4 帧算**五六分钟**后写回复开始超时；管道线程跟不上，积压的值一拍一拍地应用——**上面那条缺陷因此才看得见**（"播放一段时间后作者开始闪、歌词对不准位置"） | `ExtReplyTimeoutsAfter` 的 7 条断言（2 次连续超时才放弃、成功即清零、硬失败不算） |
| 对齐组被**它前面的文字**截掉内容 | 行里在组之前先有一段长读数时，`[[center]]ABC[[/center]]` 只画出 `AB`（再长一个字母整组消失），而组明明落在面板正中、位置完全正确：相位 A 拿**行流游标**当组的预算，而组是相位 B 才整体定位的；相位 B/C 只会**缩短** run，于是这次截断永久生效（2026-10-15 用户报"位置标签把中间内容的最后一个字吞了"，宿主复现：26 个 `W` + `[[center]]ABC[[/center]]` → `"AB"`） | `a group's content survives a long flow run before it (all three alignments)`、`and the group's width counts the characters the flow run was stealing`（前缀 0..40 个 `W` × 三种对齐；负对照：预算换回 `regionCursor` → 6 条红；组宽少算最后一段 → 同名断言红） |
| bridge：**冻结的 store 行时间被当成播放位置** | `storeline` 这一级取的是 `playingLyricLineNumber` 那一行的起始时间（实测冻结在第 10 行 → 常数 29.5 s），却排在 memory/media/推算时钟**之前**——它是唯一不随时间前进的位置来源，于是迷你条 DOM 那一拍读不到时位置被钉死、歌词永远停在同一行（2026-10-15 用户报"手动切歌后歌词不动"）。**静态可证**：用"第 k 行的起始时间"去选行，选回来的必然是第 k 行（同义反复，一步也推不动）；memory 那条来源早有同性质守卫（`Test-PosAfterTrackChange`），storeline 没有 | 自检 4 条：`store 行时间：冻结不动 = 不可用（歌词不会被它钉住）`（负对照：改成恒 true → 红 2 条）等；判据函数 `Test-StoreLineUsable` |
| bridge：**行时间表只用曲目 id 校验** | id 来自 `playing` 切片、时间表来自 `async:lyric` 切片，同一个 state 里各自更新；换歌那一拍（`135c069` 让"换歌当拍就重读"）可能读到"新 id + 旧表"，缓存之后永不重读 → 选行按旧表走到头被钳位 → 歌词停住。**这一条是"洞"而不是已复现的活缺陷**：它的触发依赖"客户端先更新 playing、后更新歌词数组"这个未在本机证实的时序 | 自检 6 条：`时间表缓存：曲目 id 相同但行数变了 = 过期（表不是现在这份数组的）`（负对照：去掉行数判据 → 红）、`时间表缓存：清空后立刻判为过期（换歌当拍就重读）`（否掉"清了却没人读"那条猜想）、空表/未知行数两条边界 |
| bridge：`-ProbeStore` 看不出"歌词为什么不动" | 探针只打迷你条 DOM 的位置与 `byPosition`，不打**主循环这一拍会用的位置来源**、也不打**表还配不配得上现在这份数组** → 三种成因（DOM 读不到 / 表过期 / 取行失败）在输出上长得一样 | 探针每轮新增 `origin=`（主循环会用的来源，判据与主循环同一对纯函数）、`lineMs=`（store 行时间）、`table=fresh/stale`；三列的读法写进 `_netease-bridge.md` |

---

## 14. 行管线的两次结构性改造（2026-10-14）

两条与"界面缺陷"无关、但会反复咬人的经验，来自"每帧不再重排"与"标记编译到设置期"这两步。

### 14.1 宿主栈溢出会伪装成一次干净运行

`ModSettings` 现在约 35 KB（行文本 + 图标配置 + 编译出来的部件程序 4 份）。宿主
`test-layout.cpp` 的 `main` 里曾有 ~30 个块作用域的 `ModSettings` 局部量，帧 1.26 MB，
超过这个工具链给宿主的 1 MB 栈 → `STATUS_STACK_OVERFLOW`，**进程直接死、一行输出都没有**。
只 `grep FAIL` 的话，这和"全部通过"长得一模一样。

- 防范：宿主的夹具改成**命名空间作用域**的暂存对象（`main` 帧现在 169 KB）；**不要**用块作用域的
  `static`——那会引入 `__cxa_guard_*`，于是 `libc++.dll` 变成导入依赖，而这个环境 PATH 里没有它
  （实测 `0xC0000135`）。
- 判据：宿主"没有输出"永远当作**失败**查，不要当作通过。这条与 §3 那条"旧 exe 冒名成功"是同一类。

### 14.2 旧扫描的行为比文档写得更细：环解析失败不是"整段退回文字"

把标记解析搬到设置期时，一度按"平表 + 失败即原文"设计。差分台架（旧实现 vs 新管线，
288 000 组随机输入 × 8 种输出上限 × 3 种环上限 × 4 个种子，**零差异**）证明真实行为是：

- 环解析**失败**后，扫描从标签自己的 `[` 之后一个字符继续，因此**嵌在里面的标签仍会被认出**
  （`[[ring:a[[/size]]]]`、`[[ring:bad[[font:Arial]]…`）——元素因此是**图**（`next` / `failNext`），
  不是平表；4 431 组输入走的就是这条路。
- 缓冲区边界的语义也是逐字照抄的：`[[size:NN]]` 的标记比其它标记多需要一个备用字符
  （`written + 5` vs `+4`），正好填满那一刻旧代码把尺寸标签当普通文字输出。

判据：**结构性重构的验收标准是"逐字节等价"，而等价性要靠差分台架证明，不是靠通读代码。**
台架要自己验证它编译的确实是"改造前"与"改造后"两份源码（用 SHA-256 对比产物），否则它证明的
可能只是自己。

### 14.3 用 shell 文本处理"清理"源文件，把 8935 行清成了 0 字节

第 5 步中途，一次 PowerShell 的过滤式"整理"把 `endfield-info-windhawk.wh.cpp` 写成 0 字节；
靠 `git checkout` 取回上一版、再用 `edit` 工具逐条重做才恢复，代价很大（而且当时工作区里
还有未提交的改动，差一点就丢）。**产品源文件只允许用 `edit` / `write` 工具改**；
PowerShell 只用来读、测量、跑门禁。判据：任何把源文件当管道目标或 `Set-Content`/`Out-File`
目标的命令都视为错误；要批量替换就写脚本、但**先输出 diff 或副本**再落盘。

### 14.4 标记的位数就是它的上限；算错了先怀疑自己的算式

`[[gap:…]]` 用**四位十进制**把像素写进标记（百分之一像素），所以它能表达的最大值是
99.99 px。实现时先用了 512 px 的上限，于是 `(51200 / 1000) % 10` 取到的是**第二位**数字，
100 px 以上的 gap 前两位被换位 —— 一个看起来像"编译器把 `%` 算错了"的症状（它两次这么怀疑
工具链，实际两次都是自己的上限与编码不匹配）。修法是把上限与 `kGapMarkerMaxValue` 变成
**同一个数**，并让测试通过一个 helper（`GapMarkerHundredths`）**读回**标记，
而不是在断言里手写数字串。判据：凡是"把数值编码进定长标记"的地方，
编码能表达的**范围**与调用方钳位的范围必须是同一个常量。

---

## 15. 内容自适应面板宽度（2026-10-15）

新增设置项 `Panel sizing`（`Fixed` 默认 / `Fit content`）、`Panel minimum width`（200）、
`Panel maximum width`（1600）；`Panel width` 保留原义（Fixed 的宽度，Fit content 下的测量基准）。
设计与全部规则、退化情形、断言清单见 [panel-width-design.md](panel-width-design.md)。

**这一次新增的东西里没有新的"屏幕缺陷"**（功能是新加的，不是修旧 bug），但**留下两条方法论记录**：

| 内容 | 依据 / 断言 |
| :-- | :-- |
| **恒真断言又出现了一次（第五处）**：帧内宽度断言在 `ComputeGeometry()` 之后把解析宽度**重新写回** `g_panelWidth`（本意是"恢复"），于是无论产品做什么，后面比的都是测试自己刚写进去的值——负对照（让绘制路径读配置宽度）**实测照样全绿**。修法是两半：产品侧 `ComputeGeometry` 只在**未设置**时播种（`if (g_panelWidth <= 0)`，它会在展开/收起/改设置时被调到）；测试侧按产品的顺序走一遍、中间不补写 | `ComputeGeometry leaves a resolved width alone (it only seeds an unset one)`；负对照实测 3 条红（另两条：`the window and the bitmap agree, and the width is the resolved one`、`which is not the configured Panel width (900)`）。与 §7.13 / 第七批那条"看着在守，其实没有"是同一类 |
| **"每帧重算"与"逐帧变化"是两件事**：去掉 `EnsurePanelWidth` 的缓存（每帧重算）只红一条（`and the next two frames resolve it zero more times`）——宽度**仍然是同一个数**，因为它本来就是那个状态的纯函数。要红到 `so all three frames of that state have the same width` / `and the same state still produces the same width`，必须让宽度真的**随帧变化**（负对照把解析次数掺进宽度：实测 3 条红）。判据：断言要指向**要防的那个性质**（变化），而不是指向一个恰好也能防住它的代理（重算） |
| **只写不读的文件级 static 是 `_deadcode.py` 的盲区**：该工具只扫**结构体字段**（`x.field` / `x->field`），不看文件作用域的 `static` 变量。这次带了两个进来：`g_panelWidthClamp`（写两次、一次没读）与 `g_lastBitmapWidth`（只写）。处理：删掉前者，把 clamp 记进解析点的日志（`Panel width: fit content=… clamp=…`，判别性数值）；后者保留并由宿主真读一次（`and the window rectangle is the width of the bitmap being submitted`）。**判据：新增文件级 static 时自己数一遍读点**，别指望门禁报出来 |

**验证**：宿主 **1059 检查**、`PASSED (0 failure(s), 1 skipped)`（跳过的仍是受限沙箱里的管道 DACL）；
`build.ps1` 1 条已知警告、DLL **201,216 B**；`_settings.py` / `_logfmt.py` / `_deadcode.py` 全绿；
`.data` raw **1024 B**（与父提交 `fdf5806` 的 196,608 B / `.data` 1024 B 相比，宽度这次改动
没有把任何静态数组拖进文件镜像——§5.1 那条"非零默认值"的判据继续成立）。