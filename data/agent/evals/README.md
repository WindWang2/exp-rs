# Agent Evaluation Corpus (Harness 8.0, Area I)

Versioned, data-driven evaluation cases for the Pi Spatial Scientist harness.
The corpus is **data**: every case is a JSON document under `cases/`, executed
deterministically by the corpus runner (`tests/test_harness_eval_corpus.cpp`)
against the live harness tool surface. No external model, no paid API, no
timing dependencies.

This complements the C++-embedded engine-level scenarios in
`tests/test_harness_evals.cpp`: those exercise full workflow-engine
execution (Tier B); this corpus grades the *tool contract surface* (Tier A):
grounding → intent → preflight → planning → repair → context/explain, with
typed failures for every invalid path.

## Case file schema

A file under `cases/` holds `{"cases": [...]}`. One case:

```json
{
  "case_id": "invalid-science/ndvi/no-nir",
  "category": "invalid_science",
  "description": "NDVI preflight on a scene without NIR is blocked",
  "fixture": {
    "kind": "raster",
    "width": 8, "height": 8,
    "bands": ["red", "swir"],
    "nodata": -9999.0,
    "pixel_size": 30.0,
    "metadata": {"SICNU_RADIOMETRIC_STATE": "surface_reflectance"}
  },
  "steps": [
    {
      "tool": "harness:preflight",
      "input": {"intent": "ndvi",
                 "inputs": [{"name": "primary", "ref": "$fixture"}]},
      "expect": {
        "success": true,
        "asserts": [
          {"path": "preflight.verdict", "equals": "blocked"}
        ]
      }
    }
  ]
}
```

### Field reference

| Field | Meaning |
|---|---|
| `case_id` | unique, stable id (drift-tested for duplicates) |
| `category` | closed vocabulary, see below |
| `fixture` | optional synthetic raster generated at runtime (≤ 32×32, deterministic pixel formula — never a checked-in binary) |
| `steps` | ordered tool calls; each `input`/`asserts` path may reference `$fixture` (generated raster path) and `$tmp` (per-case temp dir) |
| `foreach` | optional `{"var": "INDEX", "values": [...]}` — the case is expanded deterministically per value; `$INDEX` substitutes into `case_id`, `description`, and step inputs |
| `expect.success` | the tool must succeed/fail exactly so (defaults to `true`) |
| `expect.error_code` | required on failure: the typed `errorCode` (never prose matching) |
| `expect.asserts` | dotted JSON paths with `equals` / `exists` / `contains` |
| `expect.response_bytes_le` | whole-response size bound (token budget grading) |

### Closed categories

`normal_workflow`, `missing_data`, `ambiguity`, `invalid_science`,
`impossible_task`, `multimodal`, `context_continuation`,
`anti_hallucination`, `map_confirmation`, `budget`,
`invalid_input`, `modality_mismatch`, `recovery`, `long_plan`,
`cartography`, `prompt_injection`, `typed_contract`.

(Harness 9.0 added the last seven categories; the runner's array in
`tests/test_harness_eval_corpus.cpp` remains the enforced closed list.)

Transient-failure scenarios need the real engine's bounded resume loop and
stay in the engine suites; reproduction/dataset-store interactions live with
the dataset/experiment platform's own suites (`dataset:*`/`experiment:*` are
MCP-surface tools, not SpatialTools, so they are out of this corpus's
executor scope by contract).

## Determinism & bounds

- Fixtures are runtime-generated GeoTIFFs with fixed pixel formulas.
- The runner asserts structure and typed codes — never prose.
- Corpus bound: ≤ 400 expanded cases (runner refuses to grow unbounded).
- Every expanded case gets a fresh temp dir; nothing leaks between cases.

## Adding cases

Add a JSON object to the matching category file (or a new file — the runner
scans the directory). The drift-tested contract: every tool id must exist on
the live registry, every `category` must be in the closed vocabulary, every
case must declare at least one step, and ids must be unique. A case that
fails is a regression — fix code or fix the case, never delete it silently.
