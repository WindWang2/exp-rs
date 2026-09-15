# REVIEW_LOG — D18 persist + IR2 run + classify path slice

Independent review of MissionContext dual-write, IR2 PipelineRunCoordinator mount, classification path Results.

## Architecture
- Sidecar + sibling XML dual-write; **not** inside `sicnuDataManager` — OK (avoids v3 governance coupling).
- IR2 dock owns `PipelineRunCoordinator` child; LabSpec lift reuses D17 helper — OK (no third scheduler).
- `WorkflowDocument` alias only; Engine 2.0 type untouched — OK (D-W3).

## Lifecycle
- Coordinator parented to dock; cancel on UI — OK.
- Project read restores mission after DataProjectSerializer — OK.

## Workflow / Agent
- ActiveWorkflowRef.runner remains `pipeline_run_coordinator`; run completion publishes WorkflowRun — OK.
- Default synthetic node executor still used (D17) — known limitation for real operators.

## Tests
- Dual-write + path-product + IR2 run identity contracts added.
- **Not executed** on agent box (no cmake/g++). Honest EVIDENCE.md.

## Findings disposition
| ID | Sev | Finding | Disposition |
|----|-----|---------|-------------|
| R1 | P2 | IR 2.0 / Engine 2.0 same C++ type name | Alias added; full rename deferred (D-W3) |
| R2 | P2 | PipelineRunCoordinator used synthetic executor | Acceptable for designer smoke; operator bind follow-up |
| R3 | P2 | Classification Result provisional without path | **Cleared when path products exist** (D-I4) |
| R4 | P2 | Builds not run on this box | Honest EVIDENCE.md |
| R5 | P1 | Mission not in project save path | **Cleared** (D-M5 dual-write) |
| R6 | P2 | Guided LabSpec / PipelineRunCoordinator not production-started | **Cleared** for start path (D-W4); guided cards UI still optional |

No new P0.
