# REVIEW_LOG — D18 IR2 operator-bind + WorkflowDocument surface

Independent review of registry NodeExecutor bind and IR2 WorkflowDocument call-site rename.

## Architecture
- Extends D17 `NodeExecutor` + existing `RSOperatorRegistry` — OK (no third scheduler / framework).
- Dock installs registry executor at construction — OK (synthetic default no longer production path).
- Unbound = typed refusal prefix `ir2.operator_unbound:` — OK (honest fail-closed).
- `WorkflowDocument` alias on IR2 surface only; Engine 2.0 type untouched — OK (D-W3b).

## Lifecycle
- Executor is a copied `std::function` into workers (same as D17) — OK.
- Operator execute may throw `RSOperatorError` → mapped to `ir2.operator_failed` — OK.

## Workflow / Agent
- ActiveWorkflowRef.runner remains `pipeline_run_coordinator` — OK.
- Bound operators still run inside PipelineRunCoordinator, not TaskCenter — intentional (D-W1).

## Tests
- Contract case `scenario3c_ir2_registry_bind_policy` added.
- **Not executed** on agent box (no cmake/g++). Honest EVIDENCE.md.

## Findings disposition
| ID | Sev | Finding | Disposition |
|----|-----|---------|-------------|
| R2 | P2 | Synthetic executor on dock Run | **Cleared** (D-W5 registry bind) |
| R7 | P2 | Unbound nodes previously succeeded synthetically | **Cleared** (typed refusal) |
| R1 | P2 | Dual WorkflowDefinition name | Alias call sites advanced; struct rename deferred |
| R4 | P2 | Builds not run on this box | Honest EVIDENCE.md |

No new P0.
