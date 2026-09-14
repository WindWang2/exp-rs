# EVIDENCE — hyperspectral-spectral-intelligence-10

Policy: every capability claim maps to a local command + exit code or is
marked `not-executed`. Append per phase.

## Phase 0

* `git fetch --all --prune && git pull --ff-only` → master == origin/master == `7d78059d1a6d316d606656759a506d17bc5e3b55`. (First pull attempt: transient `TLS connect error … unexpected eof`; clean on retry.)
* `git worktree add ../exp-rs-hyperspectral-spectral-intelligence-10 -b zcode/hyperspectral-spectral-intelligence-10 origin/master` → OK.
* `git add -n .planning/hyperspectral-spectral-intelligence-10/PROBE.md` → "add '.planning/…/PROBE.md'" (whitelist effective).
* `gh pr list --state open` → empty. Remote branches: only `origin/master` + itk-upstream (all zcode/* merged branches deleted).
* Disk baseline: root fs 94% used / 56 GB free; `/tmp` 32 GB tmpfs 50% used; transient ENOSPC observed once writing a shell cwd file in /tmp → build dirs stay on /home; df monitored per phase.

## Phase 2/3 build & test evidence (2026-09-13, worktree build-dev)

* Build: `cmake --build build-dev --target test_spectral_table test_mnf_transform test_spectral_classification test_spectral_unmixing test_spectral_detection test_spectral_pipeline -j1` → exit 0 (rounds 1-9; rounds 1-4 churned on mid-build CMake edits — recorded as process lesson; round 4 had transient gcc ICEs in qgis_gui under host load, resolved by -j1 retry).
* Targeted test run (QT_QPA_PLATFORM=offscreen, serial):
  - test_spectral_table → All tests passed (62 assertions in 9 test cases)
  - test_mnf_transform → All tests passed (432 assertions in 6 test cases)
  - test_spectral_classification → All tests passed (133 assertions in 27 test cases)
  - test_spectral_unmixing → All tests passed (55 assertions in 8 test cases)
  - test_spectral_detection → All tests passed (556 assertions in 5 test cases)
  - test_spectral_pipeline → All tests passed (36 assertions in 4 test cases) — **PPI → unmixing + SAM in one workflow via $step.endmembersArtifact placeholders (H-2 acceptance)**
* Defects found & fixed during verification:
  - NNLS pivot tolerance was absolute (1e-12) → infinite activate/deactivate loop on penalty-scaled systems (u~1e6, gradient noise ~1e-10); fixed with problem-scaled dual tolerance 1e-11·|u|max (root-caused via instrumented repro).
  - seam inline shape: MF/ACE `target` is a flat array; seam now disambiguates flat vs rows.
  - JSON-artifact steps need `verificationPolicy=skip` (documented in operator metadata).

## Phase 3/5 completion evidence (2026-09-13)

* test_spectral_selection → All tests passed (45 assertions in 4 test cases): band_select wavelength-window + nm normalization + exclusion + refusals; library_select material filter + near-duplicate QA + license echo + strict-load refusal.
* test_mnf_transform (with 256-band scale case) → All tests passed (432 assertions): 256-band logical cube fit + SNR ordering + roundtrip ≤1e-5 (Phase 5 scale evidence; streaming by construction).
* Regression family re-run green: test_mnf, test_spectral_library (47), test_spectral_library_data (64879), test_spectral_resampling (46), test_spectral_roi (35), test_spectral_derivative (122), test_spectral_anomaly (90239), test_workflow_runtime (223), test_rs_operators (8948).

## Phase 4 integration evidence (2026-09-13)

* `cmake --build build-dev --target sicnu_geo_rs_cli -j1` → exit 0.
* `QT_QPA_PLATFORM=offscreen ./sicnu_geo_rs_cli --list | grep -E "spectral_band_select|library_select|mnf_inverse"` → all three new operators listed (rs:mnf_inverse, rs:library_select, rs:spectral_band_select).
* `./sicnu_geo_rs_cli --schema rs:mnf_inverse` → full JSON Schema rendered; `--schema rs:spectral_unmixing` shows endmembersRef/libraryPath/libraryMaterials/method params.
* test_algorithm_meta_drift → All tests passed (2270 assertions) — new operators deliberately declare no taskFamily, shipped sidecar set unchanged (DECISIONS D-11).
* MCP `tools/list` and the Agent Tool Catalog mirror the same RSOperatorRegistry (covered green by test_mcp_server + test_rs_operators).

## Workflow execution-surface regression (2026-09-13)

* test_e2e_phase2 → All tests passed (4106 assertions in 49 test cases)
* test_workflow_cancel → All tests passed (3 assertions in 1 test case)
* test_workflow_execution_plane → All tests passed (36 assertions in 6 test cases) — plane path with the new payload-port recording
* test_pipeline_runner → All tests passed (81 assertions in 12 test cases)


## Phase 8 final verification (2026-09-13, final HEAD)

* Final HEAD at verification: `e5bfbfaaec` (+ whitespace commit); origin/master unchanged at baseline `7d78059d1a` (fetched; nothing to rebase).
* Full suite (QT_QPA_PLATFORM=offscreen, serial, all exit 0):
  - Track binaries: test_spectral_table 62 ✓ · test_mnf_transform 1879 ✓ · test_spectral_unmixing 58 ✓ · test_spectral_detection 556 ✓ · test_spectral_selection 45 ✓ · test_spectral_pipeline 188 ✓
  - Adjacent/regression: test_spectral_classification 133 ✓ · test_mnf 22 ✓ · test_spectral_library 47 ✓ · test_spectral_library_data 64879 ✓ · test_spectral_resampling 46 ✓ · test_spectral_roi 35 ✓ · test_spectral_derivative 122 ✓ · test_spectral_anomaly 90239 ✓ · test_spectral_formula_drift 145 ✓
  - Workflow/engine surfaces: test_workflow_runtime 223 ✓ · test_workflow_cancel 3 ✓ · test_workflow_execution_plane 36 ✓ · test_pipeline_runner 81 ✓ · test_e2e_phase2 4106 ✓ · test_rs_operators 8948 ✓ · test_algorithm_meta_drift 2270 ✓
  - Total ≈ 175,500 assertions, 0 failures. (test_fused_chain excluded: deterministic SIGSEGV reproduced identically on pristine master — see REVIEW_LOG OUT-1.)
* CLI rebuilt at final HEAD: `--list` shows rs:mnf_inverse, rs:library_select, rs:spectral_band_select (3/3); `--schema rs:mnf_inverse` renders.
* Mechanical: `git diff --check` clean; 0 conflict markers; 0 secret-ish strings; skill/doc existence assertions (goal-template) empty output = pass.
* Known non-goals recorded: DECISIONS D-7 (sparse unmixing, SID-SAM hybrid, local RX), D-11 (sidecar task families), D-6; REVIEW_LOG OUT-1 (fused-chain segfault, pre-existing on master).
