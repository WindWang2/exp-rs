# Progress — RS14-07 Parameter Sensitivity & Uncertainty Studio

## Campaign 1 (PR #1198, merged 2026-09-22)

Slices A–F implemented test-first and merged: `sicnu_study`
(spec/sampling/runner/analysis/spatial/export), the light test suites
(`test_study_spec|sampling|runner|analysis|spatial|export`), the
`matrixCellId` extraction in `experiment_matrix`. Merge commit `75edf9994`.

## Campaign 2 — completion of the merged state (this branch)

Recon against `origin/master` `a9dc33fa7` found four DoD gaps left by the
merge (the e2e slice's build wiring and exemplar files were not part of the
squashed merge, and the bridge leaf was never added to the build):

| Gap | Evidence at recon | Closure |
|---|---|---|
| `test_study_e2e.cpp` not built | no target anywhere; referenced `examples/studies/ndvi-threshold.sicnu-study.json` did not exist | `sicnu_add_test(test_study_e2e)` + link `Sicnu::study`/`Sicnu::study_bridge`/`GDAL::GDAL` |
| Exemplar specs missing | no `examples/studies/` | three shipped specs (OAT/grid/LHS), contract-tested through the production reader |
| `sicnu_study_bridge` dead code | no `add_subdirectory` anywhere | added after `src/processing` (needs `sicnu_task_center`), jsoncpp body dep linked |
| Integration/teaching docs missing | no `study` mention in `docs/` | `docs/experiments/parameter-studio.md` + `docs/integration.md` RS14-07 section |

Commits (one logical slice each):

1. `feat(study): ship the three teaching exemplar specs + reader contract test` — W1
2. `build(study): wire sicnu_study_bridge and the full-stack e2e into the build` — W2/W3
3. `docs(study): parameter studio guide + integration seams + planning record` — W4

Verification discipline: `-j1` throughout; light suites (`sicnu_study` +
Catch2, no QGIS) built/run first; the heavy e2e chain built ONCE before
final review.

## Review record

**Round 1** (independent adversarial reviewer over `a9dc33fa7..HEAD` + docs):
checklist A–H — exemplar-vs-operator-schema correctness, sampling/budget
arithmetic, e2e consistency, build wiring, oracle potency (mutation analysis),
scope containment, single-source-of-truth, docs accuracy. Verdict:
fix-first. Findings and resolutions:

| # | Sev | Finding | Resolution |
|---|---|---|---|
| 1 | P1 | classification exemplar's declared metrics unreachable: `overallAccuracy`/`kappa` require `testSplit > 0` (default 0), `meanConfidence` requires probabilityOutput which `svm` rejects | `testSplit: 0.3` added, metrics → `[overallAccuracy, kappa]`; test pins testSplit > 0 + exact metric pair |
| 2 | P3 | docs used `sicnu.studyspec.v1` as a literal document type — no such marker exists | reworded: spec is versioned by `schema_version`, no `document_type` |
| 3 | P3 | "production wires ExecutionPlaneStudyBackend" overstated | reworded: e2e wires it; production surfaces are future wiring |
| 4 | P3 | integration.md said report echoes `spec_json` | corrected to the serialized `"spec"` key |
| 5 | P3 | missing explicit `<QSet>` include | added |
| 6 | P3 | light-suite metric oracle contains-only | exact `metricNames` assertions for all three exemplars |
| 7 | P3 | LHS different-seed oracle compared pointIds (seed embedded) | compares sampled threshold values |

Post-fix: exemplar suite 163 assertions green (`-j1`).

**Round 2** (independent re-review; sampler RNG replicated externally to
sanity-check the LHS oracle): F1–F5/F7 verified fixed; the two e2e compile
fixes confirmed correct (`sicnu::dataset::runStatusToString`,
`.toStdString()` WARN); scope confirmed clean. New findings + resolutions:

| # | Sev | Finding | Resolution |
|---|---|---|---|
| 6' | P3 | F6 fix had landed 2/3: the LHS TEST_CASE pinned no exact `metricNames`, while the round-1 commit message and this file claimed all three | exact `["changedPercent","thresholdUsed"]` assertion added; the false completion record is corrected here |
| N1 | P3 | classification exemplar swept an INERT parameter: `rejectThreshold` is consumed only behind `uncertaintyOutput` (which `svm` refuses), so all 15 points emitted identical metrics and `declared_best` resolved an all-ties field | exemplar renamed `classification-training-budget.sicnu-study.json` and now sweeps `maxSamplesPerClass` (100…600), a training-budget parameter the operator genuinely consumes ahead of the held-out evaluation — the declared objective has real signal |
| N2 | P3 | replicate seeds are recorded but NOT injected into submitted parameters (the execution spine has no seed field), so "uncertainty across seeds" overpromised | doc honesty note added (identical submissions ⇒ zero-width bands are a truthful null for seed-ignoring operators); changing the runner's submission contract is deliberate non-scope |

Post-fix: exemplar suite 168 assertions green (`-j1`); no new compiler
warnings in touched files; full-stack e2e green (91 assertions).

Also fixed during bring-up (first time the e2e file was ever compiled — it
had no target until this branch): `runStatusToString` namespace drift to
`sicnu::dataset`, and the Catch2 WARN diagnostic needed `toStdString()`.

**Gate verdict**: PR-ready after round-2 resolutions.
