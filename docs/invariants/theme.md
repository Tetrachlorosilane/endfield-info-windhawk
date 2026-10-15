# 配色与变量有效性不变量（原 AGENTS.md §5.2）

**何时读这个文件**：改主题解析、颜色设置项、环色优先级、"取不到的读数"（`--`）或 PDH/NVML
有效性判定之前。

**同读**：[window.md](window.md)（窗口与置顶）、[icons.md](icons.md)（环与图片的配色来源）；
平台侧的 NVML 加载缺陷在 [../platform-notes.md](../platform-notes.md)。

---

## 1. 主题与颜色字段

- **命名主题完全胜出，颜色字段被忽略。** 只有 `Theme = Custom` 才读颜色字段。
  曾做成"预设打底、字段覆盖"，结果预设永远看不到。
- **进入命名主题分支时要显式清 `ringColorUserSet`**，而不是仅仅不读它——
  否则残留字段可能从某条路径泄漏进预设配色。
- **环色优先级**：`:COLOR` > `Ring colour`（仅 Custom）> 主题边框色。

## 2. "已知但取不到"必须画成 `--`

- **`{gpu.temp}` 等"已知但取不到"的变量渲染为 `--`**，未知变量保持字面文本。
  二者必须区分（见 AGENTS.md §6 第 3 条）。**每个 NVML-only 读数都要有自己的有效性标志**
  （`gpuTempValid` / `gpuFanValid` / `gpuPowerValid` / `vramUsedValid` / `vramTotalValid`）：
  少一个标志，"这台机器读不到"就会被画成一个看着很像真读数的 `0`——温度、风扇、功耗、
  显存总量各栽过一次。

## 3. PDH 的"这个值能不能用"只有一个判据

- **`PdhValueUsable()` 接受 `PDH_CSTATUS_VALID_DATA` 和 `PDH_CSTATUS_NEW_DATA` 两者。**
  CPU 时钟路径一直接受两者，而 GPU 的数组读取只接受 `ERROR_SUCCESS`（即 VALID_DATA）——
  于是**计数器刚建好时的第一个采样被丢掉**，表现为 GPU 读数晚一拍才出现。
  三条路径现在问同一个函数。

## 4. 这套配色的外部数据面

`{ext.*}`（外部提供者推来的值）的 `severity` 档位与配色规则由
[../external-data-island-design.md](../external-data-island-design.md) §5 与 §9 拥有
（**提供者不能指定颜色**，只能选档位），这里的命名主题优先级对它同样成立。
