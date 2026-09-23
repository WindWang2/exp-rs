# Oracle — completion/teaching-admin-real-adapters

Baseline: master `e4904cd3c568e1e730396237ec7034dc9215b272` (recon seed confirmed 2026-09-24).
Open PRs #1277–#1284 checked: none touch `src/teaching_admin/**`, `src/app/teaching_admin/**`,
`tests/test_teaching_admin_core.cpp`, or the three bundle/batch scripts (recon agent, gh).

## Gaps proven on current master (RED / structural evidence)

| # | Gap | Evidence (current master) |
|---|-----|---------------------------|
| G1 | Dock batch grade = fabricated scorer | `src/app/teaching_admin/teaching_admin_dock.cpp:344-349`: callable returns `status="pass"`, `score=80.0`, `"local dry-run grade"` for every openable submission; button labeled "local mock-capable". Second grader truth. |
| G2 | Pack validation ignores repoRoot; no digest/version-pin check | `src/teaching_admin/data_pack_manager.cpp:45-47` `validatePackDocument(..., repoRoot)` body begins `Q_UNUSED( repoRoot )`; confinement is lexical only; `inventoryPacks` computes digests but never verifies `sha256` of inputs nor flags `declared_offline_bytes` mismatch. |
| G3 | Operator allow-set is a hardcoded demo | `teaching_admin_dock.cpp:66` default `"rs:extract_bands,rs:resample,rs:ndvi"`; real truth = `data/processing/algorithm_meta/capability/rs-*.json` sidecars (recon Q9). |
| G4 | LabSpec validation not fail-closed on version/dangling refs | `labspec_authoring.cpp:44-49` accepts any non-empty `schema` string (e.g. `sicnu.labspec.v99` passes); `grading_ref.intent_ref` / `data` paths never checked for existence. |
| G5 | No student projection / answer-leak guard for authored LabSpec | Only teacher-side `projectRecipeCompileView` + `projectCourseHomePreview` exist; nothing strips `grading_ref`/`expected_results`/step param values for students (cockpit masks params in `src/teaching/lab_step_timeline.cpp` — different lib, LabSpec-side gap remains). |
| G6 | Bundle adapter POSIX-only + no version-pin cross-check | `script_adapters.cpp:138` hardcodes `bash` (a `build_offline_bundle.cmd` twin exists, unused); `verifyOfflineBundle` never reads `manifest.json` `bundle_version`/`schema`. |
| G7 | No pack foundry-drift adapter | `scripts/gen_lab_packs.py --check` exists (exit 1 + `DRIFT <path>` lines) but teaching_admin's `runGenLabPacks` only runs the writing mode. |

## Oracles (objective exit criteria)

- **O1 (grader authority)**: a QtCore-only CLI adapter in `sicnu_teaching_admin` grades via
  `sicnu_geo_rs_cli lab --grade` (exit 0/1/2/3 + `{schema,digest,generated_utc,report}` transcript),
  mirroring `run_classroom_batch.py` canonical argv; missing CLI ⇒ typed `unavailable` row with
  mandatory non-empty reason (vocabulary of `src/cli/lab_batch_runner.h`), never a fabricated score.
  Dock consumes the adapter; the `score=80` lambda is deleted. Tests: fake-CLI helper (deterministic,
  portable plain-C++ executable) + real-shaped contract test; real CLI used when `SICNU_GEO_RS_CLI`
  set (WARN-skip otherwise).
- **O2 (pack confinement/digest/pin)**: `validatePackDocument` uses repoRoot to resolve and enforce
  containment (typed `path_traversal` on post-resolution escape); `inventoryPacks` verifies per-input
  `sha256` when declared (`digest_mismatch`) and flags declared-vs-computed byte mismatch
  (`byte_mismatch`); pack provenance-tier policy mirrors `src/agent/lab_data_pack.h` vocabulary
  (committed-fixture: sha256 required; generated: informative) without linking `sicnu_agent`.
- **O3 (operator truth)**: `discoverKnownOperators(capabilityDir)` reads the real
  `data/processing/algorithm_meta/capability/rs-*.json` sidecars (ids + param schemas); dock default
  replaced; unknown-operator fail-closed behavior preserved.
- **O4 (labspec fail-closed)**: unknown/unsupported `schema`/`spec_version` ⇒ typed `schema_mismatch`
  error; dangling `grading_ref.intent_ref` / nonexistent `data` refs ⇒ typed `dangling_ref` errors
  (existence checked against a caller-provided root; empty root ⇒ skipped, not silently passed).
- **O5 (student projection)**: `projectStudentLabView(labSpec)` derived from the same validated spec
  as the teacher projection; strips `grading_ref`, `expected_results`, and masks step param values
  with `"***"` (same convention as `lab_step_timeline`). Leak oracle: serialization of the student
  view must not contain teacher-only material; mutation check (re-adding a teacher field) must fail
  the test (adversarial oracle, documented in ledger).
- **O6 (bundle)**: adapter picks `build_offline_bundle.sh` vs `.cmd` by platform; `verifyOfflineBundle`
  cross-checks `manifest.json` `bundle_version` against an expected version (typed
  `version_mismatch` issue) and surfaces manifest `schema`; no second manifest verifier — the
  canonical script stays the only digest authority.
- **O7 (foundry reuse)**: `checkLabPackDrift` adapter wraps `gen_lab_packs.py --check`
  (exit 0/1, `DRIFT` lines parsed into typed issues) — reuse, no regeneration logic in C++.
- **O8 (verification discipline)**: only narrow targets built (`test_teaching_admin_core`, fake-CLI
  helper, optional dock smoke), `-j2` max; no full-app build; key oracles run twice consecutively.
- **O9 (review gate)**: independent adversarial reviewer starts from `origin/master...HEAD`, P0/P1/P2
  must be zero with regression tests before PR.

## Anti-goals

- No second grader / pack verifier / labspec loader / manifest verifier.
- No in-process link to `sicnu_agent` / `sicnu_operators` (forbidden-link guard stays intact).
- No full-app/QGIS build as DoD.
