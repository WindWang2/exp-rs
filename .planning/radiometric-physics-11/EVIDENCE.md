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

## OUT_OF_SCOPE (recorded per GOAL Autonomy defaults #6)

- **P0 (out of scope, pre-existing on master a5b11b7f10):** `src/agent/data_platform_tools.cpp`
  does not compile on a fresh build (`BenchmarkService` used unqualified at :1184/:1221/:1265;
  D19 regression introduced by the a5b11b7f10 merge; the main checkout's stale Sep-14 object
  masked it). Already fixed on the parallel branch `origin/zcode/multimodal-registration-11`
  (commits 23b326a485 / 08ebe07270, "master build-unblock — qualify D19 BenchmarkService").
  This track does NOT carry the fix (avoids conflicting with that branch); the affected
  library (sicnu_agent) is not a dependency of any radiometric-physics-11 test target, and
  this track's `capability_catalog.cpp` object compiles cleanly. Regression gate uses the
  non-agent suites; `test_spatial_contracts` (agent-harness suite, links sicnu_agent) is
  therefore excluded from this track's regression run as not-executable here.
- A foreign build process (test_edit_session/sicnu_geo_rs) was observed running in this
  worktree's build-dev during Phase 6; all builds were serialized after it finished and the
  targeted gates re-run clean.

## Phase 5/6/7 — test evidence (local only, no online CI)

Build entry: `cmake --preset dev-default` + offline knobs
(`-DSICNU_LAB_SKIP_PYTHON_BINDINGS=ON -DFETCHCONTENT_SOURCE_DIR_CATCH2=…`,
host-network workaround, exit 0). Final target build:
`cmake --build build-dev --target test_solar_geometry test_radiometric_transition
test_atmospheric_provider test_brdf_normalization test_radiometric_qa -j2` → exit 0.

Double gate (Oracle #5, after the final rebase, consecutive runs):

```
ctest -R "test_solar_geometry::|test_radiometric_transition::|test_atmospheric_provider::|test_brdf_normalization::|test_radiometric_qa::" -j1
PASS 1: 100% tests passed out of 49
PASS 2: 100% tests passed out of 49
```

Regression (master's neighboring suites, run directly, QT_QPA_PLATFORM=offscreen):

```
test_atmospheric:             All tests passed (37486 assertions in 40 test cases)
test_radiometric_calibration: All tests passed (195688 assertions in 27 test cases)
test_topographic_correction:  All tests passed (496 assertions in 7 test cases)
test_qa_mask:                 All tests passed (117 assertions in 8 test cases)
test_satellite_products:      All tests passed (479 assertions in 20 test cases)
```

not-executed (with reason): `test_spatial_contracts` — links `sicnu_agent`, which does not
compile on a fresh master checkout for a reason unrelated to this diff (see OUT_OF_SCOPE);
this track's changes do not depend on it and its exclusion is not used to claim any
radiometric capability.

Hygiene: `git diff --check origin/master...HEAD` → exit 0; conflict-marker scan clean
(the `src/native/README.md` grep hit is a pre-existing Markdown setext underline, not a
conflict marker, and is not in this diff); secret scan hits are GOAL-checklist text only;
zero generated artifacts/build outputs in the diff (43 files, +5437/−0 lines before the
final planning-artifact commits).
