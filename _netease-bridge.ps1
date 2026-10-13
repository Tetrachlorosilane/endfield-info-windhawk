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
    [int]$LyricRetryMs = 20000
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
# -SelfTest：不联网、不连管道，只验证解析与选行
# ---------------------------------------------------------------------------
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
            $elapsedMs = [int]((Get-Date) - $started).TotalMilliseconds
            $line = Select-LrcLine $lyricLines $elapsedMs

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
    if ($writer) {
        Write-Frame $writer 'BYE'
        $writer.Dispose()
        $client.Dispose()
    }
}
