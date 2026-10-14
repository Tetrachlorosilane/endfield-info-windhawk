<#
.SYNOPSIS
    示例：把网易云音乐正在播放的曲名与歌词推进 endfield-info-windhawk 的命名管道。

.DESCRIPTION
    这是一个**示例程序**，不是 mod 的一部分：它演示外部程序怎么用 EVENT/SET 把
    "现在在听什么"送到面板与灵动岛上。

    它怎么拿到曲目：
      1. 窗口标题。网易云 PC 版（Electron）的主窗口标题是「歌名 - 歌手」，
         这是最省事也最稳的一条路——不需要注入、不需要调试端口。
      2. 歌词：用歌名+歌手去调网易云自己的歌词接口（公开的 web 接口），
         拿到 LRC 后按"这首歌被看到的时刻"推算当前行。
         注意：推算出来的时间会漂——**暂停时会继续往前走**。要按真实播放位置
         选行，需要 SMTC（Windows 媒体会话），那是下一步的事，见 README。

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
    # 不联网也能验证 LRC 解析与选行
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
    # 进程名匹配（网易云 PC 版：cloudmusic）
    [string]$ProcessName = 'cloudmusic',
    # 换歌时发一条 EVENT，让灵动岛弹一次
    [switch]$Event,
    # 卡片在屏时长（毫秒）。**这不是值的寿命**：值用 -TtlMs（默认 10 分钟），事件用它。
    # 早先两处都发 $TtlMs，于是每张卡片在屏幕上待 10 分钟：队列只有 8 格，两张卡就能把
    # 岛堵住几十分钟，歌词密集的歌看起来就像坏了。
    [int]$EventTtlMs = 6000,
    # 两条岛事件之间的最小间隔（毫秒）：歌词很密的歌、或窗口标题抖动时不许刷屏。
    [int]$EventMinIntervalMs = 4000,
    # 只打印要发的帧，不连管道
    [switch]$DryRun,
    # 打印拿到的 JSON 片段，方便接口形状变了时自己改
    [switch]$DumpJson,
    # 离线自检：LRC 解析 + 选行 + 帧格式
    [switch]$SelfTest,
    # 跑一轮就退出
    [switch]$Once,
    # 直接指定歌曲 id（跳过搜索），用于接口查不到时手工兜底。id 是 64 位的
    [long]$SongId = 0,
    # 取不到歌词时多久重试一次。只在启动时查一次的话，接口抖一下整首歌就都没有歌词
    [int]$LyricRetryMs = 20000,
    # 位置来源：memory = 从客户端内存读播放位置（毫秒 int32，差分发现）；
    # media = 媒体会话（SMTC，真实位置）；clock = 推算时钟；
    # cdp = 通过调试端口读页面里的实际播放时间（需要用户自己给快捷方式加调试参数）；
    # **auto 的实际顺序**（见主循环）：迷你条 DOM 的真 currentTime → store 当前行的起始时间
    # → CDP 其它位置 → 内存差分 → 媒体会话 → 推算时钟。每一步失败就静默降级，
    # 所以"到底用了哪个"由发出去的 `pos` key 回答（`{ext.netease.pos}`）。
    [ValidateSet('auto', 'memory', 'cdp', 'media', 'clock')]
    [string]$PositionSource = 'auto',
    # 内存发现时两次采样的间隔（秒）——这段时间里必须在播放
    [int]$MemoryGapSeconds = 12,
    # 只探一次 CDP（不读位置）：列出端口与调试目标，供下一步确定取值表达式
    [switch]$ProbeCdp,
    # 依次尝试的 CDP 端口（两个成熟项目分别用 9229 / 9223，9222 是通用默认）
    [int[]]$CdpPorts = @(9229, 9223, 9222),
    # 歌词来源：auto = 先用 CDP store（若开着调试端口）再退回"LRC + 位置选行"；
    # store = 只用 CDP store（精确到客户端自己的当前行 ✓）；lrc = 只用接口 LRC + 位置
    [ValidateSet('auto', 'store', 'lrc')]
    [string]$LyricSource = 'auto',
    # 位置扫描器路径，缺省用同目录的 _netease-lyric-scan.ps1
    [string]$ScannerPath = '',
    # 位置助手脚本，缺省用同目录的 _netease-position.ps1
    [string]$PositionHelper = '',
    # 超过这么久没有新采样就当成不可用（毫秒）
    [int]$PositionStaleMs = 3000,
    # 歌词行的"负延迟"：把**选行用的位置**提早这么多毫秒（默认 -1 = 按来源自动）。
    # 用户报告"歌词滞后约 1 秒"，来源就是两处系统性滞后：DOM 只给「分:秒」（位置向下取整
    # 到秒，平均落后 0.5 s）+ 桥每 IntervalMs 才轮询一次（平均再落后半个周期）。
    # auto = 半个周期，DOM 来源再补 0.5 s（默认合计 1000 ms）；0 = 关掉；给正数就用自己的值。
    [int]$LeadMs = -1,
    # 只探一次位置来源并打印结论
    [switch]$ProbePosition,
    # 只读诊断：把"按位置算出来的行"和"客户端自己认为的行"并排打出来（见下）
    [switch]$ProbeStore,
    # 探针轮数
    [int]$ProbeRounds = 10
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# LRC：解析与选行（纯函数，-SelfTest 覆盖的就是它们）
# ---------------------------------------------------------------------------
function ConvertFrom-Lrc {
    param([string]$Text)
    $out = New-Object System.Collections.ArrayList
    if (-not $Text) { return @() }

    foreach ($raw in ($Text -split "`r?`n")) {
        # 一行可以有多个时间标签；长度可能是 .xx 或 .xxx
        $tags = [regex]::Matches($raw, '\[(\d+):(\d+)(?:[.:](\d+))?\]')
        if ($tags.Count -eq 0) { continue }
        $line = ([regex]::Replace($raw, '\[[^\]]*\]', '')).Trim()
        if ($line -eq '') { continue }        # 纯时间戳 / 元信息行

        foreach ($tag in $tags) {
            $fracMs = 0
            if ($tag.Groups[3].Success) {
                $frac = $tag.Groups[3].Value
                switch ($frac.Length) {
                    1 { $fracMs = [int]$frac * 100 }
                    2 { $fracMs = [int]$frac * 10 }
                    default { $fracMs = [int]$frac }
                }
            }
            $minutes = [int]$tag.Groups[1].Value
            $seconds = [int]$tag.Groups[2].Value
            [void]$out.Add([pscustomobject]@{
                    At   = ($minutes * 60 + $seconds) * 1000 + $fracMs
                    Text = $line
                })
        }
    }
    @($out | Sort-Object At)
}

function Select-LrcLine {
    param($Lines, [int]$PositionMs)
    if (-not $Lines -or $Lines.Count -eq 0) { return '' }
    $current = $Lines[0].Text
    foreach ($l in $Lines) {
        if ($l.At -le $PositionMs) { $current = $l.Text } else { break }
    }
    $current
}

# ---------------------------------------------------------------------------
# 曲目：从窗口标题拿「歌名 - 歌手」
#
# **不能用 Process.MainWindowTitle**。2026-10-14 真机实测：网易云开着「桌面歌词」时，
# cloudmusic.exe 的 MainWindowTitle 是**「桌面歌词」**，带歌名的那个顶层窗口反而不是
# 主窗口（`Get-Process -Id 15824` → `MainWindowTitle: 桌面歌词`，而同一个 pid 的另一个
# 顶层窗口标题是「下等马 - 洛天依Official/ChiliChill乐团」）。于是按 MainWindowTitle
# 取标题永远拿不到「 - 」，`Get-PlayingTrack` 判为"没有可用窗口"，桥**一直停在等待
# 循环里、从不连接管道**——面板上 title 与 lyric 两行就永远是 `--`。
#
# 所以自己枚举该进程的顶层窗口，挑标题里有「 - 」的那个。
# ---------------------------------------------------------------------------
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
#
# 按**窗口所属进程名**判定，而不是"先列出所有进程、再筛 pid"：进程**列表**在受限环境里
# 可能只给一部分（实测：沙箱里 `Get-Process` 只返回 13 个进程，而按 pid 单查是可靠的）。
# 所以这里枚举窗口、取每个窗口的 pid、再按 pid 查进程名；每次调用只为**去重后的几个
# pid** 各查一次（有标题的顶层窗口通常不到 20 个）。
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

function Get-PlayingTrack {
    $title = Select-SongTitle (Get-NeteaseWindowTitles)
    if (-not $title) {
        # 兜底：枚举拿不到东西时退回主窗口标题（有的客户端/版本里主窗口就是曲目标题）
        $procs = @(Get-Process -ErrorAction SilentlyContinue |
            Where-Object { $_.ProcessName -like "*$ProcessName*" -and $_.MainWindowTitle })
        if ($procs.Count -gt 0) { $title = Select-SongTitle @($procs[0].MainWindowTitle) }
    }
    if (-not $title) { return $null }

    $sep = $title.LastIndexOf(' - ')
    if ($sep -lt 1) { return $null }

    [pscustomobject]@{
        Raw    = $title
        Title  = $title.Substring(0, $sep).Trim()
        Artist = $title.Substring($sep + 3).Trim()
    }
}

# ---------------------------------------------------------------------------
# 歌词：先用歌名+歌手查 id，再取 LRC。id 按曲目缓存，换歌才查一次。
# ---------------------------------------------------------------------------
$script:LyricCache = @{}

# 网易云对"不像浏览器的请求"会给风控（code=-460）或干脆空结果，所以带上 UA 与 Referer。
$script:Headers = @{
    Referer      = 'https://music.163.com/'
    'User-Agent' = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36'
}
# 诊断说三次就够，之后安静重试（有界计数器，不刷屏）
$script:DiagLeft = 3

function Write-Diag {
    param([string]$Text)
    if ($script:DiagLeft -le 0) { return }
    $script:DiagLeft--
    Write-Host "  ! $Text"
}

function Get-Brief {
    param($Value, [int]$Max = 180)
    if ($null -eq $Value) { return '<null>' }
    $s = ($Value | ConvertTo-Json -Depth 4 -Compress)
    if ($null -eq $s) { return '<empty>' }
    $s = $s -replace '\s+', ' '
    if ($s.Length -gt $Max) { return $s.Substring(0, $Max) + '…' }
    $s
}

function Get-SongId {
    param([string]$Title, [string]$Artist)
    $query = [uri]::EscapeDataString("$Title $Artist")
    # 两个已知端点都试：老接口与新 web 接口在某些网络下表现不同
    $urls = @(
        "https://music.163.com/api/search/get/web?s=$query&type=1&offset=0&limit=1",
        "https://music.163.com/api/search/get?s=$query&type=1&offset=0&limit=1"
    )
    $lastBrief = '<无响应>'
    $lastCode = 0
    foreach ($url in $urls) {
        try {
            $json = Invoke-RestMethod -Uri $url -TimeoutSec 8 -Headers $script:Headers
        }
        catch {
            Write-Diag ("搜索请求抛错：{0}" -f $_.Exception.Message)
            continue
        }
        if ($DumpJson) { Write-Host "search -> $($json | ConvertTo-Json -Depth 4 -Compress)" }

        $songs = $null
        if ($json.result -and $json.result.songs) { $songs = $json.result.songs }
        elseif ($json.result -and $json.result.song) { $songs = $json.result.song }
        # id 是 64 位的：3342981041 这种一放进 [int] 就抛异常（而搜索其实是成功的）
        if ($songs -and $songs.Count -gt 0) { return [long]$songs[0].id }

        $lastBrief = Get-Brief $json
        $lastCode = $json.code
    }
    # 所有端点都没给出可用列表时说清楚长什么样，别让它看起来像"歌词的问题"
    Write-Diag ("搜索在所有端点上都没命中：code={0} 响应={1}" -f $lastCode, $lastBrief)
    if ($lastBrief -match '"abroad":true') {
        Write-Diag "响应带 abroad=true（加密载荷），改用 -SongId 手工指定最省事"
    }
    0
}

function Get-LyricLines {
    param([string]$Title, [string]$Artist)
    if ($SongId -gt 0) { $id = $SongId }
    else { $id = Get-SongId -Title $Title -Artist $Artist }
    if ($id -le 0) { return @() }

    $urls = @(
        "https://music.163.com/api/song/lyric?id=$id&lv=-1&kv=-1&tv=-1",
        "https://music.163.com/api/song/lyric?os=pc&id=$id&lv=-1&kv=-1&tv=-1"
    )
    foreach ($url in $urls) {
        try {
            $json = Invoke-RestMethod -Uri $url -TimeoutSec 8 -Headers $script:Headers
        }
        catch {
            Write-Diag ("歌词请求抛错：{0}" -f $_.Exception.Message)
            continue
        }
        if ($DumpJson) { Write-Host "lyric -> $($json | ConvertTo-Json -Depth 3 -Compress)" }
        if ($json.lrc -and $json.lrc.lyric) {
            $parsed = @(ConvertFrom-Lrc -Text ([string]$json.lrc.lyric))
            # 纯音乐：接口在 lrc 里放的是「作曲/编曲」这类制作信息 + 一句「纯音乐，请欣赏」。
            # 把制作信息当歌词滚动是错的，所以只留那最后一句（没有就用末行兜底）。
            if ($json.pureMusic) {
                Write-Diag ("这是纯音乐曲目（pureMusic=true），只显示最后一行的说明")
                $pure = @($parsed | Where-Object { $_.Text -match '纯音乐' })
                if ($pure.Count -gt 0) { return $pure }
                if ($parsed.Count -gt 0) { return @($parsed[-1]) }
            }
            return $parsed
        }

        Write-Diag ("歌词返回里没有 lrc.lyric：id={0} code={1} 有 lrc={2} 有 tlyric={3} " +
            "nolyric={4} uncollected={5} pureMusic={6} 响应={7}" -f $id, $json.code,
            ($null -ne $json.lrc), ($null -ne $json.tlyric), ($null -ne $json.nolyric),
            ($null -ne $json.uncollected), ($null -ne $json.pureMusic), (Get-Brief $json))
    }
    @()
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
# 播放位置：媒体会话（真实位置）优先，推算时钟兜底
# ---------------------------------------------------------------------------
# 为什么是"助手进程 + 文件"而不是管道：SMTC 是 WinRT，只有 Windows PowerShell 5.1 读得到
# （pwsh 7 不行），所以位置必须由另一个宿主进程产生；而在 PowerShell 里做事件式异步读
# 很容易把两边一起卡死——写文件、读最后一行、再加上新鲜度判定，简单且不会互相阻塞。
function ConvertFrom-PositionLine {
    param([string]$Line)
    if (-not $Line) { return $null }
    $m = [regex]::Match($Line.Trim(), '^position=(-?\d+(?:\.\d+)?|-) status=(\w+)$')
    if (-not $m.Success) { return $null }
    if ($m.Groups[1].Value -eq '-') { return $null }          # 没有会话
    [pscustomobject]@{ Position = [double]$m.Groups[1].Value; Status = $m.Groups[2].Value }
}

function Test-PositionFresh {
    param($Sample, [datetime]$WrittenAt, [datetime]$Now, [int]$StaleMs)
    if (-not $Sample) { return $false }
    return (($Now - $WrittenAt).TotalMilliseconds -le $StaleMs)
}

# 暂停/缓冲时不推进——这正是"暂停"和"拖动后停住"能被跟上的原因；
# 播放时按采样年龄外推，保证两次采样之间也是连续的。
function Get-PositionMs {
    param($Sample, [double]$AgeSeconds)
    if (-not $Sample) { return $null }
    $seconds = $Sample.Position
    if ($Sample.Status -eq 'Playing') { $seconds += $AgeSeconds }
    [int]([math]::Max(0.0, $seconds) * 1000)
}

# 位置来源的标签（纯函数，-SelfTest 覆盖）。**这一行的"当前歌词"是算出来的**——用位置在
# 行时间戳上选行——所以"位置是哪来的"就是它准不准的全部答案。以前这个只能靠猜：链路上
# 每一步都可能失败并静默降级。现在把它当一个 key 发出去，面板上写 `{ext.netease.pos}`
# 就能看到（`dom 41.3s` = 迷你条的真 currentTime；`storeline 29.5s` = store 当前行的起始
# 时间（按行跳变）；`mem`/`cdp` = 内存差分 / CDP 其它位置；`clock` = 推算时钟，会漂）。
function Format-PositionSource {
    param([string]$Source, [int]$PositionMs, [int]$LeadMs = 0)
    $name = switch ($Source) {
        'media' { 'media' }
        'dom' { 'dom' }
        'storeline' { 'storeline' }
        'cdp' { 'cdp' }
        'memory' { 'mem' }
        default { 'clock' }
    }
    $leadText = if ($LeadMs -gt 0) { " +$($LeadMs)ms" } else { '' }
    return ("{0} {1:0.0}s{2}" -f $name, ($PositionMs / 1000.0), $leadText)
}

# 提早换行多少毫秒（纯函数，-SelfTest 覆盖）。**为什么要提早**——位置有两个系统性滞后：
#   · 迷你条 DOM 只给「分:秒」，位置是**向下取整到秒**的（真实 90.9 s 报 90）→ 平均落后 0.5 s、
#     最坏 1 s；
#   · 桥每 -IntervalMs（默认 1000 ms）才轮询一次 → 平均再落后半个周期。
# 所以 auto = 半个轮询周期，DOM 来源再补 0.5 s 的取整偏差（默认合计 1 s）。被提早的只是
# **选行用的位置**：发出去的 `pos` 仍是原始位置并标出提早量（`dom 90.0s +1000ms`），
# 所以"提早了多少、有没有过头"看得见。`-LeadMs 0` 关掉，`-LeadMs N` 用自己的值。
function Get-LeadMs {
    param([string]$Source, [int]$IntervalMs, [int]$OverrideMs = -1)
    if ($OverrideMs -ge 0) { return $OverrideMs }
    $lead = [int][math]::Round($IntervalMs / 2.0)
    if ($Source -eq 'dom') { $lead += 500 }
    return $lead
}

$script:PositionProc = $null
$script:PositionFile = Join-Path ([System.IO.Path]::GetTempPath()) 'endfield-netease-position.txt'

function Get-PowerShell51 {
    $exe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    if (Test-Path $exe) { return $exe }
    return 'powershell.exe'
}

function Start-PositionHelper {
    if (-not $PositionHelper) { $PositionHelper = Join-Path $PSScriptRoot '_netease-position.ps1' }
    if (-not (Test-Path $PositionHelper)) { return $false }
    Remove-Item $script:PositionFile -ErrorAction SilentlyContinue
    try {
        $script:PositionProc = Start-Process -FilePath (Get-PowerShell51) -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput $script:PositionFile `
            -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PositionHelper,
                            '-IntervalMs', '500')
    }
    catch {
        Write-Host ("  ! 位置助手启动失败：{0}" -f $_.Exception.Message)
        return $false
    }
    return $true
}

function Stop-PositionHelper {
    if ($script:PositionProc) {
        try { if (-not $script:PositionProc.HasExited) { $script:PositionProc.Kill() } } catch { }
        $script:PositionProc = $null
    }
}

function Get-PositionSample {
    if (-not (Test-Path $script:PositionFile)) { return $null }
    try {
        $info = Get-Item $script:PositionFile
        $line = Get-Content $script:PositionFile -Tail 1 -ErrorAction Stop
    }
    catch { return $null }
    $sample = ConvertFrom-PositionLine -Line $line
    if (-not (Test-PositionFresh -Sample $sample -WrittenAt $info.LastWriteTime -Now (Get-Date) `
                -StaleMs $PositionStaleMs)) { return $null }
    $sample | Add-Member -NotePropertyName At -NotePropertyValue $info.LastWriteTime -Force
    $sample
}

# ---------------------------------------------------------------------------
# 位置来源「memory」：从客户端内存读播放位置（毫秒 int32）
# ---------------------------------------------------------------------------
# 依据（真机实测，见 AGENTS §10）：客户端把文本存成 UTF-8；"当前歌词行"不是会变化的字符串
# （文本差分原理上无效）；而**播放位置是 int32 毫秒**，可以用"两次采样恰好增加一个间隔"的
# 差分扫描定位。地址是绝对的、受 ASLR 影响 → 所以：**先试缓存地址、不成立才重新发现**，
# 绝不写死地址。只读（PROCESS_QUERY_INFORMATION | PROCESS_VM_READ）。
$script:PosAddrFile = Join-Path ([System.IO.Path]::GetTempPath()) 'endfield-netease-posaddr.txt'
$script:MemReaderReady = $false

function Enable-MemReader {
    if ($script:MemReaderReady) { return $true }
    try {
        Add-Type @"
using System;
using System.Runtime.InteropServices;
public class NpMem {
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr OpenProcess(int a, bool i, int pid);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    const int QI = 0x0400, VM = 0x0010;
    // 返回十进制字符串；失败返回 "-" 或 "err<码>"（把错误码带出来，便于判别）
    public static string ReadOne(int pid, long addr) {
        IntPtr h = OpenProcess(QI | VM, false, pid);
        if (h == IntPtr.Zero) return "err" + Marshal.GetLastWin32Error();
        byte[] b = new byte[4];
        IntPtr read;
        bool ok = ReadProcessMemory(h, (IntPtr)addr, b, (IntPtr)4, out read) && (long)read == 4;
        CloseHandle(h);
        if (!ok) return "-";
        return (b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)).ToString();
    }
}
"@
        $script:MemReaderReady = $true
        return $true
    }
    catch {
        Write-Host ("  ! 内存读取器编译失败：{0}" -f $_.Exception.Message)
        return $false
    }
}

function Get-NeteasePid {
    $p = Get-Process -Name cloudmusic -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $p) {
        $p = Get-Process -ErrorAction SilentlyContinue |
             Where-Object { $_.ProcessName -like '*cloudmusic*' } | Select-Object -First 1
    }
    if ($p) { return $p.Id }
    return 0
}

# 纯函数：解析扫描器给出的一行候选（`0x…|int32ms|51800 -> 67070  (+15270ms)`）
function ConvertFrom-ScanCandidate {
    param([string]$Line)
    if (-not $Line) { return $null }
    $m = [regex]::Match($Line, '^\s*(0x[0-9A-Fa-f]+)\|int32ms\|\s*(\d+)\s*->\s*(\d+)')
    if (-not $m.Success) { return $null }
    [pscustomobject]@{
        Addr   = $m.Groups[1].Value
        Before = [int]$m.Groups[2].Value
        After  = [int]$m.Groups[3].Value
    }
}

# 纯函数：用两拍读数判断"是否在播放"（增长≈两拍间隔 → 播放；几乎不动 → 暂停）
function Test-Playing {
    param([int]$Prev, [int]$Now, [double]$ElapsedSeconds, [int]$ToleranceMs = 1500)
    $d = $Now - $Prev
    return ($d -ge ([int]($ElapsedSeconds * 1000) - $ToleranceMs))
}

# 进程身份：pid + 进程启动时间。地址是堆地址、受 ASLR 影响，**客户端一重启就变**（已实测：
# 老地址重启后不可访问）。所以缓存必须以"进程实例"为键，重启即自然作废。
function Get-NeteaseProcInfo {
    $p = Get-Process -Name cloudmusic -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $p) {
        $p = Get-Process -ErrorAction SilentlyContinue |
             Where-Object { $_.ProcessName -like '*cloudmusic*' } | Select-Object -First 1
    }
    if (-not $p) { return $null }
    $start = 0
    try { $start = $p.StartTime.Ticks } catch { $start = 0 }
    return [pscustomobject]@{ Pid = $p.Id; Start = $start }
}

function Read-PosCache {
    param($Proc)
    if (-not $Proc -or -not (Test-Path $script:PosAddrFile)) { return 0 }
    $s = Get-Content $script:PosAddrFile -First 1 -ErrorAction SilentlyContinue
    if (-not $s) { return 0 }
    $m = [regex]::Match($s.Trim(), '^(\d+)\|(\d+)\|(0x[0-9A-Fa-f]{4,15})$')
    if (-not $m.Success) { return 0 }
    if ([int]$m.Groups[1].Value -ne $Proc.Pid) { return 0 }
    if ([int64]$m.Groups[2].Value -ne $Proc.Start) { return 0 }    # 换了进程实例 → 作废
    try {
        $u = [System.Convert]::ToUInt64($m.Groups[3].Value.Substring(2), 16)
        return [System.BitConverter]::ToInt64([System.BitConverter]::GetBytes($u), 0)
    }
    catch { return 0 }
}

function Write-PosCache {
    param($Proc, [int64]$Addr)
    if (-not $Proc -or $Addr -eq 0) { return }
    try {
        ('{0}|{1}|0x{2:X}' -f $Proc.Pid, $Proc.Start, $Addr) |
            Set-Content -Path $script:PosAddrFile -Encoding ascii -ErrorAction Stop
    }
    catch { }
}

# 读一次原始值；不可用返回 $null
function Get-PosValue {
    param([int]$Pid, [int64]$Addr)
    if ($Pid -le 0 -or $Addr -eq 0) { return $null }
    $s = [NpMem]::ReadOne($Pid, $Addr)
    if ($s -notmatch '^\d+$') { return $null }
    return [int]$s
}

# 纯函数：切歌时的"粗校验"——曲名变了，位置应当回落到开头或明显跳变（而不是继续原样累加）
function Test-PosAfterTrackChange {
    param([int]$Prev, [int]$Now)
    if ($Now -lt $Prev) { return $true }                    # 归零/回落 ✓
    if ([math]::Abs($Now - $Prev) -gt 3000) { return $true } # 明显跳变 ✓
    return $false                                            # 几乎原样 → 可疑 ✗
}

function Test-PosAddrAlive {
    param([int]$Pid, [int64]$Addr)
    $a = Get-PosValue -Pid $Pid -Addr $Addr
    if ($null -eq $a -or $a -lt 1000) { return $false }
    Start-Sleep -Milliseconds 1000
    $b = Get-PosValue -Pid $Pid -Addr $Addr
    if ($null -eq $b) { return $false }
    return (Test-Playing -Prev $a -Now $b -ElapsedSeconds 1.0 -ToleranceMs 700)
}

# 差分发现：调用扫描器（发现逻辑只有那一处实现）。**有界**：每个客户端进程实例最多 3 次，
# 只在"确实在播放"时做，两次之间至少隔 60 秒；**绝不因为"值不再增长"而重扫**（用户定的策略：
# 进程活着就不要轻易全局扫描，值停住先靠切歌粗校验）。
$script:PosAttempts = 0
$script:PosLastAttempt = [datetime]::MinValue
$script:PosAttemptMax = 3
$script:PosAttemptSpacingSec = 60
$script:PosDisabled = $false

function Find-PosAddr {
    param($Proc, [switch]$TrackPlaying)
    if ($script:PosDisabled -or -not $Proc) { return 0 }
    if (-not $TrackPlaying) { return 0 }         # 没在播就没法差分（增长是判据），留到下次
    if ($script:PosAttempts -ge $script:PosAttemptMax) { return 0 }
    if (((Get-Date) - $script:PosLastAttempt).TotalSeconds -lt $script:PosAttemptSpacingSec) { return 0 }
    if (-not $ScannerPath) { $ScannerPath = Join-Path $PSScriptRoot '_netease-lyric-scan.ps1' }
    if (-not (Test-Path $ScannerPath)) { Write-Host '  ! 找不到位置扫描器，内存来源不可用'; return 0 }

    $script:PosAttempts++
    $script:PosLastAttempt = Get-Date
    Write-Host ("  内存位置：第 {0}/{1} 次差分发现（间隔 {2} 秒，请保持播放）…" -f `
            $script:PosAttempts, $script:PosAttemptMax, $MemoryGapSeconds)

    $out = & pwsh -NoProfile -ExecutionPolicy Bypass -File $ScannerPath `
        -WholeProcess -FindClock -GapSeconds $MemoryGapSeconds -MaxMb 512 -TargetPid $Proc.Pid 2>&1
    foreach ($line in $out) {
        $c = ConvertFrom-ScanCandidate $line
        if (-not $c) { continue }
        $addr = 0
        try {
            $addr = [System.BitConverter]::ToInt64(
                [System.BitConverter]::GetBytes([System.Convert]::ToUInt64($c.Addr.Substring(2), 16)), 0)
        }
        catch { continue }
        if ($addr -ne 0 -and (Test-PosAddrAlive -Pid $Proc.Pid -Addr $addr)) {
            Write-PosCache -Proc $Proc -Addr $addr
            return $addr
        }
    }
    Write-Host '  ! 这一轮没找到可用的位置地址（保持播放、稍后会再试一次；期间用推算时钟）'
    return 0
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

# CDP 位置：读页面里 audio/video 元素的 currentTime。
# 刻意用"通用锚点"而不是猜出来的私有选择器：-ProbeCdp 已把页面里有什么打印出来，真正的表达式
# 等那份输出回来再固化；在那之前这条能取到就用、取不到就返回 $null 继续降级 ✓。
function Get-CdpPositionMs {
    if (-not (Get-CdpTab)) { return $null }
    $expr = "(function(){var a=document.querySelector('audio,video');" +
            "if(!a||isNaN(a.currentTime))return '';" +
            "return JSON.stringify({t:a.currentTime,d:a.duration,p:a.paused});})()"
    $raw = Invoke-CdpExpression -Expression $expr
    if (-not $raw) { return $null }
    $m = [regex]::Match($raw, '"value"\s*:\s*"(\{.*?\})"')
    if (-not $m.Success) { return $null }
    $o = $null
    try { $o = ($m.Groups[1].Value -replace '\\"', '"') | ConvertFrom-Json } catch { return $null }
    if ($null -eq $o -or $null -eq $o.t) { return $null }
    try { return [int]([double]$o.t * 1000) } catch { return $null }
}

# ---------------------------------------------------------------------------
# CDP store：首选歌词来源（真机结论 2026-10-13）
# ---------------------------------------------------------------------------
# 当前行 = `async:lyric.lyricLines[playing.playingLyricLineNumber]` ✓✓
# —— 客户端自己维护的行号，**拖动进度条 / 暂停 / 切歌天然正确** ✓，不需要位置、不需要时间推算 ✓。
# 访问方式照 cloudmusic-desktop-mcp 的做法（server.py:283-287）：往 webpackJsonp 塞一个模块，
# 把 webpack 的 `require` 存到 window 上，再 `getStore()`（**不调 getState** ✓，见 server.py:339-346）。
#
# **规则化的探查**（用户要求保留）：模块 id **随客户端版本变** ✗（这台是 11，MCP 钉的是 12），
# 所以这里**不写死 id** ✓ —— 运行期扫 webpack 的**缓存** `req.c`（只含已实例化模块 ✓，**不执行任何模块** ✗）
# 找导出里带 `getStore` 的那个 ✓，并把结果缓存在页面上（`window.__ei_store_id__` ✓），
# 客户端升级后若失效会自己再找一次 ✓ -ProbeCdp 同样走这条自寻址的路 ✓。
# 单位陷阱（同一首歌、同一切片里三种单位 ✗）：`lyricLines[i].time` 是**秒** ✓、
# `curTrack.duration` 是**毫秒** ✓、`playing.resourceDuration` 是**秒** ✓。
# 迷你条上的"已播 / 总长"（mm:ss / mm:ss）—— 这是客户端**自己显示**的真 currentTime。
# 真机实测（2026-10-13，用户在 DevTools 里找到的控件）：
#   #page_pc_mini_bar … div.curtime-thumb … span.cmd-button-content  ->  "02:58 / 03:06"
# **不锚带哈希的类名**（TimeWarpper_t1eityq1 / SpaceContainer_s8o78dm 会随构建变），
# 只锚 "#page_pc_mini_bar + 文本形状 m:ss / m:ss"；并顺带读进度柄比例做交叉校验。
# 取**整条时间数组**（秒）—— 只在换歌时调一次。**只搬数字**（2000 行也才约 8 KB），
# 因此不需要假设长度；超出安全上限时**如实上报** truncated，而不是悄悄截断。
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
function Get-CdpDomTime {
    if (-not (Get-CdpTab)) { return $null }
    $expr = @"
(function(){
  try{
    var root = document.querySelector('#page_pc_mini_bar') || document;
    var nodes = root.querySelectorAll('span,div');
    var re = /^\s*(\d{1,3}):(\d{2})\s*\/\s*(\d{1,3}):(\d{2})\s*$/;
    for (var i = 0; i < nodes.length && i < 4000; i++) {
      var t = (nodes[i].textContent || '').trim(), m = re.exec(t);
      if (m) {
        var ratio = null;
        try {
          var th = root.querySelector('[class*=curtime], [class*=TimeWarpper], [class*=thumb]');
          var st = th ? (th.getAttribute('style') || '') : '';
          var lm = /left:\s*([\d.]+)%/.exec(st);
          if (lm) ratio = (+lm[1]) / 100;
        } catch (e2) { }
        return JSON.stringify({ ok:true, curS:(+m[1])*60+(+m[2]), totalS:(+m[3])*60+(+m[4]), ratio:ratio, raw:t });
      }
    }
    return JSON.stringify({ ok:false, why:'no mm:ss / mm:ss inside #page_pc_mini_bar' });
  }catch(x){ return JSON.stringify({ ok:false, why:''+x }); }
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
    var L=a.lyricLines||[], T=a.tlyricLines||[], i=p.playingLyricLineNumber;
    var ok=(typeof i==='number'&&i>=0&&i<L.length);
    return JSON.stringify({ok:true,moduleId:id,name:p.resourceName||'',trackId:p.resourceTrackId||'',
      state:p.playingState,durationMs:(p.curTrack&&p.curTrack.duration)||0,
      lines:L.length,index:i,offset:a.offset,usedLyric:a.currentUsedLyric,
      line:ok?(L[i].lyric||L[i].text||''):'',lineTimeSec:ok?L[i].time:null,
      trans:(typeof i==='number'&&i>=0&&i<T.length)?(T[i].lyric||T[i].text||''):''});
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
        Write-Host '  没找到 CDP 端口 —— 说明客户端不是带调试参数启动的。'
        # 注意顺序：这个分支在文件里执行得较早，Show-CdpShortcutGuidance 可能还没定义
        # （PowerShell 自上而下执行）。真正的降级路径（内存失败时）在文件后段调用它，那里已定义。
        if (Get-Command Show-CdpShortcutGuidance -ErrorAction SilentlyContinue) {
            Show-CdpShortcutGuidance
        }
        else {
            Write-Host '  改快捷方式的步骤：右键快捷方式 → 属性 → 「目标」→ 在引号内 exe 路径后追加'
            Write-Host '  --remote-debugging-port=9229 → 保存 → 完全退出客户端再启动 → 重跑 -ProbeCdp'
            Write-Host '  （本插件不会替你启动或重启音乐软件。）'
        }
    }
    else {
        Write-Host '  端口可用 ✓。下面用一条**只读探测表达式**问一下页面里有什么'
        Write-Host '  （页面标题、audio/video 的 currentTime 与时长、若干候选节点的文本）：'
        $res = Invoke-CdpExpression -Expression $script:CdpProbeExpression
        if ($res) {
            Write-Host ''
            Write-Host ('  ' + $res)
            Write-Host ''
            Write-Host '  把上面这行发我：据此固化"取位置与当前歌词行"的表达式，接成 -PositionSource cdp ✓'
        }
        else {
            Write-Host '  求值失败 ✗（页面可能不接受 Runtime.evaluate，或调试目标是别的东西）。'
            Write-Host '  把上面 target 的输出发我，我按它挑目标重试 ✓'
        }
    }
    exit 0
}

if ($ProbePosition) {


    if (-not $PositionHelper) { $PositionHelper = Join-Path $PSScriptRoot '_netease-position.ps1' }
    $exe = Get-PowerShell51
    Write-Host ("探测位置来源：{0} -Probe" -f $PositionHelper)
    & $exe -NoProfile -ExecutionPolicy Bypass -File $PositionHelper -Probe
    exit $LASTEXITCODE
}

# ---------------------------------------------------------------------------
# 位置来源「memory」的对外入口 + 「cdp」的手动启动指导
# ---------------------------------------------------------------------------
# 设计取舍（用户明确要求）：**插件绝不自己启动音乐软件**。CDP 只在客户端"已经被用户用
# 带调试端口的快捷方式启动"时才可用；我们要做的是**把怎么改快捷方式讲清楚**，而不是替他启动。
function Show-CdpShortcutGuidance {
    Write-Host ''
    Write-Host '  想要精确同步（拖动进度条、暂停都能跟上），有两条路，任选其一：'
    Write-Host '  ① 内存位置（默认）：本脚本会在客户端进程启动后自动发现一次，无需你做任何事；'
    Write-Host '  ② CDP（当内存读取被安全软件拦下、或你想彻底不碰内存时）：需要你手动给快捷方式加一个参数——'
    Write-Host '     · 右键网易云音乐的快捷方式 → 属性 → 「目标」'
    Write-Host '     · 在引号中的 exe 路径后面追加一个空格和：--remote-debugging-port=9229'
    Write-Host '       例如："D:\Program Files\NetEase\CloudMusic\cloudmusic.exe" --remote-debugging-port=9229'
    Write-Host '     · 保存后完全退出客户端再启动（改完第一次需要重启客户端才生效）'
    Write-Host '     · 之后跑一次：pwsh -File _netease-bridge.ps1 -ProbeCdp  验证端口可用'
    Write-Host '  说明：本插件**不会**替你启动或重启音乐软件（那属于改动你的使用习惯），只读取状态。'
    Write-Host ''
}

# 内存位置：返回毫秒；不可用返回 $null（调用方会继续往下降级）
$script:PosAddr = 0
$script:PosPid = 0
$script:PosStart = 0
$script:PosLastValue = $null
$script:PosLastAt = $null
$script:PosLastTitle = ''
$script:PosWarned = $false

function Get-MemoryPosition {
    param($Now)
    if ($script:PosDisabled) { return $null }
    if (-not (Enable-MemReader)) { return $null }
    $info = Get-NeteaseProcInfo
    if (-not $info) { return $null }

    # 进程实例换了（客户端重启）→ 缓存与地址一起作废，本生命周期重新发现一次
    if ($info.Pid -ne $script:PosPid -or $info.Start -ne $script:PosStart) {
        $script:PosPid = $info.Pid
        $script:PosStart = $info.Start
        $script:PosAddr = Read-PosCache -Proc $info
        $script:PosLastValue = $null
        $script:PosLastAt = $null
        $script:PosLastTitle = ''
        $script:PosAttempts = 0
        $script:PosDisabled = $false
        if ($script:PosAddr -ne 0) { Write-Host ('  位置：内存（沿用本进程实例的缓存地址 0x{0:X}）' -f $script:PosAddr) }
    }
    if ($script:PosAddr -eq 0) {
        # 只在"确实在播放"时发现（差分需要增长）；有界重试，绝不因为"值不增"而重扫
        $script:PosAddr = Find-PosAddr -Proc $info -TrackPlaying:($null -ne $Now)
        if ($script:PosAddr -ne 0) { Write-Host ('  位置：内存（发现成功，地址 0x{0:X}；本进程实例内不再扫描）' -f $script:PosAddr) }
        else { return $null }
    }

    $v = Get-PosValue -Pid $info.Pid -Addr $script:PosAddr
    if ($null -eq $v) {
        if (-not $script:PosWarned) {
            $script:PosWarned = $true
            Write-Host '  ! 内存位置读取失败（地址已失效或被安全软件拦下）——本次生命周期不再扫描，改用降级来源'
            Show-CdpShortcutGuidance
        }
        $script:PosDisabled = $true
        return $null
    }

    # 切歌粗校验：曲名变了，位置应当回落或明显跳变；否则判该地址失效（只禁用，不重扫）
    if ($Now -and $script:PosLastTitle -and $Now.Title -ne $script:PosLastTitle -and $null -ne $script:PosLastValue) {
        if (-not (Test-PosAfterTrackChange -Prev $script:PosLastValue -Now $v)) {
            Write-Host '  ! 切歌后位置未重置或未跳变——内存来源判定失效，本次生命周期改用降级来源'
            $script:PosDisabled = $true
            $script:PosAddr = 0
            return $null
        }
    }
    if ($Now) { $script:PosLastTitle = $Now.Title }

    # 暂停判定：两拍之间几乎不动 → 位置冻结（不推进）
    if ($null -ne $script:PosLastValue -and $null -ne $script:PosLastAt) {
        $elapsed = ((Get-Date) - $script:PosLastAt).TotalSeconds
        if (-not (Test-Playing -Prev $script:PosLastValue -Now $v -ElapsedSeconds $elapsed -ToleranceMs 900)) {
            $script:PosLastValue = $v
            $script:PosLastAt = Get-Date
            return [int]$v          # 暂停：直接给当前值，不外推
        }
    }
    $script:PosLastValue = $v
    $script:PosLastAt = Get-Date
    return [int]$v
}


if ($SelfTest) {
    # --- Select-StoreLineIndex（选行纯函数）：**不假设长度**（实测同一客户端见过 64 行与 67 行）---
    $irr = [object[]]@(0..79 | ForEach-Object { [double]($_ * 2.5) })   # object[]：才能真正放 null
    $irr[3] = $null                      # 元信息行没有时间戳 → 必须跳过
    $idxCases = @(
        @{ m = 0;       want = 0;    d = 'position 0 -> line 0 (intro)' },
        @{ m = 2500;    want = 1;    d = 'exact line start -> that line (closed interval)' },
        @{ m = 2501;    want = 1;    d = 'between lines -> previous line' },
        @{ m = 5000;    want = 2;    d = 'a null timestamp is skipped, no crash' },
        @{ m = 999999;  want = 79;   d = 'past the last line -> clamped to last' },
        @{ m = 1000;    want = -1;   d = 'empty array -> -1 (caller falls back to LRC)'; empty = $true },
        @{ m = 100;     want = 0;    d = 'single line array -> 0'; one = $true },
        @{ m = 1500500; want = 1499; d = '2000-line array -> the 1500th second = index 1499'; big = $true }
    )
    foreach ($c in $idxCases) {
        $ts = if ($c.empty) { [double[]]@() } elseif ($c.one) { [double[]]@(5) } elseif ($c.big) { [double[]](1..2000 | ForEach-Object { [double]$_ }) } else { $irr }
        $got = Select-StoreLineIndex -TimesSec $ts -PositionMs $c.m
        if ($got -ne $c.want) { $bad++; Write-Host ("  FAIL Select-StoreLineIndex: {0} (got {1}, want {2})" -f $c.d, $got, $c.want) }
        else { Write-Host ("  OK   Select-StoreLineIndex: {0}" -f $c.d) }
    }
    $fixture = @"
[ti:示例]
[00:00.00]第一句
[00:12.50]第二句
[01:05.001]第三句
[99:99.99]
"@
    # 一个固定时刻，给"两拍之间过了多久"这类断言用（不依赖真实时钟）。
    $stamp = [datetime]::new(2026, 10, 14, 12, 0, 0, [DateTimeKind]::Local)
    $lines = ConvertFrom-Lrc -Text $fixture
    $checks = @(
        @{ Name = '解析出 3 行（元信息与纯时间戳行被丢掉）'; Ok = ($lines.Count -eq 3) }
        @{ Name = '首行时间 0ms'; Ok = ($lines[0].At -eq 0) }
        @{ Name = '第二行 12500ms（两位小数）'; Ok = ($lines[1].At -eq 12500) }
        @{ Name = '第三行 65001ms（三位小数）'; Ok = ($lines[2].At -eq 65001) }
        @{ Name = '0ms 选第一句'; Ok = ((Select-LrcLine $lines 0) -eq '第一句') }
        @{ Name = '12.6s 选第二句'; Ok = ((Select-LrcLine $lines 12600) -eq '第二句') }
        @{ Name = '65.5s 选第三句'; Ok = ((Select-LrcLine $lines 65500) -eq '第三句') }
        @{ Name = '早于首行时也给第一句'; Ok = ((Select-LrcLine $lines -5000) -eq '第一句') }
        @{ Name = '空歌词给空串'; Ok = ((Select-LrcLine @() 1000) -eq '') }
        @{ Name = '位置行：正常采样'; Ok = ((ConvertFrom-PositionLine 'position=12.34 status=Playing').Position -eq 12.34) }
        @{ Name = '位置行：暂停状态可读'; Ok = ((ConvertFrom-PositionLine 'position=0 status=Paused').Status -eq 'Paused') }
        @{ Name = '位置行：无会话/垃圾/空一律不可用'; Ok = (-not (ConvertFrom-PositionLine 'position=- status=none')) -and (-not (ConvertFrom-PositionLine 'garbage')) -and (-not (ConvertFrom-PositionLine '')) }
        @{ Name = '暂停时位置不推进（拖完停住也不会漂）'; Ok = ((Get-PositionMs -Sample (ConvertFrom-PositionLine 'position=30 status=Paused') -AgeSeconds 5) -eq 30000) }
        @{ Name = '播放时按采样年龄外推'; Ok = ((Get-PositionMs -Sample (ConvertFrom-PositionLine 'position=30 status=Playing') -AgeSeconds 0.5) -eq 30500) }
        @{ Name = '采样过旧视为不可用'; Ok = (-not (Test-PositionFresh -Sample (ConvertFrom-PositionLine 'position=1 status=Playing') -WrittenAt (Get-Date).AddSeconds(-10) -Now (Get-Date) -StaleMs 3000)) }
        @{ Name = '采样新鲜则可用'; Ok = (Test-PositionFresh -Sample (ConvertFrom-PositionLine 'position=1 status=Playing') -WrittenAt (Get-Date) -Now (Get-Date) -StaleMs 3000) }
        @{ Name = '位置候选行：正常'; Ok = ((ConvertFrom-ScanCandidate '  0x80014BFB08|int32ms|51800 -> 67070  (+15270ms)').Addr -eq '0x80014BFB08') }
        @{ Name = '位置候选行：垃圾与空一律不可用'; Ok = (-not (ConvertFrom-ScanCandidate 'nonsense')) -and (-not (ConvertFrom-ScanCandidate '')) }
        @{ Name = '两拍判定：增长≈间隔 = 播放'; Ok = (Test-Playing -Prev 1000 -Now 2000 -ElapsedSeconds 1.0 -ToleranceMs 400) }
        @{ Name = '两拍判定：几乎不动 = 暂停'; Ok = (-not (Test-Playing -Prev 1000 -Now 1000 -ElapsedSeconds 1.0 -ToleranceMs 400)) }
        @{ Name = '切歌粗校验：回落或明显跳变 = 有效'; Ok = (Test-PosAfterTrackChange -Prev 60000 -Now 1200) -and (Test-PosAfterTrackChange -Prev 60000 -Now 20000) }
        @{ Name = '切歌粗校验：几乎原样 = 可疑'; Ok = (-not (Test-PosAfterTrackChange -Prev 60000 -Now 60200)) }
        @{ Name = '连不上管道时返回空值（不抛异常）'; Ok = ($null -eq (Connect-PipeClient -Pipe ("endfield-info-windhawk-selftest-{0}" -f [guid]::NewGuid().ToString('N')) -TimeoutMs 200 -WaitSeconds 0)) }
        # 岛事件的节流。歌词密时事件会来得比卡片能展示的快得多，所以"不要发那么多"是桥的责任，
        # mod 侧丢弃过旧事件只是兜底（两边都改了 ✓）。
        @{ Name = '岛事件：首次（从未发过）总是允许'; Ok = (Test-EventAllowed -Now $stamp -LastAt ([datetime]::MinValue) -MinMs 4000) }
        @{ Name = '岛事件：间隔不足时不发（密集歌词不刷屏）'; Ok = (-not (Test-EventAllowed -Now $stamp.AddMilliseconds(3999) -LastAt $stamp -MinMs 4000)) }
        @{ Name = '岛事件：达到最小间隔才发'; Ok = (Test-EventAllowed -Now $stamp.AddMilliseconds(4000) -LastAt $stamp -MinMs 4000) }
        @{ Name = '岛事件：间隔设为 0 表示不节流'; Ok = (Test-EventAllowed -Now $stamp -LastAt $stamp -MinMs 0) }
        # 曲目标题的判定。真机形状：同一进程里既有「桌面歌词」也有「歌名 - 歌手」，
        # 而后者不是 MainWindowTitle（2026-10-14 实测，见 Get-PlayingTrack 的注释）。
        @{ Name = '曲目标题：桌面歌词窗口不算曲目'; Ok = ($null -eq (Select-SongTitle @('桌面歌词', '网易云音乐'))) }
        @{ Name = '曲目标题：从多窗口里挑出带「 - 」的那个'; Ok = ((Select-SongTitle @('桌面歌词', '下等马 - 洛天依Official/ChiliChill乐团', '网易云音乐')) -eq '下等马 - 洛天依Official/ChiliChill乐团') }
        @{ Name = '曲目标题：多个候选取最长的'; Ok = ((Select-SongTitle @('A - B', '下等马 - 洛天依Official/ChiliChill乐团')) -eq '下等马 - 洛天依Official/ChiliChill乐团') }
        @{ Name = '曲目标题：空标题与空数组都不算'; Ok = ($null -eq (Select-SongTitle @('', '   ')) -and $null -eq (Select-SongTitle @())) }
        # 活的枚举：网易云在跑就必须至少看到一个顶层窗口（不在跑则这条无意义，视为通过）
        @{ Name = '窗口枚举：网易云在跑时能看到它的顶层窗口'; Ok = ((@(Get-NeteaseWindowTitles).Count -gt 0) -or (-not (Get-NeteasePid))) }
        # 位置来源标签：面板上的"当前歌词"是算出来的，所以这一行必须能说清位置是哪来的
        @{ Name = '位置来源标签：迷你条 DOM（真 currentTime）'; Ok = ((Format-PositionSource -Source dom -PositionMs 41300) -eq 'dom 41.3s') }
        @{ Name = '位置来源标签：store 当前行起始时间'; Ok = ((Format-PositionSource -Source storeline -PositionMs 29521) -eq 'storeline 29.5s') }
        @{ Name = '位置来源标签：内存差分 / 媒体会话'; Ok = ((Format-PositionSource -Source memory -PositionMs 1000) -eq 'mem 1.0s') -and ((Format-PositionSource -Source media -PositionMs 0) -eq 'media 0.0s') }
        @{ Name = '位置来源标签：未知来源按推算时钟算'; Ok = ((Format-PositionSource -Source nonsense -PositionMs 500) -eq 'clock 0.5s') }
        # 负延迟：位置有两个系统性滞后（DOM 向下取整到秒 + 每周期才轮询一次），按来源补偿
        @{ Name = '负延迟：DOM 来源 = 半周期 + 0.5s 取整偏差'; Ok = ((Get-LeadMs -Source dom -IntervalMs 1000) -eq 1000) }
        @{ Name = '负延迟：精确来源只补半个周期'; Ok = ((Get-LeadMs -Source media -IntervalMs 1000) -eq 500) -and ((Get-LeadMs -Source memory -IntervalMs 1000) -eq 500) }
        @{ Name = '负延迟：周期变了自动值跟着变'; Ok = ((Get-LeadMs -Source dom -IntervalMs 400) -eq 700) }
        @{ Name = '负延迟：显式值覆盖自动值（0 = 关掉）'; Ok = ((Get-LeadMs -Source dom -IntervalMs 1000 -OverrideMs 0) -eq 0) -and ((Get-LeadMs -Source dom -IntervalMs 1000 -OverrideMs 1500) -eq 1500) }
        @{ Name = 'pos 标签带出提早量'; Ok = ((Format-PositionSource -Source dom -PositionMs 90000 -LeadMs 1000) -eq 'dom 90.0s +1000ms') -and ((Format-PositionSource -Source dom -PositionMs 90000) -eq 'dom 90.0s') }
        # 一轮的发布计划。真机缺陷就在这个形状上：同一个 key 有两个发布者时，面板会在两个值
        # 之间闪 —— store 的 resourceName 没有作者、store 的当前行来自会冻结的行号，两者都曾
        # 直接 Publish，与最后拼好的值抢同一个 key（2026-10-14 实测 storeIndex 冻结在 10，
        # 而位置已经走到第 21 行以上）。有序字典让重复键在结构上不可能，这里钉住形状。
        @{ Name = '发布计划：标题带作者'; Ok = ((Get-PublishPlan -Title 'A' -Artist 'B' -Line 'x' -Translation '' -Pos 'p')['title'] -eq 'A - B') }
        @{ Name = '发布计划：没有作者时不留下悬空的分隔符'; Ok = ((Get-PublishPlan -Title 'A' -Artist '' -Line 'x' -Translation '' -Pos 'p')['title'] -eq 'A') }
        @{ Name = '发布计划：整轮恰好 title/lyric/translation/pos 四个 key'; Ok = (((Get-PublishPlan -Title 'A' -Artist 'B' -Line 'x' -Translation 't' -Pos 'p').Keys -join ',') -eq 'title,lyric,translation,pos') }
        @{ Name = '发布计划：取不到歌词时不发 lyric（而不是发空串盖掉上一行）'; Ok = (-not (Get-PublishPlan -Title 'A' -Artist 'B' -Line '' -Translation '' -Pos 'p').Contains('lyric')) }
        @{ Name = '发布计划：翻译总是出现（空串 = 这一行没有翻译，清掉上一行的）'; Ok = ((Get-PublishPlan -Title 'A' -Artist 'B' -Line 'x' -Translation '' -Pos '').Contains('translation')) }
        @{ Name = '发布计划：没有位置时不发 pos'; Ok = (-not (Get-PublishPlan -Title 'A' -Artist 'B' -Line 'x' -Translation '' -Pos '').Contains('pos')) }
        # 组合起来的效果：同一个行边界（10 s），不加提早量时 9.9 s 仍是第 0 行，
        # 加 1 s 后（=10.9 s）已经是第 1 行 —— 换行确实提前了 1 秒。
        @{ Name = '负延迟确实把换行提早（9.9s 仍是 0 行，+1s 后是 1 行）'; Ok = (((Select-StoreLineIndex -TimesSec @(0, 10, 20, 30) -PositionMs 9900) -eq 0) -and ((Select-StoreLineIndex -TimesSec @(0, 10, 20, 30) -PositionMs (9900 + 1000)) -eq 1)) }
    )
    $bad = 0
    # 活体证据：判定用的是这一份窗口列表，打出来省得下一个人再猜一次
    $liveWindows = @(Get-NeteaseWindowTitles)
    Write-Host ("网易云顶层窗口（{0} 个）：{1}" -f $liveWindows.Count, (($liveWindows | ForEach-Object { "[$_]" }) -join ' '))
    foreach ($c in $checks) {
        if ($c.Ok) { Write-Host ("  OK   {0}" -f $c.Name) } else { Write-Host ("  FAIL {0}" -f $c.Name); $bad++ }
    }
    Write-Host ("SELFTEST {0}" -f $(if ($bad -eq 0) { 'PASSED' } else { "FAILED ($bad)" }))
    exit $(if ($bad -eq 0) { 0 } else { 1 })
}

# ---------------------------------------------------------------------------
# 主循环
# ---------------------------------------------------------------------------
if ($ProbeStore) {
    # 只读探针：歌词"算在哪一行"（位置 + store 的行时间表）与"客户端自己认为在哪一行"
    # （playingLyricLineNumber）并排打出来。两者不一致、或者后者长时间不动，就是
    # "歌词对不准真实位置"的直接证据——所以**值绝不能用后者发布**（真机 2026-10-14）。
    Write-Host ("store 探针：{0} 轮，每 {1} ms 一轮" -f $ProbeRounds, $IntervalMs)
    $stx = Get-CdpStoreLyricTimes
    Write-Host ("行时间表：ok={0} 行数={1} trackId={2}" -f `
        $(if ($stx) { $stx.ok } else { 'n/a' }), `
        $(if ($stx -and $stx.times) { @($stx.times).Count } else { 0 }), `
        $(if ($stx) { $stx.trackId } else { '' }))
    for ($i = 1; $i -le $ProbeRounds; $i++) {
        $dom = Get-CdpDomTime
        $ss = Get-CdpStoreState
        $posMs = if ($dom -and $dom.ok) { [int]$dom.curS * 1000 } else { $null }
        $sel = -2
        if ($stx -and $stx.ok -and $null -ne $posMs) {
            $sel = Select-StoreLineIndex -TimesSec ([object[]]$stx.times) -PositionMs $posMs
        }
        $storeIdx = if ($ss -and $ss.ok -and $null -ne $ss.index) { [int]$ss.index } else { -2 }
        Write-Host ("  {0,2}. pos={1,-8} storeIndex={2,-4} byPosition={3,-4} times={4,-5} offset={5,-6} used={6,-14} storeLine='{7}'" -f `
            $i, $(if ($null -eq $posMs) { 'n/a' } else { "$($posMs)ms" }), $storeIdx, $sel, `
            $(if ($stx -and $stx.times) { @($stx.times).Count } else { 0 }), `
            $(if ($ss) { $ss.offset } else { '' }), $(if ($ss) { $ss.usedLyric } else { '' }), `
            $(if ($ss) { $ss.line } else { '' }))
        if ($i -lt $ProbeRounds) { Start-Sleep -Milliseconds $IntervalMs }
    }
    exit 0
}

$track = Get-PlayingTrack
if (-not $track) {
    # 发布器是被 mod 长期托管的（看门狗 / 作业对象负责收尾），所以"暂时没有窗口"不是退出理由：
    # 早先这里直接 exit 2，于是 mod 在 18 秒里把它启动了 5 次然后判定失败（真机日志 2026-10-13）。
    Write-Host ("暂时没有可用的网易云窗口（进程名匹配 '*{0}*'，某个顶层窗口标题里要有「 - 」）。" -f $ProcessName)
    Write-Host "每 5 秒重试；窗口出现就继续。若一直不出现，用 -ProcessName 指定真实进程名。"
    Write-Host "网易云进程的顶层窗口（这就是判定用的那一份；主窗口标题不算数）："
    $winTitles = @(Get-NeteaseWindowTitles)
    if ($winTitles.Count -eq 0) {
        Write-Host "  （一个都没看到——进程名不对，或者窗口还没建出来）"
    } else {
        $winTitles | ForEach-Object { Write-Host ("  [{0}]" -f $_) }
    }
    while (-not $track) {
        Start-Sleep -Seconds 5
        $track = Get-PlayingTrack
    }
    Write-Host ("已找到窗口：{0}" -f $track.Title)
}

Write-Host ("曲目：{0} — {1}" -f $track.Title, $track.Artist)
$lyricLines = @()
$lyricNote = '（未取到歌词）'
try {
    $lyricLines = @(Get-LyricLines -Title $track.Title -Artist $track.Artist)
    if ($lyricLines.Count -gt 0) {
        $lyricNote = "（{0} 行 LRC）" -f $lyricLines.Count
    }
    else { $lyricNote = '（接口没给歌词，或用 -SongId 手工指定）' }
}
catch { $lyricNote = "（歌词接口失败：$($_.Exception.Message)）" }
Write-Host ("歌词：{0}" -f $lyricNote)

$writer = $null
$positionMode = 'clock'

# 位置来源：先试媒体会话（真实位置），拿不到就退回推算时钟
if ($PositionSource -ne 'clock') {
    if (Start-PositionHelper) {
        $deadline = (Get-Date).AddSeconds(3)
        while ((Get-Date) -lt $deadline -and -not (Get-PositionSample)) { Start-Sleep -Milliseconds 200 }
        if (Get-PositionSample) { $positionMode = 'media' }
    }
    if ($positionMode -eq 'media') {
        Write-Host '位置：媒体会话（真实位置——拖动进度条、暂停都跟得上）'
    }
    elseif ($PositionSource -eq 'media') {
        Write-Host '位置：媒体会话不可用，且已要求只用它——歌词不会推进（先跑 -ProbePosition 看原因）'
        Stop-PositionHelper
    }
    else {
        Write-Host '位置：媒体会话不可用，退回推算时钟（手动拖动进度条后会失准，暂停时它也会继续走）'
        Stop-PositionHelper
    }
}
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

# 这首歌的起点：用「第一次看到它」当作 0 秒。LRC 行按这个时钟推进。
$started = Get-Date
$lastRaw = $track.Raw
$announced = $false
# 上一条岛事件的时间。初值取最小时间，于是"从未发过"永远允许第一条 ✓。
$script:LastEventAt = [datetime]::MinValue
# store（客户端自己的状态）缓存：曲目 id、行时间表、当前行号/文本/翻译。显式初始化一次，
# 免得"读到没赋值过的变量"看起来像有默认值。
$script:StoreTimes = $null
$script:StoreTimesTrack = ''
$script:StoreLineIndex = -1
$script:StoreLineText = ''
$script:StoreLineTrans = ''
$lyricAttemptAt = Get-Date
$lyricMisses = 0

try {
    while ($true) {
        Start-Sleep -Milliseconds $IntervalMs
        # 逐轮清空：同一轮里同一个 key 出现两次是缺陷，Publish 会点名（见那里的注释）。
        $script:IterationKeys.Clear()
        $now = Get-PlayingTrack

        if ($now -and $now.Raw -ne $lastRaw) {
            # 换歌：重新取歌词，并（可选）弹一次岛
            $track = $now
            $lastRaw = $now.Raw
            $started = Get-Date
            $script:Sent.Clear()
            # 换歌时 store 的缓存一起作废。不清的话，新歌第一拍若拿不到行时间表，
            # 选行就会用**上一首**的时间表、而 `$idx -ne $StoreLineIndex` 的判断还可能
            # 正好命中，于是上一首的最后一句被当成新歌的当前行发出去。
            $script:StoreTimes = $null
            $script:StoreTimesTrack = ''
            $script:StoreLineIndex = -1
            $script:StoreLineText = ''
            $script:StoreLineTrans = ''
            $lyricAttemptAt = Get-Date
            $lyricMisses = 0
            try { $lyricLines = @(Get-LyricLines -Title $track.Title -Artist $track.Artist) }
            catch { $lyricLines = @() }
            Write-Host ("换歌：{0} — {1}（{2} 行 LRC）" -f $track.Title, $track.Artist, $lyricLines.Count)
            if ($Event) {
                $first = Select-LrcLine $lyricLines 0
                [void](Write-IslandEvent -Writer $writer -Provider $Provider -CardMs $EventTtlMs `
                        -Title ("{0} - {1}" -f $track.Title, $track.Artist) -Body $first)
            }
            $announced = $true
        }

        if ($now) {
            # 没有歌词就隔一会儿再试。只在启动时查一次的话，接口抖动一下、或者网易云
            # 刚起来网络还没就绪，整首歌都会没有歌词——行里就一直是 --，而且看不出原因。
            if ($lyricLines.Count -eq 0 -and
                ((Get-Date) - $lyricAttemptAt).TotalMilliseconds -ge $LyricRetryMs) {
                $lyricAttemptAt = Get-Date
                try {
                    $lyricLines = @(Get-LyricLines -Title $now.Title -Artist $now.Artist)
                    if ($lyricLines.Count -gt 0) {
                        $lyricMisses = 0
                        Write-Host ("取到歌词：{0} 行" -f $lyricLines.Count)
                    }
                    elseif ($lyricMisses -lt 3) {
                        # 有界：说三次就够，之后安静重试，不刷屏
                        $lyricMisses++
                        Write-Host "（接口没给歌词，继续重试；用 -DumpJson 看原始返回、-SongId 手工指定）"
                    }
                }
                catch {
                    if ($lyricMisses -lt 3) {
                        $lyricMisses++
                        Write-Host ("（歌词接口失败：{0}）" -f $_.Exception.Message)
                    }
                }
            }
            # 首选位置来源：迷你条 DOM 的"已播 / 总长" —— 客户端**自己显示**的真 currentTime
            # （用户在 DevTools 里找到的控件；不锚哈希类名，理由见 Get-CdpDomTime 的注释）
            $positionMs = $null
            $posSrc = 'clock'
            if ($PositionSource -in 'auto', 'cdp') {
                $domTime = Get-CdpDomTime
                if ($domTime -and $domTime.ok) {
                    $positionMs = [int]$domTime.curS * 1000
                    $posSrc = 'dom'
                }
            }            # 首选来源：CDP store —— 精确到客户端自己的当前行 ✓；它的行时间顺便当位置 ✓
            # （用户实测：内存扫描极不稳定 ✗ → CDP/store 优先 ✓、内存降为次选 ✓。
            #   注意：store 的 lineTimeSec 是**行起始时间** ✓，会按行跳变；要连续毫秒仍需内存/SMTC ✓）
            # ⚠ 下面这行 `$positionMs = $null` **必须留在 DOM 那一段之前**：它后面再出现一次，
            #   就会把刚取到的真 currentTime 清掉，位置只能退回 store 的行起始时间（= 冻结的
            #   playingLyricLineNumber 那一行），歌词于是永远停在同一行 ✗（2026-10-14 修）。
            # ⚠ store 是**数据来源**，不是发布者：这里只把它的值读进变量，这一轮的最后统一发一次
            #   （见 Get-PublishPlan）。这里曾经有三个 Publish('title'/'lyric'/'translation')，
            #   它们和最后那批发布抢同一个 key：store 的 resourceName 没有作者，于是面板在
            #   "歌名"与"歌名 - 作者"之间闪（作者忽隐忽现）；store 的当前行来自会冻结的
            #   playingLyricLineNumber，于是歌词隔一拍就跳回旧行（2026-10-14 -DryRun 实测）。
            $storeLine = $null
            if ($LyricSource -in 'auto', 'store') {
                $ss = Get-CdpStoreState
                if ($ss -and $ss.ok) {
                    # 只在按位置选行的通路不可用时当兜底（见下面 $line 的拼装）；
                    # 绝不在这里发布，也不把 store 的翻译当值——它属于 store 自己那一行。
                    if ($ss.line) { $storeLine = '' + $ss.line }
                    if ($null -eq $positionMs -and $null -ne $ss.lineTimeSec) {
                        $positionMs = [int]([double]$ss.lineTimeSec * 1000)
                        $posSrc = 'storeline'
                    }
                }
            }
            # 位置降级链：cdp → memory → media → clock（store 没给出位置才往下走 ✓）
            if ($null -eq $positionMs -and $PositionSource -in 'auto', 'cdp') {
                $positionMs = Get-CdpPositionMs
                if ($null -ne $positionMs) { $posSrc = 'cdp' }
            }
            if ($null -eq $positionMs -and $PositionSource -in 'auto', 'memory') {
                $positionMs = Get-MemoryPosition -Now $now
                if ($null -ne $positionMs) { $posSrc = 'memory' }
            }
            if ($null -eq $positionMs -and $PositionSource -in 'auto', 'cdp') {
                $positionMs = Get-CdpPositionMs
                if ($null -ne $positionMs) { $posSrc = 'cdp' }
            }
            if ($null -eq $positionMs -and $positionMode -eq 'media') {
                $sample = Get-PositionSample
                if ($sample) {
                    $positionMs = Get-PositionMs -Sample $sample `
                        -AgeSeconds ((Get-Date) - $sample.At).TotalSeconds
                    if ($null -ne $positionMs) { $posSrc = 'media' }
                }
            }
            if ($null -eq $positionMs) {
                $positionMs = [int]((Get-Date) - $started).TotalMilliseconds
                $posSrc = 'clock'
            }
            # 换行的"负延迟"（见 Get-LeadMs）。⚠ 变量名**不能**叫 $leadMs：PowerShell 变量名
            # 大小写不敏感，那会覆盖 -LeadMs 参数本身，自动值就永远停在第一次算出来的数。
            $leadApply = Get-LeadMs -Source $posSrc -IntervalMs $IntervalMs -OverrideMs $LeadMs
            # 当前行：用位置在 store 的 times 上选（不再用那个冻结的 playingLyricLineNumber）
            # times 只在换歌时取一次（纯数字）；文本/翻译只在行号变化时按需取。
            if ($LyricSource -in 'auto', 'store') {
                $trackNow = if ($ss -and $ss.ok) { '' + $ss.trackId } else { '' }
                if (-not $script:StoreTimes -or $script:StoreTimesTrack -ne $trackNow) {
                    $stx = Get-CdpStoreLyricTimes
                    if ($stx -and $stx.ok) {
                        $script:StoreTimes = [object[]]$stx.times
                        $script:StoreTimesTrack = '' + $stx.trackId
                        $script:StoreLineIndex = -1
                        $script:StoreLineText = ''
                        $script:StoreLineTrans = ''
                    }
                }
                if ($script:StoreTimes -and $null -ne $positionMs) {
                    # 选行用**提早后**的位置（见 Get-LeadMs）：只影响换行的时刻，
                    # 发出去的 pos 仍是原始位置。
                    $idx = Select-StoreLineIndex -TimesSec $script:StoreTimes `
                        -PositionMs ([int]$positionMs + $leadApply)
                    if ($idx -ge 0 -and $idx -ne $script:StoreLineIndex) {
                        $ln = Get-CdpStoreLyricLine -Index $idx
                        if ($ln -and $ln.ok) {
                            $script:StoreLineIndex = $idx
                            $script:StoreLineText = '' + $ln.text
                            # 翻译跟着**这一行**走：新行没有翻译就清空，不能留着上一行的
                            # （它是同一个 key，留着的表现就是"翻译和歌词对不上"）。
                            $script:StoreLineTrans = if ($ln.trans) { '' + $ln.trans } else { '' }
                        }
                    }
                    if ($script:StoreLineText) { $storeLine = $script:StoreLineText }
                }
            }
            # 兜底：接口 LRC + 位置选行（store 不可用时才走这条 ✓）
            $line = if ($storeLine) { $storeLine } else { Select-LrcLine $lyricLines ([int]$positionMs + $leadApply) }

            # 一轮的帧只在这里生成：每个 key 恰好一条（见 Get-PublishPlan）。
            $plan = Get-PublishPlan -Title $now.Title -Artist $now.Artist -Line $line `
                -Translation $script:StoreLineTrans -Pos $(if ($null -ne $positionMs) {
                    Format-PositionSource -Source $posSrc -PositionMs ([int]$positionMs) `
                        -LeadMs $leadApply
                } else { '' })
            foreach ($key in $plan.Keys) { [void](Publish $writer $key $plan[$key]) }
            if ($Event -and -not $announced) {
                # 只有真的发出去了才算"播报过"：被节流掉的话，下一拍还有机会。
                if (Write-IslandEvent -Writer $writer -Provider $Provider -CardMs $EventTtlMs `
                        -Title ("{0} - {1}" -f $now.Title, $now.Artist) -Body $line) {
                    $announced = $true
                }
            }
        }

        if ($Once) { break }
    }
}
finally {
    Stop-PositionHelper
    if ($writer) {
        Write-Frame $writer 'BYE'
        $writer.Dispose()
        $client.Dispose()
    }
}
