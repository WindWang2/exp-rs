# EVIDENCE — scientific-contract-verification-10

Policy: every capability claim maps to a local command + exit code, or is marked not-executed.
Host: 16 cores / 62 GB; all builds `cmake --build build-ci-fast -j2` with `CMAKE_BUILD_PARALLEL_LEVEL=2`; tests serial.

## Phase 0 (baseline)

- `git fetch --all --prune && git checkout master && git pull --ff-only` → exit 0; origin/master = `7d78059d1a6d316d606656759a506d17bc5e3b55`.
- `git worktree add ../exp-rs-scientific-contract-verification-10 -b zcode/scientific-contract-verification-10 origin/master` → exit 0.
- 7/7 findings re-verified LIVE on `7d78059d1a` (file:line evidence in BASELINE.md).
- `.gitignore` whitelist appended; shared `.git/info/exclude` shadows check-ignore → `git add -f` used (348-file precedent; DECISIONS D-7). `git ls-files .planning/scientific-contract-verification-10/ | wc -l` → 13.

## Build (worktree `build-ci-fast`, preset ci-fast: Release + Ninja + GCC)

- Configure: `cmake --preset ci-fast -DFETCHCONTENT_SOURCE_DIR_PYBIND11=<main>/build/_deps/pybind11-src -DFETCHCONTENT_SOURCE_DIR_CATCH2=<main>/build/_deps/catch2-src` → exit 0 (network FetchContent clones flaked: SSL EOF on github.com; reused main-checkout `_deps` caches — same commits, no version drift).
- `cmake --build … --target sicnu_geospatial sicnu_operators sicnu_contracts -j2` → BUILD1_OK (~45 min; qgis_core vendored build dominates).
- All 8 C++ suites + capability_knowledge_tool + contract_inventory built → exit 0.

## Findings fixes — test evidence (all at worktree HEAD)

| Suite | Result |
|---|---|
| tests/test_io_operators (F-OPS-4 regression incl. CRS-less reproject transform) | All tests passed (92 assertions in 7 cases) |
| tests/test_model_runtime_8 (F-OPS-1: encoding escalation + manifest refusal) | All tests passed (2358 assertions in 15 cases) |
| tests/test_qa_mask (F-OPS-3: fail-closed + SCL all + unreadableSamples) | All tests passed (117 assertions in 8 cases) |
| tests/test_model_tasks (F-OPS-5: bucket/dense equivalence, cancel probe, 100k budget) | All tests passed (12595 assertions in 10 cases) |
| tests/test_tensor_blob (F-OPS-2: ND ROI byte-equality) | All tests passed (173 assertions in 7 cases) |
| node --test pi/test/ (F-PI-1/2: flood-zombie + deadline + parity) | 10/10 pass |

## Platform 10.0 — test evidence

| Suite | Result |
|---|---|
| test_scientific_contract_10 | All tests passed (1045 assertions in 5 cases) |
| test_drift_projection_10 | All tests passed (1885 assertions in 3 cases) |
| test_science_verification_10 | All tests passed (1096 assertions in 5 cases) |

## Regenerated artifacts (conscious-diff ritual)

- `capability_knowledge_tool gen-pages .` → pi/knowledge/capability-{io,index}.md updated (3 new operators).
- `contract_inventory --source-root . --out data/contracts/contract_graph.snap.json` → `nodes: 973 edges: 386 findings: 0` (was 824/263 at contract-9).
- `data/agent/capabilities/preprocess.json` +3 operator entries (minimal append, +27 lines).

## Known pre-existing master failures (not this track's scope)

- `tests/test_capability_drift`: 2 failing cases reproduce identically on origin/master — cartography:diff_templates/explain/export knowledge entries and the harness.optical_ndvi_landsat recipe summary (verified: `git show origin/master:data/agent/capabilities/tools.json` contains none of the entries; this branch's diff does not touch those files). Strictly improved by this branch: the rs:gaofen_import/rs:zy3_import/rs:hj_import uncovered-operator failure is FIXED here.

## Phase budgets (tool calls / files touched)

- Phase 0: ~25 calls / 15 files · Phase 1 (recon+verify): ~20 calls / 0 files · Phases 2–5 (implement+verify): ~120 calls / 28 files · docs/planning: ~10 calls / 15 files.

## Round-2 review fixes + final HEAD evidence (commits ce88af53..05597aa7)

- Both subagent reviews' 22 findings fixed; dispositions in REVIEW_LOG.md.
- header-probe aggregate repaired (Unix Makefiles AUTOMOC forwarding gap):
  `cmake --build build-ci-fast --target sicnu_header_probes -j2` → Built.
- Ladder rerun at final HEAD: `python3 scripts/verification_ladder.py
  --build-dir build-ci-fast --lanes L0,L1,L2 --build-jobs 2` → exit 0,
  **OVERALL PASSED**, 20/20 items passed
  (.planning/…/ladder_l0_l2.json).
- READINESS regenerated at final code SHA `7cca494625` →
  `overall=insufficient-evidence, passed=20, failed=0, not_built=16,
  skipped=3` (honest: L3–L7 not executed in this worktree).
- Final suite sweep at final HEAD: test_scientific_contract_10 1048 ✓ ·
  test_drift_projection_10 1885 ✓ · test_science_verification_10 1187 ✓ ·
  test_qa_mask 117 ✓ · test_tensor_blob 173 ✓ · test_model_tasks 12595 ✓ ·
  test_model_runtime_8 2358 ✓ · test_capability_knowledge 1028 ✓ ·
  test_io_operators 92 ✓ · node --test pi/test 10/10 ✓.
- Hygiene: `git diff origin/master --check` → only the jsoncpp-generated
  snapshot's pre-existing `"key" : ` style (123 identical lines on
  origin/master); conflict-marker scan clean; secret scan clean.
