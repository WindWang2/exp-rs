# The Authoritative Execution Path (Operator Execution Convergence)

Status: current architecture — see ADR 0126 (OBIA convergence, issue #663),
ADR 0120/0121/0122 (atomic architecture, agent layer, Pi), ADR 0123 (workflow
engine 2.0) for history.

## One execution path

Every frontend is a **client**. Frontends express *what* to run; they never
run algorithms themselves.

```
GUI dialog / OBIA window     CLI pipeline        Agent copilot
Task panel / Workflow UI     MCP tools (+Pi)     Workflow engine
        │                          │                   │
        ▼                          ▼                   ▼
GuiJobHandle / TaskCenter::submitJob / ToolCallDispatcher / submitPipeline
        │                          │
        ▼                          ▼
   TaskCenter  (admission: slots, RSS watermark, RAM budget, DAG gating)
        │
        ▼
   JobEngine worker ── resolution order (ADR 0062):
        1. per-job executor lambda   ← transitional only; new GUI code must NOT use this
        2. prefix executor ("processing:", "py:")
        3. RSOperatorRegistry::create(algorithmId)   ← THE canonical path
        4. AtomicAlgorithmRegistry fallback
        │
        ▼
   RSOperator::run(params, RSOperatorContext&)   — the operator contract
        │  (schema-validated params, [0,1] progress, cooperative cancel,
        │   RSOperatorError codes; kernels live in src/analysis)
        ▼
   Outputs: file/CSV at caller-chosen paths → dialog auto-load or
   OutputCommitter → DataManager::registerSource (assets, lineage)
```

### Frontend responsibilities
- Collect parameters and map them 1:1 onto the **operator schema** — widget
  defaults come FROM the schema, never from independent constants.
- Submit real operator ids (e.g. `runOperatorTask("rs:spectral_index", json)`
  or `TaskCenter::submitJob(req)` with no executor).
- Rehydrate presentation state from operator file outputs; show progress and
  human-readable errors.
- Boundary rule (ADR 0126): a GUI may hold analysis **data structures**
  (label maps, stat tables, hierarchies) but may not **execute** kernels or
  construct classifier backends.

### Operator responsibilities
- Own the executable contract: schema (params/defaults/outputs), semantic
  validation sufficient for NON-GUI callers (Agent/MCP/CLI bypass dialogs),
  kernel delegation with progress/cancel/error plumbing, output writing,
  result JSON. Policy that used to live in frontends (e.g. the OBIA
  prefer-OTB/fallback rule) belongs here.

### Kernel responsibilities (`src/analysis`)
- Pure computation; progress hooks + cancellation polling; structured errors
  (empty-result sentinels mapped by operators); no GUI dependencies, no
  QMessageBox, no dialog includes.

## Adding a new remote-sensing operator (the standard path)

1. **Implement/reuse the kernel** in `src/analysis/<domain>/` — data in,
   data out, optional `isCanceled`/`onProgress` hooks (see
   `RsSimpleSegmenter`, `RsSegmentFeatures`).
2. **Define the operator** in `src/operators/rs/`: `name()` = `rs:<op>`,
   `schema()` (declare EVERY parameter + default + outputs — this document is
   the single source of truth for frontends and agents), `metadata()`
   (purpose/useCases/limitations/workflowHints), `executionEstimate()`,
   `run()` (validate → delegate to the kernel → write outputs → result JSON).
   Throw `RSOperatorError` with precise codes; poll `context.throwIfCancelled()`.
3. **Register** in `src/operators/rs/rs_operators_init.cpp` — both the static
   `REGISTER_RS_OPERATOR` and the guaranteed `initBuiltinRsOperators()` list
   (#707), plus `src/operators/CMakeLists.txt`.
4. **Frontend adapter** (GUI): a dialog/panel submits the operator id with
   schema-shaped params; if session state is needed, rehydrate it from the
   operator's file outputs (pattern: `src/app/obia/rs_obia_operator_adapter`).
5. **Agent discovery is automatic**: the registry → `AtomicAlgorithmRegistry`
   bridge projects descriptors into the tool catalog (`tools/list`,
   `tools/search`); keep `metadata()`/`schema()` honest and no further wiring
   is needed. Pi rides MCP (`pi/mcp_bridge.ts`).
6. **Tests** mirroring the layering: kernel semantics (analysis tests),
   operator contract (schema/validation/outputs/cancel — pattern:
   `tests/test_obia_operators.cpp`), seam (TaskCenter resolution — pattern:
   `tests/test_obia_task_center.cpp`), thin GUI adapter (dispatch + mapping —
   pattern: `tests/test_obia_main_window.cpp`).

### Anti-patterns (rejected)
- `TaskCenter::submitJob(req, executor-lambda, ...)` from GUI code with a
  `module:*`/`callable:*` pseudo id that calls kernels directly — the exact
  pattern issue #663 removed. The executor channel remains for genuinely
  app-owned jobs (georeferencer sessions), not for algorithms with an
  operator equivalent.
- Independent defaults/validation/policy duplicated between GUI and operator.
- Inventing a second execution framework beside TaskCenter/JobEngine.

## Known remaining debt (documented, owners elsewhere)
- Pixel-classification window in-memory session (ADR 0019 GUI parity).
- Post-process chain operators (sieve/clump), consolidation operator, CV.
- Georeferencer interactive GCP kernels (no operator equivalents).
- Output-registration divergence D1–D4 (see `.plan/architecture-map.md`
  history in PR review; operation-log coverage, agent-plan commit path).
