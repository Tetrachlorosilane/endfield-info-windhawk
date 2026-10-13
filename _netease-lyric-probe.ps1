<#
.SYNOPSIS
    探针：网易云「桌面歌词」那一行文字，能否被正经 API 读到（**不读内存**）。

.DESCRIPTION
    存在的理由：读客户端内存能拿到当前歌词行（两篇 CSDN 博客的做法），但偏移随版本变化、
    还要 OpenProcess/ReadProcessMemory。而「桌面歌词」是一个真正的 Win32 窗口
    （类名 DesktopLyrics、标题 桌面歌词），所以先问一句：它的文字会不会已经通过
    窗口标题 / 子控件文本 / UI Automation 暴露出来了？会 → 零风险解决；不会 → 再谈内存。

    本脚本只做只读枚举：EnumWindows / EnumChildWindows / GetWindowText / GetClassName /
    UI Automation。不改任何东西、不读任何进程内存。

.NOTES
    建议用 Windows PowerShell 5.1 运行（UIA 与 Add-Type 在那里最稳）：
        powershell -NoProfile -ExecutionPolicy Bypass -File _netease-lyric-probe.ps1
    前提：网易云**正在播放**，并且客户端的「桌面歌词」已开启（那是这一行的宿主窗口）。
.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File _netease-lyric-probe.ps1 -All
#>
param(
    [string]$Class = 'DesktopLyrics',
    [string]$Title = '桌面歌词',
    # 顺带列出所有疑似网易云的窗口（类名/标题/进程名匹配），便于版本不同时对照
    [switch]$All
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class NpW {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int GetWindowLongW(IntPtr h, int i);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
}
"@

function Get-WinText { param([IntPtr]$H)
    $sb = New-Object Text.StringBuilder 512
    [void][NpW]::GetWindowTextW($H, $sb, 512)
    $sb.ToString()
}
function Get-WinClass { param([IntPtr]$H)
    $sb = New-Object Text.StringBuilder 256
    [void][NpW]::GetClassNameW($H, $sb, 256)
    $sb.ToString()
}
function Get-WinPid { param([IntPtr]$H)
    # 注意：不能叫 $pid —— 那是 PowerShell 的只读自动变量（本项目踩过一次同类坑）
    $procId = 0
    [void][NpW]::GetWindowThreadProcessId($H, [ref]$procId)
    $procId
}

# 看起来像"歌词行"的文本：含 CJK/假名/谚文，或形如 [00:12.34]
function Test-LyricLike { param([string]$S)
    if (-not $S) { return $false }
    if ($S -match '[\u3040-\u30ff\u3400-\u4dbf\u4e00-\u9fff\uac00-\ud7af]') { return $true }
    if ($S -match '^\s*[\[\(]\d{1,2}:\d{2}') { return $true }
    return $false
}

$script:Found = New-Object Collections.ArrayList

# 顶层窗口枚举
$cb = [NpW+EnumProc]{
    param([IntPtr]$h, [IntPtr]$l)
    $cls = Get-WinClass $h
    $txt = Get-WinText $h
    $p = Get-WinPid $h
    $proc = (Get-Process -Id $p -ErrorAction SilentlyContinue).ProcessName
    if ($cls -eq $Class -or $txt -eq $Title -or ($All -and ($cls -match 'lyric|Lyric' -or $proc -match 'cloudmusic|Netease'))) {
        [void]$script:Found.Add([pscustomobject]@{ H = $h; Cls = $cls; Text = $txt; Pid = $p; Proc = $proc })
    }
    return $true
}
[void][NpW]::EnumWindows($cb, [IntPtr]::Zero)

if ($script:Found.Count -eq 0) {
    Write-Host "没找到「桌面歌词」窗口（类名 '$Class' / 标题 '$Title'）。"
    Write-Host "→ 如果客户端没在播放、或「桌面歌词」没开，这是正常的：请在网易云里开启桌面歌词后再跑一次。"
    Write-Host "→ 如果开着播放也找不到，用 -All 再跑一次，把输出发我（可能类名/标题随版本变了）。"
    exit 2
}

foreach ($w in $script:Found) {
    $ex = [NpW]::GetWindowLongW($w.H, -20)      # GWL_EXSTYLE
    $layered = ($ex -band 0x80000) -ne 0
    Write-Host ("=== 窗口 class={0} title='{1}' pid={2} ({3}) visible={4} layered={5}" -f `
            $w.Cls, $w.Text, $w.Pid, $w.Proc, [NpW]::IsWindowVisible($w.H), $layered)
    Write-Host ("    自身标题是否像歌词: {0}" -f (Test-LyricLike $w.Text))

    # 1) 子控件文本（原生控件走这条）
    $kids = New-Object Collections.ArrayList
    $ccb = [NpW+EnumProc]{
        param([IntPtr]$h, [IntPtr]$l)
        [void]$kids.Add([pscustomobject]@{ Cls = (Get-WinClass $h); Text = (Get-WinText $h) })
        return $true
    }
    [void][NpW]::EnumChildWindows($w.H, $ccb, [IntPtr]::Zero)
    if ($kids.Count -eq 0) { Write-Host "    子控件: 无" }
    else {
        Write-Host ("    子控件 {0} 个：" -f $kids.Count)
        $kids | Select-Object -First 25 | ForEach-Object {
            Write-Host ("      class={0,-24} text='{1}' lyricLike={2}" -f $_.Cls, $_.Text, (Test-LyricLike $_.Text))
        }
    }

    # 2) UI Automation：名字 / 控件类型 / 可读的 Text/Value/LegacyIAccessible
    try {
        $cond = New-Object Windows.Automation.PropertyCondition(
            [Windows.Automation.AutomationElement]::NativeWindowHandleProperty, [int]$w.H)
        $root = [Windows.Automation.AutomationElement]::RootElement.FindFirst(
            [Windows.Automation.TreeScope]::Children, $cond)
        if (-not $root) { Write-Host "    UIA: 找不到对应元素"; continue }

        $all = $root.FindAll([Windows.Automation.TreeScope]::Descendants,
            [Windows.Automation.Condition]::TrueCondition)
        Write-Host ("    UIA 后代元素 {0} 个：" -f $all.Count)
        $shown = 0
        foreach ($e in $all) {
            if ($shown -ge 30) { break }
            $name = ''
            try { $name = $e.Current.Name } catch { }
            $cls2 = ''
            try { $cls2 = $e.Current.ClassName } catch { }
            $ct = ''
            try { $ct = $e.Current.ControlType.ProgrammaticName } catch { }
            $val = ''
            foreach ($pt in 'TextPattern', 'ValuePattern', 'LegacyIAccessiblePattern', 'RangeValuePattern') {
                try {
                    $p = $e.GetCurrentPattern([Windows.Automation.AutomationPattern]::$pt)
                    if ($p) {
                        if ($pt -eq 'TextPattern' -and $p.DocumentRange) { $val = $p.DocumentRange.GetText(-1) }
                        elseif ($p.Current) { $val = [string]$p.Current.Value }
                    }
                } catch { }
            }
            $joined = (@($name, $val) | Where-Object { $_ }) -join ' | '
            if ($joined) {
                Write-Host ("      {0,-34} {1,-24} '{2}' lyricLike={3}" -f $ct, $cls2, $joined, (Test-LyricLike $joined))
                $shown++
            }
        }
    }
    catch { Write-Host ("    UIA 枚举失败：{0}" -f $_.Exception.Message) }
}

Write-Host ''
Write-Host '结论怎么读：'
Write-Host '  上面任何一行的 text/名字里出现歌词内容（lyricLike=True 而它不是「桌面歌词」四个字）→ 可以走正经 API，不碰内存。'
Write-Host '  全是空/只有窗口标题 → 这行是自绘的，正经 API 读不到；那时我们再谈「读内存」的可选方案。'
