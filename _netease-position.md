# `_netease-position.ps1`

**位置助手**：向 `_netease-bridge.ps1` 报告"这首歌播到第几毫秒了"。桥会在需要精确位置时调用它。

它存在的理由：读 Windows 媒体会话（SMTC）必须走 .NET 的 `Windows.Media.Control` 投影，
而那个投影**只在 Windows PowerShell 5.1 里可用**（PowerShell 7 里 `Add-Type` 拿不到它）。
所以这一小段固定用 5.1 跑，桥自己用 pwsh 7 —— 分工是刻意的，不是历史遗留。

## 怎么跑

```powershell
# 直接跑（人看的格式）
powershell -NoProfile -ExecutionPolicy Bypass -File _netease-position.ps1

# 桥就是这么调它的：要求机器可读的一行输出
powershell -NoProfile -ExecutionPolicy Bypass -File _netease-position.ps1 -Raw

# 只问"这台机器有没有媒体会话"
powershell -NoProfile -ExecutionPolicy Bypass -File _netease-position.ps1 -Probe
```

## 需要什么

* **Windows PowerShell 5.1**（`powershell.exe`，不是 `pwsh.exe`）。
* 正在播放的媒体会话。没有会话时 `-Raw` 打印 `none`，退出码非 0。

## 输出与判读

| 输出 | 含义 |
| :-- | :-- |
| `playing <毫秒> <标题>` | 有会话且在放，位置是真实毫秒 |
| `paused <毫秒> <标题>` | 有会话但暂停 |
| `none` | 没有可用的媒体会话（这台机器 SMTC 就是没数据，别怀疑脚本） |

`-Probe` 会打印会话数量与每个会话的播放状态。**真机实测（2026-10-14）：这台机器
`-ProbePosition` 得到 `PROBE none (no active media session)`**——网易云没有把会话注册给 SMTC，
所以桥的位置链里 SMTC 这一环是死的，`dom`（客户端迷你条上的真实时钟）才是活的来源。

## 坑

* **必须 5.1。** 用 pwsh 7 跑会拿不到类型，输出 `none`——看起来像"没有媒体会话"，
  实际是"这个宿主读不到"，两者含义完全不同，别据此下结论。
* **一次调用有启动开销**（起一个 5.1 进程 + 加载投影，几百毫秒），所以桥不会每帧调它，
  只在真正需要精确位置时才调，并且失败就降级。
* **失败不是错误**：没有会话、客户端没注册、宿主不对，都返回 `none` 让桥走别的来源。
