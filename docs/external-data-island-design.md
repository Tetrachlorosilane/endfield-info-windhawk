# 外部数据源与「灵动岛」形态设计（提案）

**状态**：提案，尚未实现。**目标读者**：实现这个 mod 的人/模型。
**前置阅读**：[README.md](../README.md) 的「全部设置项」、[AGENTS.md](../AGENTS.md) 的 §4/§5。

> 行号基于提交 `3db06ab`。行号会漂移，**以符号名为准**。

本文回答两件事，它们是**两个独立阶段**，不要混着做：

- **A 阶段（数据面）**：让面板显示**非内置采集器**推来的文本。改动面小，风险低。
- **B 阶段（形态面）**：把面板从"固定宽度的行列表"变成"内容驱动的变形胶囊"（灵动岛）。
  它会正面对撞 AGENTS §5.4/§5.5 的几条不变量，风险中高。

A 是 B 的数据底座：先有 `{ext.…}` 这种变量，B 的"事件"才有东西可展示。

---

## 1. 目标与非目标

### 目标

1. **任意进程**（PowerShell 一行、Python、C#、Electron、另一个 Windhawk mod）都能把文本推给面板，
   不需要编译、不需要注入、不需要管理员权限。
2. 外部值与内置变量在模板里**完全平权**：`{ext.music.title}` 可以和 `{cpu}` 写在同一行、
   可以用格式说明符、可以被 `[[ring:…]]` 当量程。
3. **零成本未启用**：没有任何一行引用 `{ext.…}` 时，不创建管道、不开线程、不占内存。
   这是本项目既有的核心性能设计（`RequiredGroups`，[1343](../endfield-info-windhawk.wh.cpp#L1343)），
   必须延续而不是绕过。
4. **失败可预期**：提供者掉线或超时 → `--`，不是陈旧数据、不是崩溃、不是空面板。
5. 仍然是**单文件交付物**：不引入新依赖（管道 API 在 kernel32，`@compilerOptions` 不用改），
   不引入 `new`/`delete`/`std::*`（体积决定，见 [4192](../endfield-info-windhawk.wh.cpp#L4192)）。

### 非目标（明确不做，避免顺手扩大攻击面）

- **不让外部决定执行动作**：v1 只传文本，不传命令、不传脚本、不传"回调提供者"（§14 Q3）。
  灵动岛卡片上的按钮只做**本地**动作（关闭/固定/切换）。
- **不二次展开 token**：外部文本里出现的 `{…}` 一律当普通字符画出来（见 §9 的注入威胁）。
- **不支持富文本**：无 ANSI、无颜色标记、无换行。颜色/字号由模板与主题决定；
  **提供者不能指定颜色**，只能选 `severity` 档位（§14 Q4）。
- **不支持远程**：管道显式 `PIPE_REJECT_REMOTE_CLIENTS`，只服务本机。
- **不做 token 鉴权**（§14 Q1）：同用户即信任，这是**信任边界**而不是"安全通道"。
- **不接受提供者传图片字节**（§14 Q2）：只接受内置字形白名单，或 1–2 个字符的文本徽章。
- **不做第二通道**（§14 Q5）：共享内存/UDP 等出现"管道覆盖不了"的场景（浏览器）再说。
- **不做多面板 / 每显示器一个岛**（§14 Q6），也**不预先**做"面板实例化"抽象。

---

## 2. 架构总览

```
   生产者（任意进程）                  windhawk.exe（本 mod 的进程）
 ┌───────────────────┐        ┌──────────────────────────────────────────────┐
 │ PowerShell 脚本    │        │  IPC 线程（仅当模板引用 {ext.…} 时创建）        │
 │ Python 小工具      │        │   CreateNamedPipe(REJECT_REMOTE)             │
 │ 另一个 Windhawk mod│──管道──►│   WaitForMultipleObjects(pipe, stopEvent)    │
 │ 浏览器/Electron    │        │   ParseExtLine（纯函数）→ 消毒 → 写入槽位表    │
 └───────────────────┘        │        │                        │            │
                              │        │ SET/DEL                │ EVENT      │
                              │        ▼                        ▼            │
                              │   外部值槽位表            事件队列（定长环）    │
                              │   （g_extLock 保护）      （g_extLock 保护）  │
                              │        │                        │            │
                              │        │                        │ PostMessage │
                              │        ▼                        ▼            │
                              │  快照发布（g_valuesLock）   HUD 线程：状态机    │
                              │        │                    → 几何/动画/命中   │
                              │        └────────► PaintPanel ◄───────┘        │
                              └──────────────────────────────────────────────┘
```

与现有三层的关系：**IPC 线程 = 采集线程的兄弟**，不是替代。
它复用既有的两把锁规则、`PostMessage` 唤醒 HUD、`InterlockedExchange` 停止标志
（[4177](../endfield-info-windhawk.wh.cpp#L4177) 的 50 ms 切片 sleep 模式）。

---

## 3. 传输层：命名管道

### 3.1 为什么是管道

| 候选 | 结论 |
| :-- | :-- |
| **命名管道** | **选它**。任何语言都有客户端；`PIPE_REJECT_REMOTE_CLIENTS` 天然只允许本机；不受 UIPI 影响；全双工可回错误 |
| `WM_COPYDATA` | 备选。发送方**阻塞**直到我们返回；无多客户端排队；跨完整性级别会被 **UIPI** 拦掉（需 `ChangeWindowMessageFilterEx` 放行）；载荷小 |
| 共享内存 + 事件 | 将来要 60 fps 级数据（帧时间曲线）再考虑；文本要走环形缓冲，复杂度不成比例 |
| 本地 TCP/UDP | 唯一优势是浏览器直接可用；代价是端口占用与防火墙提示，鉴权只剩"回环" |
| 监视文件目录 | 延迟大、要清理、要解析，不做主线 |
| 往 `windhawk.exe` 附 DLL | **不做**。只能解决"本进程内直调"，解决不了数据怎么来；且要第二个 mod 注入本 mod 所在的进程，脆弱难调试 |

> **DLL 的正确位置在生产端**：想读某个游戏/播放器的内部状态，应当写一个**注入到那个程序**的
> Windhawk mod（hook 它的内部函数），再经管道把结果推过来。面板侧只认协议，
> 就能同时服务"Windhawk mod""自写小程序""一行 PowerShell""浏览器扩展"。

### 3.2 管道参数

| 项 | 取值 | 理由 |
| :-- | :-- | :-- |
| 名字 | `\\.\pipe\endfield-info-windhawk` | 加 mod 名便于用户发现；**不要**加随机后缀（否则客户端不知道连哪里）。多实例用 `CreateNamedPipe` 的第 4 参数 |
| 打开模式 | `PIPE_ACCESS_DUPLEX` | 需要回 `OK`/`ERR` 便于调试 |
| 类型/读模式 | `PIPE_TYPE_MESSAGE \| PIPE_READMODE_MESSAGE \| PIPE_WAIT` | 消息模式保证一条命令一次 `ReadFile` 到齐，解析器不用做跨包拼装 |
| 实例数 | 4（设置项可调 1–8） | 多个生产者并存 |
| 标志 | `PIPE_REJECT_REMOTE_CLIENTS` | **必须**。否则 `\\hostname\pipe\…` 走 SMB 可被远程连接 |
| 缓冲 | 出/入各 8 KB | ≥ 单行上限 × 2 |
| 超时 | `ConnectNamedPipe` 用 overlapped + 停止事件 | 让 `Uninit` 能立刻退，不阻塞在 `ConnectNamedPipe` 上 |
| 安全描述符 | 显式 `SECURITY_ATTRIBUTES`：同用户 + SYSTEM + Administrators | 不依赖进程默认 DACL |

**默认 DACL 的现实**：不显式给安全描述符时，管道拿到创建进程的默认 DACL（通常 = 创建者用户 + SYSTEM/管理员），
且默认中等完整性标签，**低完整性/沙箱进程默认写不进来**（强制完整性策略阻止 write-up）。
这是免费的防线，但不要依赖它——§9 要求显式写 DACL，并把它写进注释说明意图。

### 3.3 生命周期

- **创建时机**：`SamplerIteration` 里 `RequiredGroups()` 返回含 `G_EXT` 时，第一次才建
  （与 GPU 后端惰性启动同构，[4158](../endfield-info-windhawk.wh.cpp#L4158)）。
- **销毁时机**：`WhTool_ModUninit` 先置停止标志、`SetEvent`、`WaitForSingleObject(2000)`、再 `CloseHandle`
  ——顺序照抄 sampler 的停机路径（[4292](../endfield-info-windhawk.wh.cpp#L4292)）。
- **设置变更**（`WhTool_ModSettingsChanged`）：只**原子更新限额**（`InterlockedExchange` 到几个 `volatile LONG`），
  **不重建管道**——重建会让所有生产者断线。管道名若要改，必须重载 mod（写进文档即可）。
- **客户端断开**：`ERROR_BROKEN_PIPE` / `ERROR_PIPE_NOT_CONNECTED` → `DisconnectNamedPipe` → 回收该实例 →
  该提供者的值按 ttl 自然过期（**不立刻清空**，避免短暂重连导致屏幕闪烁）。

---

## 4. 协议 v1

### 4.1 帧格式

```
<命令> <参数...>\n            UTF-8，单帧 ≤ ExtMaxLine（默认 4096 字节，含换行）
```
- 命令与参数之间是**单个空格**；`\t` 视为空格；`\r` 丢弃。
- 最后一个参数总是"其余全部"（所以文本里可以有空格），**不需要引号转义**——这是刻意的：
  转义规则越少，客户端越好写，解析器越好测。
- 空行忽略。`#` 开头的行当注释忽略（方便手工 `echo` 调试）。

### 4.2 命令

| 命令 | 语法 | 语义 |
| :-- | :-- | :-- |
| `HELLO` | `HELLO <proto> <provider> [glyph\|badge]` | 注册提供者。`proto` 目前只有 `1`。可选的第 4 个字段是内置字形名（白名单）或 1–2 字符文本徽章，供卡片/图标位使用（§14 Q2）。未 `HELLO` 就 `SET` → `ERR 401` |
| `SET` | `SET <provider> <key> <ttl\|-> <text...>` | 定义/覆盖一个值。`ttl` 单位 ms，`-` 表示用设置默认值 |
| `DEL` | `DEL <provider> <key>` | 删除该键（`{ext.p.k}` 立刻回到"未知/--"分支，见 §5） |
| `EVENT` | `EVENT <provider> <severity> <ttl\|-> <title> <body...>` | 一次性事件，进入灵动岛队列（B 阶段用） |
| `BYE` | `BYE` | 关闭连接；不等价于 `DEL`（值按 ttl 过期） |
| 应答 | `OK [detail]` / `ERR <code> [detail]` | 每条命令**恰好一条**应答 |

`severity` ∈ `info` / `warn` / `critical`（决定优先级与配色档位，见 §7.3）。

**应答的写入失败不致命**：生产者若从不读应答，出缓冲会满。服务端把写入失败当作
"客户端不关心应答"，继续服务，不惩罚该连接（否则一个 `echo` 客户端就能卡死自己那条实例）。

### 4.3 字段语法（解析器必须严格照此实现，并作为纯函数测试）

```
provider := [a-z0-9_-]{1,16}        区分大小写？→ 统一小写化后比较（避免 {ext.Music} 与 {ext.music} 两个键）
key      := [a-z0-9_.-]{1,32}       同上小写化
ttl      := '-' | [0-9]{1,7}        毫秒；0 表示"立即过期"（等价 DEL）
severity := info | warn | critical
text     := 任意 UTF-16 可表示字符，去掉 C0/C1 控制字符（\t→空格），长度按 §4.4 截断
```

### 4.4 值的消毒规则（**必须**，全部为纯函数）

| 规则 | 做法 | 理由 |
| :-- | :-- | :-- |
| 单行化 | 丢弃 `\r`、`\n`，`\t` → 空格，其余 C0 控制字符（0x00–0x1F）与 C1（0x80–0x9F）删除 | 行是"一行读数"，换行会破坏版面与截断逻辑（AGENTS §5.1） |
| 长度 | 截断到 `ExtMaxValue`（默认 256 字符），截断点加 `…` | 行缓冲是定长 `WCHAR[640]`（[2341](../endfield-info-windhawk.wh.cpp#L2341)），长文本会把同一行别的变量挤掉 |
| **不展开 token** | `{` / `}` 原样保留为字符 | 否则提供者能注入 `{cpu}`，甚至递归注入 `{ext.…}`（§9 威胁 4） |
| 数值旁路 | 严格解析 `[+-]?digits[.digits]?`（无空格、无 `%`、无千分位）成功时，额外存一个 `double` | 让 `[[ring:{ext.p.k}]]`、`{ext.p.k|0}` 能工作；**显示仍是原始文本** |
| 空白 | 前后空白裁剪（`CopyUserPath` 同款风格，[728](../endfield-info-windhawk.wh.cpp#L728)） | 手写客户端常见 |

### 4.5 错误码

| 码 | 含义 | 是否惩罚连接 |
| :-- | :-- | :-- |
| 400 | 命令无法解析（未知命令/参数不足） | 计数；连续 32 条 → 断开 |
| 401 | 未 `HELLO` 就 `SET`/`EVENT` | 计数；3 条 → 断开 |
| 402 | 帧超长（> `ExtMaxLine`） | 丢弃该帧并断开（消息模式下一次 `ReadFile` 就是整帧） |
| 403 | 幂等/容量：提供者或键超出上限 | 丢弃，计数 |
| 404 | 速率超限 | 丢弃，计数（**不**断开：真实的突发不该被踢） |
| 405 | `provider` 不在允许名单 | 丢弃，计数 |
| 500 | 服务端内部失败（槽位/内存异常） | 记日志，断开 |

所有 `ERR` 都只打**计数与码**，**绝不打值的内容**（§9 威胁 6）。

### 4.6 会话示例

```text
C: HELLO 1 music
S: OK proto=1 provider=music
C: SET music title - Now Playing: Blue Hour
S: OK
C: SET music progress 60000 0.42
S: OK
C: EVENT music info 8000 曲目切换 Blue Hour — 海の声
S: OK
C: BYE
```

模板里就能写：

```
♪ {ext.music.title}   [[ring:{ext.music.progress}:1:5:FF6FB3E0]]
```

---

## 5. 面板侧数据模型与变量语义

### 5.1 槽位表（定长，无分配）

```c
// 每个提供者一个槽位；每个槽位一组定长键值。上限都很小，线性查找足够。
static const int kMaxExtProviders = 8;
static const int kMaxExtKeys      = 32;

struct ExtEntry {
    WCHAR  key[33];
    WCHAR  text[kExtMaxValueChars + 1];
    double number;
    bool   numeric;      // number 有效？
    DWORD  updatedAt;    // GetTickCount()
    DWORD  ttlMs;        // 0 = 已失效
};
struct ExtProvider {
    WCHAR  name[17];
    ExtEntry entries[kMaxExtKeys];
    int    count;
    DWORD  lastSeenAt;
    unsigned drops;      // 有界计数，用于日志
};
```

- 淘汰：新键到来且 `count == kMaxExtKeys` → 淘汰**最早过期**的条目（同 ttl 时淘汰最久未更新），
  计 `drops` 并在日志里打一行（有界计数，AGENTS §9）。
- 提供者槽位满 → `ERR 403`。
- 一把 `g_extLock`（`CRITICAL_SECTION`）保护槽位表；**不做无锁**：写入频率低（人级），
  复杂度不值当。

### 5.2 变量查找顺序

```
{ext.p.k} 求值：
  1. 名字以 "ext." 开头吗？否 → 走既有 LookupRaw/LookupNumber（[1857] / [1878]）
  2. 是 → 在槽位表里找 provider=p, key=k
       找到且未过期  → LR_RESOLVED（文本用 text；数值用 number，numeric==false 时数值失败）
       找到但已过期  → LR_UNAVAILABLE   → 画 "--"
       没找到        → LR_UNAVAILABLE   → 画 "--"
  3. 名字以 "ext." 开头但格式非法（如 {ext.} / {ext.a.b.c}）→ LR_UNKNOWN → 保留字面
```

**关键取舍**：`ext.*` 的"键不存在"返回 `LR_UNAVAILABLE`（`--`）而不是 `LR_UNKNOWN`（字面文本）。
理由：外部键是**运行期**存在的，`{ext.music.title}` 在提供者还没启动时显示 `{ext.music.title}`
会让人以为写错了，而 `--` 正确表达"这个数据源现在没数据"。非法**格式**才保留字面。

### 5.3 与按需加载的接线（三处，缺一不可）

| 位置 | 改动 |
| :-- | :-- |
| `enum Group`（[1303](../endfield-info-windhawk.wh.cpp#L1303)） | 加 `G_EXT = 1 << 5` |
| `RequiredGroups`（[1343](../endfield-info-windhawk.wh.cpp#L1343)） | `{` 后若以 `ext.` 开头 → `groups |= G_EXT`（静态表 `GroupForName` 认不出运行期键，必须单独判前缀） |
| `ResolveToken`（[2257](../endfield-info-windhawk.wh.cpp#L2257)） | 在**早于** `IsKnownNumberVariable` 的位置加 `ext.` 分支：格式合法 → 命中或 `LR_UNAVAILABLE`；格式非法 → 直接 `LR_UNKNOWN`（保留字面） |
| `IsKnownNumberVariable`（[2235](../endfield-info-windhawk.wh.cpp#L2235)） | **不动**。它是无设置依赖的纯结构判断（23 个 `MATCH`）；把 `ext.` 混进去会把 `{ext.}` 这类**拼错**也吞成 `--`，与 §5.2 的规则矛盾 |
| `LookupRaw` / `LookupNumber`（[1857](../endfield-info-windhawk.wh.cpp#L1857) / [1878](../endfield-info-windhawk.wh.cpp#L1878)） | **不动**。它们只服务内置变量；`ext.*` 的文本与数值都在 `ResolveToken` 的新分支里直接产出 |

> 测试注意：`test-layout.cpp` 的 token 覆盖列表断言"32 个变量全部能解析"。
> `ext.*` 是运行期的，**不能**加进那张静态表；要另写一组断言（§12）。

### 5.4 与快照的关系

关键事实：**展开 token 的是采样线程**（`PublishRows` → `ExpandTokensAndRings`，
[4107](../endfield-info-windhawk.wh.cpp#L4107)），HUD 只读已经展开好的 `g_rowState`。
所以外部值必须让**采样线程**看见，而不是 HUD：

- 不把槽位表塞进 `Values`（[1366](../endfield-info-windhawk.wh.cpp#L1366) 是采集器的固定形状；
  整表按定长算约 150 KB，也不能按帧拷）。
- 采样线程在 `PublishRows` 展开行文本期间**持 `g_extLock`**（展开是微秒级，
  IPC 写入等这一小会儿可以接受），展开完立即释放。
- **延迟**：若只靠采样周期，一个 `SET` 最坏要等 `UpdateMs`（默认 1000 ms）才上屏。
  所以 IPC 线程写入后置一个 `volatile LONG g_extDirty`，采样线程既有的 **50 ms 切片 sleep**
  （[4178](../endfield-info-windhawk.wh.cpp#L4178)）看到它就提前结束等待、立即再采一次并清标志
  → 实际延迟约 50 ms，且天然合并（一批 `SET` 只触发一次采样、一次重绘）。
- `EVENT` 走另一条路：它不进 `{ext.…}`，卡片直接显示事件自带文本，
  所以 IPC 线程直接 `PostMessage(g_window, kMsgExtEvent)` → HUD 立刻重绘，**完全不等**采样周期。

---

## 6. 设置项（静态 YAML，只能手输）

Windhawk 没有动态下拉框，所以"允许的提供者"这类只能用**分号分隔的手输列表**
（与 `FontFamily` 的校验同款：[1113](../endfield-info-windhawk.wh.cpp#L1113)）。

| 键 | 类型 | 默认 | 说明 |
| :-- | :-- | :-- | :-- |
| `Enable external data` | 布尔/0-1 | `0` | **默认关闭** → 老配置行为完全不变 |
| `Allowed providers` | 字符串 | 空 | 分号分隔；空 = 允许全部（但仍记录每个名字）；带 `*` 通配不支持 |
| `Value lifetime` | 数字 | `30000` | `SET` 用 `-` 时的默认 ttl（ms），范围 100–600000 |
| `Max value length` | 数字 | `256` | 范围 16–512 |
| `Max providers` | 数字 | `8` | 范围 1–8（受定长表约束） |
| `Max updates per second` | 数字 | `100` | 每连接；超限丢弃并计数 |
| `Event default duration` | 数字 | `6000` | B 阶段：`EVENT` 用 `-` 时的 ttl |
| `Event queue depth` | 数字 | `4` | B 阶段：1–8 |
| `Island enabled` | 布尔/0-1 | `0` | B 阶段总开关 |

全部走 `ClampInt`，与既有设置项同风格（[1127](../endfield-info-windhawk.wh.cpp#L1127)）。

---

## 7. 事件通道与灵动岛状态机（B 阶段）

### 7.1 与既有显示状态的叠加关系

现有三态（`g_visible` / `g_modeProximity` / `g_expanded`，[2651](../endfield-info-windhawk.wh.cpp#L2651)）
**保持不动**。灵动岛是叠加在它上面的一层"临时形态"：

```
基础面板：Collapsed(8px 标签) ──点击/靠近──► Expanded(行面板)
                 ▲                                │
                 └──────── 事件到达/结束 ──────────┘
灵动岛层：  Idle ──event──► Popping ──► Card(ttl) ──► Collapsing ──► Idle
```

**"永不隐藏"原则不变**：`Card` 收起后仍是基础面板的胶囊/标签，永远可点。

### 7.2 状态机

| 状态 | 几何 | 进入 | 退出 |
| :-- | :-- | :-- | :-- |
| `Idle` | 基础态（现有几何） | 初始 / `Collapsing` 结束 | 队列非空 → `Popping` |
| `Popping` | 基础态 → 卡片目标尺寸，`EaseBackOut` 过冲 | 队列非空 | 高度与宽度都到位 → `Card` |
| `Card` | 卡片目标尺寸（`ComputeIslandRect`） | `Popping` 完成 | ttl 到期 / 本地"关闭"按钮 / 更高优先级事件抢占 → `Collapsing` |
| `Collapsing` | 卡片 → 基础态，时长 = 展开 × `kCollapseDurationRatio` | 上者 | 到位 → `Idle`（若队列非空则直接 `Popping`） |

- **动画通道**（复用 `IntroKey`/`EvaluateKeys` 关键帧表，[826](../endfield-info-windhawk.wh.cpp#L826)）：
  `width`、`height`、`scale`、`contentAlpha`、`iconScale`。现在这套机制已经在驱动
  height/scale/contentAlpha/iconScale（[905](../endfield-info-windhawk.wh.cpp#L905)），加 `width` 是**增量**而不是重写。
- **缓动**照旧用三次贝塞尔（`EaseIn/Out/InOut/Smooth/BackOut`，[846](../endfield-info-windhawk.wh.cpp#L846)），
  过冲强度沿用 `kPanelBounceStrength = 0.04`（[808](../endfield-info-windhawk.wh.cpp#L808)）——**不要**用原项目的 0.275。

### 7.3 队列与优先级

- 同 `provider` 的新事件**替换**自己尚未展示的事件（"曲目切换"连点三次只该弹一次）。
- 相同 `(provider, title)` 在 2 s 内重复 → 不排队，而是给当前卡片加计数徽标。
- 队列按 `severity` 分层：`critical` 抢占当前 `info`；同级按 FIFO。
- 队列满 → 丢弃**最低优先级 + 最早**的一条，计 `drops`。
- `critical` 的 ttl 到期也**不**自动消失？→ **要消失**。理由：本 mod 没有常驻通知中心，
  留下不走的卡片会退化成"永远占屏"，与 §5.5"永不隐藏"是对立面的另一种坏结果。

### 7.4 卡片内容模型（比行列表更强的一种布局）

```c
struct IslandCard {
    const WCHAR *provider;     // 由 HELLO 提供；用于日志与合并，不直接绘制
    const WCHAR *title;        // 主行，单行截断
    const WCHAR *body;         // 副行，可为空
    int   glyph;               // HELLO 指定的内置字形（IconStyleFromName 白名单，[756]）；
                               // glyph == kBadge 时改用 badgeText。不接受路径或图片字节（§14 Q2）
    WCHAR badgeText[3];        // 1–2 字符文本徽章
    DWORD accent;              // 只能由 severity 派生：info=主题 accent，warn=黄，critical=红；
                               // **提供者不能指定颜色**（§14 Q4）
    unsigned repeat;           // 合并计数
};
```

卡片是"主视觉 + 0–2 行副文本 + 可选本地按钮"，**不是**行列表的变形。
它需要 `PaintPanel` 里一条**并列的绘制分支**，而不是硬塞进 `RowState`。

---

## 8. 几何、命中与不变量的冲突处理（**B 阶段最容易踩的地方**）

AGENTS §5.4/§5.5 的四条不变量会被正面挑战，逐条给出处理：

| 不变量 | 冲突 | 处理 |
| :-- | :-- | :-- |
| `g_displayHeight` 是静止几何，动画**不得**写回（[2672](../endfield-info-windhawk.wh.cpp#L2672)） | 宽度/高度现在有两个来源（基础态 + 卡片态） | 把"静止几何"从一个 `int` 泛化成 **`g_restingRect`（当前状态的目标矩形）**：`PlaceWindow` 与 `WM_NCHITTEST` 都读它；动画只写 `g_paintedRect`。**语义不变，只是从标量变成矩形** |
| 命中矩形 = 静止几何 | 卡片动画期间"看得见的"和"能点的"不一致 | **动画期间取 from/to 两态的并集**（看得见的都能点），settle 后精确到目标矩形。绝不允许出现"动画中途窗口缩到 8px"那类回归 |
| 面板永不隐藏（§5.5） | 卡片收起 | 收回后回到基础态（胶囊/标签），**不是**隐藏 |
| 只按圆角矩形做命中（[3987](../endfield-info-windhawk.wh.cpp#L3987)） | 卡片里有按钮 | 引入**区域表**：`HitRegion{ rect, actionId }`，按当前状态取一张表；圆角外的 `HTTRANSPARENT` 判定保留 |
| 圆角 alpha 掩码假设单矩形（[3640](../endfield-info-windhawk.wh.cpp#L3640)） | 变形 | 该循环**已经参数化**（用 `width`/`height` 局部变量），只要传入动画尺寸即可，无需重写 |
| 位置由 `PlaceWindow` 决定且宽度写死 `g_settings.panelWidth`（[3715](../endfield-info-windhawk.wh.cpp#L3715)） | 宽度要动画 | `PlaceWindow` 改为读"当前静止矩形"，**每帧仍要调用**它（收回时逐帧跟随收缩是既有教训） |

命中与绘制的**唯一真相**必须是一个函数：

```c
IslandRect ComputeIslandRect(const IslandState &s, const ContentMetrics &m,
                             const RECT &monitor, const ModSettings &cfg);   // 纯函数，可断言
```

---

## 9. 安全模型

### 9.1 威胁清单与对策

| # | 威胁 | 对策 |
| :-- | :-- | :-- |
| 1 | **远程**进程经 `\\hostname\pipe\` 连接 | `PIPE_REJECT_REMOTE_CLIENTS`（硬性） |
| 2 | 同机其他用户 | 显式 DACL：同用户 + SYSTEM + Administrators |
| 3 | 低完整性/沙箱进程（浏览器渲染器）刷屏 | 默认中等完整性标签已挡住 write-up；再加速率上限与队列上限 |
| 4 | **token 注入**：值里塞 `{cpu}` 或 `{ext.…}` | 外部文本**永不二次展开**（§4.4）；这条必须写进注释与测试 |
| 5 | 洪泛（每秒上万条） | 每连接速率上限 + 丢弃计数；速率超限**不断开**（真实突发不该被踢） |
| 6 | **敏感信息泄漏进日志** | 日志只打提供者名、键名长度、字节数、错误码；**绝不打值**。键名可打（它决定屏幕内容，是判别性信息） |
| 7 | 提供者假冒（无鉴权） | **已决：不做 token**（§14 Q1）。同用户即信任，且 token 存设置里同用户一样读得到，只是假的安全感。改为：默认关闭 + 允许名单 + 速率/容量上限；并用 `GetNamedPipeClientProcessId` + `QueryFullProcessImageName` 把**客户端进程名打进日志**（诊断，不是鉴权） |
| 8 | 提供者指定图标/路径/图片字节 → 读文件或喂 GDI+ 解码器 | **已决（§14 Q2）**：路径与字节都不接受，只接受内置字形名（`IconStyleFromName` 白名单，[756](../endfield-info-windhawk.wh.cpp#L756)）或 1–2 字符文本徽章 |
| 9 | 畸形 UTF-8 | 解码失败 → `ERR 400` 丢弃该帧；解码用 `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS)` |
| 10 | 让面板执行动作 | 非目标（§1）：v1 不传命令 |

### 9.2 明确写下"不做什么"

- 不允许远程、不允许文件系统访问、不允许执行、不解析富文本/ANSI。
- 不把外部值写进日志、不写盘、不写注册表。
- 不做"点按钮回调提供者"——那等于给 IPC 加一条**命令通道**，是完全不同量级的攻击面。

---

## 10. 失败与降级

| 情况 | 行为 |
| :-- | :-- |
| 管道创建失败 | 记日志（含 `GetLastError`），`G_EXT` 视作不可用 → `{ext.…}` 全部 `--`；**不影响**其余采集 |
| 客户端断开 | 该提供者值按 ttl 自然过期，不立刻清空（避免重连闪屏） |
| 提供者静默 | ttl 到期 → `--`（复用 `LR_UNAVAILABLE` 语义，[2313](../endfield-info-windhawk.wh.cpp#L2313)） |
| 模板写了非法 `{ext.}` | `LR_UNKNOWN` → 保留字面（拼错可见） |
| 设置里关掉 `Enable external data` | 不建管道；已连接的客户端收到 `ERR 405` 后被断开 |
| mod 重载 | 客户端会拿到 `ERROR_BROKEN_PIPE`，应自行重连（写进协议文档；提供 `_extclient.py` 做示例） |

---

## 11. 性能预算

| 场景 | 成本 |
| :-- | :-- |
| 无 `{ext.…}` 引用 | **0**：不建管道、不建线程、不占内存（延续 `RequiredGroups` 设计） |
| 有引用、空闲 | 1 个管道实例 + 1 个阻塞在 `WaitForMultipleObjects` 的线程（不可测） |
| 每条 `SET` | 解析 + 定长槽位线性查找（≤ 8×32 项）+ 一次 `PostMessage`（不重绘，等下一个 tick 或事件） |
| 每个 `EVENT` | 一次 `PostMessage` + 一次立即重绘（动画期间 16 ms 帧时钟，空闲 40 ms，见 [3842](../endfield-info-windhawk.wh.cpp#L3842)/[3890](../endfield-info-windhawk.wh.cpp#L3890)） |
| 重绘成本 | 与现在相同量级：整帧 DIB + GDI+ + `UpdateLayeredWindow`（[3682](../endfield-info-windhawk.wh.cpp#L3682)）。**事件不应高于 ~10 次/秒**，否则面板会持续占 CPU；超过就合并（§7.3） |

---

## 12. 可测试性与断言清单

按 AGENTS 铁律 2：**凡是"编译通过但屏幕上看得出错"的逻辑都要抽成纯函数**。本设计的纯函数：

| 纯函数 | 断言（`test-layout.cpp`） |
| :-- | :-- |
| `ParseExtLine` | 命令/参数切分；缺参 → 400；未知命令 → 400；`SET p k - 含 空格 的文本` 文本完整保留；`#` 注释；空行 |
| `SanitizeExtText` | 去 `\r\n`、`\t`→空格、去 C0/C1；截断到上限且**末尾加 `…`**；`{cpu}` **原样保留**（负对照：若实现里加了展开，此断言必须变红） |
| `ExtParseNumber` | `42`/`-1.5`/`.5`?/`1e3`? → 明确取舍并断言；`73%`、` 42`、`4 2` → non-numeric |
| `ExtLookup` | 未过期 → RESOLVED；过期 → UNAVAILABLE；不存在 → UNAVAILABLE；`ext.` 后格式非法 → UNKNOWN |
| `AllocExtSlot` | 容量满时淘汰"最早过期"；同 ttl 淘汰最久未更新 |
| `RequiredGroups` | 含 `{ext.p.k}` → 置 `G_EXT`；`{ext.}` 非法 → **不**置位 |
| `StepIsland` | 抢占（critical 抢 info）；同 provider 替换；2 s 内同标题合并计数；队列满丢最低优先级；ttl 到期回 Idle |
| `ComputeIslandRect` | 各状态下矩形正确；**超宽文本不引起宽度无界增长**（必须钳到屏宽上限） |
| `HitTestIsland` | 圆角外 `HTTRANSPARENT`；按钮区域内命中对应 actionId；动画期间用并集 |

端到端冒烟：新增 `_extclient.py`（几十行，`NamedPipeClientStream` / `open(r'\\.\pipe\…')`）
发几条 `SET`/`EVENT`，人工确认屏幕；再发畸形帧确认 `ERR` 码与**没把值写进日志**。
日志判别性字段：`EXT connect provider=… inst=…`、`EXT set provider=… keyLen=… bytes=…`、
`EXT drop reason=overlong|rate|capacity count=…`、`EXT expire provider=… keys=…`。

---

## 13. 分期计划与验收标准

| 里程碑 | 内容 | 验收 |
| :-- | :-- | :-- |
| **M1 数据面** | `G_EXT` + 槽位表 + 管道线程 + `{ext.p.k}` + ttl/消毒/限速 + 设置项 | 关掉开关时空闲成本为 0；`{ext.p.k}` 能显示；提供者退出后 5 s 内变 `--`；`test-layout.cpp` 全绿 + `_settings.py` `no structural problems found` + `build.ps1` 只剩那 2 条已知警告 |
| **M2 事件面** | `EVENT` + 队列/优先级/合并 + `Popping/Card/Collapsing` + width 动画通道 | 连点同 provider 只弹一次；critical 抢 info；ttl 到期收回；收回后基础面板仍可点 |
| **M3 卡片与交互** | `ComputeIslandRect` + 区域表 + 本地按钮 + 悬停态 | 动画期间"看得见的都能点"；圆角外仍穿透；**不**出现"窗口缩到 8px"回归 |
| **M4 打磨** | 提供者图标（内置字形白名单）、每提供者配色档、诊断页 | 文档与 AGENTS 更新；`_extclient.py` 进仓库 |

**M1 与 M2 之间是可交付的独立状态**：M1 完成后即使不做灵动岛，也已经解决了"显示外部文本"这个原始需求。

---

## 14. 决策记录（原先的六个待拍板问题，已逐条决定）

每条给出**结论 + 理由 + 什么情况下会改主意**。结论已经落到 §1 的非目标里。

### Q1 鉴权 → **不做 token**

同用户即信任。token **挡不住它想挡的东西**：设置存在当前用户下、管道同用户可连、
进程内存可读——一个同用户进程拿到 token 并不比拿到管道更难。它买到的是"手输配置的麻烦"
和**假的安全感**。真正可验证的控制是另外三条：

- 默认关闭（`Enable external data = 0`）+ 提供者允许名单 + 速率/容量上限（已设计）；
- 诊断层面记录**客户端是谁**：`GetNamedPipeClientProcessId` → `QueryFullProcessImageName`，
  日志打进程名/镜像路径，出问题时能立刻定位是哪个程序在刷；
- 若将来真需要"只接受特定来源"，比 token 靠谱的是检查客户端令牌的**完整性级别**
  （`GetTokenInformation(TokenIntegrityLevel)`）：可选开关"只接受高完整性客户端"，
  能挡住沙箱化浏览器之类的中/低完整性来源，而且**可验证**。

*改主意的条件*：出现"同用户下某个普通程序刷屏、用户又关不掉"的真实抱怨。

### Q2 图标 → **只接受内置字形 + 1–2 字符文本徽章，不接受图片字节**

- 传字节需要 `IStream`（`CreateStreamOnHGlobal` 在 ole32），会破坏 AGENTS §10 明确记录的
  "依赖仅 8 个系统 DLL"这一性质；
- 更要紧的是**职责边界**：那等于把 GDI+ 的图片解码器开放给任意同用户进程
  （GDI+ 解码器有历史漏洞），换来的只是一个**纯装饰**效果；
- 表达力问题用更便宜的办法解决：图标位可以画 **1–2 个字符的文本徽章**（`♪`、`GPU`），
  走已有的字形绘制路径，零解码风险；用户真想要某张图，用**已有的 `IconN path` 设置**
  自己指定文件（那条路径是用户显式选择的）；
- 形状：`HELLO <proto> <provider> [glyph|badge]`，`glyph` ∈ `None/CPU/Memory/Disk/Network/GPU`
  （`IconStyleFromName` 白名单，[756](../endfield-info-windhawk.wh.cpp#L756)），`badge` 为 1–2 字符。

*改主意的条件*：出现"必须由提供者携带品牌图标"的真实需求，且愿意接受
"先验魔数只喂 PNG 解码器 + 字节/尺寸上限 + 独立缓存"的额外约束。

### Q3 按钮回调 → **明确拒绝（v1 与可预见的 v2 都不做）**

这不是"多一个字段"：它把单向展示通道变成**命令通道**，还要求面板维护"待回调"状态
（超时、断线、重放、幂等）。想要"点一下切歌"这类能力，正确做法是将来单独设计一条
**显式开启 + 动作 id 白名单 + 绝不执行 shell** 的通道，并按独立特性评审，
而不是塞进 `EVENT`。

### Q4 配色 → **只给 `severity` 档位，不给颜色**

与 AGENTS §5.2 的既有决定同源：**配色单一来源**。"命名主题完全胜出、颜色字段只在
`Theme = Custom` 下生效"这条规则是花了代价才建立起来的；允许任意 AARRGGBB 会让面板
变成调色板。

- 语义需求已由 `severity`（`info`/`warn`/`critical`）覆盖：档位映射到**主题派生**的
  三档颜色（一律经 `WithOpacity`，[613](../endfield-info-windhawk.wh.cpp#L613)）；
- 用户想给某行上色，本来就能在模板里写 `[[ring:…:COLOR]]`——
  **颜色是用户的权力，不是提供者的**。

### Q5 第二通道 → **不做；判据是"管道覆盖不了的场景"**

- 唯一真正的缺口是**浏览器/网页**（连不上命名管道）。将来若出现"网页要推送"的真实需求，
  再评估回环 UDP（`127.0.0.1`）——那是唯一无法用管道替代的场景；
- 共享内存只在需要 **>100 次/秒**时才有意义，而 §11 已论证面板重绘成本不支持那个频率上屏：
  它解决的是**面板消费不掉的数据率**，属于"先把渲染做成增量"的另一件事；
- 每多一条通道 = 永久多一份安全面与测试面。

### Q6 多面板 → **不做，也不预先抽象**

单实例假设贯穿 30+ 个全局量（`g_window` / `g_hudThread` / `g_intro` / `g_rowState` /
`g_values` / `g_settings` / `g_monitors[]` / `g_iconImage[]` …），而且**测试宿主直接
`#include` 产品源码、断言的就是这些全局函数**。半成品的"面板实例化"会污染整份代码，
却没有测试守得住。

真要做的是一次**有意的重构**：状态收进 `struct Panel`、相关函数加 `Panel &` 参数、
测试宿主跟着改。*触发条件*：出现"主屏放状态行、副屏放通知岛"这类真实需求——
现在已有的 `Monitor = Topmost / Primary` 已覆盖 90% 的诉求。

---

## 附录 A：客户端示例

PowerShell：

```powershell
$p = New-Object System.IO.Pipes.NamedPipeClientStream('.', 'endfield-info-windhawk', 'Out')
$p.Connect(2000)
$w = New-Object System.IO.StreamWriter($p, [System.Text.UTF8Encoding]::new($false))
$w.AutoFlush = $true
$w.WriteLine('HELLO 1 music')
$w.WriteLine('SET music title - Now Playing: Blue Hour')
$w.WriteLine('SET music progress 60000 0.42')
$w.WriteLine('BYE')
$p.Dispose()
```

Python（无第三方依赖）：

```python
with open(r'\\.\pipe\endfield-info-windhawk', 'wb', buffering=0) as h:
    h.write(b'HELLO 1 music\n')
    h.write('SET music title - 海の声\n'.encode('utf-8'))
```

## 附录 B：与现有代码的对应表

| 现有符号 | 位置 | 本设计如何用它 |
| :-- | :-- | :-- |
| `RequiredGroups` | [1343](../endfield-info-windhawk.wh.cpp#L1343) | 决定是否建管道（新增 `ext.` 前缀判定） |
| `Group` / `kVariables[]` | [1303](../endfield-info-windhawk.wh.cpp#L1303) / [1318](../endfield-info-windhawk.wh.cpp#L1318) | 新增 `G_EXT`；静态表**不动**，运行期键走槽位表 |
| `ExpandTokens` 三态 | [2284](../endfield-info-windhawk.wh.cpp#L2284) | `--` 与"保留字面"的语义原样复用 |
| `g_valuesLock` 快照发布 | [4107](../endfield-info-windhawk.wh.cpp#L4107) | 外部值**不进** `Values`；HUD 展开前浅拷贝槽位表 |
| `kMsgRatesReady` 唤醒 | [259](../endfield-info-windhawk.wh.cpp#L259) / [3931](../endfield-info-windhawk.wh.cpp#L3931) | 新增 `kMsgExtEvent`，事件不等采样周期 |
| sampler 停机模式 | [4177](../endfield-info-windhawk.wh.cpp#L4177) | IPC 线程照抄（停止标志 + 50 ms 切片/事件） |
| `IntroKey`/`EvaluateKeys`/缓动 | [826](../endfield-info-windhawk.wh.cpp#L826) / [846](../endfield-info-windhawk.wh.cpp#L846) | 加 `width` 通道，驱动灵动岛变形 |
| `g_displayHeight`/`g_paintedHeight` | [2672](../endfield-info-windhawk.wh.cpp#L2672) | 泛化为 `g_restingRect`/`g_paintedRect`，**语义不变** |
| 圆角 alpha 掩码 | [3640](../endfield-info-windhawk.wh.cpp#L3640) | 已参数化，变形无需重写 |
| `WM_NCHITTEST` 圆角判定 | [3987](../endfield-info-windhawk.wh.cpp#L3987) | 扩成区域表；圆角外穿透不变 |
| `ReadSettings` + `ClampInt` | [1089](../endfield-info-windhawk.wh.cpp#L1089) | 新增设置项照此校验 |
| `test-layout.cpp` 打桩宿主 | — | 所有纯函数直接进断言 |
