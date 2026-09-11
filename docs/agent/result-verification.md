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

---

## Harness 7.0: Derived Expectations & New Checks (2026-09)

`harness:run_status` / `harness:execute_plan` no longer verify outputs with
vacuous defaults. Expectations are layered, most specific wins:

1. Structural defaults (4.0 semantics; provenance stays warning-class unless
   tightened below).
2. Capability-knowledge contract for the plan intent: when a serving entry's
   `verification.checks` declares `finite_fraction` / `nodata_fraction`, the
   thresholds tighten to 0.5 / 0.9; `provenance` and `uncertainty` switch the
   corresponding sidecar checks on.
3. The plan's `verification.expectations` block — `crs`, `width`, `height`,
   `class_values`, `max_nodata_fraction`, `min_finite_fraction`,
   `expected_extent`, `require_provenance`, `require_uncertainty`.

New checks: `extent_covers_aoi` (output geotransform must cover the declared
AOI rectangle; error) and `uncertainty_present` (`<output>.uncertainty.json`
sidecar per the recipe uncertainty-path convention; warning).

The derived expectations are echoed in `verification.expectations` of the run
status document. FAIL propagation is unchanged: any FAIL verdict forces
`status: "failed"`.

---

## Harness 8.0: Evidence Sidecars & Honest Severity (2026-09)

The 7.0 known gap — "no codepath writes these sidecars yet" (adversarial
review F1) — is closed. Every terminal, completed run now persists evidence
next to its artifacts via `src/agent/harness/evidence.{h,cpp}`:

| Sidecar | Writer | Content |
|---|---|---|
| `<out>.provenance.json` | engine (temp-output path, #698) or harness run-identity writer when absent | plan/run identity, intent, plan fingerprint, cleanup policy — the harness NEVER overwrites an engine sidecar |
| `<out>.uncertainty.json` | harness, **only from operator-declared facts** | step-attributed facts from closed result keys (`uncertainty`, `uncertaintyOutput`, `uncertainty_band`, `confidence`); a method that produces no uncertainty yields no file — absence is the honest answer |
| `<out>.verification.json` | harness, after verification | verdict, checks, expectations, quality summary (failed/warning counts), run identity, uncertainty pointer |

All writes are atomic (QSaveFile, same convention as the engine's writer). A
declared-but-unwritable uncertainty sidecar is an **error-class** check
(`uncertainty_written`) — declared evidence may never silently disappear. The
sidecar presence checks for methods that declare nothing remain
warning-class. The run document reports an `evidence[]` array with the
sidecar paths.

New checks (Area G): `band_count_matches` (declared `expected_band_count`)
and the post-verification `appendCheck` semantics (verdict recomputed from
ALL checks — FAIL-never-success preserved, asserted by the Tier-B suite).
