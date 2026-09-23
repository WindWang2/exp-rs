# Hardening 13/20 — curriculum / LabSpec runtime / recipes / sample foundry

Track: `curriculum-lab-recipes-foundry` · branch `hardening/curriculum-lab-recipes-foundry`
Baseline: master `a9dc33fa7` (post-#1236). Scope: 教学底座 only — curriculum
harness/data, LabSpec v1/v2/v3 runtime, recipe compiler/registry, sample
foundry, lab packs/grading fixtures. No UI (#1237 owns `src/teaching/**` and
`src/app/teaching/**`; untouched).

## Oracles for this slice

1. The entire teaching-foundation test layer is registered and green:
   `test_lab_runtime`, `test_curriculum`, `test_lab_document`, `test_lab_source`,
   `test_recipe_{compiler,equivalence,lookup,registry,schema,validator}`,
   `test_teaching_foundation_e2e` (new). Before this slice **none** of these
   were built by any target — CI never ran them (S1 below).
2. `python3 scripts/check_lab_registry.py`, `check_curriculum.py`,
   `gen_lab_packs.py --check` stay green on the shipped tree.
3. Every behavioral fix below is bound to a regression test that FAILED on the
   pre-fix implementation (RED evidence in 02-test-ledger.md).
4. Targeted ctest selection passes twice consecutively before PR.

## Defects confirmed on master (recon → fix)

| id | severity | defect | fix |
|----|----------|--------|-----|
| S1 | P0 build/CI | `src/recipes` never compiled (no CMake target); `sicnu_lab_runtime` never linked; 3428 lines of teaching tests (`test_lab_runtime`, `test_curriculum`, `test_recipe_*`, …) registered nowhere — the whole foundation was untested from CI's point of view | `src/recipes/CMakeLists.txt` (sicnu_recipes), root `add_subdirectory`, light `sicnu_add_foundation_test` lane, all suites registered |
| S2 | P1 gate drift | `check_lab_registry.py` / `check_curriculum.py` / `curriculum_catalog::resolveLabReference` stuck at spec_version {1,2} while loader+schema support v3 — a legal v3 lab is rejected by the gate and routed as `unknown` by the curriculum harness | v3 accepted everywhere; `runtime` key allowed (v3-only, shallow object check; deep validation stays in sicnu_lab_runtime) |
| S3 | P1 contract | `spec_runtime` "parsing never throws" broken: `prompt_zh`/`objective_zh`/`text_zh`/choice entries fed to jsoncpp `asString()` unguarded → `Json::LogicError` on object/array values | typed `lab.runtime.*` rejections |
| S4 | P1 silent default | `expected_numeric` non-numeric/missing bounds silently became 0.0 (grading baseline falsified); hint `level` non-int silently became 1 | typed rejections; both bounds required+numeric; absent level still defaults to 1 (documented default) |
| S5 | P1 integrity | session envelope "refuses unknown keys … never adopted" enforced top-level only — nested checkpoint/answer/hint/choice/ref entries smuggled arbitrary fields through load/save cycles | per-entry vocabulary mirror of `sessionToJson` |
| S6 | P1 UB | `session_store::create()` strtoll'd arbitrary-digit seq names (saturating into signed overflow `value+1`); unreadable lab dir silently reused seq 1 (clobber risk) | 18-digit cap (matches read path), typed `lab.session.io` on unscannable dir |
| S8 | P2 ledger | `recordToolUse` accepted caller-forged seq (gaps/duplicates break latest-wins gates) | ledger assigns seq, caller values ignored (same as every other recorder) |
| S9 | P2 dual source | wrapper `.lab.json` `grading_rules`/`grading_ref.pipeline` vs registry canonical pointers unchecked — silent grading-chain fork possible with all gates green | pointer-parity check in the registry gate |
| S11 | P2 determinism | `gen_lab_packs.py` emitted `bytes` on generated entries iff the file existed locally — pack output was a function of the machine, contradicting the script's own "identical repo state → byte-identical packs" | generated entries never pin bytes (matches committed state; verifier treats absent bytes as no size check) |
| S13 | P2 provenance (live clue from `agent/flash-lab-foundry-determinism`) | foundry GDAL env pins (`GDAL_PAM_ENABLED`/`GDAL_NUM_THREADS`/`SHAPE_ENCODING`) never restored — library links leak the pins into the host process | `ScopedConfigOption` RAII restore + restoration test |
| S7 | P3 platform | Windows branch of session save had no fsync counterpart — header documents "fsync … atomic old-or-new" | `FlushFileBuffers` before rename (compile-checked only; no Windows host in this lane) |
| S14a | P3 | `recipes/lab_source.cpp` `loadRegistry` returns a silently empty view on a corrupt registry (downstream `no_steps` misattributes the cause) | kept as-is this slice (fail direction correct); recorded as known limitation |

## Not done (classified)

- **Owned by other tracks**: anything under `src/teaching/**` /
  `src/app/teaching/**` (#1237); `src/teaching_admin/**` (#1239); the science
  context broker's recipe *router* consumer (#1240) — we only widen what it
  reads.
- **Not reproducible here**: Windows `FlushFileBuffers` runtime behavior (no
  Windows host; compile-checked only).
- **Explicit future direction (not built)**: a machine consumer for
  `data/labs/data-specs/*.json` (S10) is a declared future layer, not a defect;
  per campaign rules we do not open it. `sicnu_lab_runtime` remains
  consumer-less by design until a teaching host lands (the E2E in this PR is
  the integration seam proving it is consumable).
- **Duplicate code consciously not consolidated**: three SHA-256
  implementations (tools / src/lab / geospatial) live in different dependency
  worlds (Qt-free vs Qt); merging them crosses module boundaries owned by
  other tracks for zero behavioral gain this slice (S12, recorded).
