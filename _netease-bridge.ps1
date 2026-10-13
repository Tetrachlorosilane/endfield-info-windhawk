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
    # auto = 依次尝试 memory → media → clock
    # cdp = 通过调试端口读页面里的实际播放时间（需要用户自己给快捷方式加调试参数）；
    [ValidateSet('auto', 'memory', 'cdp', 'media', 'clock')]
    [string]$PositionSource = 'auto',
    # 内存发现时两次采样的间隔（秒）——这段时间里必须在播放
    [int]$MemoryGapSeconds = 12,
    # 只探一次 CDP（不读位置）：列出端口与调试目标，供下一步确定取值表达式
    [switch]$ProbeCdp,
    # 依次尝试的 CDP 端口（两个成熟项目分别用 9229 / 9223，9222 是通用默认）
    [int[]]$CdpPorts = @(9229, 9223, 9222),
    # 位置扫描器路径，缺省用同目录的 _netease-lyric-scan.ps1
    [string]$ScannerPath = '',
    # 位置助手脚本，缺省用同目录的 _netease-position.ps1
    [string]$PositionHelper = '',
    # 超过这么久没有新采样就当成不可用（毫秒）
    [int]$PositionStaleMs = 3000,
    # 只探一次位置来源并打印结论
    [switch]$ProbePosition
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
# ---------------------------------------------------------------------------
function Get-PlayingTrack {
    $procs = @(Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.ProcessName -like "*$ProcessName*" -and $_.MainWindowTitle })
    if ($procs.Count -eq 0) { return $null }

    $title = $procs[0].MainWindowTitle.Trim()
    # 没在播时标题就是产品名，没有「 - 」
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

function Write-Frame {
    param($Writer, [string]$Line)
    if ($DryRun) { Write-Host "  -> $Line"; return }
    $Writer.WriteLine($Line)
    $Writer.Flush()
}

function Publish {
    param($Writer, [string]$Key, [string]$Text)
    if ($script:Sent[$Key] -eq $Text) { return $false }
    $script:Sent[$Key] = $Text
    if ($TtlMs -gt 0) { Write-Frame $Writer "SET $Provider $Key $TtlMs $Text" }
    else { Write-Frame $Writer "SET $Provider $Key - $Text" }
    $true
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
  // 真机结论（2026-10-13）：DOM 里既没有当前时间也没有进度条，更没有歌词 ✗
  // （找到的 m:ss 全是歌单每首的时长；那个页面此刻是首页视图）。两个成熟项目走的是
  // **读页面里的 React store**（Redux：带 getState ✓）而不是抠 DOM ✓，所以这里补上这一步：
  // 列出 window 上带 getState 的对象，以及它 state 顶层的键名（只列键，不 dump 内容）。
  o.stores = {};
  for (var k in window) {
    try {
      var v = window[k];
      if (v && typeof v.getState === 'function') {
        var s = v.getState();
        o.stores[k] = s ? Object.keys(s).slice(0, 24) : [];
      }
    } catch (x) { }
  }
  return JSON.stringify(o);
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
    $fixture = @"
[ti:示例]
[00:00.00]第一句
[00:12.50]第二句
[01:05.001]第三句
[99:99.99]
"@
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
    )
    $bad = 0
    foreach ($c in $checks) {
        if ($c.Ok) { Write-Host ("  OK   {0}" -f $c.Name) } else { Write-Host ("  FAIL {0}" -f $c.Name); $bad++ }
    }
    Write-Host ("SELFTEST {0}" -f $(if ($bad -eq 0) { 'PASSED' } else { "FAILED ($bad)" }))
    exit $(if ($bad -eq 0) { 0 } else { 1 })
}

# ---------------------------------------------------------------------------
# 主循环
# ---------------------------------------------------------------------------
$track = Get-PlayingTrack
if (-not $track) {
    Write-Host "没找到在播放的网易云窗口（进程名匹配 '*$ProcessName*'，标题里要有「 - 」）。"
    Write-Host "确认它在播放，或用 -ProcessName 指定真实的进程名："
    Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowTitle } |
        ForEach-Object { Write-Host ("  {0,-28} {1}" -f $_.ProcessName, $_.MainWindowTitle) }
    exit 2
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
    $client = New-Object System.IO.Pipes.NamedPipeClientStream('.', $Pipe, [System.IO.Pipes.PipeDirection]::InOut)
    $client.Connect(3000)
    $writer = New-Object System.IO.StreamWriter($client, (New-Object System.Text.UTF8Encoding($false)))
    $writer.AutoFlush = $true
    Write-Frame $writer "HELLO 1 $Provider"
    Write-Host ("已连上管道 {0}" -f $Pipe)
}

# 这首歌的起点：用「第一次看到它」当作 0 秒。LRC 行按这个时钟推进。
$started = Get-Date
$lastRaw = $track.Raw
$announced = $false
$lyricAttemptAt = Get-Date
$lyricMisses = 0

try {
    while ($true) {
        Start-Sleep -Milliseconds $IntervalMs
        $now = Get-PlayingTrack

        if ($now -and $now.Raw -ne $lastRaw) {
            # 换歌：重新取歌词，并（可选）弹一次岛
            $track = $now
            $lastRaw = $now.Raw
            $started = Get-Date
            $script:Sent.Clear()
            $lyricAttemptAt = Get-Date
            $lyricMisses = 0
            try { $lyricLines = @(Get-LyricLines -Title $track.Title -Artist $track.Artist) }
            catch { $lyricLines = @() }
            Write-Host ("换歌：{0} — {1}（{2} 行 LRC）" -f $track.Title, $track.Artist, $lyricLines.Count)
            if ($Event -and $writer) {
                $first = Select-LrcLine $lyricLines 0
                Write-Frame $writer "EVENT $Provider info $TtlMs $($track.Title) - $($track.Artist)  $first"
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
            # 位置降级链：memory（默认，进程存活期内只发现一次）→ media → clock
            $positionMs = $null
            if ($PositionSource -in 'auto', 'memory') {
                $positionMs = Get-MemoryPosition -Now $now
            }
            if ($null -eq $positionMs -and $PositionSource -in 'auto', 'cdp') {
                $positionMs = Get-CdpPositionMs
            }
            if ($null -eq $positionMs -and $positionMode -eq 'media') {
                $sample = Get-PositionSample
                if ($sample) {
                    $positionMs = Get-PositionMs -Sample $sample `
                        -AgeSeconds ((Get-Date) - $sample.At).TotalSeconds
                }
            }
            if ($null -eq $positionMs) {
                $positionMs = [int]((Get-Date) - $started).TotalMilliseconds
            }
            $line = Select-LrcLine $lyricLines $positionMs

            [void](Publish $writer 'title' ("{0} - {1}" -f $now.Title, $now.Artist))
            if ($line) { [void](Publish $writer 'lyric' $line) }
            if ($Event -and -not $announced -and $writer) {
                Write-Frame $writer "EVENT $Provider info $TtlMs $($now.Title) - $($now.Artist)  $line"
                $announced = $true
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
