# Result Verification (Harness 4.0)

Every important processing output is verified automatically after the run
(mission Phase 9). Verification lives in `harness_verification.{h,cpp}` and is
surfaced through `harness:run_status` — the same document `harness:execute_plan`
tells the agent to poll.

## Verdicts (closed tri-state)

| Verdict | Meaning |
|---|---|
| `PASS` | all checks passed |
| `PASS_WITH_WARNINGS` | no error, at least one warning-class check failed |
| `FAIL` | at least one error-class check failed |

**A FAIL verdict forces the run result `status` to `"failed"`.** There is no
code path that reports success after a FAIL — this is asserted by the eval
suite (`tests/test_harness_evals.cpp`, "FAIL verification cannot be reported
as success").

## Checks

Per artifact (raster or vector, kind inferred from extension unless pinned):

- `artifact_exists` — the declared output exists on disk
- `opens` — GDAL/OGR can open it
- `dimensions` — raster size and band count positive (and equal to the pinned
  width/height when the plan declares expectations) → `GRID_MISMATCH`
- `crs_present` / `crs_matches` — CRS non-empty; equal to the expected CRS
  when pinned → `CRS_MISMATCH`
- `finite_fraction` — fraction of finite samples on a bounded (≤64×64) band-1
  probe ≥ the declared minimum → `OUTPUT_INVALID`
- `nodata_fraction` — NoData fraction ≤ the declared maximum
- `class_values` — when a closed class domain is declared, all sampled values
  must be members → `OUTPUT_INVALID`
- `non_empty` — vector feature count > 0
- `provenance_present` — derivation/provenance sidecar exists (warning-class
  on the workflow-run path; error-class on the copilot path)

## Run result document

`harness:run_status {run_id, plan?}` returns the real engine state — the
harness never maintains fictional progress:

```json
{
  "run_id": "run-...",
  "state": "Completed",          // the Workflow Engine 2.0 state
  "progress": 1.0,
  "steps": [ { "step_id": "ndvi", "status": "Completed", "cache_hit": false,
               "output": "/ws/ndvi.tif", "execution_id": "task-42" } ],
  "verification": {
    "verdict": "PASS",
    "artifacts": [ { "path": "/ws/ndvi.tif", "verdict": "PASS", "checks": [ ... ] } ]
  },
  "status": "completed"          // "failed" whenever verification is FAIL
}
```

Map-producing plans additionally get a `map_confirmation` block (Phase 10):
the declared layout must exist, the map-producing step must have completed,
and — when the plan declares a MapSpec — the compose → preflight → repair
loop (bounded to 3 repair passes) must pass with no errors
(`MAP_PREFLIGHT_FAILED` otherwise). Export happens only on a non-FAIL
confirmation.

## Retry (Phase 13)

On a failed run whose recorded error is transient-class (worker startup,
transient I/O), `harness:run_status` may resume **once** per call via the
engine's own `resumeRun` — completed steps with valid outputs are never
re-executed. Non-idempotent and destructive operations are never auto-retried;
the LLM cannot raise the bound.
