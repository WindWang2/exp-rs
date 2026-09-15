# EVIDENCE — F14 radiometric-physics-11

Local evidence only; no online CI dependency. Each claim: command + exit code.

## Phase 0 — baseline audit (2026-09-15)

- `git fetch origin --prune` → exit 0 (first attempt hit a transient TLS EOF, retry clean).
- `git rev-parse origin/master` → `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`.
- `gh pr list --state open` → single open PR #1008 `zcode/radiometric-spectral-workbench`
  (45 files). Diff read for dedupe (read-only).
- `gh issue list --state open` → #1001..#1007, all other tracks' residuals; none radiometric.
- Master capability audit evidence: headers read (see BASELINE.md table);
  `grep -rin "declination|equation_of_time|earthSunDistance" src/` → only unrelated
  QgsMagneticModel; `grep -rln "viewZenith|view_zenith|BRDF|RossThick" src/` → empty.
- Worktree created: `git worktree add ../exp-rs-radiometric-physics-11 -b
  zcode/radiometric-physics-11 origin/master` → HEAD a5b11b7f10.
- `.gitignore`: appended one whitelist block for `.planning/radiometric-physics-11/*.md`
  (house pattern); `git check-ignore -v` verified non-ignored for tracked .md files.
- `cmake --preset dev-default` (worktree `build-dev`) → exit 0 (log: /tmp/configure_track.log
  summarized here at build time below).

## Build environment (fixed once)

- Host: 16 cores, 62 GiB RAM, load ~2.5 at start. Generator: Unix Makefiles (repo default for
  dev-default). Hard caps honored: `CMAKE_BUILD_PARALLEL_LEVEL=2`, build `-j2` (fallback `-j1`),
  tests `-j1`, `QT_QPA_PLATFORM=offscreen`. CPU/RSS/load sampled during builds (results below).

## Phase gates

(appended per phase as work completes)

## Phase 0/1 (2026-09-15, continued)

- `cmake --preset dev-default` attempt 1/2 failed: FetchContent git clones (pybind11,
  Catch2) hit repeated host TLS EOFs. Resolution: attempt 3 with the repo's own offline
  knob `-DSICNU_LAB_SKIP_PYTHON_BINDINGS=ON` (D10) + `-DFETCHCONTENT_SOURCE_DIR_CATCH2`
  pointed at the main checkout's already-populated `_deps/catch2-src` → "Generating done".
  No repository files changed by this.
- First full build started: `cmake --build build-dev --target test_solar_geometry -j2`
  (pulls qgis_core → sicnu_processing → sicnu_operators chain). Resource sampling in
  /tmp/build_monitor.log: load ≤ ~14.7 on 16 cores (< the 1.5× threshold), cc1plus RSS
  ~0.6–1 GB each (< 70% of 62 GiB) → kept `-j2`.

## Phase commits

- b635553c22 phase 0 planning track
- 301cff2cdd solar geometry module + tests
- f795569873 transition authority + provider seam
- ab78e75c5e BRDF normalization + QA flags
- b06d32d1f0 three operators
- 9d7989d69e integration registration (CMake/registry/capability catalog/docs/CHANGELOG)
- 01d9ddfa71 include fixes
- 419f32c5d3 operator E2E tests + QA mask buffer fix (real bug found pre-run:
  readBandWindow(float*) contract vs uint8 mask buffer)
- `git fetch origin && git rebase origin/master` → up to date (origin/master still a5b11b7f10).
