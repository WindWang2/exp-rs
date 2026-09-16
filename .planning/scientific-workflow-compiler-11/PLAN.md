# PLAN — Scientific Workflow Compiler & Grounding 11.0

基线：`origin/master@a5b11b7f`。真实缺口清单见 BASELINE.md「真实缺口」G1–G8。
原则：master 已有能力不重造；每个包先证据、再最小 vertical slice、再 known-answer/negative test、再文档同步、再 commit。

## 包 → 缺口 → 设计映射

| WP | 缺口 | 新增/扩展（全部落 primary scope） | 独立 oracle |
|---|---|---|---|
| A fact model 2.0 | G1 | 新模块 `workflow_facts.*`（harness）：time cadence/regularity/coverage 解析与规范化（ISO8601 子集，独立真值解析器，不依赖被测实现）；spatial resolution 分级 + extent 规范化；quality mask 语义规范化；product generation facts；model task facts 投影；per-node resource facts。全部带 `fact_status` 来源等级 | `tests/test_workflow_facts_11.cpp` known-answer + negative（坏时间戳/混合时区/非法 cadence → typed unknown，不伪 pass） |
| B live grounding | G2 | 新模块 `grounding_probes.*`：fact-scoped bounded probe（字段集合、字节/超时预算、GDAL 只读打开、stat 缓存 + 失效）；`model manifest` / product registry 探针走既有 model_catalog_tool / workspace seam 的只读适配 | `tests/test_grounding_probes_11.cpp`：真实小 GTiff probe、超时/上限 fail-closed、缓存失效、只读目录 |
| C analysis 2.0 | G3 | `workflow_analysis.*` 扩展 4 检查族（additive）：`temporal_calendar`（跨节点时间覆盖/日历矛盾）、`numeric_domain_chain`（dn/reflectance/index 混链）、`band_identity`（同 role 波段跨边一致性）、`output_identity`（declared kind/semantic vs 上游事实）。新错误码 additive 进 harness_error.h：TEMPORAL_CALENDAR_CONFLICT、NUMERIC_DOMAIN_CHAIN、BAND_IDENTITY_MISMATCH、OUTPUT_IDENTITY_MISMATCH | `tests/test_workflow_analysis_11.cpp`：每检查 pass/fail/warn/skip 四态 + UNKNOWN 不伪 PASS 断言 |
| D repair 2.0 | G4 | `workflow_repair.*` 扩展：prepared decision 文档（完整 wiring+params+facts 引用+成本/风险/证据三元排序，确定性顺序）；shape_preserving 自动路径不变 | `tests/test_workflow_repair_11.cpp` 扩展：排序确定性、science_changing 永不自动插入（负例）、decision 文档可直接 lowering |
| E provenance projection | G5 | 新模块 `provenance_projection.*`：IR+analysis+repair+refusals → `metadata.compiler` 规范块（版本化 schema、bounded、canonical 序列化）+ `<output>.compile.json` sidecar writer（QSaveFile atomic）；接线 `compilePlanToWorkflowJson` 调用路径（harness 侧） | `tests/test_provenance_projection_11.cpp`：同输入同投影字节、失败/只读目录 atomic 语义、schema drift anchor |
| F explain/diagnose | G6 | 新模块 `workflow_explain.*`：session(compile 决策链) + run 错误码 → 逐因回溯（哪条事实/契约/repair/refusal 导致决定），zh-CN 文案 + bounded 输出（≤8KiB） | `tests/test_workflow_explain_11.cpp`：known-answer 回溯 + bounded + 中文可读断言 |
| G eval corpus | G7 | `data/agent/evals/cases/` 追加 compiler/grounding case 族（temporal cadence、mixed modality、anti-hallucination、refusal→decision）；corpus runner 自动覆盖 | `ctest -R test_harness_eval_corpus`（含新 cases）+ anti-hallucination 断言 |
| H bridge robustness | G8 | `pi/test/` 追加 compiler-surface drift/abort/budget node 测试；knowledge budget（tool_shortlist）已有 → 只加守护测试；schema drift 锚到 `irLimits()`/schema_version | `node --test pi/test/scientific_workflow_compiler_11.test.mjs` |

## 执行顺序（Phase）

- **Phase 0**（本文件 + 审计工件 + dev-default configure 验证）→ commit。
- **Phase 1**：A 包（workflow_facts + 测试）→ commit；rebase。
- **Phase 2**：B 包 probes + C 包分析扩展 → commit；rebase。
- **Phase 3**：D 包 repair 2.0 + E 包 projection + F 包 explain → commit；rebase。
- **Phase 4**：G 包 corpus cases + H 包 node 测试 + planner/understand 接线 + docs/ADR → integration commit；rebase。
- **Phase 5**：硬化（bounds、cancel、失败清理、Unicode path、只读源、性能记录 PERFORMANCE.md）→ commit。
- **Phase 6**：E2E known-answer（真实小 GDAL fixtures 全链 compile→projection→run seam 只读验证）→ commit。
- **Phase 7**：subagent #2 独立 review（只读，全 diff）→ P0/P1 修复 → 重跑 gate。
- **Phase 8**：双验证 + rebase + PR。

## 预算分配

500M 总额，按 GOAL Phase 表（30/80/90/75/65/55/45/35/25M）；超 1.5× 记 EVIDENCE 继续。
