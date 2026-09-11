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

---

## Harness 7.0: Specification Table & New Rule Packs (2026-09)

The intent → pack dispatch is a single specification table (`IntentSpecTable`
in `scientific_preflight.cpp`), not an if/else chain. The same table powers
`intentRequirements(intent)` — the machine-readable mirror whose band-role and
min-scene demands `test_capability_drift` pins against the capability
knowledge layer (`data/agent/capabilities/`).

Packs by intent (7.0 state):

| Intent(s) | Pack | Notes |
|---|---|---|
| ndvi / evi / savi / ndre | band_ratio | EVI demands Blue, NDRE demands Red edge (spec-correct since 7.0) |
| ndwi / water / flood | band_ratio + flood | Green+NIR; flood adds physics caveats |
| mndwi / ndsi | band_ratio | Green+SWIR |
| nbr / dnbr / ndbi / bsi | band_ratio | dNBR requires a pair |
| change | optical_change | pair CRS/grid/resolution/radiometry |
| sar_change / sar_flood | sar_change | polarization + calibration + grid; sar_flood adds flood caveats |
| sar / sar_water | sar_single | modality + calibration/polarization warnings |
| classify | classify + land_cover | legends, sample sufficiency; `supervised:false` refs entry exempts training demand |
| accuracy | classify + accuracy + pair | reference vs class-map domain intersection |
| phenology / temporal | temporal_series | facts-driven blockers (scene floor 12 / 3, ordering), warnings without facts |
| terrain | terrain | projected-CRS demand |
| inference | inference | model manifest vs dataset: band roles, modality, temporal length, radiometry, resolution window; unknown model → MODEL_NOT_READY |
| qa / preprocess / ship | shared | resolvability only |

Mixed-modality inputs (optical + SAR on one intent) additionally run the
**multimodal pack**: cross-CRS is a blocker, registration/resolution
mismatches and SAR calibration gaps are declared warnings.

Refs entries accept Harness 7.0 extras: `"model"` (model catalog id),
`"supervised"` (bool), `"temporal_facts"` ({scene_count, dates[], max_gap_days?}).

---

## Harness 8.0: Assumptions & SAR-Only Optical Intent Refusal (2026-09)

- Preflight documents now carry an **`assumptions`** array: the warning-class
  issues. Blockers, checks, and honest unknowns are separated so the agent
  sees exactly which science rests on unverified facts.
- Optical index intents on **SAR-only inputs** are now a `MODALITY_MISMATCH`
  blocker. The 7.0 fusion behavior is unchanged (a SAR companion beside an
  optical input is skipped by the spectral checks with a warning): what is
  refused is the silent `ok` for an optical index with no optical input at
  all — that plan could only fail at operator time.
