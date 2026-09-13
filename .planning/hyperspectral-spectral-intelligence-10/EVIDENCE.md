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
