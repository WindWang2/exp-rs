# Plan — RS14-13-fault-injection-labs (Scientific Fault Injection Lab Framework)

## 1. Problem statement

The repo has a mature teaching-lab *grading* platform (LabSpec, `OutputVerifier` with 16 assertion kinds, six hard-coded diagnostic signatures, wrong-answer corpus of pre-baked wrong GeoTIFFs). What it lacks is a **declarative, replayable fault-injection layer**: a way to say "take this clean base fixture, copy it into a sandbox, apply fault family F with params P and seed S, then assert that observables {Oᵢ} moved by ≥ δⱼ, that the diagnosis engine reports signature D, and that the source fixture is bit-unchanged afterwards" — offline, deterministically, with a learning objective attached. Today every wrong variant is a hand-authored TIF; adding a fault means touching the kernel kind enum, the schema, the corpus, and the diagnostics table by hand. This track builds the missing capability layer as a standalone, Qt-free module with a versioned scenario schema, a fault-family registry with metadata, an in-memory + GDAL fixture store, a sandboxed runner with deterministic replay and cleanup verification, and 12+ exemplar scenarios. It fixes no production bug and reuses every existing oracle rather than re-implementing one.

## 2. User stories

**Undergraduate experiment (teaching mode).** A student runs `ndvi_basics`, gets an all-negative index, and the existing copilot already says "band roles unresolved, verify band order" — but the student never *sees* the failure being constructed. With this framework the course ships scenarios like "fs_band_role_swap_ndvi": the student (or the instructor in demo mode) watches a known-clean fixture become scientifically wrong through a *named, explained* transform, observes which measurable facts move (index mean flips sign, band_roles sequence changes), and is then asked to diagnose it before the reveal. Every scenario carries a learning objective and an expected diagnosis, so the pedagogical loop is: construct fault → measure → diagnose → verify → cleanup.

**AI agent (agent mode).** A future agent gets `faultFamilyCatalog()` (machine-readable family metadata: domain, seed policy, sandbox class, expected observable ids) and `runFaultScenario()` returning `sicnu.faultlab.report/1` canonical JSON (deterministic digest, typed diagnostics, per-expectation evidence). The agent can enumerate scenarios, run them offline, and consume verdicts without a GUI — and the same JSON is what the teaching mode renders. Both modes share one runner; no mode-specific logic forks.

## 3. Architecture

New Qt-free leaf module `src/faultlab/` (C++20 + jsoncpp only), plus a GDAL adapter sub-lib. No production code is modified; existing oracles are consumed through their existing headers.

```
src/faultlab/
  fault_types.h          value objects: BandSpec, FaultGrid, Observable, FaultSpec, typed Result/Diagnostic
  deterministic.h        seeded PRNG (splitmix64) + seedFor(purpose) — mirrors src/dataset/deterministic_random.h discipline
  fault_registry.h/.cpp  fault-family catalog + metadata (10 families) + typed lookup errors
  fault_transforms.h/.cpp pure deterministic transforms family -> (FaultGrid&, params) -> typed outcome
  fault_observables.h/.cpp measure observable set from a FaultGrid (role resolution, stats, grid, dates, samples, provenance)
  fault_expectations.h/.cpp expectation relations (equals/not_equals/changed/delta_ge/delta_le/in_range/truth_is) + evidence
  fault_scenario.h/.cpp  scenario value object + loader/validator for sicnu.lab.faults/1 (fail-closed version gate)
  fault_fixtures.h/.cpp  deterministic closed-form base fixtures (7 fixtures)
  fault_sandbox.h/.cpp   sandbox policy: copy-before-apply, source digest, cleanup verification, byte budget
  fault_runner.h/.cpp    runFaultScenario: materialize → copy → inject → measure → compare → diagnose-map → cleanup → replay → report
  fault_report.h/.cpp    sicnu.faultlab.report/1 canonical JSON (no wall clock, sha256 of canonical body)
  faultlab.h             umbrella include
  gdal/raster_fixture_store.h/.cpp  IFixtureStore over GeoTIFFs: materialize/load/byte-copy (+ per-band SICNU_BAND_ROLE, NoData)
data/faultlab/
  faults.schema.json     sicnu.lab.faults/1 (JSON-Schema, additionalProperties:false)
  scenarios/*.json       13 exemplar scenarios
docs/faultlab/
  README.md              authoring guide (how to add a family/scenario)
  ARCHITECTURE.md         design + seams + budgets
docs/integration.md      future wiring points (agent tool, ExperimentRunRecorder, verification_ladder lane)
tests/test_faultlab.cpp             core semantics (Catch2 + sicnu_faultlab, no GDAL)
tests/test_faultlab_scenarios.cpp   13 exemplars end-to-end + family/LO coverage (no GDAL)
tests/test_faultlab_gdal.cpp        GeoTIFF copy/sandbox/source-unchanged proof (Sicnu::faultlab_gdal + GDAL)
tests/test_faultlab_verifier.cpp    OutputVerifier + diagnoseLabObservation integration (sicnu_agent)
```

### Data model (`FaultGrid`)

Raster-agnostic value object: `width/height`, `crsId`, `geoTransform[6]`, `noDataValue`, `bands[]` (each: `role` string from the ADR-0065 lowercase vocabulary, `samples` doubles with NaN = no-data, `scale`, `offset`, `acquisitionDate` optional), plus `extras` JSON for domain payloads (train/test sample points with coordinates, model channel order + weights, provenance block). Canonical JSON serialization (stable key order) is the digest basis; no timestamps anywhere.

### Fault families (10 required, all registered)

| family id | domain | default expected diagnosis | primary observables |
|---|---|---|---|
| `band_role_swap` | metadata | `all_negative_index` (role-resolved index mean flips) | `band_roles`, `index_mean{num,den}` |
| `omit_quality_mask` | metadata | `diagnostic.unmatched` (no canonical signature; typed, honest) | `band_count`, `band_roles`, `valid_fraction` |
| `grid_shift` | geometry | `scale_stripes` → `diagnostic.harness.grid_mismatch` | `geo_transform.origin_x/y` |
| `crs_mismatch` | geometry | `crs_mismatch` → `diagnostic.harness.crs_mismatch` | `crs` |
| `wrong_scale_offset` | metadata | `diagnostic.unmatched` | `band.mean/min/max`, `scale`, `offset` |
| `temporal_shuffle` | temporal | `diagnostic.unmatched` | `acquisition_dates`, `band_roles` |
| `temporal_gap` | temporal | `diagnostic.unmatched` | `acquisition_dates`, `band_count` |
| `train_test_spatial_leakage` | ml | `diagnostic.unmatched` | `leakage.overlap_fraction`, `leakage.test_count` |
| `nodata_as_data` | metadata | `diagnostic.unmatched` | `nodata_fraction`, `finite_fraction`, `band.mean` |
| `threshold_misuse` | ml | `kappa_near_zero` (when discrimination destroyed) or scenario-declared | `threshold`, `positive_fraction`, `kappa` |
| `model_channel_mismatch` | ml | `diagnostic.unmatched` | `channel_order`, `model_output_mean` |
| `provenance_removal` | artifact | `diagnostic.unmatched` (+ verifier `provenance` assertion must fire) | `provenance.generator_present` |

(12 rows — the 10 required families plus `grid_shift`/`crs_mismatch` split out of "grid shift / CRS mismatch" and `temporal_shuffle`/`temporal_gap` out of "temporal shuffle/gap", because each variant has a distinct diagnosis/observable profile.)

Default diagnosis `diagnostic.unmatched` is a *typed* expectation value, not a silent fallback: the runner asserts the actual diagnosis equals the declared one, including unmatched.

### Scenario schema `sicnu.lab.faults/1`

```
schema_version (const "sicnu.lab.faults/1"), scenario_id (^fs_[a-z0-9_]+$),
title, title_zh,
fault { family, params (object), seed (uint32) },
base_fixture { fixture_id, params (object), seed (uint32) },
sandbox { class ("temp_copy"), max_bytes (uint, ≤ 64 MiB), require_source_unchanged (true) },
expected { observables [ {id, relation, value?, tolerance?} ] (minItems 1),
           diagnosis { signature }, verifier { rules_ref, must_fail_assertions[] } (optional) },
learning_objective { id (^LO-[0-9]{2}$), statement, statement_zh },
cleanup { required (true), verify_no_residue (true) }
```
Version gate: loader accepts exactly `sicnu.lab.faults/1`; anything else → typed `faultlab.schema_version` (fail-closed, no silent downgrade), mirroring the lab-rules/labspec loader policy.

### Runner pipeline (`runFaultScenario`)

1. Load + validate scenario (typed errors).
2. Materialize base fixture deterministically (closed-form + seeded splitmix64; never wall clock).
3. Digest source fixture (canonical JSON sha256).
4. Copy into sandbox (in-memory copy for core path; byte-copy via GDAL store for raster path).
5. Apply fault transform to the copy only (typed outcome; unsafe/unknown → typed diagnostic, no fallback).
6. Measure observables on faulted copy; also measure on clean copy (delta basis).
7. Check expectations → per-item evidence `{id, relation, expected, observed, delta, passed}`.
8. Diagnosis check: expected signature vs family default table (integration level maps through the six lab signatures).
9. Cleanup: remove sandbox, verify removal; re-digest source, assert unchanged.
10. Replay: run the whole pipeline twice, compare report digests.
11. Emit `sicnu.faultlab.report/1` canonical JSON + sha256 digest of canonical body.

### Sandbox / safety contract

- Faults mutate only sandbox copies. The source fixture object/file is never written; runner re-digests the source after the run and fails typed `faultlab.source_mutated` on any drift.
- Sandbox lives in a temp dir (QTemporaryDir-style; core uses `std::filesystem::temp_directory_path` + unique suffix); cleanup verified (`verify_no_residue`); residue → typed `faultlab.cleanup_failed`.
- Byte budget: fixture samples ≤ 64 MiB by schema (`sandbox.max_bytes`); runner enforces before materialize (`faultlab.budget_exceeded`).
- Never prints/returns absolute temp paths in the report digest-stable body (sanitized), so reports are reproducible across machines.

### Determinism

- No wall clock, no RNG except seeded splitmix64 (`deterministic.h`), no `std::distributions` (portable-uniform precedent from `tools/sample_foundry`).
- All fixtures closed-form; all observable measurements pure functions of the grid.
- Report digest = sha256 over canonical body (sorted keys, fixed float formatting, no timestamps).
- Replay: two consecutive runs must produce byte-identical digests (`faultlab.replay_mismatch` otherwise).

## 4. Public API / data schema (summary)

```cpp
namespace sicnu::faultlab {
// registry
const std::vector<FaultFamilyInfo>& faultFamilyCatalog();
const FaultFamilyInfo* findFaultFamily(const std::string& id);   // typed nullptr -> caller emits faultlab.fault_unknown_family
// transforms
FaultOutcome applyFault(FaultGrid& grid, const FaultSpec& spec); // FaultOutcome {ok, diagnostic}
// observables / expectations
ObservableSet measureObservables(const FaultGrid&, const ObservableRequest&);
std::vector<ExpectationResult> checkExpectations(const ObservableSet& clean, const ObservableSet& faulted, const std::vector<ObservableExpectation>&);
// scenarios
Result<FaultScenario> loadFaultScenario(const Json::Value& doc);   // sicnu.lab.faults/1 gate
Result<FaultScenario> loadFaultScenarioFile(const std::string& path);
// runner
Result<FaultRunReport> runFaultScenario(const FaultScenario&, const FaultRunOptions& = {});
std::string faultReportToJson(const FaultRunReport&);              // sicnu.faultlab.report/1
}
```

JSON schema files: `data/faultlab/faults.schema.json`; report schema documented in `docs/faultlab/ARCHITECTURE.md` (canonical C++ JSON, `sicnu.faultlab.report/1` string id field).

## 5. Migration / compatibility

None required: purely additive. No existing schema changes; no new assertion kinds in `output_verifier`; no new diagnostic codes in `lab_diagnostics`; `tests/CMakeLists.txt` gains one appended block; root `CMakeLists.txt` gains two `add_subdirectory` lines. Loader rejects foreign schema versions fail-closed.

## 6. Observability

- Per-run report: scenario id, fault family, seed, per-expectation evidence, diagnosis expectation vs actual, cleanup facts (sandbox removed, source digest before/after), replay digests, typed diagnostics, overall verdict, report digest.
- Aggregate: `faultFamilyCatalog()` doubles as a coverage index (families × scenarios × LOs) — Slice G asserts full coverage.

## 7. Security / trust boundary

- Fault injector only ever touches sandbox copies; source-mutation check is a hard gate.
- No network, no GUI, no plugin loading; all tests offline (offline gate env already stamped globally).
- Report body contains no absolute paths/timestamps → safe to publish as benchmark evidence.
- Scenario params are validated against a closed per-family param vocabulary; unknown params → typed `faultlab.fault_unsupported_params`, never ignored.

## 8. Performance budget

- Fixtures ≤ 64×64 samples/band; total fixture bytes ≤ 64 MiB enforced by schema.
- Transform/measure cost O(samples); runner bounded passes (clean + faulted + replay = 3 materializations).
- Core tests link no GDAL/QGIS (fast); GDAL and agent-integration tests are separate narrow targets.

## 9. Test strategy

- TDD per slice: red test first (core semantics), minimal impl, refactor, narrow ctest run.
- Required coverage per core behavior: happy path, invalid/unsafe input (unknown family, bad version, unsafe params), boundary (empty grid, all-no-data, byte budget), determinism/replay, cleanup/source-immutability, teaching/agent semantic consistency (same report object drives both modes).
- Potency self-test (Slice G): every registered family must move ≥1 declared observable beyond tolerance when applied to its designated fixture (else the scenario suite is vacuous — issue #1179 class); base fixture digest must be identical before/after; sandbox must be gone.
- Integration: `OutputVerifier` assertions must fire exactly where scenarios declare `must_fail_assertions`; `diagnoseLabObservation` must return the declared signature (or unmatched) for the measured observation.

## 10. Work packages (see slices.md)

A: schema/sandbox core types + digest + deterministic PRNG.
B: metadata/state faults (band_role_swap, omit_quality_mask, wrong_scale_offset, nodata_as_data).
C: geometry/temporal faults (grid_shift, crs_mismatch, temporal_shuffle, temporal_gap).
D: ML/evaluation faults (train_test_spatial_leakage, threshold_misuse, model_channel_mismatch).
E: artifact/provenance faults (provenance_removal) + report schema.
F: scenario runner + deterministic replay + cleanup verification.
G: self-test potency + 13 exemplar scenarios + coverage gates + GDAL/verifier integration tests.

## 11. Rollback / kill-switch

Purely additive module; rollback = delete `src/faultlab/`, `data/faultlab/`, the appended test block, two CMake lines, `docs/faultlab/`. No runtime flag needed (nothing loads unless called). Scenario files are data; removing one cannot break others (loader is per-file).

## 12. Definition of Done (track-specific)

1. **Undergraduate value**: end-to-end teaching scenario (band-role swap) where construct→measure→diagnose→verify gives clearer process feedback than the current point-and-grade mode (demonstrated by `test_faultlab_scenarios` + report).
2. **Agent value**: `faultFamilyCatalog()` + `runFaultScenario()` + `sicnu.faultlab.report/1` JSON — machine-readable, no GUI.
3. **Single source of truth**: no new grading engine, no new diagnostic codes, no new assertion kinds, no new store; expectations cite existing signature ids and artifact checks.
4. **Typed failures**: unknown/unsafe/unsupported → typed diagnostics (`faultlab.*`), no silent fallback anywhere.
5. **Offline**: all tests + exemplars run with no network.
6. **Bounded resources**: byte budget enforced, O(samples) transforms, 3-pass runner cap.
7. **Dynamic dedup**: re-check master/open issues/sibling tracks before PR; remove any overlap (documented in PR).
8. Plus public DoD: plan/code/tests/docs consistent; targeted tests green; no new warnings; two review rounds; PR self-contained.
