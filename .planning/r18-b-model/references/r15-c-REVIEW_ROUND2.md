# R15-C Review Round 2 — 独立复审记录

- **Reviewer**：未参与本轨任何生产代码/测试编写的独立 subagent（只读；未修改/构建/git 写操作）。
- **对象**：`git show 30e8af67e`（第 1 轮修复提交）+ `git diff 46791bc6..HEAD` 全量（8 commits）。
- **结论**：**PASS**（无开放 P0/P1/P2）。3 个 P3，其中 1 个已修复、1 个已记录为已知限制、1 个为记录更正。

## 发现与处置

| # | 级别 | 发现 | 处置 |
|---|---|---|---|
| R2-1 | P3 | `model_publish.cpp` fold 分支注释仍描述旧删除顺序（"drops the main backup first…"），而该顺序正是被修复的问题根源——未来读者可能据旧注释把顺序改回去 | **已修复**：注释同步为新顺序（companions/prov 先、main 后）并加显式"勿改回"警告（改回会使撕裂状态重新可达） |
| R2-2 | P3 | "VRAM but" 投影两处拼写缺口：(a) resolveDevice 的 "no fitting CUDA device and cpu_fallback is disabled"（仅显式 runtime.device 可达）同属预算类却投影 InvalidInputData——同一超预算条件经 legacy 路径得 ResourceBudgetExceeded、经 "auto" 得 InvalidInputData；(b) fallback 链复合 reason（主候选非预算失败+后备预算失败）会读到后备的 "VRAM but" | **记录为已知限制**（reviewer 判定不阻塞）：(a) 该消息是混合条件（无设备 vs 无空间），映射预算错误会误导无 GPU 主机，保持 InvalidInputData；(b) 链式 trail 是既有文档行为、主候选 verdict 优先。两者已在代码注释中显式记录；类型化 reason（改 evaluateRuntimeReadiness 签名）留待后续 |
| R2-3 | P3 | 记录更正（非代码缺陷）：提交 30e8af67e 称 "must be a 4-d" pattern "matched no message in the tree" 不准确——分类器先小写化消息，这两个 pattern 对树中 "must be a 4-D … NCHW blob" 系列本是 LIVE 的。替换为 ("must be" && "nchw") 后 reviewer 对全部 8 条含 nchw 的消息逐个推演：分类与旧 pattern 完全一致（6 条 fed-input→ShapeMismatch；2 条 rank-4 输出 "is not 4-D NCHW"→Unknown；dtype 消息被更早的 IncompatibleSchema 分支拦截），代码变更行为保持、健全 | **已更正记录**（LEDGER.md）；无需代码改动 |

## Reviewer 的逐项复检结论（摘要）

1. **P1 复检通过**：(a) readiness 层全部 IncompatibleHardware 产生点已枚举，含 "VRAM but" 的恰为两条目标拼写，其余 5 条不含、不会误映射；(b) 新测试非虚（EnvRestore RAII 在 REQUIRE 失败路径也执行；输入存在性检查在 readiness 门之前故需真实栅格；catalog 从不调用 evaluateRuntimeReadiness；"onnx" provider 无条件注册 → 必达 VRAM 拒绝；旧代码大小写敏感+不连续 → RED 方向可独立推导）；(c) 无其他消息意外含 "VRAM but"。
2. **P2 复检通过**：两个 guard 的 removeBackupFamily 三个调用点各自崩溃的全部后缀已推演——新顺序下任何 mid-disarm 崩溃必留主备份 → fold/Window A/B/C 全部正确门控；"主备份缺失+companions 在备份名"现唯一来源于 mid-park 崩溃（终路径上是旧产品，fold 恢复其自身 companions 正确）——撕裂歧义被结构性消除。新回归用例在 master 上也通过（守卫而非 RED，实现者已如实记录；手工构造"主缺失+litter"状态在新代码下仍会撕裂，故该状态不能作 RED 用例——测试选型正确）。顺序改变不影响 Window B 假设。
3. **P3 复检通过**：OpenVINO `>= 0` 稳健（acquire 处 effective.runtime.gpu=device.gpu 保证 mDevice=="GPU" 时 resolvedCudaIndex 即实选索引）；分类器行为保持；tile 注释与代码一致。
4. **全量复审 CLEAN**：8 commit 逐个核对，未触及任何禁止文件（gpu_plane.cpp、src/python/isolated、src/agent、src/workflow、src/processing/algorithms、src/geospatial、src/operators/rs、.github/workflows 均零接触）；tests/CMakeLists.txt 6 个新目标全部注册。
5. **证据核验**：P1 的 RED/GREEN 方向与两个新用例的非虚性均可从代码独立推导；实现者的构建/运行证据未复跑（只读审查）。

## Reviewer 明确标注的 NOT VERIFIED

- 未构建/运行任何测试（只读）；exit 42 具体数值未复现（失败方向可推导）。
- test_device_planner 本宿主挂起（无 GPU/NVML）。
- PERFORMANCE.md 数字；#1513 远程 draft 内容。
- 工作树未提交的 .planning 文件仅作证据过目。

**结论：无开放 P0/P1 → 满足提交 Ready PR 的门槛（P2/P3 均已处置或记录）。**
