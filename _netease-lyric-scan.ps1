<#
.SYNOPSIS
    探针：在网易云进程里用「特征扫描」找当前歌词行（**只读**，不写、不注入）。

.DESCRIPTION
    两篇 CSDN 博客的做法是硬编码"dll 基址 + 若干偏移"。那是别人客户端版本的地址，
    客户端一升级就失效（第二篇就是第一篇两年后失效的实证）。这里换成特征扫描：

      1. 只打开 PROCESS_VM_READ | PROCESS_QUERY_INFORMATION（**不用** PROCESS_ALL_ACCESS）；
      2. 只扫 MEM_PRIVATE 的可读提交区（堆；跳过映像、跳过 guard 页）；
      3. 在字节流里找「UTF-16LE 文本 + 含 CJK」的串，作为候选；
      4. **隔几秒再采一次**：候选里"文本变了"的那些，就是客户端正在刷新的当前歌词行。

    第 4 步是关键：不靠偏移、不靠版本，只靠"这一行会随时间变化"这个事实。

.NOTES
    只读、可随时 Ctrl+C；扫描量由 -MaxMb 限制。
.EXAMPLE
    pwsh -File _netease-lyric-scan.ps1 -GapSeconds 6
#>
param(
    # 留空则自动用「桌面歌词」窗口所属进程
    [int]$TargetPid = 0,
    # 两次采样之间的间隔（秒）——歌词要在这段时间里至少换一行
    [int]$GapSeconds = 6,
    # 扫描上限（MB），越大越全也越慢
    [int]$MaxMb = 512,
    # 候选上限（内存里会同时存在大量歌词文本，属正常）
    [int]$MaxHits = 800
)

$ErrorActionPreference = 'Stop'

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public class NpScan {
    [StructLayout(LayoutKind.Sequential)]
    public struct MBI {
        public IntPtr BaseAddress;
        public IntPtr AllocationBase;
        public uint AllocationProtect;
        public uint __a1;
        public IntPtr RegionSize;
        public uint State;
        public uint Protect;
        public uint Type;
        public uint __a2;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool VirtualQueryEx(IntPtr h, IntPtr addr, out MBI mbi, IntPtr len);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);

    const int PROCESS_QUERY_INFORMATION = 0x0400;
    const int PROCESS_VM_READ = 0x0010;
    const uint MEM_COMMIT = 0x1000;
    const uint MEM_PRIVATE = 0x20000;
    const uint PAGE_GUARD = 0x100;
    const uint PAGE_NOACCESS = 0x01;

    static bool IsCjk(char c) {
        return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) ||
               (c >= 0x3040 && c <= 0x30FF) || (c >= 0xAC00 && c <= 0xD7AF);
    }
    static bool IsTextish(char c) {
        if (c == 0) return false;
        if (IsCjk(c)) return true;
        if (char.IsLetterOrDigit(c)) return true;
        return " .,:;!?'\"()[]<>-–—…&/\\|+*%#@".IndexOf(c) >= 0;
    }

    // 返回 "地址|文本"，只收含 CJK 的串
    public static List<string> Scan(int pid, int maxMb, int maxHits, out long scannedBytes) {
        var found = new List<string>();
        scannedBytes = 0;
        IntPtr h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, pid);
        if (h == IntPtr.Zero) throw new Exception("OpenProcess 失败，错误码 " + Marshal.GetLastWin32Error());

        long cap = (long)maxMb * 1024 * 1024;
        byte[] buf = new byte[4 * 1024 * 1024];
        IntPtr addr = IntPtr.Zero;
        MBI mbi;
        int mbiSize = Marshal.SizeOf(typeof(MBI));

        while (VirtualQueryEx(h, addr, out mbi, (IntPtr)mbiSize) != false) {
            IntPtr next = (IntPtr)((long)mbi.BaseAddress + (long)mbi.RegionSize);
            bool ok = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
                      (mbi.Protect & PAGE_GUARD) == 0 && (mbi.Protect & PAGE_NOACCESS) == 0 &&
                      (mbi.Protect & 0xFF) != 0;
            if (ok) {
                long size = (long)mbi.RegionSize;
                long off = 0;
                while (off < size) {
                    int chunk = (int)Math.Min((long)buf.Length, size - off);
                    IntPtr read;
                    if (ReadProcessMemory(h, (IntPtr)((long)mbi.BaseAddress + off), buf, (IntPtr)chunk, out read) && (long)read > 0) {
                        int n = (int)(long)read;
                        scannedBytes += n;
                        int i = 0;
                        while (i + 1 < n) {
                            if (buf[i + 1] == 0 && IsCjk((char)buf[i])) {
                                int start = i;
                                // 向前补全
                                while (start >= 2 && buf[start - 1] == 0 && IsTextish((char)buf[start - 2])) start -= 2;
                                int end = i;
                                while (end + 1 < n && buf[end + 1] == 0 && IsTextish((char)buf[end])) end += 2;
                                int len = (end - start) / 2 + 1;
                                if (len >= 2) {
                                    char[] chars = new char[len];
                                    for (int k = 0; k < len; k++) chars[k] = (char)buf[start + k * 2];
                                    string s = new string(chars);
                                    int cjk = 0;
                                    foreach (char c in s) if (IsCjk(c)) cjk++;
                                    if (cjk >= 2) {
                                        long pa = (long)mbi.BaseAddress + off + start;
                                        found.Add("0x" + pa.ToString("X") + "|" + s);
                                        if (found.Count >= maxHits) { CloseHandle(h); return found; }
                                    }
                                }
                                i = end + 2;
                                continue;
                            }
                            i += 2;
                        }
                    }
                    off += chunk;
                    if (scannedBytes >= cap) { CloseHandle(h); return found; }
                }
            }
            if ((long)next <= (long)mbi.BaseAddress) break;
            addr = next;
            if (scannedBytes >= cap) break;
        }
        CloseHandle(h);
        return found;
    }
}
"@

function Get-LyricWindowPid {
    if ($TargetPid -gt 0) { return $TargetPid }
    Add-Type -AssemblyName UIAutomationClient -ErrorAction SilentlyContinue
    foreach ($p in Get-Process -ErrorAction SilentlyContinue) {
        if ($p.MainWindowTitle -eq '桌面歌词') { return $p.Id }
    }
    return 0
}

$target = Get-LyricWindowPid
if ($target -le 0) {
    Write-Host "没找到「桌面歌词」窗口所属的进程（请确认音乐在播、桌面歌词已显示），或用 -TargetPid 指定。"
    Write-Host "提示：窗口标题是「桌面歌词」；这个窗口属于 cloudmusic.exe。"
    exit 2
}
Write-Host ("目标进程 pid={0}；扫描上限 {1} MB" -f $target, $MaxMb)

Write-Host "第一次采样…"
$scanned = 0
$first = [NpScan]::Scan($target, $MaxMb, $MaxHits, [ref]$scanned)
Write-Host ("  候选 {0} 条，读了 {1:N0} MB" -f $first.Count, ($scanned / 1MB))

Write-Host ("等待 {0} 秒（让歌词换行）…" -f $GapSeconds)
Start-Sleep -Seconds $GapSeconds

Write-Host "第二次采样…"
$second = [NpScan]::Scan($target, $MaxMb, $MaxHits, [ref]$scanned)

# 同一个地址、文本变了 → 就是客户端在刷新的那一行
$map2 = @{}
foreach ($row in $second) {
    $i = $row.IndexOf('|')
    $map2[$row.Substring(0, $i)] = $row.Substring($i + 1)
}
$changed = New-Object Collections.ArrayList
foreach ($row in $first) {
    $i = $row.IndexOf('|')
    $addr = $row.Substring(0, $i)
    $before = $row.Substring($i + 1)
    if ($map2.ContainsKey($addr) -and $map2[$addr] -ne $before) {
        [void]$changed.Add([pscustomobject]@{ Addr = $addr; Before = $before; After = $map2[$addr] })
    }
}

Write-Host ''
Write-Host ("=== 两次之间发生变化、且含中文的候选：{0} 条 ===" -f $changed.Count)
$changed | Select-Object -First 12 | ForEach-Object {
    Write-Host ("  {0}  '{1}'  ->  '{2}'" -f $_.Addr, $_.Before, $_.After)
}
Write-Host ''
if ($changed.Count -gt 0) {
    Write-Host '结论：特征扫描成立 ✓ —— 变化的那一条就是当前歌词行（不依赖版本偏移）。'
    Write-Host '下一步：把它做成 bridge 的 -LyricSource memory（最小权限、校验失败即回退）。'
}
else {
    Write-Host '结论：这一轮没抓到"变化的歌词行"。可能原因：这段时间没换行（换首歌或等久一点）、'
    Write-Host '      扫描上限太小（加大 -MaxMb）、或歌词文本不在 MEM_PRIVATE 堆里（用 -MaxMb 更大再试）。'
}
