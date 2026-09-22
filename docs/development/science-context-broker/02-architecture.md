# Architecture — Science Context Broker

## Bundle contract

`schema: "exp.science_context.v1"` — single projection with sections:

- `identity` (`bundle_id`, `goal`, `intent`)
- `assets` — passport summaries + evidence buckets (known/assumed/unknown/conflicted)
- `capabilities` — direct / prep / unavailable / impossible + typed reasons; `structural_only=true`
- `recipes` — deterministic top-K; human-only + verifier flags; never auto-exec
- `constraints` — autonomy / offline / budget / determinism
- `open_questions`
- `planner_projection` — typed fields for `CompileWorkflowRequest` (no prompt mush)
- `truncation` — deterministic metadata
- `observability` — lightweight counters (no Control Center UI)

## Modules (`src/science_context/`)

| Module | Role |
|---|---|
| `AssetStateProvider` | asset key → passport → summary + cache; conflicted ≠ auto-pick |
| `observedStateFromPassport` | passport → capability-facing ObservedState |
| `routeCapabilities` | goals+state → structural candidates (closed intent rules) |
| `RecipeRouter` | injected registry; modality/intent filter; top-K |
| `projectPlanningContext` | Bundle → PlanningContext → compile-request JSON |
| `applyContextBudget` | deterministic truncation |
| `ContextCache` | keys: asset digest, catalog gen, registry rev, pack digest, autonomy |
| `BrokerObservability` | future Control Center metrics API |
| `ScienceContextBroker::synthesize` | orchestration |
| `agent_adapter` | `scientific:context`, `scientific:capabilities`, `data:asset_passport`, `recipe:search` |

## Surfaces

Append-only registration in `data_platform_tools.cpp` (same pattern as suitability).
MCP/`tools/list` parity via DataPlatform projection — **no** `mcp_server.cpp` edit.

## Limits

- Capability rules are a structural subset of harness knowledge (ndvi/evi/change/sar_change/classify); live operator semantics stay in harness/planner.
- Recipe docs are injected (tests / future registry wiring); `src/recipes` library remains unbundled on this tip.
- No Control Center UI; no auto-execution; L2 autonomy never sets `allow_autonomous_exec`.
