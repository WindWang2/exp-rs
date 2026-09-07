# Spatial Scientist Architecture (Harness 4.0)

> **Pi is the generic agent foundation; ExpRS implements the geospatial /
> remote-sensing harness and domain tools.**
> Pi owns the agent loop, planning, memory, and reasoning (ADR 0122). ExpRS
> owns spatial context, dataset grounding, operator schemas, workflow
> execution, scientific constraints, artifact semantics, map semantics,
> verification, and provenance. Nothing in this document turns ExpRS into an
> agent runtime.

## Pipeline

```
Pi (generic agent loop)
  → ExpRS Spatial Harness (src/agent/harness/)
  → Tool Discovery / Typed Spatial Context
  → Scientific Preflight
  → Plan / Workflow
  → RSOperator / Data / Model Runtime / MapSpec
  → Structured Artifacts
  → Verification
  → Scientific Answer / Map / Dataset
```

## Layers

| Layer | Owner | Location |
|---|---|---|
| Generic agent loop, subagents | Pi | `pi/` (TS bridge), `pi/roles/` |
| Tool discovery + manifests + taxonomy | Harness | `src/agent/harness/` (`tool_manifest`, `tool_taxonomy`, `harness_tools`) |
| Typed spatial context | Harness | `harness:context` (revision-stamped WorkspaceState) |
| Dataset grounding | Harness | `EntityResolver`, `spatial:understand` |
| Scientific preflight | Harness | `scientific_preflight.{h,cpp}` (`harness:preflight`) |
| Plan model + compiler | Harness | `agent_plan.{h,cpp}` (`harness:plan`) |
| Execution | **Workflow Engine 2.0** | `WorkflowRunCoordinator` → `TaskCenter` (ADR 0123) |
| Algorithms | Operator registry | `src/operators/` (~95 `rs:*` operators) |
| Verification | Harness | `harness_verification.{h,cpp}` (`harness:run_status`) |
| Map confirmation | Harness + cartography | `plan_tools.cpp` `confirmMapOutput` (MapSpec, ADR 0127) |
| Recipes | Harness + metadata | `data/agent/recipes/*.json`, `recipe_catalog.{h,cpp}` |

## Harness invariants

1. **One engine**: plans compile to `WorkflowDefinition` JSON and run through
   `WorkflowRunCoordinator::startTrackedPipelineJson`. There is no second
   scheduler; LLM-driven concurrency does not exist — TaskCenter admission
   (RSS watermark, RAM budget, ADR 0063) owns parallelism.
2. **One catalog**: every callable tool is an `AgentTool` in
   `AgentToolCatalog`; each carries a bounded `harness` manifest block
   (taxonomy, risk class, side effects, resource hints, cancellation,
   preconditions, expected artifacts). The manifest is derived from
   `AgentMetadata` / a namespace risk table — never hand-maintained twice.
3. **Anti-hallucination by construction**: `unknown → inspect`,
   `ambiguous → resolve` (candidates listed, never silently picked),
   `missing → typed failure` (`HarnessError`, mission Phase 12 codes).
4. **Deterministic science**: preflight rule packs, verification checks,
   recipes, and evals are deterministic code/JSON. The LLM never adjudicates
   scientific safety.
5. **FAIL ≠ success**: a FAIL verification verdict forces run status
   `failed`. No code path reports success after FAIL.
6. **Token budget is a tested contract**: harness tool responses are bounded
   (registry-enforced cap 512 KiB; context ≤ 256 KiB, manifest pages ≤ 64 KiB,
   error catalog ≤ 8 KiB — asserted by the eval suite).

## Tool namespaces

| Namespace | Taxonomy domain | Examples | Execution |
|---|---|---|---|
| `harness:` | harness.* | `preflight`, `plan`, `execute_plan`, `run_status`, `context`, `tool_manifest`, `error_codes`, `search_recipes`, `instantiate_recipe` | inline |
| `spatial:` | data/raster.* | `understand`, `raster_inspect`, `sample_pixels`, `search_capabilities` | inline |
| `workflow:` | workflow.preflight | `preflight` | inline |
| `rs:` / `gdal:` / … | processing.run | operators | TaskCenter (async) |
| `cartography:` / `layout:` / `symbology:` | map.* | compose/preflight/repair/export | inline |
| `project:` / `asset:` / `lineage:` / `result:` / `run:` | project/result/provenance.* | governance surfaces | inline |

## Plan lifecycle

```
Understand → Resolve entities → Preflight → Executable plan → Resource
estimate → Execute → Verify → Present
```

`harness:preflight` blocks scientifically unfit inputs before execution;
`harness:plan` validates and compiles; `harness:execute_plan` submits to the
engine; `harness:run_status` observes the real run state and verifies outputs
(automatic transient-resume is bounded to one attempt — Phase 13).

## History

- ADR 0122 — Pi-Based Spatial Intelligence Layer (3.0 baseline)
- ADR 0127/0128 — MapSpec + Spatial Scientist contracts (3.0)
- Harness 4.0 (this work) — error taxonomy, tool manifests, typed context,
  grounding, scientific preflight, plan lifecycle, verification, recipes,
  evals. Recorded in `CHANGELOG.md` and `.planning/pi-spatial-scientist-harness-4/`.

## Known limits

- The harness does not make examples into "fully autonomous correctness":
  scientific validity is enforced only for the shipped rule packs; unknown
  intents degrade to shared structural rules.
- Provenance sidecars for workflow-run outputs are written by the workflow
  runtime; run-level verification warns (not fails) when a sidecar is absent
  until the OutputCommitter registration lands on the MCP workflow path.
- LLM-submitted workflow step outputs are registered as governed assets only
  when the host wires the OutputCommitter seam; the CLI pipeline runner does
  this today, the MCP path is follow-up work (tracked as P1-E1).
