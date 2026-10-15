<#
.SYNOPSIS
    示例：把网易云音乐正在播放的曲名与歌词推进 endfield-info-windhawk 的命名管道。

.DESCRIPTION
    这是一个**示例程序**，不是 mod 的一部分：它演示外部程序怎么用 EVENT/SET 把
    "现在在听什么"送到面板与灵动岛上。

    它怎么拿到曲目与歌词（2026-10-15 重写）：**只读客户端自己的黑胶页**（CDP 调试页面）——
        · 当前行 = `li.line.current`（客户端自己维护的那一行，原文在第一个 `<p>`、
          翻译在第二个；拖动进度条、暂停、切歌天然正确）；
        · 位置   = 那个元素上的 `data-time`（秒，十分之一秒精度）；
        · 曲名   = `span.title` 的 `title` 属性；歌手同法。
    实测（见 docs/netease-lyric-source-design.md）：**这些字段与前台上有没有窗口无关**——
    把窗口关到托盘、进程只剩后台在播时，页面照旧带着当前行与 data-time。
    早先"关掉窗口后歌词和曲名一起没了"的原因不在 CDP，而在**曲名是从顶层窗口标题里读的**：
    窗口一关就一个可见窗口都没有，`Get-PlayingTrack` 返回空，主循环整段被跳过。
    所以现在**不再读窗口标题为主**（只在完全没有调试端口时当最后的兜底）。

    它不做的事：**不注入、不挂钩、不读写客户端内存**。旧版有过三条别的路（内存差分扫描、
    SMTC 媒体会话、接口 LRC + 推算时钟），它们都只为了"在拿不到 CDP 时猜一个位置"；
    现在位置由客户端自己给，那三条已删除（脚本 `_netease-position.ps1` /
    `_netease-lyric-scan.ps1` 随之退役）。

    它怎么发：
      只用协议 v1 的三条：`HELLO 1 <provider>` 握手，`SET <p> <key> <ttl|-> <text>`
      推值，`EVENT <p> <severity> <ttl|-> <标题> <正文...>` 让岛上弹卡片。
      **只在内容变化时才写**：一秒一次地重复同一句歌词是没有意义的流量，
      管道的限速会把它丢掉，但更好的做法是根本不发。

.EXAMPLE
    # 先看不发：把要发的帧打印出来
    pwsh -File _netease-bridge.ps1 -DryRun -Once

    # 真发：曲名进 {ext.netease.title}，当前歌词行进 {ext.netease.lyric}
    pwsh -File _netease-bridge.ps1 -Ttl 600000

    # 顺带在换歌时弹一次岛（正文就是第一句歌词）
    pwsh -File _netease-bridge.ps1 -Event -Ttl 600000

.EXAMPLE
    # 不连管道、不读页面也能验证纯函数（候选表顺序、段切分、新鲜度判定）
    pwsh -File _netease-bridge.ps1 -SelfTest
#>
[CmdletBinding()]
param(
    # 面板里用 {ext.<provider>.title} / {ext.<provider>.lyric} 取值
    [string]$Provider = 'netease',
    [string]$Pipe = 'endfield-info-windhawk',
    [int]$IntervalMs = 1000,
    # 推过去的值的寿命：歌一直在播，所以给得长一点，避免行里闪 --
    [int]$TtlMs = 600000,
    # 进程名匹配（网易云 PC 版：cloudmusic）。**只在没有调试端口时**用于最后的标题兜底。
    [string]$ProcessName = 'cloudmusic',
    # 换歌时发一条 EVENT，让灵动岛弹一次
    [switch]$Event,
    # 卡片在屏时长（毫秒）。**这不是值的寿命**：值用 -TtlMs（默认 10 分钟），事件用它。
    # 早先两处都发 $TtlMs，于是每张卡片在屏幕上待 10 分钟：队列只有 8 格，两张卡就能把
    # 岛堵住几十分钟，歌词密集的歌看起来就像坏了。
    [int]$EventTtlMs = 6000,
    # 两条岛事件之间的最小间隔（毫秒）：歌词很密时不许刷屏。
    [int]$EventMinIntervalMs = 4000,
    # 只打印要发的帧，不连管道
    [switch]$DryRun,
    # 离线自检：候选表顺序 + 段切分 + data-time 解析 + 新鲜度判定 + 帧格式
    [switch]$SelfTest,
    # 跑一轮就退出
    [switch]$Once,
    # 只探一次 CDP：列出端口与调试目标（托盘模式也照列）
    [switch]$ProbeCdp,
    # 依次尝试的 CDP 端口（两个成熟项目分别用 9229 / 9223，9222 是通用默认）
    [int[]]$CdpPorts = @(9229, 9223, 9222),
    # 歌词行的"负延迟"：把**选行用的位置**提早这么多毫秒（默认 -1 = 自动）。
    # 位置与歌词行同出于黑胶页，所以这里只有一个系统性滞后：桥每 IntervalMs 才轮询一次
    # （平均落后半个周期）。`data-time` 是**十分之一秒**，没有"分:秒"那种取整偏差，
    # 所以 auto = 半个周期（不再额外补 500 ms）。0 = 关掉；给正数就用自己的值。
    [int]$LeadMs = -1,
    # 只读诊断：黑胶页的候选命中、data-time 新鲜度判定、与 store 的身份是否一致。
    # **托盘模式（前台窗口已关）唯一一条能说清"歌词为什么没了"的探针**。
    [switch]$ProbeVinyl,
    # 只读诊断：把黑胶页的当前行与 store 自己报的行并排打出来（核对两者是否同一行）
    [switch]$ProbeStore,
    # 探针轮数
    [int]$ProbeRounds = 10
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# 曲目身份：黑胶页 → store → 顶层窗口标题（兜底）
#
# 身份有什么用：`title` 要发出去；换歌检测与缓存失效也要一个键。**早先它只有一条路
# ——顶层窗口标题**，而那正是"关掉窗口后曲名和歌词一起没了"的成因：窗口一关，真机实测
# 一个 `IsWindowVisible=1` 的网易云窗口都没有了，`Get-PlayingTrack` 返回空，主循环
# 里 `if ($now) { … }` 整段被跳过。现在顺序反过来：
#
#   ① 黑胶页 `span.title` 的 `title` 属性（托盘模式也有 —— 它不依赖窗口）
#   ② store 的 `playing.resourceName` + `resourceArtists[].name`
#   ③ 顶层窗口标题（**只在完全没有调试端口时**才可能命中；保留它是因为那是"没开 CDP"
#      的机器上唯一能拿到歌手的地方）
#
# ②的歌手列实测是 `Mili, 塞壬唱片-MSR`，用 ' / ' 拼起来与①的属性值、与窗口标题里的
# 歌手**逐字一致**（真机 `title="Mili / 塞壬唱片-MSR"`，窗口标题 `铁花飞 - Mili/塞壬唱片-MSR`
# 只差一个空格）——所以换来源不会让 `title` 那一行抖动。
# ---------------------------------------------------------------------------

# 纯函数：把歌手列表拼成发布的形态（纯函数，-SelfTest 覆盖）。' / ' 与客户端自己的一致。
function Join-ArtistNames {
    param([string[]]$Names)
    if (-not $Names) { return '' }
    $ok = @()
    foreach ($n in $Names) {
        if ($null -eq $n) { continue }
        $t = ('' + $n).Trim()
        if ($t) { $ok += $t }
    }
    return ($ok -join ' / ')
}

# 纯函数：身份最终取值与"它来自哪一条来源"（-SelfTest 覆盖）。
# 参数按优先级传入；空串 = 这一来源没有值。返回 Title / Artist / Source，三个都不缺才可用。
function Resolve-TrackIdentity {
    param([string]$VinylTitle, [string]$VinylArtist,
          [string]$StoreTitle, [string]$StoreArtist,
          [string]$WindowTitle)
    if ($VinylTitle) {
        return [pscustomobject]@{ Title = $VinylTitle; Artist = $VinylArtist; Source = 'vinyl' }
    }
    if ($StoreTitle) {
        return [pscustomobject]@{ Title = $StoreTitle; Artist = $StoreArtist; Source = 'store' }
    }
    if ($WindowTitle) {
        $sep = $WindowTitle.LastIndexOf(' - ')
        if ($sep -ge 1) {
            return [pscustomobject]@{
                Title  = $WindowTitle.Substring(0, $sep).Trim()
                Artist = $WindowTitle.Substring($sep + 3).Trim()
                Source = 'window'
            }
        }
        return [pscustomobject]@{ Title = $WindowTitle.Trim(); Artist = ''; Source = 'window' }
    }
    return $null
}

# 兜底③用的窗口枚举（**只在没有调试端口时才可能被用到**）。保留的理由与代价见上面的注释：
# 没开 CDP 的机器上，歌手只有窗口标题里有。
#
# 按**窗口所属进程名**判定，而不是"先列出所有进程、再筛 pid"：进程**列表**在受限环境里
# 可能只给一部分（实测：沙箱里 `Get-Process` 只返回 22 个进程，而按 pid 单查是可靠的）。
if (-not ('Endfield.Win32' -as [type])) {
    Add-Type -Namespace Endfield -Name Win32 -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, System.IntPtr lParam);
public delegate bool EnumWindowsProc(System.IntPtr hWnd, System.IntPtr lParam);
[DllImport("user32.dll")] public static extern int GetWindowTextLength(System.IntPtr hWnd);
[DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] public static extern int GetWindowText(System.IntPtr hWnd, System.Text.StringBuilder s, int n);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(System.IntPtr hWnd);
[DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(System.IntPtr hWnd, out int pid);
'@
}

# 纯函数：从一组窗口标题里挑出曲目标题。没有「 - 」就没有曲目（没在播时标题是产品名，
# 桌面歌词窗口的标题也是产品名之外的东西）。多个候选取**最长**的——完整标题比截断的长。
# **注意它的可见性边界**：它只看得见**可见**的顶层窗口，所以托盘模式下它必然返回空；
# 它现在只服务兜底③（见 `Get-WindowTrackTitle` 的注释）。
function Select-SongTitle {
    param([string[]]$Titles)
    $best = $null
    foreach ($t in $Titles) {
        if (-not $t) { continue }
        $t = $t.Trim()
        if ($t.IndexOf(' - ') -lt 1) { continue }
        if (-not $best -or $t.Length -gt $best.Length) { $best = $t }
    }
    return $best
}

# 该进程（名字匹配 `*$ProcessName*`）所有**可见且有标题**的顶层窗口标题。
function Get-NeteaseWindowTitles {
    $titles = New-Object System.Collections.ArrayList
    $ownerNames = @{}
    $cb = [Endfield.Win32+EnumWindowsProc] {
        param($hWnd, $lParam)
        if (-not [Endfield.Win32]::IsWindowVisible($hWnd)) { return $true }
        $len = [Endfield.Win32]::GetWindowTextLength($hWnd)
        if ($len -le 0) { return $true }
        $owner = 0
        [void][Endfield.Win32]::GetWindowThreadProcessId($hWnd, [ref]$owner)
        if (-not $ownerNames.ContainsKey($owner)) {
            $name = ''
            try { $name = (Get-Process -Id $owner -ErrorAction Stop).ProcessName } catch { $name = '' }
            $ownerNames[$owner] = $name
        }
        if ($ownerNames[$owner] -like "*$ProcessName*") {
            $sb = New-Object System.Text.StringBuilder ($len + 2)
            [void][Endfield.Win32]::GetWindowText($hWnd, $sb, $sb.Capacity)
            [void]$titles.Add($sb.ToString())
        }
        return $true
    }
    [void][Endfield.Win32]::EnumWindows($cb, [System.IntPtr]::Zero)
    return $titles.ToArray()
}

# 兜底③：窗口标题里那个「歌名 - 歌手」。**它只在"没有 CDP 或页面读不到"时才有机会命中**，
# 因为托盘模式下没有可见窗口（真机实测：4 个窗口全部 IsWindowVisible=0）。
function Get-WindowTrackTitle {
    $title = Select-SongTitle (Get-NeteaseWindowTitles)
    if ($title) { return $title }
    # 再从主窗口标题试一次（有的客户端/版本里主窗口就是曲目标题）
    $procs = @(Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.ProcessName -like "*$ProcessName*" -and $_.MainWindowTitle })
    if ($procs.Count -gt 0) { return (Select-SongTitle @($procs[0].MainWindowTitle)) }
    return $null
}

# ---------------------------------------------------------------------------
# 管道客户端：协议 v1，一行一帧，只在内容变化时写
# ---------------------------------------------------------------------------
$script:Sent = @{}
# 一轮里发过的 key（诊断用，见 Publish）与它已经报过几次。
$script:IterationKeys = @{}
$script:DuplicateKeys = 0

function Write-Frame {
    param($Writer, [string]$Line)
    if ($DryRun) { Write-Host "  -> $Line"; return }
    $Writer.WriteLine($Line)
    $Writer.Flush()
}

function Connect-PipeClient {
    # 管道只在 mod 需要外部数据时存在，而且 mod 重载时它会消失又回来，所以"连不上"
    # 往往是瞬态的，必须等一等。原来这里是一句裸的 $client.Connect(3000)：瞬时拒绝
    # 变成未处理异常，脚本当场死掉，mod 的看门狗再把它拉起来——重启预算就烧在
    # "再等一秒就能连上"的事情上。现在按窗口重试，等不到返回 $null（由调用方决定
    # 怎么处理），**不抛异常**。
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Pipe,
        [int]$TimeoutMs = 3000,
        [int]$WaitSeconds = 60
    )
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    $announced = $false
    while ($true) {
        $client = $null
        try {
            $client = New-Object System.IO.Pipes.NamedPipeClientStream('.', $Pipe, [System.IO.Pipes.PipeDirection]::InOut)
            $client.Connect($TimeoutMs)
            return $client
        }
        catch {
            if ($client) { $client.Dispose() }
            if ((Get-Date) -ge $deadline) {
                Write-Host ("管道 {0} 等满 {1} 秒仍连不上：{2}" -f $Pipe, $WaitSeconds, $_.Exception.Message)
                return $null
            }
            if (-not $announced) {
                $announced = $true
                Write-Host ("管道 {0} 暂时连不上（mod 可能正在启动或重载），每 2 秒重试" -f $Pipe)
            }
            Start-Sleep -Seconds 2
        }
    }
}

function Publish {
    param($Writer, [string]$Key, [string]$Text)
    # 一轮里同一个 key 被写两次 = 面板会在两个值之间闪。真机踩到过（store 的曲名没有作者，
    # 和最后拼好的"歌名 - 作者"抢同一个 key），所以这里点名；结构上的保证是 Get-PublishPlan
    # 返回有序字典——字典不可能有重复键，两处发布者从此写不出来。
    if ($script:IterationKeys.ContainsKey($Key)) {
        if ($script:DuplicateKeys -lt 8) {
            $script:DuplicateKeys++
            Write-Host ("  ! 同一个 key 在一轮里发了两次：{0}（面板会在两个值之间闪）" -f $Key)
        }
    }
    $script:IterationKeys[$Key] = $true
    if ($script:Sent[$Key] -eq $Text) { return $false }
    $script:Sent[$Key] = $Text
    if ($TtlMs -gt 0) { Write-Frame $Writer "SET $Provider $Key $TtlMs $Text" }
    else { Write-Frame $Writer "SET $Provider $Key - $Text" }
    $true
}

# 一轮要发的帧，key -> 文本。**故意做成返回有序字典的纯函数**：字典不允许重复键，
# 于是"同一个 key 有两个发布者"这件事在结构上不可能发生——那正是真机上"作者忽隐忽现、
# 歌词跳回旧行"的成因（store 的 resourceName 与拼好的标题抢 title；store 那个会冻结的
# 当前行与按位置选出的行抢 lyric）。自检直接断言这个形状。
# 空值不发：lyric/pos 取不到时整条 key 缺席（而不是发空串把上一轮的值盖掉）；
# translation 例外——它总是出现，空串的含义是"这一行没有翻译，清掉上一行的"。
function Get-PublishPlan {
    param([string]$Title, [string]$Artist, [string]$Line, [string]$Translation, [string]$Pos)
    $plan = [ordered]@{}
    $plan['title'] = if ($Artist) { '{0} - {1}' -f $Title, $Artist } else { $Title }
    if ($Line) { $plan['lyric'] = $Line }
    $plan['translation'] = $Translation
    if ($Pos) { $plan['pos'] = $Pos }
    return $plan
}

# 岛事件的节流。歌词密的时候（或窗口标题抖动被误判成"换歌"时），事件会来得比卡片能展示的
# 快得多；mod 侧现在会丢弃过旧的事件，但更该做的是**根本不要发那么多**——每张卡片在屏几秒，
# 事件也每几秒最多一条。纯函数，所以自检能直接钉住这条规则。
function Test-EventAllowed {
    param([datetime]$Now, [datetime]$LastAt, [int]$MinMs)
    if ($MinMs -le 0) { return $true }
    return (($Now - $LastAt).TotalMilliseconds -ge $MinMs)
}

# 发一条岛事件：受最小间隔约束，用的是卡片的在屏时长（$EventTtlMs）而不是值的寿命（$TtlMs）。
function Write-IslandEvent {
    param($Writer, [string]$Provider, [int]$CardMs, [string]$Title, [string]$Body)
    if (-not $Writer) { return $false }
    $now = Get-Date
    if (-not (Test-EventAllowed -Now $now -LastAt $script:LastEventAt -MinMs $EventMinIntervalMs)) {
        Write-Host ("跳过岛事件（距上一条不足 {0} ms）：{1}" -f $EventMinIntervalMs, $Title)
        return $false
    }
    $script:LastEventAt = $now
    Write-Frame $Writer ("EVENT {0} info {1} {2}  {3}" -f $Provider, $CardMs, $Title, $Body)
    return $true
}

# ---------------------------------------------------------------------------
# 位置：**客户端自己给的**，不再有降级链
# ---------------------------------------------------------------------------
# 位置与歌词行同出于一处（黑胶页当前行上的 `data-time`），所以这一节只剩两个纯函数：
# 一个把来源名格式化成面板上的标签，一个算"换行提早多少毫秒"。
#
# **这里曾经是一整条降级链**（迷你条 DOM → store 行时间 → CDP 其它位置 → 内存差分 →
# SMTC 媒体会话 → 推算时钟），三层硬件/进程技巧（读客户端内存、WinRT 媒体会话、按"第一
# 次看到这首歌"推算时钟）只为了在拿不到 CDP 时**猜**一个位置。真机结论是那条链的每一级
# 都比客户端自己给的 `data-time` 差，而且各有各的失效形状（内存地址随 ASLR 变、SMTC
# 根本没注册——`-ProbePosition` 实测 `PROBE none`、推算时钟暂停时照样往前走）。
# 现在全部删除，见 docs/netease-lyric-source-design.md 的"删掉的东西"一节。
function Format-PositionSource {
    param([string]$Source, [int]$PositionMs, [int]$LeadMs = 0)
    # 有位置就必须有名有姓：面板上 `{ext.netease.pos}` 显示的就是这一串。认得出来的来源
    # 照实写，认不出来的写成 `n/a`（**不要**悄悄当成 clock —— 那条来源已经不存在了，
    # 把未知来源显示成"推算时钟"会让人以为还有一条时钟在兜底）。
    $name = switch ($Source) {
        'vinyl' { 'vinyl' }
        'dom' { 'dom' }
        'vinyldt' { 'vinyldt' }
        default { 'n/a' }
    }
    $leadText = if ($LeadMs -gt 0) { " +$($LeadMs)ms" } else { '' }
    # 秒数**截断**到 0.1 s（与客户端自己显示的形态一致）：22.95 s 显示 `22.9s`，
    # 而不是四舍五入成 `23.0s`——`pos` 是给人对时间的，四舍五入会让 `-LeadMs` 的
    # 效果和显示出来的数对不上（22950 + 500 提前量看起来像 23.0 s 的提前，其实不是）。
    $sec = [math]::Floor($PositionMs / 100.0) / 10.0
    return ("{0} {1:0.0}s{2}" -f $name, $sec, $leadText)
}

# 提早换行多少毫秒（纯函数，-SelfTest 覆盖）。**为什么要提早**：桥每 -IntervalMs
# （默认 1000 ms）才轮询一次 → 平均落后半个周期。位置与"当前行"是**同一次读**拿到的
# （都在黑胶页那一个元素上），所以位置本身没有第二条滞后 —— `data-time` 是**十分之一秒**
# （真机 `22.95`），不存在迷你条"分:秒"那种向下取整到秒的偏差，**所以不再额外补 500 ms**。
# 被提早的只是**选行用的位置**：发出去的 `pos` 仍是原始位置并标出提早量
# （`vinyl 22.9s +500ms`），所以"提早了多少、有没有过头"看得见。
# `-LeadMs 0` 关掉，`-LeadMs N` 用自己的值。
function Get-LeadMs {
    param([string]$Source, [int]$IntervalMs, [int]$OverrideMs = -1)
    if ($OverrideMs -ge 0) { return $OverrideMs }
    return [int][math]::Round($IntervalMs / 2.0)
}

# CDP 探测：只列端口与目标，**不读位置**（取值表达式要先在你机器上看清客户端暴露了什么，
# 不猜）。CRD/CDP 的 /json/version 是规范 JSON，所以这里可以用普通 HTTP 客户端（与端口
# 20017 那个"只发头就关"的私有服务不同）。
function Invoke-CdpProbe {
    foreach ($port in $CdpPorts) {
        try {
            $v = Invoke-RestMethod -Uri "http://127.0.0.1:$port/json/version" -TimeoutSec 2 -ErrorAction Stop
            Write-Host ("  ✓ 端口 {0} 是 CDP：{1}" -f $port, ('' + $v.Browser -replace '\s+', ' '))
            try {
                $list = Invoke-RestMethod -Uri "http://127.0.0.1:$port/json/list" -TimeoutSec 2 -ErrorAction Stop
                if ($list) {
                    foreach ($t in $list) {
                        Write-Host ("      target: type={0}  title='{1}'" -f $t.type, $t.title)
                        Write-Host ("              url={0}" -f $t.url)
                    }
                }
                else { Write-Host '      （/json/list 为空）' }
            }
            catch { Write-Host '      （/json/list 读取失败）' }
            return $true
        }
        catch {
            Write-Host ("  · 端口 {0} 无响应（{1}）" -f $port, ('' + $_.Exception.Message -replace '\s+', ' '))
        }
    }
    return $false
}

# 找到第一个带 webSocketDebuggerUrl 的调试目标（CDP 的 /json/list）。
function Get-CdpTab {
    foreach ($port in $CdpPorts) {
        try {
            $list = Invoke-RestMethod -Uri "http://127.0.0.1:$port/json/list" -TimeoutSec 2 -ErrorAction Stop
            foreach ($t in $list) {
                if ($t.webSocketDebuggerUrl) {
                    return [pscustomobject]@{ Port = $port; Url = $t.webSocketDebuggerUrl; Title = $t.title }
                }
            }
        }
        catch { }
    }
    return $null
}

# 在客户端页面里求值一条表达式（只读：只调用 Runtime.evaluate，表达式由我们给定）。
# 全程有上界（连接 4 s、最多读 20 帧）——没有上界的等待会把诊断能力一起吃掉（AGENTS §7.10）。
function Invoke-CdpExpression {
    param([string]$Expression)
    $tab = Get-CdpTab
    if (-not $tab) { return $null }
    $ws = $null
    try {
        $ws = New-Object System.Net.WebSockets.ClientWebSocket
        $cts = New-Object System.Threading.CancellationTokenSource
        $cts.CancelAfter(4000)
        $ws.ConnectAsync([Uri]$tab.Url, $cts.Token).Wait(4000) | Out-Null

        $payload = @{
            id     = 1
            method = 'Runtime.evaluate'
            params = @{ expression = $Expression; returnByValue = $true }
        } | ConvertTo-Json -Depth 6 -Compress
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($payload)
        $seg = New-Object System.ArraySegment[byte] -ArgumentList @(, $bytes)
        $ws.SendAsync($seg, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, $cts.Token).Wait(4000) | Out-Null

        $buf = New-Object byte[] 16384
        for ($i = 0; $i -lt 20; $i++) {
            $recvSeg = New-Object System.ArraySegment[byte] -ArgumentList @(, $buf)
            $r = $ws.ReceiveAsync($recvSeg, $cts.Token)
            $r.Wait(4000) | Out-Null
            $text = [System.Text.Encoding]::UTF8.GetString($buf, 0, $r.Result.Count)
            if ($text -match '"id"\s*:\s*1') { return $text }
        }
        return $null
    }
    catch { return $null }
    finally { if ($ws) { try { $ws.Dispose() } catch { } } }
}

# ---------------------------------------------------------------------------
# store 的歌词数组（**兜底**：黑胶页那一拍没给出当前行时才用）
# ---------------------------------------------------------------------------
# 取**整条时间数组**（秒）—— 只在这条兜底路径上按需调一次。**只搬数字**（2000 行也才约 8 KB），
# 因此不需要假设长度；超出安全上限时**如实上报** truncated，而不是悄悄截断。
# 为什么留着它：黑胶页的选择器一旦在客户端升级后落空（`li.line.current` 换了名字），客户端
# **自己的歌词数组**还在，用它 + `data-time` 选一行比"什么都不发"好；而它选出来的行必然与
# 位置自洽（同一个数组、同一个位置），所以不会引入"冻结值遮住活值"那种形状。
# 准入判据是 `Test-StoreLyricUsable`（store 报的曲目 id 必须与这一轮的身份一致），-SelfTest 覆盖。
function Get-CdpStoreLyricTimes {
    if (-not (Get-CdpTab)) { return $null }
    $expr = @"
(function(){
  try{
    if(!window.__ei_probe_require__){
      webpackJsonp.push([[987661],{987661:function(m,e,require){window.__ei_probe_require__=require;}},[[987661]]]);
    }
    var req=window.__ei_probe_require__, cache=req.c||{}, id=window.__ei_store_id__;
    if(!id){
      for(var k in cache){ try{ var ex=cache[k]&&cache[k].exports; if(!ex) continue;
        var cd=[ex,ex.a,ex.default];
        for(var i=0;i<cd.length;i++){ if(cd[i]&&typeof cd[i].getStore==='function'){ id=k; window.__ei_store_id__=k; break; } }
      }catch(e){} if(id) break; }
    }
    if(!id) return JSON.stringify({ok:false, why:'no store module'});
    var h=[cache[id].exports,cache[id].exports.a,cache[id].exports.default]
            .filter(function(x){return x&&typeof x.getStore==='function';})[0];
    var g=h.getStore(), st=(g&&typeof g.getState==='function')?g.getState():g;
    var p=(st&&st.playing)||{}, a=(st&&st['async:lyric'])||{}, L=a.lyricLines||[];
    var CAP=2000, times=[];
    for(var i=0;i<L.length && i<CAP;i++){
      times.push((L[i]&&typeof L[i].time==='number')?L[i].time:null);   // 允许 null（元信息行）
    }
    return JSON.stringify({ok:true, moduleId:id, trackId:p.resourceTrackId||'',
      name:p.resourceName||'', state:p.playingState, offset:a.offset,
      lineCount:L.length, usedLyric:a.currentUsedLyric, truncated:(L.length>CAP), times:times});
  }catch(x){ return JSON.stringify({ok:false, why:''+x}); }
})()
"@
    $raw = Invoke-CdpExpression -Expression $expr
    if (-not $raw) { return $null }
    $m = [regex]::Match($raw, '"value"\s*:\s*"((?:[^"\\]|\\.)*)"', 'Singleline')
    if (-not $m.Success) { return $null }
    try { return (('"' + $m.Groups[1].Value + '"') | ConvertFrom-Json) | ConvertFrom-Json } catch { return $null }
}

# 按需取**某一行的文本与翻译**（行号变化时才调，很小）。越界返回空，不报错。
function Get-CdpStoreLyricLine {
    param([int]$Index)
    if ($Index -lt 0 -or -not (Get-CdpTab)) { return $null }
    $expr = @"
(function(){
  try{
    var req=window.__ei_probe_require__, cache=(req&&req.c)||{}, id=window.__ei_store_id__;
    if(!id) return JSON.stringify({ok:false, why:'store id not cached yet'});
    var ex=cache[id]&&cache[id].exports;
    var h=[ex,ex&&ex.a,ex&&ex.default].filter(function(x){return x&&typeof x.getStore==='function';})[0];
    var g=h.getStore(), st=(g&&typeof g.getState==='function')?g.getState():g;
    var a=(st&&st['async:lyric'])||{}, L=a.lyricLines||[], T=a.tlyricLines||[], i=$Index;
    var t=(i<L.length)?L[i]:null, q=(i<T.length)?T[i]:null;
    return JSON.stringify({ok:true, index:i, lines:L.length, trans:T.length,
      text:(t?(''+(t.lyric||t.text||'')).slice(0,200):''),
      trans:(q?(''+(q.lyric||q.text||'')).slice(0,200):'')});
  }catch(x){ return JSON.stringify({ok:false, why:''+x}); }
})()
"@
    $raw = Invoke-CdpExpression -Expression $expr
    if (-not $raw) { return $null }
    $m = [regex]::Match($raw, '"value"\s*:\s*"((?:[^"\\]|\\.)*)"', 'Singleline')
    if (-not $m.Success) { return $null }
    try { return (('"' + $m.Groups[1].Value + '"') | ConvertFrom-Json) | ConvertFrom-Json } catch { return $null }
}

# 纯函数：在 times（**秒**，注意单位）上按位置（**毫秒**）选行；返回索引，-1 = 无法判定。
# **不假设长度**（同一客户端上实测见过 64 行与 67 行）；跳过 null 时间戳；越界钳位到末行。
# ⚠ 参数必须是 object[]：若写成 double[]，JSON 里的 null 会被静默转成 0，于是那一行会匹配任何位置（断言抓到过）。
function Select-StoreLineIndex {
    param([object[]]$TimesSec, [int]$PositionMs)   # object[] 而非 double[]：JSON 的 null 不能被转成 0
    if (-not $TimesSec -or $TimesSec.Count -eq 0) { return -1 }
    $idx = -1
    for ($i = 0; $i -lt $TimesSec.Count; $i++) {
        $v = $TimesSec[$i]
        if ($null -eq $v) { continue }
        if ([double]$v * 1000.0 -le $PositionMs) { $idx = $i } else { break }
    }
    if ($idx -lt 0) { return 0 }
    return $idx
}

# ---------------------------------------------------------------------------
# CDP store：**只提供身份**（曲名 / 歌手 / 曲目 id / 播放状态）
# ---------------------------------------------------------------------------
# 访问方式照 cloudmusic-desktop-mcp 的做法（server.py:283-287）：往 webpackJsonp 塞一个模块，
# 把 webpack 的 `require` 存到 window 上，再 `getStore()`（**不调 getState** ✓，见 server.py:339-346）。
#
# **规则化的探查**（用户要求保留）：模块 id **随客户端版本变** ✗（这台是 11，MCP 钉的是 12），
# 所以这里**不写死 id** ✓ —— 运行期扫 webpack 的**缓存** `req.c`（只含已实例化模块 ✓，**不执行任何模块** ✗）
# 找导出里带 `getStore` 的那个 ✓，并把结果缓存在页面上（`window.__ei_store_id__` ✓），
# 客户端升级后若失效会自己再找一次 ✓。
#
# 它**不再提供当前行与位置**：那两样现在都来自黑胶页（见下面那一节）。这里剩下的字段是
# 身份与判活：`resourceName` / `resourceArtists[].name`（歌手列实测是 `Mili, 塞壬唱片-MSR`，
# 用 ' / ' 拼起来与黑胶页 `title="Mili / 塞壬唱片-MSR"` 逐字一致）/ `resourceTrackId` /
# `playId`（换歌会变）/ `playingState`（2 = 播放中，真机更正见 docs/state-and-handover.md）。
# 单位陷阱（同一个 state 里三种单位 ✗）：`curTrack.duration` 是**毫秒** ✓、
# `playing.resourceDuration` 是**秒** ✓ —— 这里只搬 durationMs，别混。
function Get-CdpStoreState {
    if (-not (Get-CdpTab)) { return $null }
    $expr = @'
(function(){
  try{
    if(!window.__ei_probe_require__){
      webpackJsonp.push([[987661],{987661:function(m,e,require){window.__ei_probe_require__=require;}},[[987661]]]);
    }
    var req=window.__ei_probe_require__, cache=req.c||{}, id=window.__ei_store_id__;
    if(!id){
      for(var k in cache){
        try{
          var ex=cache[k]&&cache[k].exports; if(!ex) continue;
          var cd=[ex,ex.a,ex.default];
          for(var i=0;i<cd.length;i++){ if(cd[i]&&typeof cd[i].getStore==='function'){ id=k; window.__ei_store_id__=k; break; } }
        }catch(e){}
        if(id) break;
      }
    }
    if(!id) return JSON.stringify({ok:false,why:'no store module in webpack cache'});
    var h=[cache[id].exports,cache[id].exports.a,cache[id].exports.default].filter(function(x){return x&&typeof x.getStore==='function';})[0];
    var g=h.getStore(), st=(g&&typeof g.getState==='function')?g.getState():g;
    var p=(st&&st.playing)||{}, a=(st&&st['async:lyric'])||{};
    var A=p.resourceArtists||[];
    var names=[];
    for(var j=0;j<A.length;j++){ var n=A[j]&&A[j].name; if(n) names.push(''+n); }
    return JSON.stringify({ok:true,moduleId:id,name:p.resourceName||'',trackId:p.resourceTrackId||'',
      playId:p.playId||'',artists:names.join(' / '),
      state:p.playingState,durationMs:(p.curTrack&&p.curTrack.duration)||0,
      lines:(a.lyricLines||[]).length,tlines:(a.tlyricLines||[]).length});
  }catch(x){ return JSON.stringify({ok:false,why:''+x}); }
})()
'@
    $raw = Invoke-CdpExpression -Expression $expr
    if (-not $raw) { return $null }
    $m = [regex]::Match($raw, '"value"\s*:\s*"((?:[^"\\]|\\.)*)"', 'Singleline')
    if (-not $m.Success) { return $null }
    # 双层解析：CDP 把返回值当**字符串**给出来，里面才是 JSON ✓（上一版手工替换转义引号把它弄坏过 ✗）
    try { return (('"' + $m.Groups[1].Value + '"') | ConvertFrom-Json) | ConvertFrom-Json }
    catch { return $null }
}

# ---------------------------------------------------------------------------
# 黑胶页（vinyl）：**托盘模式的歌词与曲名来源**（真机 2026-10-15）
# ---------------------------------------------------------------------------
# 为什么需要它：早先曲名只从"顶层窗口标题"来（`Get-NeteaseWindowTitles` → `Select-SongTitle`），
# 而用户把前台窗口关掉（播放转到托盘）之后**一个可见的网易云顶层窗口都没有** —— 真机实测：
# 4 个窗口（`铁花飞 - Mili/塞壬唱片-MSR` ×2、`桌面歌词`、`迷你播放器`）**全部 IsWindowVisible=0**。
# 于是 `Get-PlayingTrack` 返回 `$null`，主循环那个 `if ($now) { … }` 整段被跳过 →
# **歌词与曲名一起没了**。这就是"关掉窗口后全都失效"的机制（与 CDP 死没死无关）。
#
# 而 CDP 那边一切照旧：`li.line.current` 上有客户端自己维护的**当前行**（原文 + 翻译）与
# `data-time`（真实位置，十分之一秒），标题在 `span.title` 的 `title` 属性里。所以新来源
# **不经过任何窗口**：只读页面。
#
# 选择器纪律（这是"通用准确"的胜负手）：**绝不锚带哈希的类名**——`ClassicPageContainer_c1h09pwc`、
# `VinylPageLayoutContainer_vwagppq`、`TrackTitleContainer_t1s72en4`、`TypographyWrapper_tca8bac`
# 都是构建产物，客户端一升级就变。按稳定性排序，每一条候选都记下"命中/未命中"：
#   ① 稳定 id（`VINYL_COVER_ELEMENT_ID` —— 名字就像故意留的锚）
#   ② 语义类名（`.line` / `.current` / `.title`）
#   ③ 属性与 data-*（`[data-testid=tid_tracktitle_container]`、`title="…"`、`data-time`）
# `-ProbeVinyl` 与主循环都会打印**是哪一条候选命中的**（本项目规矩：打判别性数值，
# 不打"失败了"）。

# 候选表（纯数据；-SelfTest 断言的是**顺序**，也就是优先级本身）。
# 这里**不写进 -SelfTest 单独维护的第二份**：断言直接遍历这两个数组，改了顺序也会一起被抓。
function Get-VinylRootCandidates {
    return @(
        '#VINYL_COVER_ELEMENT_ID'
        '[id=VINYL_COVER_ELEMENT_ID]'
        'div.display'
    )
}
function Get-VinylLineCandidates {
    return @(
        '#VINYL_COVER_ELEMENT_ID li.line.current'
        'li.line.current'
        'li[class~="line"][class~="current"]'
        'li[data-time].current'
    )
}
function Get-VinylTitleCandidates {
    # 属性优先（干净值），元素文本兜底。两跳之间**只差命名空间**，不是两种"猜法"。
    return @(
        '[data-testid=tid_tracktitle_container] span.title'
        '#VINYL_COVER_ELEMENT_ID span.title'
        'span.cmd-typography.title'
    )
}
function Get-VinylArtistCandidates {
    # 歌手那一行是 `li.artist`，值在**内层 span 的 title 属性**上（`title="Mili / 塞壬唱片-MSR"`）——
    # 直接取 `li` 的 textContent 会带上"歌手："前缀，所以要先取属性。
    return @(
        '[data-testid=tid_tracktitle_artist] span[title]'
        '[data-testid=tid_tracktitle_artist]'
        '#VINYL_COVER_ELEMENT_ID li.info.artist span[title]'
    )
}

# 纯函数：把 `data-time`（字符串，秒）解成毫秒。解不出返回 -1（**不是 0**：0 s 是真值，
# 用 0 当"不知道"会让第一行与读失败长得一样）。
function ConvertFrom-VinylSeconds {
    param([string]$Text)
    if (-not $Text) { return -1 }
    $t = $Text.Trim()
    if ($t -notmatch '^\d+(\.\d+)?$') { return -1 }
    $v = 0.0
    if (-not [double]::TryParse($t, [ref]$v)) { return -1 }
    if ($v -lt 0) { return -1 }
    if ($v -gt 86400) { return -1 }          # 一天的秒数：超出就是解析错了，不是位置
    return [int][math]::Round($v * 1000.0)
}

# 纯函数：把当前行的 `<p>` 数组切成原文 / 翻译（-SelfTest 覆盖，客户端的真实 HTML 是夹具）。
#
# 真机形状（2026-10-15 实测，64 行全部一致）：**第一个 `<p>` 是原文，第二个是翻译**；
# 而 store 的 `tlyricLines[i].lyric` 与第二个 `<p>` 的 textContent **64/64 逐行相等** ——
# 所以这不是猜的。没有翻译的行**仍然有第二个 `<p>`**，只是内容为空（"作词: …" 那两行、
# 以及当前这首的 64 行里有 29 行翻译非空）。
#
# 兜底：只有一个 `<p>` 时它就是原文、翻译为空（`-SelfTest` 有一条专门喂单 `<p>`）。
# 两个都为空 → 原文为空（调用方据此判"这一拍没有可用行"）。
function Split-VinylParagraphs {
    param([string[]]$Paragraphs)
    $orig = ''
    $xlat = ''
    if ($Paragraphs -and $Paragraphs.Count -gt 0 -and $null -ne $Paragraphs[0]) { $orig = ('' + $Paragraphs[0]).Trim() }
    if ($Paragraphs -and $Paragraphs.Count -gt 1 -and $null -ne $Paragraphs[1]) { $xlat = ('' + $Paragraphs[1]).Trim() }
    # 首段为空、次段非空：只有一种成因说得通——这一行是"翻译单独成段"，那时次段才是原文。
    # （真机上没见过，但把两个字段都留空去丢一行歌词是更糟的失败方式。）
    if (-not $orig -and $xlat) { return @{ Original = $xlat; Translation = '' } }
    return @{ Original = $orig; Translation = $xlat }
}

# 纯函数：一条被选中的行（命中的候选 + 属性 + 段数组）→ 规范化的读数（-SelfTest 覆盖）。
# 命中的候选名原样带出去（`Hit`），探针与主循环都把它打印出来。
function ConvertFrom-VinylLineSpec {
    param($Spec)
    if (-not $Spec) { return $null }
    $paras = @()
    if ($null -ne $Spec.paras) { $paras = @($Spec.paras | ForEach-Object { '' + $_ }) }
    $split = Split-VinylParagraphs -Paragraphs $paras
    [pscustomobject]@{
        Hit      = '' + $Spec.hit
        DataTime = '' + $Spec.dataTime
        TimeMs   = ConvertFrom-VinylSeconds -Text ('' + $Spec.dataTime)
        Original = $split.Original
        Translation = $split.Translation
        ParaCount = $paras.Count
    }
}

# 纯函数：标题取值——`title` **属性**优先，元素文本兜底（-SelfTest 覆盖）。
# 为什么要防文本：`span.title` 里面嵌着两个空 `<div class="label">`，它们的 textContent
# 本身是空的（所以本机拿到的文本恰好也是干净的 `铁花飞`），但那只说明**现在**它们是空的；
# 属性是客户端自己写的干净值，不依赖子节点长什么样。
# 空白（空属性 / 纯空白文本）**不算命中**，要继续往下试候选。
function Get-VinylTitleFromSpec {
    param($Spec)
    if (-not $Spec) { return $null }
    if ($Spec.attr) {
        $a = ('' + $Spec.attr).Trim()
        if ($a) { return $a }
    }
    if ($null -ne $Spec.text) {
        $t = ('' + $Spec.text).Trim()
        if ($t) { return $t }
    }
    return $null
}

# 纯函数：歌手那一行。先取内层 span 的 `title` 属性（干净：`Mili / 塞壬唱片-MSR`），
# 再退回 li 的文本并**剥掉"歌手："前缀**（本地化前缀会变，所以这是兜底而非常态）。
function Get-VinylArtistFromSpec {
    param($Spec)
    if (-not $Spec) { return $null }
    if ($Spec.attr) {
        $a = ('' + $Spec.attr).Trim()
        if ($a) { return $a }
    }
    $t = ('' + $Spec.text).Trim()
    if (-not $t) { return $null }
    $t = $t -replace '^\s*[^：:]{1,8}[：:]\s*', ''    # "歌手："这类前缀：以冒号结尾的最多 8 个字
    $t = $t.Trim()
    if ($t) { return $t }
    return $null
}

# 纯函数：**黑胶页这一拍的读数还能不能用**（-SelfTest 覆盖）。这是这个来源的"进/不进链子"闸门。
#
# 它为什么必须存在：本项目有两次真实的缺陷都是**冻结的来源把会前进的来源遮住**
# （`playingLyricLineNumber` 冻结在 10、`storeline` 因此把 memory/media/时钟全挡住）。
# 现在的位置只有这一条来源，所以"读到一个不动的数"必须能被指出来 —— 否则界面上是"歌词
# 停在这一句"，而看不出是**播放器本来就停着**还是**页面不更新了**（两者修法相反）。
#
# 判据（四条，各对应一个真机形状；阈值都是**实测**定的，见 docs 的设计说明）：
#   ① 没有当前行 → 不可用（窗口关了、页面还没挂载、选择器失效）——调用方在别处判，这里看 time
#   ② `data-time` 解析不出（< 0）→ 不可用
#   ③ **往回走超过 `BackToleranceMs`** → 不可用。真机实测：切歌当拍读到过 data-time="1"，
#      而那一声明的位置还在上一首的 240 s 上 —— 用它当位置会把面板钉在新歌第 0 行。
#      1 s 的容差是给"同一行边界上的抖动"留的（页面按 0.1 s 粒度更新）。
#   ④ **往前跳超过 `ForwardToleranceMs`** → 不可用（读到的是上一轮/上一首的陈旧节点）。
#      阈值 30 s 不是随手取的：**真机实测这一首的相邻句间隔 2.6 s ~ 26.7 s**（音乐段落之间
#      本来就有长间奏），所以 5 s 那种"看起来谨慎"的上界会把正常换行也判死。判据宁松勿误杀：
#      这里要挡的是"几百秒的跳变"，不是"这句唱得久"。
#   ⑤ **连续 `RepeatLimit` 次读到完全相同的数** → 不可用（**这一条才是"冻结"的真正形状**）。
#      注意"两拍相同"本身是正常的（暂停时位置与行一起不动，那是自洽的），所以不判它；
#      只有"一直同一个数"才是页面不更新。真机反例：选择器命中了别处一个恒为 29.5 s 的
#      `data-time` 节点 —— 那时每一拍都会"可用"，界面上就是永远停在那一句。
# 注意判据③④**都不**判暂停：暂停时 data-time 不动、位置也不动，那是自洽的。
function Test-VinylLineUsable {
    param([int]$PrevTimeMs, [int]$NowTimeMs,
          [int]$BackToleranceMs = 1000, [int]$ForwardToleranceMs = 30000,
          [int]$SameReads = 0, [int]$RepeatLimit = 3)
    if ($NowTimeMs -lt 0) { return $false }
    if ($SameReads -ge $RepeatLimit) { return $false }
    if ($PrevTimeMs -lt 0) { return $true }                      # 第一次见到：当引导值
    if ($NowTimeMs -ge $PrevTimeMs) {
        return (($NowTimeMs - $PrevTimeMs) -le $ForwardToleranceMs)
    }
    return (($PrevTimeMs - $NowTimeMs) -le $BackToleranceMs)
}

# 纯函数：store 那一套（曲目 id → 行号 → 该行文本/翻译）在这一拍能不能信（-SelfTest 覆盖）。
# 需要它是因为新来源**自己发文本**：黑胶页给的是"当前行"，而 store 给的是"按 times 选出来的
# 第 idx 行"。两者在**切歌当拍**会错配（store 还是上一首的数组，而位置已经属于新歌），
# 于是上一个来源的翻译会被贴在这一句上。判据只有两条，都是硬事实：
#   · store 报的曲目 id 与这一轮的曲目 id 一致；
#   · 行号落在数组范围内。
# `-SelfTest` 的负对照就是把"曲目 id 必须相等"这一条拿掉，看它是否变红。
function Test-StoreLyricUsable {
    param([string]$StoreTrack, [string]$CurrentTrack, [int]$Index, [int]$Lines)
    if (-not $StoreTrack) { return $false }
    if ($StoreTrack -ne $CurrentTrack) { return $false }
    if ($Index -lt 0) { return $false }
    return ($Index -lt $Lines)
}

# 纯函数：把一组候选的命中情况拼成一行判别性日志（如 `#VINYL_COVER_ELEMENT_ID li.line.current`）。
# 它就是"报告哪一条候选命中了"的落点；没命中时给 '(none)'，而不是空字符串。
function Format-VinylHit {
    param([string]$Hit)
    if (-not $Hit) { return '(none)' }
    return $Hit
}

# **一轮只做一次**的小求值：一个紧凑 JSON 对象（不是整页 DOM dump）。
# 页面里没有 audio/video 元素（真机结论 2026-10-13），所以位置只能来自 DOM 自己维护的
# `data-time`；这条表达式同时回答"有没有当前行 / 原文与翻译是什么 / 标题与歌手是谁"。
$script:VinylExpression = @'
(function(){
  function pick(list){
    for (var i=0;i<list.length;i++){ try{ var e=document.querySelector(list[i]); if(e) return {e:e,hit:list[i]}; }catch(x){} }
    return {e:null,hit:''};
  }
  function txt(e){ return e?(''+e.textContent):''; }
  var o={};
  try{
    var roots=__ROOTS__, lines=__LINES__, titles=__TITLES__, artists=__ARTISTS__;
    var r=pick(roots);
    o.root = r.hit;
    var l=pick(lines);
    o.lineHit = l.hit;
    if(l.e){
      o.dataTime = l.e.getAttribute('data-time');
      var ps=l.e.querySelectorAll('p');
      o.paras=[];
      for(var i=0;i<ps.length && i<4;i++){ o.paras.push(txt(ps[i])); }
      o.liCount = document.querySelectorAll('li.line').length;
    }
    var t=pick(titles);
    o.titleHit = t.hit;
    if(t.e){ o.title=(t.e.getAttribute('title')||''); o.titleText=txt(t.e); }
    var a=pick(artists);
    o.artistHit = a.hit;
    if(a.e){ o.artist=(a.e.getAttribute('title')||''); o.artistText=txt(a.e); }
  }catch(x){ o.err=''+x; }
  return JSON.stringify(o);
})()
'@

# 把候选表装进上面那条表达式（只装一次；候选表是纯数据，见 Get-Vinyl*Candidates）。
function Initialize-VinylExpression {
    $script:VinylExpression = $script:VinylExpression.
        Replace('__ROOTS__', (Get-VinylRootCandidates | ConvertTo-Json -Compress)).
        Replace('__LINES__', (Get-VinylLineCandidates | ConvertTo-Json -Compress)).
        Replace('__TITLES__', (Get-VinylTitleCandidates | ConvertTo-Json -Compress)).
        Replace('__ARTISTS__', (Get-VinylArtistCandidates | ConvertTo-Json -Compress))
}
Initialize-VinylExpression

# 读黑胶页这一拍。失败/页面不在就返回 $null —— **调用方必须原样继续降级**（新来源是额外的
# 一条路，不是替代：没有 CDP 时前台那套照旧工作）。
function Get-CdpVinyl {
    if (-not (Get-CdpTab)) { return $null }
    $raw = Invoke-CdpExpression -Expression $script:VinylExpression
    if (-not $raw) { return $null }
    $m = [regex]::Match($raw, '"value"\s*:\s*"((?:[^"\\]|\\.)*)"', 'Singleline')
    if (-not $m.Success) { return $null }
    $o = $null
    try { $o = (('"' + $m.Groups[1].Value + '"') | ConvertFrom-Json) | ConvertFrom-Json } catch { return $null }
    if ($null -eq $o) { return $null }
    $line = ConvertFrom-VinylLineSpec -Spec ([pscustomobject]@{
        hit = $o.lineHit; dataTime = $o.dataTime; paras = $o.paras })
    [pscustomobject]@{
        RootHit   = '' + $o.root
        Root      = [bool]$o.root
        LineHit   = Format-VinylHit -Hit $o.lineHit
        Line      = $line                                 # $null 或规范化的行读数
        LiCount   = if ($null -ne $o.liCount) { [int]$o.liCount } else { 0 }
        TitleHit  = '' + $o.titleHit
        Title     = Get-VinylTitleFromSpec -Spec ([pscustomobject]@{ attr = $o.title; text = $o.titleText })
        ArtistHit = '' + $o.artistHit
        Artist    = Get-VinylArtistFromSpec -Spec ([pscustomobject]@{ attr = $o.artist; text = $o.artistText })
        Why       = '' + $o.err
    }
}

# 探测表达式：只读、无副作用，一次把"页面里有什么"尽量问全，避免来回猜。
# 真机结论（2026-10-13）：这个页面**没有 audio/video 元素** ✗，所以位置不可能来自媒体元素；
# 于是改成问三类东西，并**带上类名**（我们要的是锚点，不是猜）：
#   ① 文本形如 m:ss 的节点        → 播放时间（当前 / 总长）
#   ② style 里带 width 的节点     → 进度条（百分比可换算成位置）
#   ③ 含中文的**叶子**节点        → 歌词行（叶子才能避免把整页 dump 出来）
$script:CdpProbeExpression = @'
(function(){
  function cls(e){ try { return ('' + (e.className || '')).slice(0, 60); } catch (x) { return ''; } }
  var o = { title: document.title, timeTexts: [], widths: [], cjk: [] };
  var all = document.querySelectorAll('*');
  for (var i = 0; i < all.length; i++) {
    var e = all[i];
    var t = ('' + (e.innerText || e.textContent || '')).trim();
    if (o.timeTexts.length < 8 && /^\d{1,2}:\d{2}$/.test(t)) o.timeTexts.push({ c: cls(e), t: t });
    var st = (e.getAttribute && e.getAttribute('style')) || '';
    if (o.widths.length < 8 && /width\s*:\s*[\d.]+/.test(st)) o.widths.push({ c: cls(e), s: st.slice(0, 80) });
    if (o.cjk.length < 12 && !e.children.length && t.length >= 2 && t.length <= 200 &&
        /[\u4e00-\u9fff]/.test(t)) o.cjk.push({ c: cls(e), t: t.slice(0, 60) });
  }
  // 真机结论（2026-10-13，读过 cloudmusic-desktop-mcp 的实现后修正）：
  // **Redux store 不在 window 上** ✗（所以"扫 getState"必然为空 ✗），它要通过 **webpack 运行时**拿：
  // cloudmusic-desktop-mcp 先往 webpackJsonp 里塞一个模块，把 webpack 的 require 存到 window 上，
  // 再 `require(12).a.getStore()` 拿到 store ✓。这里照同一条路，但：
  //   · 用**自己的**空闲模块号（987661），不占用别人的 ✓；
  //   · 只**列键名**与少量短值（不外传任何可识别内容 ✓）；
  //   · 全程 try/catch，失败也只回报错误文本 ✓。
  o.store = {};
  try {
    if (!window.__ei_probe_require__) {
      webpackJsonp.push([[987661], { 987661: function (m, e, require) { window.__ei_probe_require__ = require; } }, [[987661]]]);
    }
    var req = window.__ei_probe_require__;
    o.store.requireCaptured = !!req;
    // 真机结论（2026-10-13）：webpack require 能拿到 ✓，但 `req(12).a.getStore` 在你这台客户端上
    // **不是函数** ✗ —— 模块 id 随版本变（cloudmusic-desktop-mcp 钉死的是 12，那是对它测试的版本 ✓）。
    // 所以这里**不猜 id**，而是扫 webpack 的**缓存**（req.c = 已实例化的模块 ✓）：
    //   · **不执行任何模块** ✗（req(id) 可能会跑初始化代码）——只读缓存里的 exports ✓；
    //   · 找导出里带 getStore（或同时带 getState+dispatch）的那个 ✓。
    var hits = [];
    var cache = (req && req.c) || {};
    for (var id in cache) {
      try {
        var ex = cache[id] && cache[id].exports;
        if (!ex) continue;
        var cands = [ex, ex.a, ex.default, ex.exports];
        for (var ci = 0; ci < cands.length; ci++) {
          var c = cands[ci];
          if (c && typeof c.getStore === 'function') { hits.push({ id: id, at: ci, kind: 'getStore' }); break; }
          if (c && typeof c.getState === 'function' && typeof c.dispatch === 'function') { hits.push({ id: id, at: ci, kind: 'redux' }); break; }
        }
      } catch (x) { }
    }
    o.store.cacheSize = Object.keys(cache).length;
    o.store.hits = hits.slice(0, 20);
    // 找到了就顺手把切片名与 playing 的键名列出来（只看结构，不取值 ✗）
    if (hits.length) {
      try {
        var ex0 = cache[hits[0].id].exports;
        var c0 = [ex0, ex0.a, ex0.default, ex0.exports][hits[0].at];
        var g = c0.getStore();
        // 真机教训（2026-10-13）：cloudmusic-desktop-mcp 的用法是 `getStore()` 之后**直接用 s.playing**
        // （server.py:339-346），**不调 getState()** ✓ —— 我上一版多调了一次，于是
        // "getState is not a function" ✗。所以这里两种形状都兼容 ✓。
        var st = (g && typeof g.getState === 'function') ? g.getState() : g;
        o.store.stateKeys = Object.keys(st || {}).slice(0, 40);
        var p = (st && st.playing) || {};
        o.store.playingKeys = Object.keys(p).slice(0, 60);
        // 只回报 playing 里的**标量**（数字 / 短字符串）——播放位置就在其中 ✓，其余类型一律跳过 ✗
        o.store.playingScalars = {};
        for (var pk in p) {
          try {
            var pv = p[pk];
            if (typeof pv === 'number') o.store.playingScalars[pk] = pv;
            else if (typeof pv === 'string' && pv.length <= 60) o.store.playingScalars[pk] = pv;
          } catch (x3) { }
        }
        // 真机发现（2026-10-13）：store 里**直接有"当前歌词行"** ✓✓ ——
        // `playingLyricLineNumber: 10`（客户端自己记的行号 ✓）与 `playingLyric`（非标量 ✓）。
        // 所以**位置不是必需的** ✓：行号 + 歌词本身就能精确定位当前行（拖动/暂停天然正确 ✓）。
        // 这里只问清 `playingLyric` 的**形状**（字符串？还是含原文/翻译/时间的对象？✓），
        // 以及两个常被拿来当"当前曲目"的对象的键名 ✓ —— 全是结构与短值，不外传长文本 ✓。
        o.store.lyricShape = {};
        try {
          var ly = p.playingLyric;
          o.store.lyricShape.type = (ly === null) ? 'null' : (typeof ly);
          if (typeof ly === 'string') o.store.lyricShape.sample = ly.slice(0, 60);
          else if (ly && typeof ly === 'object') {
            o.store.lyricShape.keys = Object.keys(ly).slice(0, 30);
            try { o.store.lyricShape.json = ('' + JSON.stringify(ly)).slice(0, 600); } catch (x4) { }
          }
        } catch (x5) { }
        try { o.store.curTrackKeys = Object.keys(p.curTrack || {}).slice(0, 40); } catch (x6) { }
        try { o.store.curPlayingKeys = Object.keys(p.curPlaying || {}).slice(0, 40); } catch (x7) { }
        try { o.store.playingLyricLineNumber = p.playingLyricLineNumber; } catch (x8) { }
        o.store.curTrackName = (p.curTrack && p.curTrack.name) || null;
        o.store.curTrackDuration = (p.curTrack && p.curTrack.duration) || null;
        o.store.curPlayingText = (p.curPlaying && typeof p.curPlaying.text === 'string')
          ? p.curPlaying.text.slice(0, 80) : null;
        // 真机发现（2026-10-13）：`playingLyric` 是**空数组** ✗（只在歌词界面激活时才有内容）。
        // 而 stateKeys 里有 **`async:lyric`** ✓ 与 **`async:decorationDesktop`** ✓（dva 动态模型 ✓）——
        // "当前歌词行"最可能在这两个切片里。只回报它们的**键 + 标量 + 短字符串** ✓。
        o.store.lyricSlices = {};
        var names = ['async:lyric', 'async:decorationDesktop'];
        for (var ni = 0; ni < names.length; ni++) {
          try {
            var sl = st[names[ni]];
            if (!sl) { o.store.lyricSlices[names[ni]] = 'missing'; continue; }
            var scal = {};
            for (var k2 in sl) {
              var v2 = sl[k2];
              if (typeof v2 === 'number') scal[k2] = v2;
              else if (typeof v2 === 'string' && v2.length <= 80) scal[k2] = v2;
            }
            o.store.lyricSlices[names[ni]] = { keys: Object.keys(sl).slice(0, 40), scalars: scal };
          } catch (x9) { o.store.lyricSlices[names[ni]] = 'err'; }
        }
        // 真机结论（2026-10-13）：**当前行 = `async:lyric.lyricLines[playing.playingLyricLineNumber]`** ✓✓
        // —— 客户端自己维护的行号 ✓，拖动/暂停天然正确 ✓（不需要位置、不需要时间推算 ✓）。
        // 单位警告：`curTrackDuration` 是**毫秒**（242613 ✓），`resourceDuration` 是**秒**（242 ✓）——别混 ✗。
        try {
          var lyr = st['async:lyric'] || {};
          var lines = lyr.lyricLines;
          o.store.lyricLineCount = (lines && lines.length) || 0;
          o.store.lyricOffset = lyr.offset;
          o.store.currentUsedLyric = lyr.currentUsedLyric;
          o.store.tlyricCount = (lyr.tlyricLines && lyr.tlyricLines.length) || 0;
          var idx = p.playingLyricLineNumber;
          o.store.lyricIndex = idx;
          if (lines && typeof idx === 'number' && idx >= 0 && idx < lines.length) {
            o.store.currentLine = lines[idx];
            var t0 = lines[idx];
            o.store.currentLineText = ('' + ((t0 && (t0.text || t0.lyric || t0.content || t0.lrc)) || '')).slice(0, 80);
          }
        } catch (xA) { o.store.lyricProbeError = '' + xA; }
      } catch (x2) { o.store.probeError = '' + x2; }
    }
  } catch (x) { o.store.error = '' + x; }
  // ---- fiber 路（照 netease-desktop-mcp/src/playlist-bridge.mjs:5-13 的做法）----
  // 从任意 React 节点取 __reactFiber / __reactInternalInstance，向上走 fiber.return（<=100 层），
  // 找 Provider 的 memoizedProps.value.store —— **不猜 webpack 模块 id**。
  // 真机教训：模块 11 的 getStore() 结果**没有 getState**，而它们找到的 store 有 getState + dispatch
  // → 先前很可能读的是**次级/陈旧的 state-like 对象**（这能解释"行号冻结"）。
  // 同时收集各层 props 的标量：**两次采样对比"谁变了"**，就能定论页面里有没有活位置。
  o.fiber = {};
  try {
    var host = document.querySelector('[class*=bar], .m-playbar, #root *') || document.body;
    var rk = Object.keys(host).filter(function (k) {
      return k.indexOf('__reactFiber') === 0 || k.indexOf('__reactInternalInstance') === 0;
    })[0];
    o.fiber.found = !!rk;
    var f = rk ? host[rk] : null;
    var props = {}, store2 = null;
    for (var d = 0; f && d < 100; d++, f = f.return) {
      try {
        var mp = f.memoizedProps;
        if (mp && typeof mp === 'object') {
          for (var k2 in mp) {
            var v2 = mp[k2];
            if (typeof v2 === 'number') props['d' + d + '.' + k2] = v2;
            else if (typeof v2 === 'string' && v2.length <= 40) props['d' + d + '.' + k2] = v2;
          }
          if (!store2 && mp.value && typeof mp.value.getState === 'function' &&
              typeof mp.value.dispatch === 'function') store2 = mp.value;
        }
        var mst = f.memoizedState;
        if (!store2 && mst && typeof mst.getState === 'function' && typeof mst.dispatch === 'function') store2 = mst;
      } catch (e3) { }
    }
    o.fiber.propCount = Object.keys(props).length;
    o.fiber.props = props;
    o.fiber.storeFound = !!store2;
    if (store2) {
      var stt = store2.getState();
      o.fiber.storeSlices = Object.keys(stt).slice(0, 40);
      var pl = stt.playing || {}, ps = {};
      for (var pk2 in pl) {
        var pv2 = pl[pk2];
        if (typeof pv2 === 'number') ps[pk2] = pv2;
        else if (typeof pv2 === 'string' && pv2.length <= 40) ps[pk2] = pv2;
      }
      o.fiber.playingScalars = ps;
      var dl = stt['async:decorationDesktop'] || {};
      o.fiber.decoKeys = Object.keys(dl).slice(0, 30);
      var cd = dl.currentData;
      o.fiber.decoCurrentDataType = (cd === null) ? 'null' : (typeof cd);
      if (cd && typeof cd === 'object') {
        o.fiber.decoCurrentDataKeys = Object.keys(cd).slice(0, 30);
        try { o.fiber.decoCurrentDataJson = ('' + JSON.stringify(cd)).slice(0, 400); } catch (e4) { }
      }
    }
  } catch (xF) { o.fiber.error = '' + xF; }  return JSON.stringify(o);
})()
'@

if ($ProbeCdp) {
    Write-Host '探测 CDP（需要客户端是"带调试端口的快捷方式"启动的）：'
    if (-not (Invoke-CdpProbe)) {
        Write-Host '  没找到 CDP 端口 —— 说明客户端不是带调试参数启动的，**歌词与曲名都不会工作**。'
        Write-Host '  改快捷方式的步骤：右键快捷方式 → 属性 → 「目标」→ 在引号内 exe 路径后追加'
        Write-Host '  --remote-debugging-port=9229 → 保存 → 完全退出客户端再启动 → 重跑 -ProbeCdp'
        Write-Host '  （本插件不会替你启动或重启音乐软件。）'
    }
    else {
        Write-Host '  端口可用 ✓。下面是黑胶页那一拍的读数（`-ProbeVinyl` 的简化版）：'
        Write-Host ("    root={0}" -f (@(Get-VinylRootCandidates) -join ' | '))
        Write-Host ("    line={0}" -f (@(Get-VinylLineCandidates) -join ' | '))
        $v = Get-CdpVinyl
        if ($v) {
            Write-Host ("    命中：root='{0}' line='{1}' title='{2}' artist='{3}'" -f `
                $v.RootHit, $v.LineHit, $v.TitleHit, $v.ArtistHit)
            Write-Host ("    读数：title='{0}' artist='{1}' data-time='{2}' → {3}ms  行数={4}" -f `
                $v.Title, $v.Artist, $(if ($v.Line) { $v.Line.DataTime } else { '' }), `
                $(if ($v.Line) { $v.Line.TimeMs } else { -1 }), $v.LiCount)
            Write-Host ("    当前行：原文='{0}' | 翻译='{1}'" -f `
                $(if ($v.Line) { $v.Line.Original } else { '' }), $(if ($v.Line) { $v.Line.Translation } else { '' }))
            Write-Host '    要多看几拍（判"值有没有往前走"）就跑 -ProbeVinyl ✓'
        }
        else {
            Write-Host '  求值失败 ✗（页面可能不接受 Runtime.evaluate，或调试目标是别的东西）。'
            Write-Host '  把上面 target 的输出发我，我按它挑目标重试 ✓'
        }
        # 兜底的"页面里有什么"：黑胶页读不到时（客户端升级换了结构），这条**不带任何选择器**
        # 的枚举能回答"现在页面上长什么样"。正常运行时不需要看它，所以只在 vinyl 读失败时打，
        # 免得把几 KB 的 JSON 糊满屏幕。
        if (-not $v) {
            Write-Host '  再问一遍"页面里有什么"（无选择器的枚举，升级后重新定锚点用）：'
            Write-Host ('  ' + $script:CdpProbeExpression)
            $res = Invoke-CdpExpression -Expression $script:CdpProbeExpression
            if ($res) { Write-Host ('  实际返回：' + $res) }
        }
    }
    exit 0
}

# ---------------------------------------------------------------------------
# 离线自检
# ---------------------------------------------------------------------------
# 夹具是**客户端真实吐出来的 HTML**（2026-10-15 从活着的 9229 页面抄下来，原文见
# .tool/investigations/cdp-vinyl-evidence.txt 与 docs/netease-lyric-source-design.md）：
# 哈希类名、`_nk`、`data-testid`、两个空 `<span class="label">` 全都原样保留 ——
# AGENTS §7.9 的规矩：**喂真输入的形状**。上一轮正是"喂了不带换行的假输入"才让两条
# 断言长期全绿却什么也没守住。
$script:VinylLineFixture = '<li class="line current" data-time="22.95" _nk="iArvpjX41"><p class="cmd-typography cmd-typography-paragraph cmd-typography-primary cmd-typography-normal cmd-typography-userselect TypographyWrapper_tca8bac" _nk="vpjX51">Such a beautiful vibrancy</p><p class="cmd-typography cmd-typography-paragraph cmd-typography-primary cmd-typography-normal cmd-typography-userselect TypographyWrapper_tca8bac" _nk="vpjX52">多么美丽的盎然生机啊</p></li>'
# 真机上"没有翻译的行"仍然有第二个 `<p>`，内容为空（实测 64 行里 29 行翻译非空）。
$script:VinylLineNoTransFixture = '<li class="line current" data-time="230.29" _nk="iArvpjX41"><p class="cmd-typography cmd-typography-paragraph" _nk="vpjX51">身世境遇不由人选择</p><p class="cmd-typography cmd-typography-paragraph" _nk="vpjX52"></p></li>'
# 单 `<p>`：真机这一首 64 行全是两段，所以这是**形状防御**（客户端换版本时不再崩，
# 而是退化成"没有翻译"）。断言写在这里，免得下次有人把它当成真实形状。
$script:VinylLineSinglePFix = '<li class="line current" data-time="9.19" _nk="iArvpjX41"><p class="cmd-typography cmd-typography-paragraph">A calloused hand</p></li>'
$script:VinylTitleFixture = '<span class="cmd-typography title cmd-typography-primary cmd-typography-normal cmd-typography-userselect TypographyWrapper_tca8bac" title="铁花飞" _nk="MeGN63"><div class="label" _nk="MeGN54"></div>铁花飞<span class="label" _nk="MeGN71"></span></span>'
$script:VinylArtistFixture = '<span title="Mili / 塞壬唱片-MSR" _nk="MeGN73"><span _nk="MeGN74"></span><span class="link" _nk="MeGN75">Mili</span><span _nk="MeGN74"> / </span><span class="link" _nk="MeGN75">塞壬唱片-MSR</span></span>'

# 很笨的 HTML 取属性/取 `<p>` 文本：**只够解析上面那三段夹具**（真机上的解析在页面里，
# 由 Get-CdpVinyl 的表达式做）。写在这里是为了让"夹具 → 纯函数"这条链在离线自检里真的
# 跑一遍，而不是让断言自己造一个 `paras` 数组——那样测的就是断言自己的假设。
function Read-FixtureAttr {
    param([string]$Html, [string]$Name)
    $m = [regex]::Match($Html, ('\b{0}="([^"]*)"' -f [regex]::Escape($Name)))
    if ($m.Success) { return $m.Groups[1].Value }
    return $null
}
function Read-FixtureParagraphs {
    param([string]$Html)
    $out = @()
    foreach ($m in [regex]::Matches($Html, '<p\b[^>]*>(.*?)</p>', 'Singleline')) { $out += $m.Groups[1].Value }
    return $out
}

if ($SelfTest) {
    $stamp = [datetime]::new(2026, 10, 15, 12, 0, 0, [DateTimeKind]::Local)

    # --- 候选表：**顺序本身就是优先级**，所以断言顺序（不是"有没有"）------------------
    # 锚点稳定性排序（这是"通用准确"的胜负手，见 docs/netease-lyric-source-design.md）：
    # ① 稳定 id（VINYL_COVER_ELEMENT_ID）→ ② 语义类名/属性（line/current/title、
    # [data-testid=…]）→ ③ 元素文本兜底。**一处带哈希的类名都不许出现在候选表里**。
    $script:RootCands = @(Get-VinylRootCandidates)
    $script:LineCands = @(Get-VinylLineCandidates)
    $script:TitleCands = @(Get-VinylTitleCandidates)
    $script:ArtistCands = @(Get-VinylArtistCandidates)
    # 负对照①（逐字可复现）：把"稳定 id 在最前"这条拿掉——候选表变成"li.line.current 优先"。
    # 断言会红在"根候选第一条是稳定 id"。真机上的代价：一个带哈希的祖先类名被写进候选表时
    # 客户端升级就整条失效，而 id 是页面作者刻意留的锚。
    $script:MutatedRootCands = @('div.display', '#VINYL_COVER_ELEMENT_ID', '[id=VINYL_COVER_ELEMENT_ID]')

    # --- data-time → 毫秒（真机 `22.95`）--------------------------------------------
    $script:SecondsCases = @(
        @{ T = '22.95'; Want = 22950; D = '真机形状 22.95 → 22950' }
        @{ T = '0'; Want = 0; D = '0 s 是**真值**，不能被当成"读失败"' }
        @{ T = '189.74'; Want = 189740; D = '整数秒 + 两位小数' }
        @{ T = '7.5'; Want = 7500; D = '一位小数' }
        @{ T = ''; Want = -1; D = '空串 = 不知道（-1，不是 0）' }
        @{ T = $null; Want = -1; D = 'null = 不知道' }
        @{ T = 'abc'; Want = -1; D = '垃圾 = 不知道' }
        @{ T = '-3'; Want = -1; D = '负数 = 不知道' }
        @{ T = '999999'; Want = -1; D = '超过一天 = 解析错了，不是位置' }
    )

    # --- 段切分 / 标题 / 歌手：真机 HTML 直接喂进去 ------------------------------------
    $script:LineSpecReal = [pscustomobject]@{
        hit = $script:LineCands[0]
        dataTime = Read-FixtureAttr -Html $script:VinylLineFixture -Name 'data-time'
        paras = Read-FixtureParagraphs -Html $script:VinylLineFixture
    }
    $script:LineReal = ConvertFrom-VinylLineSpec -Spec $script:LineSpecReal
    $script:LineNoTrans = ConvertFrom-VinylLineSpec -Spec ([pscustomobject]@{
        hit = $script:LineCands[1]; dataTime = '230.29'
        paras = Read-FixtureParagraphs -Html $script:VinylLineNoTransFixture })
    $script:LineSingleP = ConvertFrom-VinylLineSpec -Spec ([pscustomobject]@{
        hit = $script:LineCands[1]; dataTime = '9.19'
        paras = Read-FixtureParagraphs -Html $script:VinylLineSinglePFix })
    $script:TitleReal = Get-VinylTitleFromSpec -Spec ([pscustomobject]@{
        attr = Read-FixtureAttr -Html $script:VinylTitleFixture -Name 'title'
        text = ([regex]::Replace($script:VinylTitleFixture, '<[^>]*>', '')) })
    $script:ArtistReal = Get-VinylArtistFromSpec -Spec ([pscustomobject]@{
        attr = Read-FixtureAttr -Html $script:VinylArtistFixture -Name 'title'
        text = ([regex]::Replace($script:VinylArtistFixture, '<[^>]*>', '')) })
    # 标题那个 `title` 属性为空时**必须**回退到元素文本；两个都空 → $null（不要发空标题）
    $script:TitleFromText = Get-VinylTitleFromSpec -Spec ([pscustomobject]@{ attr = ''; text = ' 铁花飞 ' })

    # --- 新鲜度 / 合理性判定：照主循环的规则跑一台小状态机 ------------------------------
    # 单条纯函数断言答不了"它会不会**一直**把同一个数当新的"——那要**多拍**才看得出来
    # （这正是上一版 storeline 守卫的教训：`Test-StoreLineUsable` 也是纯函数，但它的失败
    #  形状是"每一拍都判可用"，只有连跑几拍才露出来）。所以这里连喂一串真机采样序列，
    # 并像主循环那样**记住连续读到同一个数的次数**（`sameReads`）。
    function Invoke-VinylGuardModel {
        param([int[]]$TimesMs)
        $prev = -1; $same = 0; $used = @()
        foreach ($t in $TimesMs) {
            if (Test-VinylLineUsable -PrevTimeMs $prev -NowTimeMs $t -SameReads $same) {
                $used += $t
                if ($prev -ge 0 -and $t -eq $prev) { $same++ } else { $same = 0 }
                $prev = $t
            }
        }
        return $used
    }
    # 真机实测序列（01:04:01 → 01:04:35，每 1.5 s 采样一次；中间有暂停）：
    #   195.92 → 202.91 → 202.91（暂停）→ 226.99 → 226.99 → 230.29 → 233.36 → 234.96
    # 暂停那两拍**必须仍然可用**（位置与行一起不动，是自洽的），跳变那几拍是真实换行
    # —— 实测这一首的相邻句间隔 2.6 s ~ 26.7 s，所以上界必须比 5 s 宽得多。
    $script:GuardRealSeq = Invoke-VinylGuardModel -TimesMs @(195920, 202910, 202910, 226990, 226990, 230290, 233360, 234960)
    # 真机**长间奏**（26.7 s 一跳）也必须算可用：这是"阈值别定太紧"的直接证据。
    $script:GuardLongGap = Invoke-VinylGuardModel -TimesMs @(195500, 222200)
    # 反例序列：冻结住的读数（选择器命中了别处一个恒为 29.5 s 的节点）——
    # 前两拍"可用"是正常的（和暂停长得一样），**第三拍起必须判不可用**。
    $script:GuardFrozenSeq = Invoke-VinylGuardModel -TimesMs @(29500, 29500, 29500, 29500, 29500)
    # 反例序列：切歌当拍读到新歌第一行（1 s），而位置还在上一首的 240 s 上 → 往回走太多。
    $script:GuardBackJumpSeq = Invoke-VinylGuardModel -TimesMs @(240000, 1000)
    # 反例序列：几百秒的跳变（读到上一首的陈旧节点）→ 不可用。
    $script:GuardStaleJumpSeq = Invoke-VinylGuardModel -TimesMs @(10000, 400000)

    $checks = @(
        # ---- 候选表顺序（"通用准确"的胜负手）--------------------------------------------
        @{ Name = '黑胶页根候选：**第一条必须是稳定 id**（真机实测它就是 #VINYL_COVER_ELEMENT_ID）'; Ok = ($script:RootCands[0] -eq '#VINYL_COVER_ELEMENT_ID') }
        @{ Name = '黑胶页根候选：负对照（把稳定 id 挪后）会红'; Ok = ($script:MutatedRootCands[0] -ne '#VINYL_COVER_ELEMENT_ID') }
        @{ Name = '当前行候选：**第一条必须同时锚稳定 id 与语义类**（#VINYL_COVER_ELEMENT_ID li.line.current）'; Ok = ($script:LineCands[0] -eq '#VINYL_COVER_ELEMENT_ID li.line.current') }
        @{ Name = '当前行候选：第二条退到纯语义类（li.line.current，不依赖 id）'; Ok = ($script:LineCands[1] -eq 'li.line.current') }
        @{ Name = '标题候选：第一条用 data-testid（属性锚，不是类名）'; Ok = ($script:TitleCands[0] -eq '[data-testid=tid_tracktitle_container] span.title') }
        @{ Name = '标题候选：文案取值只锚语义类 title（属性 title= 优先，见下面的取值断言）'; Ok = ($script:TitleCands[1] -eq '#VINYL_COVER_ELEMENT_ID span.title') }
        @{ Name = '歌手候选：第一条锚 data-testid 且**取属性**（textContent 会带"歌手："前缀）'; Ok = ($script:ArtistCands[0] -eq '[data-testid=tid_tracktitle_artist] span[title]') }
        @{ Name = '候选表里没有任何带哈希的类名（_c1h09pwc / _vwagppq / _t1s72en4 / _tca8bac 都是构建产物）'; Ok = (-not (@($script:RootCands + $script:LineCands + $script:TitleCands + $script:ArtistCands) -match '_[a-z][0-9a-z]{5}[0-9]')) }
        @{ Name = '候选表非空且互不重复（重复的候选只会白跑一次 querySelector）'; Ok = (@($script:RootCands).Count -ge 2 -and (@($script:RootCands | Select-Object -Unique).Count -eq $script:RootCands.Count) -and (@($script:LineCands | Select-Object -Unique).Count -eq $script:LineCands.Count)) }
        # ---- data-time → 毫秒 ------------------------------------------------------------
        @{ Name = ('data-time：{0}' -f $script:SecondsCases[0].D); Ok = ((ConvertFrom-VinylSeconds -Text $script:SecondsCases[0].T) -eq $script:SecondsCases[0].Want) }
        @{ Name = ('data-time：{0}' -f $script:SecondsCases[1].D); Ok = ((ConvertFrom-VinylSeconds -Text $script:SecondsCases[1].T) -eq 0) }
        @{ Name = ('data-time：{0}' -f $script:SecondsCases[2].D); Ok = ((ConvertFrom-VinylSeconds -Text $script:SecondsCases[2].T) -eq $script:SecondsCases[2].Want) }
        @{ Name = ('data-time：{0}' -f $script:SecondsCases[3].D); Ok = ((ConvertFrom-VinylSeconds -Text $script:SecondsCases[3].T) -eq $script:SecondsCases[3].Want) }
        @{ Name = 'data-time：空 / null / 垃圾 / 负数 / 超一天 一律返回 -1（"不知道"，不是 0）'; Ok = (@(-1, -1, -1, -1, -1) -join ',' ) -eq (@($script:SecondsCases[4..8] | ForEach-Object { ConvertFrom-VinylSeconds -Text $_.T }) -join ',') }
        # ---- 原文 / 翻译 切分（真机 HTML）------------------------------------------------
        @{ Name = '段切分：真机两段 → 第一个 <p> 是原文（Such a beautiful vibrancy）'; Ok = ($script:LineReal.Original -eq 'Such a beautiful vibrancy') }
        @{ Name = '段切分：第二个 <p> 是翻译（多么美丽的盎然生机啊）—— 与 store 的 tlyricLines 逐行相等'; Ok = ($script:LineReal.Translation -eq '多么美丽的盎然生机啊') }
        @{ Name = '段切分：data-time 22.95 → 22950 ms（从同一段夹具里取）'; Ok = ($script:LineReal.TimeMs -eq 22950) }
        @{ Name = '段切分：没有翻译的行有**空的**第二段 → 翻译为空串（不是上一行的残留）'; Ok = ($script:LineNoTrans.Original -eq '身世境遇不由人选择' -and $script:LineNoTrans.Translation -eq '') }
        @{ Name = '段切分：只有一个 <p> 时它是原文、翻译为空（形状防御，真机 64 行全是两段）'; Ok = ($script:LineSingleP.Original -eq 'A calloused hand' -and $script:LineSingleP.Translation -eq '' -and $script:LineSingleP.ParaCount -eq 1) }
        @{ Name = '段切分：负对照（拿第二段当原文）会红'; Ok = ($script:LineReal.Original -ne $script:LineReal.Translation) }
        @{ Name = '段切分：命中过的候选被原样带出来（探针靠它点名）'; Ok = ($script:LineReal.Hit -eq $script:LineCands[0]) }
        # ---- 标题：属性优先，文本兜底，两级空值都不算命中 ---------------------------------
        @{ Name = '标题：真机 HTML → 铁花飞（取 title 属性）'; Ok = ($script:TitleReal -eq '铁花飞') }
        @{ Name = '标题：属性为空时回退到元素文本并去掉空白（夹具里两个空 <span class="label"> 不会污染）'; Ok = ($script:TitleFromText -eq '铁花飞') }
        @{ Name = '标题：属性与文本都为空 → $null（宁可不发，也不发一个空标题）'; Ok = ($null -eq (Get-VinylTitleFromSpec -Spec ([pscustomobject]@{ attr = '   '; text = '' }))) -and ($null -eq (Get-VinylTitleFromSpec -Spec $null)) }
        @{ Name = '歌手：真机 HTML → Mili / 塞壬唱片-MSR（取内层 span 的 title 属性，不带"歌手："）'; Ok = ($script:ArtistReal -eq 'Mili / 塞壬唱片-MSR') }
        @{ Name = '歌手：没有属性时剥掉"歌手："前缀'; Ok = ((Get-VinylArtistFromSpec -Spec ([pscustomobject]@{ attr = ''; text = '歌手：Mili / 塞壬唱片-MSR' })) -eq 'Mili / 塞壬唱片-MSR') }
        @{ Name = '歌手拼接：与客户端自己的形态一致（斜杠分隔，且跳过空名字）'; Ok = ((Join-ArtistNames @('Mili', '塞壬唱片-MSR')) -eq 'Mili / 塞壬唱片-MSR') -and ((Join-ArtistNames @('Mili', '', '  ', $null)) -eq 'Mili') -and ((Join-ArtistNames @()) -eq '') }
        # ---- 身份来源优先级（托盘模式的关键：窗口那条排最后）------------------------------
        @{ Name = '身份：黑胶页有标题时用它（托盘模式走的就是这条）'; Ok = ((Resolve-TrackIdentity -VinylTitle '铁花飞' -VinylArtist 'Mili / 塞壬唱片-MSR' -StoreTitle '铁花飞' -StoreArtist 'Mili / 塞壬唱片-MSR' -WindowTitle '').Source -eq 'vinyl') }
        @{ Name = '身份：黑胶页没有时退到 store，并用 resourceArtists 拼歌手'; Ok = ((Resolve-TrackIdentity -StoreTitle '铁花飞' -StoreArtist 'Mili / 塞壬唱片-MSR' -WindowTitle '').Artist -eq 'Mili / 塞壬唱片-MSR') }
        @{ Name = '身份：没有调试端口时用窗口标题，且按**最后一个**「 - 」切（歌名里可能有「 - 」）'; Ok = ((Resolve-TrackIdentity -WindowTitle 'A - B - C').Title -eq 'A - B') }
        @{ Name = '身份：三条来源都空 → $null（主循环据此走"响亮失败"那条路，而不是发一个空标题）'; Ok = ($null -eq (Resolve-TrackIdentity -VinylTitle '' -StoreTitle '' -WindowTitle '')) }
        # ---- 新鲜度判定：多拍序列（这是"不许拿冻结值遮住会前进的来源"那条不变量的落点）----
        @{ Name = '新鲜度：真机采样序列（195.9→234.96，含暂停两拍）**每一拍都可用**'; Ok = ($script:GuardRealSeq.Count -eq 8) }
        @{ Name = '新鲜度：真机长间奏 26.7 s 一跳也算可用（阈值宁松勿误杀）'; Ok = ($script:GuardLongGap.Count -eq 2) }
        @{ Name = '新鲜度：冻结的读数最多被采用 RepeatLimit 拍（与暂停无从区分），之后判不可用'; Ok = ($script:GuardFrozenSeq.Count -eq 4) }
        @{ Name = '新鲜度：切歌当拍往回跳太多（240 s → 1 s）= 不可用（否则位置被钉在新歌第 0 行）'; Ok = ($script:GuardBackJumpSeq.Count -eq 1) }
        @{ Name = '新鲜度：几百秒的跳变（上一首的陈旧节点）= 不可用'; Ok = ($script:GuardStaleJumpSeq.Count -eq 1) }
        @{ Name = '新鲜度：连续读到同一个数达到 RepeatLimit(3) 就不可用（**冻结的真正形状**）'; Ok = (-not (Test-VinylLineUsable -PrevTimeMs 29500 -NowTimeMs 29500 -SameReads 3)) -and (Test-VinylLineUsable -PrevTimeMs 29500 -NowTimeMs 29500 -SameReads 1) }
        @{ Name = '新鲜度：第一次见到就当引导值（prev=-1）'; Ok = (Test-VinylLineUsable -PrevTimeMs -1 -NowTimeMs 29500) }
        @{ Name = '新鲜度：小回退（选中同一行的相邻读数）可用；大回退不可用'; Ok = ((Test-VinylLineUsable -PrevTimeMs 10000 -NowTimeMs 9500)) -and (-not (Test-VinylLineUsable -PrevTimeMs 10000 -NowTimeMs 1000)) }
        @{ Name = '新鲜度：往前跳一点点可用；跳超过 30 s（陈旧节点）不可用'; Ok = ((Test-VinylLineUsable -PrevTimeMs 10000 -NowTimeMs 14000)) -and (-not (Test-VinylLineUsable -PrevTimeMs 10000 -NowTimeMs 60000)) }
        @{ Name = '新鲜度：解不出 time（-1）= 不可用'; Ok = (-not (Test-VinylLineUsable -PrevTimeMs -1 -NowTimeMs -1)) }
        # store 那套（曲目 id → 行号 → 文本）只在**没有黑胶页**时兜底：切歌当拍它还是上一首的数组，
        # 拿它的文本贴到这一句上就是"翻译串行"。判据就是曲目 id。
        @{ Name = 'store 兜底：曲目 id 一致且在范围内才可用'; Ok = (Test-StoreLyricUsable -StoreTrack '3342981041' -CurrentTrack '3342981041' -Index 4 -Lines 64) }
        @{ Name = 'store 兜底：曲目 id 对不上（切歌当拍）= 不可用'; Ok = (-not (Test-StoreLyricUsable -StoreTrack '111' -CurrentTrack '3342981041' -Index 4 -Lines 64)) }
        @{ Name = 'store 兜底：id 为空 / 行号越界 = 不可用'; Ok = (-not (Test-StoreLyricUsable -StoreTrack '' -CurrentTrack 'x' -Index 0 -Lines 5)) -and (-not (Test-StoreLyricUsable -StoreTrack 'x' -CurrentTrack 'x' -Index 5 -Lines 5)) -and (-not (Test-StoreLyricUsable -StoreTrack 'x' -CurrentTrack 'x' -Index -1 -Lines 5)) }
        # ---- 位置标签与负延迟（改动后 pos 只剩 vinyl 一级）--------------------------------
        @{ Name = '位置来源标签：黑胶页 data-time（真机 22950ms）→ vinyl 22.9s'; Ok = ((Format-PositionSource -Source vinyl -PositionMs 22950) -eq 'vinyl 22.9s') }
        @{ Name = '位置来源标签：认不出来的来源写 n/a（**不再**显示成 clock —— 那条来源已删除）'; Ok = ((Format-PositionSource -Source nonsense -PositionMs 500) -eq 'n/a 0.5s') }
        @{ Name = '负延迟：只看半个轮询周期（data-time 是十分之一秒，没有"分:秒"取整偏差）'; Ok = ((Get-LeadMs -Source vinyl -IntervalMs 1000) -eq 500) }
        @{ Name = '负延迟：周期变了自动值跟着变（400 ms → 200 ms）'; Ok = ((Get-LeadMs -Source vinyl -IntervalMs 400) -eq 200) }
        @{ Name = '负延迟：显式值覆盖自动值（0 = 关掉）'; Ok = ((Get-LeadMs -Source vinyl -IntervalMs 1000 -OverrideMs 0) -eq 0) -and ((Get-LeadMs -Source vinyl -IntervalMs 1000 -OverrideMs 1500) -eq 1500) }
        @{ Name = 'pos 标签带出提早量'; Ok = ((Format-PositionSource -Source vinyl -PositionMs 22950 -LeadMs 500) -eq 'vinyl 22.9s +500ms') -and ((Format-PositionSource -Source vinyl -PositionMs 22950) -eq 'vinyl 22.9s') }
        @{ Name = '命中的候选名可读（空 = (none)，探针与主循环共用一个格式化）'; Ok = ((Format-VinylHit -Hit '') -eq '(none)') -and ((Format-VinylHit -Hit 'li.line.current') -eq 'li.line.current') }
        # ---- 协议与管道（与改前一样，一行都没动）----------------------------------------
        @{ Name = '连不上管道时返回空值（不抛异常）'; Ok = ($null -eq (Connect-PipeClient -Pipe ("endfield-info-windhawk-selftest-{0}" -f [guid]::NewGuid().ToString('N')) -TimeoutMs 200 -WaitSeconds 0)) }
        @{ Name = '岛事件：首次（从未发过）总是允许'; Ok = (Test-EventAllowed -Now $stamp -LastAt ([datetime]::MinValue) -MinMs 4000) }
        @{ Name = '岛事件：间隔不足时不发（密集歌词不刷屏）'; Ok = (-not (Test-EventAllowed -Now $stamp.AddMilliseconds(3999) -LastAt $stamp -MinMs 4000)) }
        @{ Name = '岛事件：达到最小间隔才发'; Ok = (Test-EventAllowed -Now $stamp.AddMilliseconds(4000) -LastAt $stamp -MinMs 4000) }
        @{ Name = '岛事件：间隔设为 0 表示不节流'; Ok = (Test-EventAllowed -Now $stamp -LastAt $stamp -MinMs 0) }
        @{ Name = '发布计划：标题带作者'; Ok = ((Get-PublishPlan -Title 'A' -Artist 'B' -Line 'x' -Translation '' -Pos 'p')['title'] -eq 'A - B') }
        @{ Name = '发布计划：没有作者时不留下悬空的分隔符'; Ok = ((Get-PublishPlan -Title 'A' -Artist '' -Line 'x' -Translation '' -Pos 'p')['title'] -eq 'A') }
        @{ Name = '发布计划：整轮恰好 title/lyric/translation/pos 四个 key（协议没变）'; Ok = (((Get-PublishPlan -Title 'A' -Artist 'B' -Line 'x' -Translation 't' -Pos 'p').Keys -join ',') -eq 'title,lyric,translation,pos') }
        @{ Name = '发布计划：取不到歌词时不发 lyric（而不是发空串盖掉上一行）'; Ok = (-not (Get-PublishPlan -Title 'A' -Artist 'B' -Line '' -Translation '' -Pos 'p').Contains('lyric')) }
        @{ Name = '发布计划：翻译总是出现（空串 = 这一行没有翻译，清掉上一行的）'; Ok = ((Get-PublishPlan -Title 'A' -Artist 'B' -Line 'x' -Translation '' -Pos '').Contains('translation')) }
        @{ Name = '发布计划：没有位置时不发 pos'; Ok = (-not (Get-PublishPlan -Title 'A' -Artist 'B' -Line 'x' -Translation '' -Pos '').Contains('pos')) }
        @{ Name = '发布计划：每个 key 只出现一次（同一轮一个 key 一个发布者）'; Ok = ((Get-PublishPlan -Title 'A' -Artist 'B' -Line 'x' -Translation 't' -Pos 'p').Count -eq (((Get-PublishPlan -Title 'A' -Artist 'B' -Line 'x' -Translation 't' -Pos 'p').Keys | Select-Object -Unique).Count)) }
    )
    $bad = 0
    # 活体证据：**窗口枚举现在只服务兜底③**，所以它空着不再是"桥坏了"的信号——托盘模式
    # 本来就该是空的。打出来免得下一个人看到 0 个窗口以为出了问题（真机实测托盘模式下
    # 4 个 cloudmusic 窗口全部 IsWindowVisible=0）。
    $liveWindows = @(Get-NeteaseWindowTitles)
    Write-Host ("网易云可见顶层窗口（{0} 个，托盘模式下为 0 是正常的）：{1}" -f $liveWindows.Count, (($liveWindows | ForEach-Object { "[$_]" }) -join ' '))
    foreach ($c in $checks) {
        if ($c.Ok) { Write-Host ("  OK   {0}" -f $c.Name) } else { Write-Host ("  FAIL {0}" -f $c.Name); $bad++ }
    }
    Write-Host ("SELFTEST {0}" -f $(if ($bad -eq 0) { 'PASSED' } else { "FAILED ($bad)" }))
    exit $(if ($bad -eq 0) { 0 } else { 1 })
}

# ---------------------------------------------------------------------------
# 只读探针：`-ProbeVinyl`（黑胶页）与 `-ProbeStore`（身份）
# ---------------------------------------------------------------------------
# 这两个探针回答的是同一个问题的两半："这一拍到底读到了什么、是哪一条候选命中的、
# 值有没有往前走"。托盘模式（前台窗口已关）下**只有它们**能说清"歌词为什么没了"——
# 因为界面上一片空白，而可能的原因有四个：端口没了 / 目标没了 / 选择器全没命中 /
# 命中了但读数是冻结的。四个原因指向四种修法，所以每一轮都把四个都打出来。
#
# ⚠ 探针里**不许**有自己的一套判据：位置是否可用与主循环是同一个 `Test-VinylLineUsable`，
# 否则探针说"没问题"而主循环在丢弃它（或反过来），就是又一次自己测自己（AGENTS §7.8）。
if ($ProbeVinyl -or $ProbeStore) {
    $ports = @($CdpPorts)
    Write-Host ("CDP 探针：端口 {0}；{1} 轮，每 {2} ms 一轮" -f ($ports -join '/'), $ProbeRounds, $IntervalMs)
    # 端口与目标先逐条打出来：`-ProbeVinyl` 的输出要能**整段贴进问题报告**，
    # 里面必须自带"有没有端口、目标长什么样"这两件事。
    foreach ($port in $ports) {
        try {
            $v = Invoke-RestMethod -Uri "http://127.0.0.1:$port/json/version" -TimeoutSec 2 -ErrorAction Stop
            Write-Host ("  端口 {0} 是 CDP：{1}" -f $port, ('' + $v.Browser -replace '\s+', ' '))
            $tl = @(Invoke-RestMethod -Uri "http://127.0.0.1:$port/json/list" -TimeoutSec 2 -ErrorAction Stop)
            if ($tl.Count -eq 0) { Write-Host '    （/json/list 为空）' }
            foreach ($t in $tl) {
                Write-Host ("    target: type={0} title='{1}' url={2}" -f $t.type, $t.title, $t.url)
                Write-Host ("            ws={0}" -f $t.webSocketDebuggerUrl)
            }
        }
        catch { Write-Host ("  端口 {0} 无响应（{1}）" -f $port, ('' + $_.Exception.Message -replace '\s+', ' ')) }
    }
    Write-Host ("  候选：root={0}" -f (@(Get-VinylRootCandidates) -join ' | '))
    Write-Host ("        line={0}" -f (@(Get-VinylLineCandidates) -join ' | '))
    Write-Host ("        title={0}" -f (@(Get-VinylTitleCandidates) -join ' | '))
    Write-Host ("        artist={0}" -f (@(Get-VinylArtistCandidates) -join ' | '))
    Write-Host ''
    Write-Host ("  {0,2} {1,-9} {2,-11} {3,-9} {4,-9} {5,-8} {6,-9} {7,-9} {8,-22} {9}" -f `
        '#', 'at', 'root', 'lineHit', 'time', 'guard', 'lines', 'titleHit', 'title/artist', '原文 | 翻译')
    $prevTimeMs = -1
    $sameTimeMs = 0
    for ($i = 1; $i -le $ProbeRounds; $i++) {
        $v = Get-CdpVinyl
        $ss = Get-CdpStoreState
        if (-not $v) {
            Write-Host ("  {0,2} {1,-9} **Get-CdpVinyl 返回空**（端口/目标/求值三处之一失败 —— 见上面逐条目标）" -f `
                $i, (Get-Date).ToString('HH:mm:ss'))
        }
        else {
            $timeMs = if ($v.Line) { [int]$v.Line.TimeMs } else { -1 }
            $usable = if ($v.Line) { [bool](Test-VinylLineUsable -PrevTimeMs $prevTimeMs -NowTimeMs $timeMs -SameReads $sameTimeMs) } else { $false }
            $guard = if (-not $v.Line) { 'no-line' } elseif ($usable) { 'ok' } else { 'stale→不用' }
            $titleText = if ($v.Title -or $v.Artist) { "$($v.Title) / $($v.Artist)" } elseif ($ss -and $ss.ok) { "$($ss.name) - $($ss.artists)" } else { '(none)' }
            $lineText = if ($v.Line) { "$($v.Line.Original) | $($v.Line.Translation)" } else { '' }
            Write-Host ("  {0,2} {1,-9} {2,-11} {3,-9} {4,-9} {5,-8} {6,-9} {7,-9} {8,-22} {9}" -f `
                $i, (Get-Date).ToString('HH:mm:ss'), $(if ($v.Root) { $v.RootHit } else { '(miss)' }), `
                $v.LineHit, $(if ($v.Line) { $v.Line.DataTime } else { '-' }), $guard, $v.LiCount, `
                $(if ($v.Title) { $v.TitleHit } else { '(miss)' }), $titleText, $lineText)
            if ($v.Line -and $usable) {
                if ($prevTimeMs -ge 0 -and $timeMs -eq $prevTimeMs) { $sameTimeMs++ } else { $sameTimeMs = 0 }
                $prevTimeMs = $timeMs
            }
            if ($i -eq 1) {
                Write-Host ("      store: ok={0} trackId='{1}' playId='{2}' state={3} lines={4} tlines={5} dur={6}ms" -f `
                    $(if ($ss) { $ss.ok } else { 'n/a' }), $(if ($ss) { $ss.trackId } else { '' }), `
                    $(if ($ss) { $ss.playId } else { '' }), $(if ($ss) { $ss.state } else { '' }), `
                    $(if ($ss) { $ss.lines } else { '' }), $(if ($ss) { $ss.tlines } else { '' }), `
                    $(if ($ss) { $ss.durationMs } else { '' }))
                Write-Host ("      line 候选输出：{0}" -f $(if ($v) { Format-VinylHit -Hit $v.LineHit } else { '(none)' }))
            }
        }
        if ($i -lt $ProbeRounds) { Start-Sleep -Milliseconds $IntervalMs }
    }
    exit 0
}

# ---------------------------------------------------------------------------
# 主循环
# ---------------------------------------------------------------------------
# 一条数据来源（黑胶页），一个失败方式：**读不到就不发**。早先这里是一条六级降级链
# （CDP 迷你条 → store 行时间 → 内存差分 → SMTC → 推算时钟），每一级都有自己的失效形状，
# 而其中最坏的是"给一个冻结的值"——它会把后面会前进的来源全部遮住，界面上看不出是"读不到"
# 还是"停在那一句"。现在没有降级链：读到了就发，读不到就**响亮地**说读不到（下面那行
# `Write-Diag` 打的是判别性数值：哪一个候选落空、time 解成了什么、位置往回/往前多少），
# 并且不发任何值 —— 面板上留 `--` 比钉在错误的一行上诚实。
$script:VinylMisses = 0

Write-Host '位置与歌词：只读客户端自己的黑胶页（CDP 的 `li.line.current` + `data-time`）'
Write-Host '  · 它与前台上有没有窗口**无关**（窗口关到托盘时字段照旧存在）'
Write-Host '  · 所以需要客户端带 --remote-debugging-port=9229 启动（见 -ProbeCdp 打印的步骤）'
Write-Host '  · 读不到就不发（面板留 `--`），绝不拿一个冻结的值顶上'

$writer = $null
if (-not $DryRun) {
    $client = Connect-PipeClient -Pipe $Pipe
    if (-not $client) {
        # 等满窗口还是连不上：说明 mod 根本没在跑（或设置里不需要外部数据）。
        # 干净退出并说清原因，比抛异常好——看门狗会按它自己的窗口再试。
        Write-Host ("这次不发布：管道 {0} 不可用" -f $Pipe)
        exit 0
    }
    $writer = New-Object System.IO.StreamWriter($client, (New-Object System.Text.UTF8Encoding($false)))
    $writer.AutoFlush = $true
    Write-Frame $writer "HELLO 1 $Provider"
    Write-Host ("已连上管道 {0}" -f $Pipe)
}

# 换歌检测用**曲目身份**（标题 + 歌手），不再用窗口标题：托盘模式下窗口标题根本没有值。
# 上一轮见到的当前行 data-time（毫秒，-1 = 还没见到）——`Test-VinylLineUsable` 的输入。
$lastRaw = ''
$announced = $false
$script:LastEventAt = [datetime]::MinValue
$prevLineMs = -1
# 连续读到**同一个** data-time 的次数（见 Test-VinylLineUsable 判据⑤）。暂停时它会涨，
# 但那时"行不动"是事实；涨到 RepeatLimit 只是让桥**不再发新的位置/歌词**，不会造假值。
$sameLineMs = 0

try {
    while ($true) {
        Start-Sleep -Milliseconds $IntervalMs
        # 逐轮清空：同一轮里同一个 key 出现两次是缺陷，Publish 会点名（见那里的注释）。
        $script:IterationKeys.Clear()

        # ---- 这一拍的唯一数据来源 -------------------------------------------------------
        $vinyl = Get-CdpVinyl
        # store：身份（曲名/歌手）与判活。**不提供位置、不提供当前行**（那是黑胶页的事）。
        $store = Get-CdpStoreState
        $windowTitle = if (-not ($vinyl -and $vinyl.Title) -and -not ($store -and $store.ok -and $store.name)) {
            '' + (Get-WindowTrackTitle)      # 只在完全没有页面时才会真的去枚举窗口
        } else { '' }
        $ident = Resolve-TrackIdentity `
            -VinylTitle $(if ($vinyl) { '' + $vinyl.Title } else { '' }) `
            -VinylArtist $(if ($vinyl) { '' + $vinyl.Artist } else { '' }) `
            -StoreTitle $(if ($store -and $store.ok) { '' + $store.name } else { '' }) `
            -StoreArtist $(if ($store -and $store.ok) { '' + $store.artists } else { '' }) `
            -WindowTitle $windowTitle

        if (-not $ident) {
            # 响亮失败：说清**是哪一环**断的（端口/目标/候选），而不是"失败了"。
            $script:VinylMisses++
            $target = if (Get-CdpTab) { '有目标（见 -ProbeVinyl / -ProbeCdp 的逐条目标）' } else { '没有可用的 CDP 目标：端口 ' + ($CdpPorts -join '/') }
            Write-Host ("[{0}] 读不到曲目（第 {1} 次）：{2}；根候选='{3}' 行候选='{4}' 标题候选='{5}' —— 本拍不发任何值" -f `
                (Get-Date).ToString('HH:mm:ss'), $script:VinylMisses, $target, `
                $(if ($vinyl) { $vinyl.RootHit } else { '(求值失败)' }), `
                $(if ($vinyl) { $vinyl.LineHit } else { '-' }), `
                $(if ($vinyl) { $vinyl.TitleHit } else { '-' }))
        }
        else {
            $trackRaw = '{0} - {1}' -f $ident.Title, $ident.Artist
            if ($trackRaw -ne $lastRaw) {
                # 换歌：清掉"已经发过"的缓存，位置守卫的基准也一起复位（新歌从 0 开始）。
                $lastRaw = $trackRaw
                $script:Sent.Clear()
                $prevLineMs = -1
                $sameLineMs = 0
                Write-Host ("换歌：{0}（身份来源 {1}）" -f $trackRaw, $ident.Source)
            }

            # 位置与当前行：**同一次读**（都在 li.line.current 上）。
            $positionMs = $null
            $line = ''
            $translation = ''
            $posSrc = 'vinyl'
            $why = ''
            if (-not $vinyl) {
                $why = 'CDP 求值失败（端口活着但 Runtime.evaluate 没回值）'
            }
            elseif (-not $vinyl.Line) {
                $why = "行候选全部落空（root='$($vinyl.RootHit)' 命中但 li.line.current 不在里面）"
            }
            else {
                $timeMs = [int]$vinyl.Line.TimeMs
                if (Test-VinylLineUsable -PrevTimeMs $prevLineMs -NowTimeMs $timeMs -SameReads $sameLineMs) {
                    if ($prevLineMs -ge 0 -and $timeMs -eq $prevLineMs) { $sameLineMs++ } else { $sameLineMs = 0 }
                    $prevLineMs = $timeMs
                    $positionMs = $timeMs
                    $line = $vinyl.Line.Original
                    $translation = $vinyl.Line.Translation
                }
                else {
                    $why = "data-time 不可用：prev=${prevLineMs}ms now=${timeMs}ms 连续相同=${sameLineMs}（往回走/往前跳太多/一直不动 → 判陈旧）"
                }
            }

            # 兜底：黑胶页那一拍没给出当前行时，用 store 的歌词数组按这个曲目自己选一行。
            # **只在 store 报的曲目 id 与这一轮身份的曲目一致时**才允许（切歌当拍 store 还是
            # 上一首的数组，用它就会把上一句贴上这一首的翻译）。判据是纯函数，-SelfTest 覆盖。
            if (-not $line -and $store -and $store.ok -and $null -ne $positionMs) {
                $stx = Get-CdpStoreLyricTimes
                if ($stx -and $stx.ok) {
                    $idx = Select-StoreLineIndex -TimesSec ([object[]]$stx.times) -PositionMs $positionMs
                    if (Test-StoreLyricUsable -StoreTrack ('' + $stx.trackId) -CurrentTrack ('' + $store.trackId) `
                            -Index $idx -Lines ([int]$stx.lineCount)) {
                        $ln = Get-CdpStoreLyricLine -Index $idx
                        if ($ln -and $ln.ok) {
                            $line = '' + $ln.text
                            $translation = if ($ln.trans) { '' + $ln.trans } else { '' }
                            $posSrc = 'store'
                            Write-Host ("  （黑胶页没给当前行，用 store 兜底：id={0} index={1}）" -f $stx.trackId, $idx)
                        }
                    }
                }
            }

            if ($why) {
                $script:VinylMisses++
                Write-Host ("[{0}] {1} —— 本拍不发位置/歌词（第 {2} 次）" -f (Get-Date).ToString('HH:mm:ss'), $why, $script:VinylMisses)
            }

            # 换行的"负延迟"（见 Get-LeadMs）。⚠ 变量名**不能**叫 $leadMs：PowerShell 变量名
            # 大小写不敏感，那会覆盖 -LeadMs 参数本身，自动值就永远停在第一次算出来的数。
            $leadApply = Get-LeadMs -Source $posSrc -IntervalMs $IntervalMs -OverrideMs $LeadMs
            # 一轮的帧只在这里生成：每个 key 恰好一条（见 Get-PublishPlan）。
            $plan = Get-PublishPlan -Title $ident.Title -Artist $ident.Artist -Line $line `
                -Translation $translation -Pos $(if ($null -ne $positionMs) {
                    Format-PositionSource -Source $posSrc -PositionMs ([int]$positionMs) -LeadMs $leadApply
                } else { '' })
            foreach ($key in $plan.Keys) { [void](Publish $writer $key $plan[$key]) }
            if ($Event -and -not $announced) {
                # 只有真的发出去了才算"播报过"：被节流掉的话，下一拍还有机会。
                if (Write-IslandEvent -Writer $writer -Provider $Provider -CardMs $EventTtlMs `
                        -Title $trackRaw -Body $line) {
                    $announced = $true
                }
            }
        }
    }
}
finally {
    if ($writer) {
        Write-Frame $writer 'BYE'
        $writer.Dispose()
        $client.Dispose()
    }
}
