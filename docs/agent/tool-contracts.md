# Tool Contracts (Harness 4.0)

Every agent-callable tool carries a **manifest** — a bounded, machine-readable
block answering what the tool touches, what it costs, and what it produces,
without prose. The manifest is derived (from `AgentMetadata` for operators,
from a namespace risk table for inline tools); there is no second registry.

## Wire shape

`tools/list` (with schemas), `get_tool_schema`, `list_tools`, and
`harness:tool_manifest` expose:

```json
"harness": {
  "taxonomy": "raster.inspect",
  "risk_class": "read_only",
  "side_effects": false,
  "idempotent": true,
  "cancellable": false,
  "memory_policy": "streaming",
  "determinism_grade": "bit_exact",
  "cost_class": "O(tile)",
  "large_raster_safe": true,
  "gpu": false,
  "produces_provenance": true,
  "expected_artifacts": [
    { "name": "output", "kind": "raster", "persistence": "committed_asset" }
  ],
  "preconditions": [ { "check": "input_exists", "description": "all input paths resolve" } ]
}
```

All fields are optional except `taxonomy` and `risk_class`.

## Taxonomy (closed vocabulary)

`domain.action`, classified from the tool id (never from descriptions):

- `data.inspect | data.search | data.register | data.convert | data.sample | data.mutate`
- `raster.inspect | raster.sample | raster.statistics`
- `processing.describe | processing.run | processing.search | processing.preflight`
- `workflow.plan | workflow.run | workflow.resume | workflow.preflight`
- `model.inspect | model.run | model.select`
- `map.compose | map.preflight | map.render | map.repair | map.mutate | map.inspect`
- `project.inspect | project.search | result.inspect | result.verify`
- `provenance.inspect | context.read | context.mutate`
- `harness.preflight | harness.plan | harness.execute | harness.verify | harness.recipe | harness.catalog`
- `other.other` (unknown ids — the classification is total, never fails)

## Risk classes (Phase 14)

Ordered by blast radius; mutated tools are the exception list, everything not
listed defaults to `read_only`:

| Class | Meaning | Examples |
|---|---|---|
| `read_only` | no state change | `spatial:raster_inspect`, `project:search` |
| `modifies_display` | ephemeral viewport state | `view:set_extent`, `canvas:draw_roi` |
| `creates_artifact` | produces outputs | all `rs:`/operator execution, `layout:export` |
| `modifies_project` | project-state mutation (undoable where the command stack applies) | `cartography:compose`, `symbology:apply_*`, `workspace:undo`, `asset:relink` |
| `destructive` | removes state | `temporal:remove_collection`, `layout:remove_item`, `cartography:chart_delete` |
| `external_process` | shells out | `gdal:*`, `otb:*`, `qgis:*` providers |
| `network` | remote access | reserved (remote reads require `SICNU_MCP_ALLOW_REMOTE=1`) |

Safety reuses the existing mechanisms — MCP allow-list, workspace path
containment, custom-tool trust gate — there is no second policy system.
Normal non-destructive scientific work stays fully automatic: an agent never
needs approval for `read_only` / `creates_artifact` tools.

## Error contract (Phase 12)

Every harness failure surfaces as a typed `HarnessError`:

```json
{
  "code": "CRS_MISMATCH",
  "summary": "CRS mismatch between 'before' and 'after'",
  "details": {},
  "recoverable": true,
  "suggested_actions": [ { "action": "reproject_to_reference", "arguments": {} } ],
  "category": "validation",
  "retry_class": "none"
}
```

Stable codes (closed table in `src/agent/harness/harness_error.h`):
`DATASET_NOT_FOUND`, `BAND_ROLE_UNRESOLVED`, `CRS_MISMATCH`, `GRID_MISMATCH`,
`INVALID_RADIOMETRY`, `INSUFFICIENT_MEMORY`, `MODEL_INCOMPATIBLE`,
`MODEL_NOT_READY`, `EXECUTION_FAILED`, `CANCELLED`, `OUTPUT_INVALID`,
`MAP_PREFLIGHT_FAILED`, `PREFLIGHT_BLOCKED`, `ENTITY_AMBIGUOUS`,
`INVALID_PLAN`, `INVALID_PARAMETER`, `TRANSIENT_FAILURE`, `IO_ERROR`,
`PATH_OUTSIDE_WORKSPACE`, `WORKFLOW_NOT_FOUND`, `TOOL_NOT_FOUND`,
`TIME_ORDER_INVALID`, `MODALITY_MISMATCH`, `POLARIZATION_MISMATCH`,
`CALIBRATION_MISMATCH`, `TRAINING_INVALID`, `NOT_SUPPORTED`,
`IDENTITY_MISMATCH`.

Retry classes: `none` (never), `manual` (explicit decision only), `transient`
(bounded auto-retry in the plan runner). Pi reads codes; it never parses logs.

---

## Harness 7.0: New Tools & Catalog Semantics (2026-09)

| Tool | Purpose |
|---|---|
| `harness:resolve_intent` | Deterministic goal classification → closed intent → feasibility-ranked capability candidates. Ambiguity returns `{status:"ambiguous", candidates[], ambiguity{code:INTENT_AMBIGUOUS}}`; no evidence returns `{status:"unresolved"}`. Optional `understanding` accepts a DatasetUnderstanding document or a dataset reference. |
| `harness:repair_plan` | Bounded (≤3 passes) deterministic plan surgery: duplicate step ids renamed (first wins), outputs referencing unknown steps dropped. Science (operator choice, wiring, verification strength) stays advisory with suggested actions. Returns repaired plan, per-pass log, remaining issues, and compiled workflow when clean. |
| `harness:decision_record` | Typed decision ledger: `record` (kind ambiguity/alternative/parameter) → `decision-N`, `resolve` (chosen), `list` (unresolved first). Surfaces in `harness:context.decisions`. |

`spatial:understand` now answers from an understanding cache keyed by
(path, asset revision) — `cached:true` when it does; `stats:true` bypasses.
`harness:context` additionally carries `plan_bindings[]` (run → plan with
verification status) and `decisions[]`. `workspace_state` gains an
`experiments[]` provider seam (`setExperimentsProvider`) mirroring workflow
runs.

Transient resume is now ledger-bounded: `repair_attempts` /
`repair_attempt_limit` (3) travel in every run status document, so poll loops
cannot retry a failed run forever.

### Recipe catalog (Area G)

- `presets` are now executable: `bindings.preset` applies flat param
  overrides, `step_params` (per-step overrides), and `keep_outputs` filters.
- `aliases` keep deleted near-clone recipe ids resolving (29 files collapsed;
  102 → 73).
- Every recipe now declares `capabilities` (its operator chain) and
  `expected_artifacts` (mirroring `outputs`).

### Output budgets (Area I)

Every `SpatialToolRegistry` registration is metered: outputs above 512 KiB
are compacted schema-aware (largest array member trimmed first) with explicit
`truncated` / `truncated_field` markers; the 512 KiB `kMaxToolOutputBytes`
registry cap is now enforced at runtime, not only in tests.

---

## Harness 8.0: Evidence-Driven Surfaces (2026-09)

### New tool: `harness:explain {run_id, plan?}`

One bounded document answering the spatial-scientist questions from
authoritative stores only (no chat memory): **data used** (inputs with
resolved identity/revision + cached typed facts), **method applicability**
(serving capabilities for the intent), **what executed** (per-step status and
cache hits), **verification** (verdicts + checks), **evidence** (sidecar
paths), **assumptions** (failed warning-class checks), and **unknowns**
(stale asset contexts, unresolved decisions).

### Plan additions (AgentPlan v2.0, additive)

| Field | Meaning |
|---|---|
| `pins.datasets.<slot>` | `{asset_entity_id | asset_id | path, revision?}` — validated against the RESOLVED dataset at execute time; a mismatch blocks with the new stable code **`IDENTITY_MISMATCH`** so silently swapped inputs can never feed a run |
| `pins.model` | `"<id>" | "<id>@<version>"` model identity pin |
| `cleanup` | `keep_all` (default) | `keep_outputs` — declared intermediate-artifact policy; forwarded in compiled workflow `metadata` (engine consumption is an execution-plane follow-up) |
| `steps[].role` | `preparation | analysis | postprocess | verification` — explainability only, closed vocabulary |

`planFingerprint(plan)` — SHA-256 (16 hex) over the canonical serialization of
the plan's scientific content (intent, inputs, steps, outputs, verification).
Identical science → identical fingerprint regardless of plan id or
timestamps; recorded in plan bindings, `harness:execute_plan` responses,
compiled workflow `metadata`, and every evidence sidecar.

### Capability knowledge: platform tool surface

Entries gain an optional `"surface"`: `"operator"` (default,
`RSOperatorRegistry`), `"spatial_tool"` (`SpatialToolRegistry`),
`"data_platform_tool"` (`dataPlatformToolDefs()` — `dataset:*`,
`experiment:*`, `reproducibility:*`). Tool-surface entries never declare
scientific intents. Knowledge now covers **every registered operator**
(132, full-registry coverage floor) plus the platform tool families
(`cartography:*`, `workflow:*`, `style:*`, `template:*`, `solution:*`,
model selection, dataset/experiment/reproducibility surfaces). Viewport/UI
tools (`view:`, `canvas:`, `layer:`, `roi:`, `data:`, `raster:`) are out of
scope by contract — they are not scientific capabilities. Plugin-provided
operators register in-process and are covered by the same floor; out-of
-process plugin tools are a recorded follow-up.

### `harness:resolve_intent` 2.0

Resolved intents now also report `missing_facts` (capability demands vs the
understanding slots actually present), `preparations` (static, typed
code→action table; codes with no safe preparation are marked
`no_safe_preparation`, never guessed), and `solution_paths` (recipes serving
the intent, deterministic order, bounded 8).

### Meter fix

The registry meter's trim path now reports `original_bytes` (the 7.0 pin
asserted it; the trim path never set it — the pin was red on master).
