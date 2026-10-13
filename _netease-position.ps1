<#
.SYNOPSIS
    把当前媒体会话的播放位置与状态按行吐出来，供 _netease-bridge.ps1 使用。

.DESCRIPTION
    为什么单独一个脚本：Windows 的媒体会话（SMTC）是 WinRT API，而 **WinRT 投影只在
    Windows PowerShell 5.1 里可用**，pwsh 7 默认读不到。所以这里负责"读位置"，
    由主脚本用 `powershell.exe` 起它，再读它写出的那一行行采样。

    输出格式（每行一次采样）：
        position=12.34 status=Playing
        position=- status=none        ← 没有会话（没在播 / 客户端没注册 SMTC）

    它只报告，不做任何判断：新鲜度、暂停冻结、歌词选行都在主脚本里（那里有断言）。

.EXAMPLE
    powershell -NoProfile -File _netease-position.ps1 -Probe
#>
param(
    # 采样间隔（毫秒）
    [int]$IntervalMs = 500,
    # 采样多少次后退出；0 = 一直跑
    [int]$Samples = 0,
    # 只探一次并打印结论，用于"这台机器到底有没有 SMTC"
    [switch]$Probe
)

$ErrorActionPreference = 'Stop'

function Await-Operation {
    param($Operation, [Type]$ResultType)
    $asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() |
        Where-Object {
            $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
            $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
        } | Select-Object -First 1
    if (-not $asTask) { throw 'AsTask(IAsyncOperation<T>) not found: not a WinRT-capable host' }
    $task = $asTask.MakeGenericMethod($ResultType).Invoke($null, @($Operation))
    $task.Wait(-1) | Out-Null
    $task.Result
}

function Get-MediaSample {
    try {
        [void][Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media.Control, ContentType = WindowsRuntime]
    }
    catch {
        # pwsh 7 会走到这里：没有 WinRT 投影
        return $null
    }
    try {
        $manager = Await-Operation ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager])
        if (-not $manager) { return $null }
        $session = $manager.GetCurrentSession()
        if (-not $session) { return $null }
        $timeline = $session.GetTimelineProperties()
        $status = $session.GetPlaybackInfo().PlaybackStatus.ToString()
        [pscustomobject]@{
            Position = [math]::Round($timeline.Position.TotalSeconds, 3)
            Status   = $status
            App      = $session.SourceAppUserModelId
        }
    }
    catch { return $null }
}

if ($Probe) {
    $sample = Get-MediaSample
    if ($sample) {
        'PROBE ok position={0} status={1} app={2}' -f $sample.Position, $sample.Status, $sample.App
        exit 0
    }
    # 分清两种失败：宿主不支持 WinRT（换 powershell.exe 5.1 再试），还是没有会话
    $host7 = $PSVersionTable.PSVersion.Major -ge 6
    if ($host7) { 'PROBE unsupported-host (run this under powershell.exe 5.1)' }
    else { 'PROBE none (no active media session)' }
    exit 3
}

$sent = 0
while ($true) {
    $sample = Get-MediaSample
    if ($sample) {
        'position={0} status={1}' -f $sample.Position, $sample.Status
    }
    else {
        'position=- status=none'
    }
    [Console]::Out.Flush()
    $sent++
    if ($Samples -gt 0 -and $sent -ge $Samples) { break }
    Start-Sleep -Milliseconds $IntervalMs
}
