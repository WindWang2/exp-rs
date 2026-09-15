# CURRENT_ARCHITECTURE — authority/seam 图（master@a5b11b7f）

## Authority（唯一真值所在）

| 域 | Authority | 备注 |
|---|---|---|
| 错误码 | `src/agent/harness/harness_error.h` error_codes 单表 | 新码只能 additive 进此表 |
| IR + fact staging | `src/agent/harness/workflow_ir.*` | fact_status: observed/declared/derived/assumed/unknown |
| 静态检查 ledger | `src/agent/harness/workflow_analysis.*` | pass/fail/warn/skip 四态 |
| repair 规则 | `src/agent/harness/workflow_repair.*` | 闭表 + risk class |
| 物理事实抽取 | `src/agent/harness/band_facts.*` | 单景 roles/grid/radiometric/SAR/acquisitionTime |
| grounding | `src/agent/harness/grounding_tools.cpp`（spatial:understand） | 缓存 key=(path,revision) |
| capability 知识 | `capability_knowledge/catalog/relations/graph.*` | 111 rs: 算子契约 |
| lowering | `agent_plan.cpp compilePlanToWorkflowJson` | 唯一 plan→workflow JSON 桥 |
| 执行平面 | `src/workflow/**`（本 track 只读） | run/checkpoint/coordinator/derivation |
| provenance sidecar | `src/agent/harness/evidence.*` + 引擎自身 | QSaveFile atomic |
| eval corpus | `data/agent/evals/cases/**` + runner test | data-driven，≤400 |
| Pi bridge | `pi/mcp_bridge.ts` 单实现 + no_drift 测试 | |

## Seams（本 track 消费/扩展点）

1. `compilePlanToWorkflowJson` metadata 根键（agent_plan.cpp:360-366）→ E 包投影入口。
2. `spatial:understand` understanding 文档 + 缓存（grounding_tools.cpp:233-292）→ A/B 包事实源。
3. `analyzeWorkflowIr` check builder（workflow_analysis.cpp:488-501）→ C 包新检查族。
4. `planRepairs`/`analyzeRepairAnalyze`（workflow_repair.*）→ D 包排序/decision 文档。
5. `context_checkpoint` session（事实身份 stale 失效）→ F 包回溯数据源。
6. `tool_shortlist` 8KiB 页预算 → H 包 knowledge-budget 守护对象。
