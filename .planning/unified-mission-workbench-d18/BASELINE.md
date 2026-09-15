# BASELINE — Unified Mission Workbench D18

Recorded: 2026-09-15 (UTC+8 / Asia/Shanghai), agent run start.

## Repository pin

| Item | Value |
|------|-------|
| Worktree | `/workspace/exp-rs-unified-mission-workbench-d18` |
| Branch | `grok/unified-mission-workbench-d18` |
| Seed commit | `b651b86101f09983f2e79237bbdccdb7dc018a5c` (chore seed) |
| Baseline `origin/master` | `ebcafb4d02ec3522eaaa4b3b62b1082c36280ffb` |
| Master tip message | `fix(ci): correct OSR WKT import in test_io_operators (#990)` |
| Master policy | **READ-ONLY** — never commit on `/workspace/exp-rs` master |

## Recently merged vertical tracks (source of truth: `git log` + `gh pr list`)

| Track | PR | Merge SHA (approx) | Title |
|-------|-----|--------------------|-------|
| D14 Geometric registration | #987 | `64418b72` | GCP / transform / TPS / RANSAC / resampler / pan-sharpen / dual-window / agent / labs |
| D15 Classification/change studio | #989 | `b91753ff` | leakage / GLCM / classifiers / change / accuracy / studio UI / agent / labs |
| D16 Temporal phenology timeline | #986 | `e8c4bf43` | cube / phenology / BFAST / trends / STARFM / timeline UI / agent / Lab08 |
| D17 Visual workflow pipeline designer | #988 | `f368b9fd` | IR 2.0 / DAG / contracts / repair / optimizer / PipelineRunCoordinator / canvas / orchestrator |
| WB visual cartography 10 | #982 | (prior wave) | professional workbench / visual analytics / cartography bridge |
| Workflow compiler 10 | #979 | (prior wave) | typed WorkflowIR 1.0, static analysis, repair |
| Large-scale execution 10 | #980 | | external-memory / multi-worker |
| EO AI model runtime 10 | #981 | | model manifests / adapters |
| Temporal EO platform 10 | #973 | | calendars / harmonic-break / multi-ROI |

Open PRs at audit time: **none**. Open issues via `gh issue list`: **none returned**.

## Already-landed authorities (do not reimplement)

- **Data / Display**: `ProjectContext` owns `DataManager` + `QgisDisplayManager` + `WorkspaceService`
- **Selection**: `SelectionContext` + `WorkbenchObjectRef` / `ObjectKind` (Workbench 10)
- **Agent workbench projection**: `workbench:context` (`WorkbenchContextTool`)
- **Execution**: TaskCenter + `WorkflowRunCoordinator` (production) + ExecutionPlane
- **Agent plans**: `AgentPlan` v2 → `compilePlanToWorkflowJson` → Workflow Engine 2.0 JSON
- **Compiler IR 1.0**: `sicnu::agent::harness` WorkflowIR (`kind: workflow_ir`, schema 1.0)
- **Designer IR 2.0**: `sicnu::workflow::WorkflowIR` in `workflow_ir_v2.*` (Qt JSON)
- **Cartography**: MapSpec / layout / preflight platform (do not rebuild)
- **Dataset / Experiment**: strong IDs (`DatasetId`, `RunId`, …), stores, lineage

## Toolchain on this box (honesty)

| Tool | Status |
|------|--------|
| `cmake` | **missing** |
| `g++` / Qt6 headers | **missing** |
| `gh` + token | available |
| Local compile/test | **not executable here** — implement + wire tests; record in `EVIDENCE.md` |

## Re-analysis method

Code inspection of `src/workflow/**`, `src/agent/harness/**`, `src/app/workbench/**`, `src/app/pipeline/**`, `src/app/workflow/**`, `src/app/main_window*.cpp`, ADRs 0149/0159/0161/0162, D14–D17 `.planning/*/DECISIONS.md`, and recent merge commits. Historical planning summaries were treated as hypotheses only.
