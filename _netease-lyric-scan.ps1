<#
.SYNOPSIS
    探针：在网易云进程里定位「当前歌词行」（**只读**，默认只扫指定模块的数据区）。

.DESCRIPTION
    为什么只扫模块：两篇 CSDN 博客的指针链最终都落在 `cloudmusic.dll` 基址 + 固定偏移，
    也就是说当前歌词行存在 **该 DLL 自己的数据段（全局缓冲）** 里。只扫那一段：
      · 范围从几百 MB 降到几十 MB，快得多；
      · 地址两次采样应当相同（静态缓冲）——"文本变了"的那条即当前歌词行；
      · 不吃版本偏移，只吃"它会随时间变化"这个事实。

    用 -WholeProcess 退回全进程扫描（模块里找不到时的对照）。

.NOTES
    只读：PROCESS_QUERY_INFORMATION | PROCESS_VM_READ；不写、不注入、不改内存。
.EXAMPLE
    pwsh -File _netease-lyric-scan.ps1 -TargetPid 47180 -GapSeconds 6
#>
param(
    # 留空则自动用「桌面歌词」窗口所属进程
    [int]$TargetPid = 0,
    # 要扫的模块（默认 cloudmusic.dll —— 两篇博客指针链的根都在它里面）
    [string]$Module = 'cloudmusic.dll',
    # 两次采样间隔（秒）：这段时间里歌词至少要换一行
    [int]$GapSeconds = 6,
    # 文本编码：utf8 / utf16 / both。解析器可能用 std::string（UTF-8）保存歌词，
    # 而只找 UTF-16LE 会给出 0 候选 —— 所以要能分别验证（默认两种都找）。
    [ValidateSet('utf8', 'utf16', 'both')]
    [string]$Encoding = 'both',
    # 全进程扫描（跳过模块限制，只看私有堆），上限 MB
    [switch]$WholeProcess,
    [int]$MaxMb = 512,
    [int]$MaxHits = 2000,
    # 换一种找法：找"播放位置"（随时间增加的整数），而不是找歌词文本
    [switch]$FindClock,
    # 差分容差（毫秒）：只报"增加量 ≈ 间隔 ± 这个值"的地址
    [int]$ClockToleranceMs = 1500,
    # 观察模式：只读这几个绝对地址（十六进制，如 0x80014BFB08），每秒打印一次
    [string[]]$WatchAddrs = @(),
    [int]$WatchSeconds = 40,
    [int]$WatchIntervalMs = 500
)

$ErrorActionPreference = 'Stop'

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public class NpScan {
    [StructLayout(LayoutKind.Sequential)]
    public struct MBI {
        public IntPtr BaseAddress, AllocationBase;
        public uint AllocationProtect, __a1;
        public IntPtr RegionSize;
        public uint State, Protect, Type, __a2;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct MODULEENTRY32W {
        public uint dwSize, th32ModuleID, th32ProcessID, GlblcntUsage, ProccntUsage;
        public IntPtr modBaseAddr;
        public uint modBaseSize;
        public IntPtr hModule;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string szModule;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string szExePath;
    }

    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr OpenProcess(int a, bool i, int pid);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, IntPtr n);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr CreateToolhelp32Snapshot(uint flags, uint pid);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool Module32FirstW(IntPtr s, ref MODULEENTRY32W m);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool Module32NextW(IntPtr s, ref MODULEENTRY32W m);

    const int PROCESS_QUERY_INFORMATION = 0x0400, PROCESS_VM_READ = 0x0010;
    const uint MEM_COMMIT = 0x1000, PAGE_GUARD = 0x100, PAGE_NOACCESS = 0x01;
    const uint TH32CS_SNAPMODULE = 0x00000008, TH32CS_SNAPMODULE32 = 0x00000010;

    static IntPtr Open(int pid) {
        IntPtr h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, pid);
        if (h == IntPtr.Zero) throw new Exception("OpenProcess 失败，错误码 " + Marshal.GetLastWin32Error());
        return h;
    }

    // 返回 "名称|基址(hex)|大小(dec)"
    public static List<string> Modules(int pid) {
        var res = new List<string>();
        IntPtr snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, (uint)pid);
        if (snap == (IntPtr)(-1)) throw new Exception("CreateToolhelp32Snapshot 失败，错误码 " + Marshal.GetLastWin32Error());
        var me = new MODULEENTRY32W();
        me.dwSize = (uint)Marshal.SizeOf(typeof(MODULEENTRY32W));
        if (Module32FirstW(snap, ref me)) {
            do {
                res.Add(me.szModule + "|0x" + ((long)me.modBaseAddr).ToString("X") + "|" + me.modBaseSize);
                me.dwSize = (uint)Marshal.SizeOf(typeof(MODULEENTRY32W));
            } while (Module32NextW(snap, ref me));
        }
        CloseHandle(snap);
        return res;
    }

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

    // UTF-8：以 CJK 的三字节序列为锚点，向前后扩展成「可打印 ASCII + 合法 UTF-8 序列」的一整段
    static bool AsciiPrintable(byte b) { return b >= 0x20 && b <= 0x7E; }

    static void ScanUtf8(byte[] buf, int n, long baseAddr, long off, List<string> found, int maxHits) {
        int i = 0;
        while (i + 2 < n) {
            if (buf[i] >= 0xE0 && buf[i] <= 0xEF && (buf[i + 1] & 0xC0) == 0x80 && (buf[i + 2] & 0xC0) == 0x80) {
                int s = i;
                while (s > 0 && AsciiPrintable(buf[s - 1])) s--;
                int e = i + 3;
                while (e < n) {
                    if (AsciiPrintable(buf[e])) { e++; continue; }
                    if (buf[e] >= 0xC2 && buf[e] <= 0xDF && e + 1 < n && (buf[e + 1] & 0xC0) == 0x80) { e += 2; continue; }
                    if (buf[e] >= 0xE0 && buf[e] <= 0xEF && e + 2 < n && (buf[e + 1] & 0xC0) == 0x80 && (buf[e + 2] & 0xC0) == 0x80) { e += 3; continue; }
                    if (buf[e] >= 0xF0 && buf[e] <= 0xF4 && e + 3 < n && (buf[e + 1] & 0xC0) == 0x80 && (buf[e + 2] & 0xC0) == 0x80 && (buf[e + 3] & 0xC0) == 0x80) { e += 4; continue; }
                    break;
                }
                int len = e - s;
                if (len >= 3 && len <= 1200) {
                    string t = System.Text.Encoding.UTF8.GetString(buf, s, len).Trim();
                    int cjk = 0;
                    foreach (char c in t) if (IsCjk(c)) cjk++;
                    if (cjk >= 2) {
                        found.Add("0x" + (baseAddr + off + s).ToString("X") + "|u8|" + t);
                        if (found.Count >= maxHits) return;
                    }
                }
                i = e;
                continue;
            }
            i++;
        }
    }

    // 观察模式：只读这几个绝对地址的 int32（用于验证"暂停冻结 / 拖动跳变 / 切歌重置"）
    public static List<string> ReadInt32s(int pid, long[] addrs) {
        var res = new List<string>();
        IntPtr h = Open(pid);
        foreach (long a in addrs) {
            byte[] b = new byte[4];
            IntPtr read;
            if (ReadProcessMemory(h, (IntPtr)a, b, (IntPtr)4, out read) && (long)read == 4) {
                int v = b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
                res.Add(v.ToString());
            }
            else { res.Add("-"); }
        }
        CloseHandle(h);
        return res;
    }

    // ---- 播放位置（时钟）差分扫描 ----
    // 为什么换这个思路：歌词文本换行时【一个字节都不变】（整首 LRC 常驻），
    // 而播放位置【本来就随时间增加】——差分扫描正是为"会匀速增长的量"设计的。
    // 判据：值必须落在 1 秒 ~ 1 小时（毫秒）内，且两次之间恰好增加了 ≈ 一个播放间隔。
    public static Dictionary<long, int> ClockMap = new Dictionary<long, int>();

    static bool ClockRegionOk(MBI mbi, bool heapOnly) {
        bool ok = mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_GUARD) == 0 &&
                  (mbi.Protect & PAGE_NOACCESS) == 0 && (mbi.Protect & 0xFF) != 0;
        return heapOnly ? (ok && mbi.Type == 0x20000) : ok;
    }

    public static List<string> ClockPass1(int pid, long start, long size, int maxMb, out long scanned, out int kept) {
        var map = new Dictionary<long, int>();
        scanned = 0; kept = 0;
        IntPtr h = Open(pid);
        long cap = size > 0 ? size : (long)maxMb * 1024 * 1024;
        long limit = size > 0 ? start + size : long.MaxValue;
        byte[] buf = new byte[4 * 1024 * 1024];
        IntPtr addr = size > 0 ? (IntPtr)start : IntPtr.Zero;
        MBI mbi; int mbiSize = Marshal.SizeOf(typeof(MBI));
        while (VirtualQueryEx(h, addr, out mbi, (IntPtr)mbiSize)) {
            long baseAddr = (long)mbi.BaseAddress;
            if (size > 0 && baseAddr >= limit) break;
            long next = baseAddr + (long)mbi.RegionSize;
            if (ClockRegionOk(mbi, size <= 0)) {
                long off = 0, rs = (long)mbi.RegionSize;
                while (off < rs) {
                    int chunk = (int)Math.Min((long)buf.Length, rs - off);
                    IntPtr read;
                    if (ReadProcessMemory(h, (IntPtr)(baseAddr + off), buf, (IntPtr)chunk, out read) && (long)read > 0) {
                        int n = (int)(long)read;
                        scanned += n;
                        for (int i = 0; i + 4 <= n; i += 4) {
                            int v = buf[i] | (buf[i + 1] << 8) | (buf[i + 2] << 16) | (buf[i + 3] << 24);
                            if (v >= 1000 && v <= 3600000) { map[baseAddr + off + i] = v; kept++; }
                        }
                    }
                    off += chunk;
                    if (size <= 0 && scanned >= cap) break;
                }
            }
            if (next <= baseAddr) break;
            addr = (IntPtr)next;
            if (size <= 0 && scanned >= cap) break;
        }
        CloseHandle(h);
        ClockMap = map;
        return new List<string>();
    }

    public static List<string> ClockPass2(int pid, long start, long size, int maxMb, int gapMs, int tolMs,
                                          int maxHits, out long scanned, out int advanced, out int same, out int decreased) {
        var hits = new List<string>();
        scanned = 0; advanced = 0; same = 0; decreased = 0;
        IntPtr h = Open(pid);
        long cap = size > 0 ? size : (long)maxMb * 1024 * 1024;
        long limit = size > 0 ? start + size : long.MaxValue;
        byte[] buf = new byte[4 * 1024 * 1024];
        IntPtr addr = size > 0 ? (IntPtr)start : IntPtr.Zero;
        MBI mbi; int mbiSize = Marshal.SizeOf(typeof(MBI));
        while (VirtualQueryEx(h, addr, out mbi, (IntPtr)mbiSize)) {
            long baseAddr = (long)mbi.BaseAddress;
            if (size > 0 && baseAddr >= limit) break;
            long next = baseAddr + (long)mbi.RegionSize;
            if (ClockRegionOk(mbi, size <= 0)) {
                long off = 0, rs = (long)mbi.RegionSize;
                while (off < rs) {
                    int chunk = (int)Math.Min((long)buf.Length, rs - off);
                    IntPtr read;
                    if (ReadProcessMemory(h, (IntPtr)(baseAddr + off), buf, (IntPtr)chunk, out read) && (long)read > 0) {
                        int n = (int)(long)read;
                        scanned += n;
                        for (int i = 0; i + 4 <= n; i += 4) {
                            int v = buf[i] | (buf[i + 1] << 8) | (buf[i + 2] << 16) | (buf[i + 3] << 24);
                            if (v < 1000 || v > 3600000) continue;
                            long a = baseAddr + off + i;
                            int v0;
                            if (!ClockMap.TryGetValue(a, out v0)) continue;
                            int d = v - v0;
                            if (d > 0) advanced++; else if (d == 0) same++; else decreased++;
                            if (d >= gapMs - tolMs && d <= gapMs + tolMs) {
                                hits.Add("0x" + a.ToString("X") + "|int32ms|" + v0 + " -> " + v + "  (+" + d + "ms)");
                                if (hits.Count >= maxHits) { CloseHandle(h); return hits; }
                            }
                        }
                    }
                    off += chunk;
                    if (size <= 0 && scanned >= cap) break;
                }
            }
            if (next <= baseAddr) break;
            addr = (IntPtr)next;
            if (size <= 0 && scanned >= cap) break;
        }
        CloseHandle(h);
        return hits;
    }

    // 扫 [start, start+size)；size<=0 表示按 maxMb 全进程扫（只看私有堆）
    public static List<string> Scan(int pid, long start, long size, int maxMb, int maxHits, out long scanned) {
        var found = new List<string>();
        scanned = 0;
        IntPtr h = Open(pid);
        long cap = size > 0 ? size : (long)maxMb * 1024 * 1024;
        long limit = size > 0 ? start + size : long.MaxValue;
        byte[] buf = new byte[4 * 1024 * 1024];
        IntPtr addr = size > 0 ? (IntPtr)start : IntPtr.Zero;
        MBI mbi;
        int mbiSize = Marshal.SizeOf(typeof(MBI));

        while (VirtualQueryEx(h, addr, out mbi, (IntPtr)mbiSize)) {
            long baseAddr = (long)mbi.BaseAddress;
            if (size > 0 && baseAddr >= limit) break;
            long next = baseAddr + (long)mbi.RegionSize;
            bool readable = mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_GUARD) == 0 &&
                            (mbi.Protect & PAGE_NOACCESS) == 0 && (mbi.Protect & 0xFF) != 0;
            if (size <= 0) readable = readable && mbi.Type == 0x20000;
            if (readable) {
                long off = 0, regionSize = (long)mbi.RegionSize;
                while (off < regionSize) {
                    int chunk = (int)Math.Min((long)buf.Length, regionSize - off);
                    IntPtr read;
                    if (ReadProcessMemory(h, (IntPtr)(baseAddr + off), buf, (IntPtr)chunk, out read) && (long)read > 0) {
                        int n = (int)(long)read;
                        scanned += n;
                        int i = 0;
                        while (i + 1 < n) {
                            if (buf[i + 1] == 0 && IsCjk((char)buf[i])) {
                                int s = i;
                                while (s >= 2 && buf[s - 1] == 0 && IsTextish((char)buf[s - 2])) s -= 2;
                                int e = i;
                                while (e + 1 < n && buf[e + 1] == 0 && IsTextish((char)buf[e])) e += 2;
                                int len = (e - s) / 2 + 1;
                                if (len >= 2) {
                                    char[] cs = new char[len];
                                    for (int k = 0; k < len; k++) cs[k] = (char)buf[s + k * 2];
                                    string t = new string(cs);
                                    int cjk = 0;
                                    foreach (char c in t) if (IsCjk(c)) cjk++;
                                    if (cjk >= 2) {
                                        found.Add("0x" + (baseAddr + off + s).ToString("X") + "|u16|" + t);
                                        if (found.Count >= maxHits) { CloseHandle(h); return found; }
                                    }
                                }
                                i = e + 2;
                                continue;
                            }
                            i += 2;
                        }
                        ScanUtf8(buf, n, baseAddr, off, found, maxHits);
                    }
                    off += chunk;
                    if (size <= 0 && scanned >= cap) { CloseHandle(h); return found; }
                }
            }
            if (next <= baseAddr) break;
            addr = (IntPtr)next;
            if (size <= 0 && scanned >= cap) break;
        }
        CloseHandle(h);
        return found;
    }
}

// 桌面歌词不是进程主窗口：Get-Process 的 MainWindowTitle 看不到它，只能 EnumWindows
public class NpWin {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
}
"@

function Get-LyricWindowPid {
    if ($TargetPid -gt 0) { return $TargetPid }
    $script:lyricPid = 0
    $cb = [NpWin+EnumProc]{
        param([IntPtr]$h, [IntPtr]$l)
        $sb = New-Object Text.StringBuilder 256
        [void][NpWin]::GetWindowTextW($h, $sb, 256)
        if ($sb.ToString() -eq '桌面歌词') {
            [uint32]$found = 0
            [void][NpWin]::GetWindowThreadProcessId($h, [ref]$found)
            $script:lyricPid = [int]$found
            return $false
        }
        return $true
    }
    [void][NpWin]::EnumWindows($cb, [IntPtr]::Zero)
    return $script:lyricPid
}

$target = Get-LyricWindowPid
if ($target -le 0) {
    Write-Host '没找到「桌面歌词」窗口（音乐要在播、桌面歌词要显示），或用 -TargetPid 指定 pid。'
    exit 2
}
Write-Host ("目标进程 pid={0}" -f $target)

$start = 0
$size = 0
if (-not $WholeProcess) {
    try { $mods = [NpScan]::Modules($target) }
    catch { Write-Host ("枚举模块失败：{0}" -f $_.Exception.Message); exit 3 }
    $hit = $mods | Where-Object { $_ -like "$Module|*" } | Select-Object -First 1
    if (-not $hit) {
        Write-Host ("目标进程里没有模块 '{0}'。现有模块（前 20 个）：" -f $Module)
        $mods | Select-Object -First 20 | ForEach-Object { "  $_" }
        exit 4
    }
    $parts = $hit.Split('|')
    $start = [Convert]::ToInt64($parts[1].Substring(2), 16)
    $size = [int64]$parts[2]
    Write-Host ("扫描模块 {0}：基址 0x{1:X}，大小 {2:N1} MB" -f $parts[0], $start, ($size / 1MB))
}
else {
    Write-Host ("全进程扫描（私有堆，上限 {0} MB）" -f $MaxMb)
}

if ($WatchAddrs.Count -gt 0) {
    # 十六进制可能超过 Int64 正数范围（0x8001… > 2^63），所以按 UInt64 解析再按位转回
    $addrs = New-Object 'System.Collections.Generic.List[long]'
    # 参数可能是一整个拼接串（踩过：'0x80014BFB08,0x800C31EF10' 被当成一个字符串 →
    # 去掉非十六进制字符后变成 20 位、超过 UInt64）。所以按"十六进制串"逐个抽取。
    $raw = ($WatchAddrs -join ' ')
    foreach ($m in [regex]::Matches($raw, '(?i)(?:0x)?([0-9a-f]{6,15})')) {
        $hex = $m.Groups[1].Value
        $u = [System.Convert]::ToUInt64($hex, 16)
        $addrs.Add([System.BitConverter]::ToInt64([System.BitConverter]::GetBytes($u), 0))
    }
    if ($addrs.Count -eq 0) { Write-Host '没解析出任何地址（用法：-WatchAddrs 0x80014BFB08,0x800C31EF10）'; exit 2 }
    Write-Host ("=== 观察 {0} 个地址，共 {1} 秒（每 {2} ms 一次） ===" -f $addrs.Count, $WatchSeconds, $WatchIntervalMs)
    Write-Host '请在这段时间里依次做：① 暂停  ② 恢复  ③ 拖动进度条  ④ 切歌'
    Write-Host '看数字的增减提示（括号里是相对上一次的差值）。'
    Write-Host ''
    $t0 = Get-Date
    $prev = $null
    while (((Get-Date) - $t0).TotalSeconds -lt $WatchSeconds) {
        $vals = [NpScan]::ReadInt32s($target, $addrs.ToArray())
        $line = ("{0,6:N1}s " -f ((Get-Date) - $t0).TotalSeconds)
        for ($k = 0; $k -lt $addrs.Count; $k++) {
            $d = ''
            if ($prev -ne $null -and $prev[$k] -ne '-' -and $vals[$k] -ne '-') {
                $delta = [int64]$vals[$k] - [int64]$prev[$k]
                if ($delta -ne 0) { $d = ('({0:+#;-#;0})' -f $delta) }
            }
            $line += (' [{0}]={1}{2}' -f $k, $vals[$k], $d)
        }
        Write-Host $line
        $prev = $vals
        Start-Sleep -Milliseconds $WatchIntervalMs
    }
    Write-Host ''
    Write-Host '判读：① 暂停 → 数字应冻住（没有差值）；③ 拖动 → 应出现远超"每拍约 +500ms"的大跳变；'
    Write-Host '      ④ 切歌 → 应归零或重置。三条都成立 = 这就是播放位置 ✓'
    exit 0
}

if ($FindClock) {
    Write-Host ("=== 播放位置差分扫描：间隔 {0} 秒，容差 ±{1} ms ===" -f $GapSeconds, $ClockToleranceMs)
    Write-Host '第一次采样（记下所有 1 秒 ~ 1 小时范围内的 int32 毫秒值）…'
    $scanned = 0
    $kept = 0
    [void][NpScan]::ClockPass1($target, $start, $size, $MaxMb, [ref]$scanned, [ref]$kept)
    Write-Host ("  记下 {0:N0} 个候选整数，读了 {1:N1} MB" -f $kept, ($scanned / 1MB))
    Write-Host ("请让音乐【正常播放】{0} 秒（不要暂停、不要拖动）…" -f $GapSeconds)
    Start-Sleep -Seconds $GapSeconds
    Write-Host '第二次采样…'
    $adv = 0; $same = 0; $dec = 0
    $hits = [NpScan]::ClockPass2($target, $start, $size, $MaxMb, ($GapSeconds * 1000), $ClockToleranceMs,
        $MaxHits, [ref]$scanned, [ref]$adv, [ref]$same, [ref]$dec)
    Write-Host ("  同一地址上：增加 {0:N0}  不变 {1:N0}  减少 {2:N0}" -f $adv, $same, $dec)
    Write-Host ''
    Write-Host ("=== 增加量 ≈ {0} 秒的候选：{1} 条 ===" -f $GapSeconds, $hits.Count)
    $hits | Select-Object -First 20 | ForEach-Object { Write-Host ("  " + $_) }
    Write-Host ''
    if ($hits.Count -eq 0) {
        if ($same -gt $adv) { Write-Host '诊断：绝大多数"不变" → 当时很可能在【暂停】✓（那就切到播放再跑一次）。' }
        else { Write-Host '诊断：有增长但没有"恰好一个间隔"的 → 位置可能不是 int32 毫秒（试试秒制/float、或对子进程跑）。' }
    }
    else {
        Write-Host '拿到的地址要过三关（三条全中才是播放位置）：'
        Write-Host '  ① 现在【暂停】，再跑一次 → 同样的地址应几乎全是"不变"；'
        Write-Host '  ② 【拖动进度条】，再跑一次 → 应出现远超容差的大跳变；'
        Write-Host '  ③ 【切歌】 → 该值应归零或重置。'
        Write-Host '三关中任何一关不成立，就说明这不是我们要的位置（我会照实说，不硬套）。'
    }
    exit 0
}

Write-Host '第一次采样…'
$scanned = 0
$first = [NpScan]::Scan($target, $start, $size, $MaxMb, $MaxHits, [ref]$scanned)
Write-Host ("  候选 {0} 条，读了 {1:N1} MB" -f $first.Count, ($scanned / 1MB))

Write-Host ("等待 {0} 秒（让歌词换行）…" -f $GapSeconds)
Start-Sleep -Seconds $GapSeconds

Write-Host '第二次采样…'
$second = [NpScan]::Scan($target, $start, $size, $MaxMb, $MaxHits, [ref]$scanned)

function Split-Hit { param([string]$Row)
    $p = $Row.Split('|', 3)
    [pscustomobject]@{ Key = ($p[0] + '|' + $p[1]); Addr = $p[0]; Enc = $p[1]; Text = $p[2] }
}

# 按 -Encoding 过滤
$want = @{ 'utf16' = 'u16'; 'utf8' = 'u8'; 'both' = 'both' }[$Encoding]
function Test-Want { param($Hit) ($want -eq 'both') -or ($Hit.Enc -eq $want) }

Write-Host ("候选按编码统计：u16={0}  u8={1}" -f `
        (@($first | ForEach-Object { Split-Hit $_ } | Where-Object { $_.Enc -eq 'u16' }).Count), `
        (@($first | ForEach-Object { Split-Hit $_ } | Where-Object { $_.Enc -eq 'u8' }).Count))

$map = @{}
foreach ($row in $second) {
    $h = Split-Hit $row
    if (Test-Want $h) { $map[$h.Key] = $h.Text }
}
$changed = New-Object Collections.ArrayList
foreach ($row in $first) {
    $h = Split-Hit $row
    if (-not (Test-Want $h)) { continue }
    if ($map.ContainsKey($h.Key) -and $map[$h.Key] -ne $h.Text) {
        [void]$changed.Add([pscustomobject]@{ Addr = $h.Addr; Enc = $h.Enc; Before = $h.Text; After = $map[$h.Key] })
    }
}

Write-Host ''
Write-Host ("=== 两次之间发生变化、且含中文的候选：{0} 条（共 {1} 条候选） ===" -f $changed.Count, $first.Count)
$changed | Select-Object -First 12 | ForEach-Object { Write-Host ("  [{0}] {1}  '{2}'  ->  '{3}'" -f $_.Enc, $_.Addr, $_.Before, $_.After) }
Write-Host ''
if ($changed.Count -gt 0) {
    Write-Host '结论：特征扫描成立 ✓ —— 变化的那条就是当前歌词行（地址稳定、不依赖版本偏移）。'
    Write-Host '下一步：做成 bridge 的 -LyricSource memory（最小权限、校验失败即回退）。'
}
else {
    Write-Host '这一轮没抓到"变化的歌词行"。换首歌/等久一点（-GapSeconds 10）再试；'
    Write-Host '若仍为 0，用 -WholeProcess 做对照，或 -Module 指定别的模块。'
}
