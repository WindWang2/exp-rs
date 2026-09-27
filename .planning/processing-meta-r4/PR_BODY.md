# PR 草稿 — hardening/r4-processing-meta

标题：meta(capability): close authored-enrichment completeness gaps for 111 rs: operators; pin snapshot byte-freshness gate (R4 processing-meta track)

## 实测基线

- origin/master `15e5c66b5`（fetch 于 2026-09-27，ahead/behind 0/0）。
- open PR 实测 5 个（#1334–#1338）。重叠处置见 `.planning/processing-meta-r4/BASELINE.md` §4：#1337 动 `data/contracts/contract_graph.snap.json` 与 `src/contracts/error_code_scanner.*`，#1335 动 `tests/test_capability_knowledge.cpp`，#1336/#1337 动 `data/help/commands.json`——本分支基于 master 独立工作，合并顺序需 rebase 时按 planning 记录重跑双跑门禁。

## 逐 WP 根因与修复

- **Phase 0 前提修正（BASELINE.md §3）**：提示词"157−57=100 缺 sidecar"的前提不成立——顶层 sparse sidecar 是"代码中声明 taskFamily 的算子集合"的生成工件，被 `test_algorithm_meta_drift.cpp` 三重钉死（集合==57、字节可再生、往返 57）；capability/ 层 157/157 已与注册表一一对应。真实缺口是 **authored enrichment 字段级**：111/157 侧车至少缺 applicability/teaching_use/prerequisites/limitations 之一（18/22/56/80 键位）。
- **WP-B（核心）**：逐算子读实现（schema 参数、拒绝路径、元数据声明）后直接编辑侧车 authored 键（README 许可的第二通道），每批 10 个算子一个原子提交（11 批），每批经 `capability_knowledge_tool gen-meta` 规范化（authored 保留、derived 重算、闭键校验）。不新增任何 accuracy/gpu 声明（实测 accuracy 有值者 0、gpu 显式 51——不臆造）。

**键位口径两笔账（独立评审更正）**：authored 新增 = applicability 18 + teaching_use 22 + 各算子实现未声明过的 prerequisites/limitations 文本（如中文条目——C++ 侧 string 赋值被描述符层丢弃、只能 authored 保留）；磁盘 diff 中其余 prerequisites/limitations 行是陈旧侧车经 gen-meta 从**既有 C++ 声明**回填（mergeStringLists 重放，内容与实现逐字一致）。门禁语义不受影响：等式钉住的是"四键非空"。
- **WP-A**：`META_COVERAGE_MATRIX.md` 157 行双口径矩阵（注册↔类↔头文件↔实现文件↔sparse↔capability↔authored 四键↔det/mem/gpu/acc），由 `gen_matrix.py` 从源树自动生成可复核。
- **WP-C**：x-rs-contract→描述符→侧车 io/crs 链路抽样贯通（rs:change_detection 等 3 例源级比对一致）；全量一致性由既有运行时门禁把守（D8 drift、knowledge mirror agreement、契约图 capability_entry 节点）。
- **WP-D**：新增 `tests/test_snapshot_gate_r4.cpp`：①图+census 生成器双跑字节稳定；②两快照"提交==活生成"字节新鲜度断言（把 `contract_inventory --check/--census-check` 变成本地红绿测试）；③合成漂移必须被 `formatSnapshotDiff` 归因到元素与字段。assembler 级文件注入不做的理由（DECISIONS #4）：装配是源扫描，注入需整树拷贝；②已使任何真实源/元数据变更必然红。
- **WP-E**：补齐后经 `capability_knowledge_tool gen-pages` 再渲染 pi/knowledge 页面并独立提交；`gen-pages --check` 零漂移。
- **WP-F**：`HELP_CONSISTENCY.md` 76/76 静态对照 + 18 条 `command.rs.*` purpose ↔ 算子 summary 语义对照（零矛盾）；算子面帮助由 schema 自动派生无缺口面。
- **WP-G**：`test_capability_completeness.cpp` 追加四键 authored census 测试（等式钉 157，无豁免），"补齐后倒退即红"；与快照 gate 职责分离不合并。

## census 摘要

- 注册口径：157 注册行全部归位；sparse（taskFamily）56/157（另有 gdal:polygonize 计入门禁的 57），**维持设计不变量**，未补顶层 sidecar。
- 字段口径：补齐前 111/157 缺 authored 键（18 applicability + 22 teaching_use + 56 prerequisites + 80 limitations = 176 键位）→ 补齐后 157/157 四键完备（census 门禁等式）。

## 快照 gate 双跑证据 / 页面 diff 审计 / 本地验证

见 `.planning/processing-meta-r4/EVIDENCE.md`（E1–E6，双跑命令与两轮结果、`gen-pages --check` 零漂移、ctest 两轮日志路径）。

## 用户可感知的行为变化

- 无产品行为变化：全部改动为元数据（capability 侧车 authored 键）、生成页面（pi/knowledge）、快照再生成与新测试。算子执行语义零改动。
- Agent/帮助面用户可见更丰富的中文适用性/教学/前置/局限说明（capability knowledge 页面与 error catalog 消费侧车）。

## 本地验证（双跑，未等待线上 CI）

- 工具链：cmake `/home/kevin/toolchain/cmake-dist`，ninja pwb-sdks，`-j2`（RSS>70% 降 -j1 有记录），`QT_QPA_PLATFORM=offscreen`。
- `ctest -R "capabilit|contract|meta|registry|snapshot" -j1` 两轮 + `contract_inventory --check/--census-check` 双跑：结果见 EVIDENCE.md。
- 如实声明：本 PR 未等待线上 CI；本地验证以 ctest 退出码为准。

## 未解决项（backlog）

1. `scripts/capability_enrichment.py` 账本分母过期（115 vs 语料 157，--strict 实测红）：白名单外未修；建议后续轨道把其账本对齐或下线该一次性迁移工具。
2. 101 个 rs: 算子未在代码中声明 taskFamily（sparse 56/157）：产品级 opt-in 决策，逐族补声明需独立轨道（会改变 task→算法发现行为）。
3. gpu 显式声明 106 缺（默认 FullRaster/nogpu 推导）：可在后续按实现逐个显式化。
4. Units/NoData 结构化契约缺失（D1 已 WARN census）：基础设施缺口，非本轨道范围。

## 独立评审结论（REVIEW_LOG.md 全文）

对抗性只读评审（1 subagent）：**pass，无 P0**。白名单零越界；11 批与 authoring 清单逐批全等；failure_modes 语义零变化；10 算子抽样实现比对**零臆造**；新门禁真红/真绿能力验证（#960 在基线态实红）。已修复的评审发现：EVIDENCE 回填+关键行内联、BASELINE §6 假阳性注记（13 条 --strict 假阳性）、键位两笔账（上）、Not Run 集合实测收口（chunk_contract 11 的 1 个失败与 metamorphic 1122 断言全绿均已归因，见 EVIDENCE E8）。batch 00 提交信息更正：除 c00.json 的 10 个算子外，该提交另含 DECISIONS #10 所记 2 个陈旧侧车的 gen-meta 规范化（trend_lambda 默认值修复、failure_modes 键序规范化，内容经逐字段核实语义不变）。

## 纪律记录

- 子代理 ≤3（实际用 1：独立评审）；全程 `-j2`（RSS 峰值记录于账本）；未等待线上 CI；白名单外零改动（账本可审计）；`.goal-loop-ledger.md` 每轮记账（token 为会话估算值，如实标注）。
