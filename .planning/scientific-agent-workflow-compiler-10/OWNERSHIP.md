# OWNERSHIP — scientific-agent-workflow-compiler-10

## Owned (write)

- `src/agent/harness/` — new compiler files (`workflow_ir.*`, `workflow_analysis.*`,
  `workflow_repair.*`, `workflow_planner.*`, `context_checkpoint.*`, `tool_shortlist.*`)
  + narrow integrations into existing files (registration seams only).
- `src/agent/harness/CMakeLists.txt` integration (append-only source list entries; the
  agent lib source list lives in `src/agent/CMakeLists.txt` — append-only).
- `tests/` — new test files `test_workflow_ir.cpp`, `test_workflow_analysis.cpp`,
  `test_workflow_repair.cpp`, `test_workflow_planner.cpp`, `test_context_checkpoint.cpp`,
  `test_tool_shortlist.cpp`; test CMake registration (append-only).
- `data/agent/evals/cases/` — new closed category files (append-only).
- `data/processing/algorithm_meta/capability/capability_relations.json` — append-only
  edge additions ONLY where repair contracts need them (validated by existing guard).
- `pi/` — drift guard (`pi/drift_check.*` or test), F-PI-1/F-PI-2 fixes, `pi/test/*`.
- `docs/adr/0149-workflow-compiler-10.md` (per-file record only; recent
  0144–0147 tracks do not touch the CONTEXT.md ADR index — same-file churn
  against 9 concurrent tracks).
- `src/agent/harness/harness_error.{h,cpp}` + `run_loop.cpp` + `plan_tools.cpp`
  + `context_ledger.{h,cpp}`: narrow additive extensions (7 new taxonomy codes;
  repeated-error guard; compiler-meta binding parameter), single commits.
- `.gitignore` — the one whitelist block (already committed first).
- `.planning/scientific-agent-workflow-compiler-10/`.

## Read-only (authority elsewhere)

| Path | Owner | Why read-only |
|---|---|---|
| `src/operators/` | scientific-algorithms / temporal / contract-verification tracks | algorithm kernels; F-OPS findings theirs |
| `src/workflow/` engine | workflow engine (ADR 0123) + execution-plane tracks | consume WorkflowRun/StepPlan; never fork the engine |
| `src/processing/framework/` | execution/data-plane tracks | TaskCenter/ToolCallDispatcher consumed at existing seams |
| `src/geospatial/` | data-fabric-10 track | reader/CRS authorities consumed via grounding |
| `src/agent/mcp_server.*` | MCP surfaces | harness tools surface through SpatialToolRegistry automatically; no server edit needed |
| `data/processing/algorithm_meta/capability/rs-*.json` | D8 derivation pipeline (`scripts/capability_knowledge_tool`) | generated; hand-edit = drift |
| `review/` | whole-repo-line-review | authority, not writable |
| `pi/knowledge/` | rendered pages (capability_pages) | generated from sidecars |

## Shared-file conflict table

| File | Also touched by | Mitigation |
|---|---|---|
| `src/agent/CMakeLists.txt` (source list) | any harness track | single append block, one integration commit |
| `tests/CMakeLists.txt` | every track | append-only new test blocks, one integration commit |
| `.gitignore` | track starts | already committed first; narrow 4-line block |
| `CONTEXT.md` (ADR index) | every ADR-producing track | append-only tail entry in Phase 9 |
| `data/agent/evals/cases/*.json` (per-category files) | harness tracks | NEW category files only; never edit existing ones |

## Cross-track interface stability promise

`workflow_ir.h` will declare its artifact-fact structs against the EXISTING
`DatasetUnderstanding` document shape (produced by `spatial:understand`) and the EXISTING
merged capability-knowledge entry keys — no new requirements on other tracks' formats.
Other tracks that later want IR facts consume `WorkflowIR::toJson()` (versioned wire
shape); we do not reach into their code.
