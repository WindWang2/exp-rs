# Slices — RS14-13-fault-injection-labs

Each slice: RED test → confirm failure is "missing capability" → minimal impl → refactor → narrow ctest → commit. Test-case names are prefixed `"fault lab: ..."` so `ctest -R 'fault lab'` selects the suite.

## Slice A — Core types, schema gate, sandbox rules, determinism
- Files: `fault_types.h`, `deterministic.h/.cpp`, `fault_scenario.h/.cpp` (schema gate only), `fault_sandbox.h/.cpp`, CMake for `src/faultlab`, `tests/test_faultlab.cpp` (first block), `data/faultlab/faults.schema.json`.
- RED: (1) `loadFaultScenario` accepts a valid `sicnu.lab.faults/1` doc and rejects `sicnu.lab.faults/2` / missing version with typed `faultlab.schema_version`; (2) unknown family id → typed `faultlab.fault_unknown_family`; (3) `Sandbox` creates a unique temp dir, `copyOf(grid)` produces an independent deep copy, `cleanup()` removes the dir and `verifyNoResidue()` returns true; source digest before/after equal; (4) splitmix64 stream: same seed → same sequence; different seeds differ; (5) canonical JSON of a `FaultGrid` is byte-stable across runs (key order, float format) and digest equals sha256 of that text.
- Commit: `feat(faultlab): slice A — scenario schema gate, sandbox contract, deterministic core types`.

## Slice B — Metadata/state faults
- Files: `fault_registry.h/.cpp` (family catalog), `fault_transforms.h/.cpp` (4 families), `fault_observables.h/.cpp`, `fault_expectations.h/.cpp`.
- RED per family (happy path): applying `band_role_swap{roleA,roleB}` swaps exactly the two roles and moves `band_roles` + `index_mean` (sign flip); `omit_quality_mask{role}` decrements `band_count` and removes the role from `band_roles`; `wrong_scale_offset{band,gain,offset}` scales finite samples only (NaN preserved) and moves `band.mean/min/max`; `nodata_as_data` replaces NaN with finite values and moves `nodata_fraction`/`finite_fraction`/`band.mean`.
- RED (invalid/unsafe): unknown params (e.g. missing role) → typed `faultlab.fault_unsupported_params`; role not present → `faultlab.fault_unsafe_target`; NaN-only band scale → boundary case (no crash, typed outcome).
- RED (expectations): relation semantics — `changed`, `delta_ge`, `delta_le`, `in_range`, `equals`, `not_equals`, `truth_is` — each with pass + fail evidence shape.
- Commit: `feat(faultlab): slice B — metadata/state fault families + observable/expectation engine`.

## Slice C — Geometry/temporal faults
- RED: `grid_shift{dx,dy}` moves `geo_transform.origin_x/y` by exactly dx*pixelX/dy*pixelY, pixel size unchanged; `crs_mismatch{crs}` changes `crs` observable, grid otherwise unchanged; `temporal_shuffle` permutes `acquisition_dates` deterministically from seed (same seed → same permutation; permutation ≠ identity for the fixture's 4 epochs); `temporal_gap{position}` removes one epoch band + its date, `band_count` −1.
- RED (invalid): unknown CRS string → typed unsupported (closed vocabulary per scenario params: EPSG:4326/3857 only, else typed error); gap position out of range → typed `faultlab.fault_unsupported_params`; shuffle on a grid without dates → typed `faultlab.fault_unsafe_target`.
- Commit: `feat(faultlab): slice C — geometry/temporal fault families`.

## Slice D — ML/evaluation faults
- RED: `train_test_spatial_leakage{mode}` duplicates train sample points into the test region; `leakage.overlap_fraction` becomes > 0 (was 0), `leakage.test_count` grows; `threshold_misuse{threshold}` binarizes the score layer with the wrong cut: `positive_fraction` and `kappa` move (kappa→~0 for a discriminating fixture at a destructive threshold); `model_channel_mismatch{permutation}` permutes channel order feeding the closed-form linear model: `channel_order` text changes and `model_output_mean` moves by the declared delta.
- RED (invalid): threshold outside [0,1] → typed; permutation not a bijection over channels → typed `faultlab.fault_unsupported_params`; fixture without model weights → `faultlab.fault_unsafe_target`.
- Commit: `feat(faultlab): slice D — ML/evaluation fault families`.

## Slice E — Artifact/provenance faults + report schema
- RED: `provenance_removal` clears the provenance block (generator/seed/product/schema); `provenance.generator_present` true→false; re-adding is a separate explicit scenario (no implicit repair — evidence rule); report serialization: `faultReportToJson` emits `sicnu.faultlab.report/1` with stable key order, no timestamps/absolute paths, digest = sha256(canonical body) and identical across two runs.
- Commit: `feat(faultlab): slice E — provenance fault family + canonical report schema`.

## Slice F — Scenario runner + deterministic replay
- Files: `fault_fixtures.h/.cpp` (7 deterministic fixtures), `fault_runner.h/.cpp`.
- RED: `runFaultScenario` on a valid scenario: report verdict pass, per-expectation evidence present, sandbox removed, source digest unchanged, replay digests equal; second run with same seed → byte-identical report digest.
- RED (failure modes): faulted scenario whose expectation is impossible (declared delta beyond achievable) → verdict fail with evidence, not exception; sandbox cleanup failure injection (pre-created file with deny-remove where possible) → typed `faultlab.cleanup_failed`; source mutation attempt (scenario params targeting the source object) → typed `faultlab.source_mutated`; byte budget exceeded fixture → typed `faultlab.budget_exceeded`.
- Commit: `feat(faultlab): slice F — scenario runner with deterministic replay and cleanup verification`.

## Slice G — Self-test potency + exemplars + integration
- Files: `data/faultlab/scenarios/*.json` (13), `tests/test_faultlab_scenarios.cpp`, `tests/test_faultlab_gdal.cpp`, `tests/test_faultlab_verifier.cpp`, `src/faultlab/gdal/*`, docs.
- RED (potency): for every registered family, its designated fixture + default params must move ≥1 family-declared observable beyond tolerance — a family that moves nothing fails the suite (vacuous-fault guard, #1179-class).
- RED (coverage): all 12+ families have ≥1 exemplar scenario; every scenario has an LO id in `^LO-[0-9]{2}$`; every scenario declares ≥1 observable expectation and a diagnosis expectation.
- RED (immutability at file level, GDAL): materialize base GeoTIFF → sha256 → byte-copy into sandbox → apply fault to copy → source sha256 unchanged; faulted copy differs.
- RED (verifier integration): `OutputVerifier::gradeArtifact` on the faulted artifact with scenario-declared rules fails exactly the `must_fail_assertions`; `diagnoseLabObservation` on the measured observation returns the scenario-declared signature (or `diagnostic.unmatched`).
- Commit: `feat(faultlab): slice G — 13 exemplar scenarios, potency self-tests, verifier/diagnosis integration`.

## Post-slices
- Deep review rounds 1+2 (adversarial subagent), fix P0/P1/P2, targeted regression, rebase onto latest master with semantic union, PR.
