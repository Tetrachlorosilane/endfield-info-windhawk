# 字体回退与逐段覆盖不变量（原 AGENTS.md §5.7）

**何时读这个文件**：动 `Fallback fonts` / `[[font:…]]` 的解析与容错、覆盖查询、
字体对象缓存，或改 `ReadSettings` 里的标记编译（`CompileRowText`）之前。

**同读**：[markup.md](markup.md)（标记机制的通用规则）、[text-measurement.md](text-measurement.md)
（字体每帧重建是"不缓存省略号宽度"的原因）。

设计规格（设置项、解析规则、`PickFontFamily` 的判定顺序、断言清单）：
[../font-and-row-metrics-design.md](../font-and-row-metrics-design.md)。

---

- **成本闸门（这条是这个功能的形状）**：`Fallback fonts` 为空**且**该行没有 `[[font:…]]` 标签时，
  走的就是**单字体那条老路**——一行一个 `GpFont`、零覆盖查询、零额外分段。闸门由
  `RowNeedsFontPath(rowHasTag, hasGlobalChain)` 判定，**在配置期**（`ReadSettings` 扫原文，结果存进
  `rowFontTag[]`），不在每帧判定。不要说"反正有缓存"就把它去掉。
- **纯 ASCII 的段不查覆盖**（`PickFontFamily` 的第一条规则）。全英文面板即使配了回退链也不付代价，
  这是性能承诺的另一半；`[perf]` 的 `glyphq=` 就是量它的。
- **逐段覆盖的语义有两档**：`[[font:族名]]` 里的**第一个**族名在 ASCII 段上直接生效（零查询）；
  非 ASCII 段按顺序问覆盖（一个都不覆盖时继续问全局链，最后才是主字体）。所以"给一段指定字体"
  不会在中文段上强行画出豆腐块。回退决策是**逐段**的：混排的一段整段交给第一个覆盖全段的候选。
- **标记机制与 `[[ring:]]` 是同一个实现**：展开期把标签换成 `kFontMarkStart` + 链序号 + `kRingMarkEnd`，
  `BuildRowLayout` 剥标记并给每个 `TextSeg` 带上 `tagChain`。**剥标记只有这一处**，绘制侧从不解析标记。
  标签 → 链序号用的是**规范文本**（`FontChainKey`），所以行里写 `A ; B` 与 `A;B` 命中同一条链。
- **坏标签绝不吞文字。** 族名写错/没装/超过 6 条链的 `[[font:…]]`，以及**画不出来的 `[[ring:…]]`**，
  都**原样留在文本里**。曾经的实现是"消费掉但不产出"：屏幕上少一段读数，而删掉的正是唯一线索
  （`ParseRing` 的注释从第一天起就承诺"调用方回退到原文"，代码却没有）。两条都有断言。
- **覆盖查询缓存 256 槽**（键 = 族哈希 + 字符，无堆分配），**字体对象缓存 8 槽**（键 = 族 + 字号）。
  字体缓存里**空槽优先于 LRU**——否则两个字号交替绘制时每帧重建对象，缓存等于没有（实测断言抓到过）。
  设置变更时在 HUD 线程整体释放（`ReleaseFontResources`），卸载时在 `GDI+ Shutdown` **之前**释放。
- **回退族名与标签族名都要过 `FontIsInstalled`**（与主字体同一个列表）。`CreateFontW` 对未知族名会
  **替换**成某个字体，而那个替身会照常回答覆盖查询——于是"回退生效了"是假的，画出来的也不是任何人都
  指定的字体。没装的因此被丢弃（标签随之原文显示，日志一条汇总）。
- **行内标记在读取设置时编译一次**（`CompileRowText` → `ModSettings::rowProgram[]`，与 `rows[]` 同一份数据），
  发布路径只展开 `{变量}` 与环的数值字段（`InstantiateRowProgram`），发布期不再解析标签、也不再查链序号。
