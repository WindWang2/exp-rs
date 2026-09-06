# Scientific Preflight (Harness 4.0)

`harness:preflight` is the deterministic gate between intent and execution
(mission Phase 5). It reads **resolved facts** — DatasetUnderstanding
documents produced by `spatial:understand` / `spatial:raster_inspect` — never
prose, and produces a `PreflightResult` contract document:

```
verdict: "ok" | "fixable" | "blocked"
issues:  [{code, severity, message, repairable, suggested_action}]
checks:  [{check, passed, severity, code, details}]
```

**A `blocked` verdict is a veto.** `harness:execute_plan` refuses to submit a
blocked plan; the LLM cannot override it and must not present the plan as
runnable. Every blocker carries a typed error code from the stable taxonomy
and a machine-actionable suggested action.

## Rule packs

| Intent | Inputs | Deterministic checks |
|---|---|---|
| `ndvi` | `primary` (optical raster) | NIR band resolvable (role or 700–1100 nm), Red band resolvable (role or 600–700 nm) → `BAND_ROLE_UNRESOLVED` when missing; radiometric state declared, warn on raw DN → `INVALID_RADIOMETRY`; NoData declaration check |
| `change` | `before`, `after` | same CRS → `CRS_MISMATCH`; same size → `GRID_MISMATCH`; differing pixel size → `GRID_MISMATCH` warning; differing radiometric states → `INVALID_RADIOMETRY`; two epochs required |
| `sar_change` | `reference`, `observation` | both SAR → `MODALITY_MISMATCH` (warn when unverifiable); polarization match → `POLARIZATION_MISMATCH`; calibration domain match (σ⁰/γ⁰/DN) → `CALIBRATION_MISMATCH`; CRS/grid checks; two epochs required |
| `classify` | `primary`, `training` | training slot bound → `TRAINING_INVALID`; training vector non-empty; raster class-map training warns; model-input compatibility is checked by `rs:infer` preflight against the ModelCatalog |
| `phenology` | `collection` | NIR/Red roles present → `BAND_ROLE_UNRESOLVED` warning; acquisition times declared → `TIME_ORDER_INVALID` warning |

Unknown intents run only the **shared rules**: every named input resolves
(`DATASET_NOT_FOUND`), at least one input present (`INVALID_PARAMETER`).
Facts that cannot be verified produce **warnings, not silent passes** — e.g. a
SAR scene without declared polarization is flagged, never assumed.

## Wavelength fallback (no guessing)

A band without an explicit `SICNU_BAND_ROLE` is *not* guessed from position.
A declared wavelength inside the documented physical window is a fact, not a
guess, and resolves the NIR/Red checks. Nothing else falls back.

## Use

```
harness:preflight {
  "intent": "change",
  "inputs": [
    { "name": "before", "ref": "asset-3" },
    { "name": "after",  "ref": "asset-4" }
  ]
}
→ { "preflight": { "verdict": "blocked",
                   "issues": [ { "code": "CRS_MISMATCH", "severity": "error",
                                 "suggested_action": { "action": "reproject_to_reference" } } ] } }
```

`harness:execute_plan` runs the same rule pack automatically when the plan
declares an `intent` — planners cannot skip it by forgetting to call
preflight first (the explicit `skip_preflight` flag exists only for custom
plans with no intent).
