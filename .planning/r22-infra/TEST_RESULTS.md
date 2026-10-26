# R22-07 Test Results (all commands executed in the worktree unless noted)

## Environment

- Host: Linux x86_64, 16 cores, 62 GiB RAM; Qt 6.12.0 / GDAL 3.13.3 / PROJ / GEOS / QGIS system installs; GCC 16.2.1.
- All builds: `flock -x /tmp/exp-rs-r21-build.lock`, `CMAKE_BUILD_PARALLEL_LEVEL=4`, `cmake --build … -j4` (hard cap honored).
- Configure: `cmake -B build-r22-infra -S . -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DSICNU_TOLERANT_TEST_DISCOVERY=ON` → exit 0.
- Host constraint (see BASELINE.md/RISK_REGISTER): Debug configure hits deterministic GCC 16.2.1 dwarf2out ICEs on this host (qgsrunprocess.cpp:302, lazperf charbuf.cpp:121, qgsmaplayer.h:1405, basic_string.h — master-untouched TUs too); Release used for all evidence.

## G0 — #1696 compile oracle

- Targets `sicnu_geo_rs_cli sicnu_worker sicnu_verification sicnu_lab_runtime`: **BUILD_EXIT=0** (Release; log /tmp/r22-infra-build3.log tail: `BUILD_EXIT=0`; 0 `error:`).
  - `sicnu_lab_runtime` compiles `session_state.cpp` with the new `<algorithm>` include (RED class: master failed with `'sort' is not a member of 'std'` on GCC+MSVC per CI run 39883187643).
  - `sicnu_verification` compiles all renamed-header consumers (`checks_*.cpp`, `availability.cpp`, `providers.h`).
- Rename completeness: `git grep -n '\bavailability\.h\b' -- src tests docs` → 0 hits; `find src -iname 'availability.h'` → 0 hits after rename (1 before). No other case-collision headers found by the `find src -iname` sweep over system-header names (Availability/String/Time/Math/etc.).
- **NOT_EXECUTED (platforms)**: real macOS/MSVC compile — no such host here; the rename is a mechanical case-collision removal and CI tier3 is the authoritative executor (not waited on per track rules).

## G1 — #1697 bundle guard oracle (RED executed; GREEN ×2)

- **RED vs master script** (sandbox, pre-existing `out/sicnu-lab-1.0` mirroring the issue's two-step repro):
  `--version '1.0/../../victim'` → `CANARY_DELETED_BY_OLD_SCRIPT` (canary outside `--out` removed by `rm -rf`). Also documented: without the pre-existing prefix the `rm -f` swallows ENOENT but `mkdir -p` still creates `victim/{bin,data,labs}` outside `--out` (path jamming).
- **GREEN (branch)**: `sh tests/test_r22_infra_bundle_guard.sh --repo-root .` →
  - run 1: `all legs passed` (PASS)
  - run 2: `all legs passed` (PASS)
  - **re-run ×2 after R1 fixes** (real stale-prefix assertion, symlinked-`--out` leg, same-mode containment fallback): `all legs passed` ×2 (PASS)
  - Coverage: 10 hostile versions (`1.0/../../victim`, `../..`, `..`, `.`, `/etc`, `a/b`, `a\b`, `a b`, `a;touch-pwned`, `$(touch pwned)`, backtick variant) → each nonzero rc + refusal message + canary intact + no recreated path + stale prefix survives the whole battery; legal `1.0` (stale cleaned, bin/ stub copied), `v1.2_beta-3`; `--out` with spaces works; `--incremental` preserves `keep.txt`; symlinked `--out` (containment must not spuriously refuse) works; no `pwned` file created.
- ctest wiring: `cmake -B build-r22-infra` regen → `ctest -N -R test_r22_infra_bundle_guard` = 1 test; executed `ctest -R test_r22_infra_bundle_guard --output-on-failure` → **Passed (0.12s)**. (After R1-5 the registration is `if(UNIX)` + `COMMAND sh …` — regen re-verified.)
- `.cmd` twin: **NOT_EXECUTED** on a real cmd.exe host (no Windows). Static assurance: PowerShell regex identical to the `.sh` whitelist; batch reviewed (delayed-expansion echo, env-var probe, errorlevel propagation); CI Windows smoke step added in ci.yml (refusal expected, exit≠0 asserted) — executed by CI only.

## G1 — #1698 install relocatability oracle

## G1 — #1698 install relocatability oracle (RED + GREEN×2, executed)

- Install completes only after building every install-ruled target set (first attempts failed on unbuilt targets — `libsicnu_core.so`, then the pybind11 module `_sicnu_operators…so`; both are expected consequences of partial target builds, not defects; CI's full-build install never hits them). Final: `cmake --install build-r22-infra --prefix /tmp/r22-install-test` → **INSTALL_RC=0**; 10 `Set non-toolchain portion of runtime path … to "$ORIGIN/../lib:$ORIGIN"` rewrites logged.
- Layout: `bin/sicnu_geo_rs`, `bin/sicnu_geo_rs_cli`, **`bin/sicnu_worker`** (new install rule), libs in `lib/`.
- RPATH proof: `readelf -d` on installed `bin/sicnu_geo_rs_cli`, `bin/sicnu_worker`, `lib/libsicnu_core.so` → `RUNPATH: [$ORIGIN/../lib:$ORIGIN]` each.
- Resolution proof: `env -u LD_LIBRARY_PATH ldd bin/sicnu_geo_rs_cli` → every `libsicnu_*.so` resolves as `/tmp/r22-install-test/bin/../lib/…` (loader actually using the RUNPATH, not an env crutch).
- Clean-env smoke ×2: `env -i PATH=/usr/bin:/bin HOME=… QT_QPA_PLATFORM=offscreen …/bin/sicnu_geo_rs_cli --help` → **RC=0** both runs (prints Usage; no LD_LIBRARY_PATH exists under env -i).
- Worker: executes from the install tree — without `--protocol 1` it prints its fail-closed protocol error JSON and exits 2 by design (`sicnu_worker_main.cpp:243`), which itself proves all its shared libs loaded via RUNPATH.
- **RED (explicit)**: copy of the installed CLI with `patchelf --remove-rpath` → clean-env run fails `libsicnu_agent.so: cannot open shared object file` (RC 127). Same binary + RPATH → RC 0. The fix is load-bearing.

## G3 — #1681/#1683 lint lane

- Lane dry logic on branch: `./scripts/run_clang_tidy_changed.sh origin/master build-r22-infra` → selects exactly this branch's own files via `merge-base 911e0b17…` (output captured; "own changes only").
- Host tooling caveats (advisory only): local clang-tidy rejects GCC-only `-mno-direct-extern-access` and nondeterministically SIGSEGVs on Qt-heavy TUs (observed RC=1 and RC=139 for the SAME pristine file across runs — env, not diff).
- Real diagnostics found & handled: `tests/test_data_foundation_scale.cpp:243` unnecessary copy-init (FIXED here); `src/dataset/dataset_ids.h:21` + `src/experiment/experiment_ids.h:14` macro-parentheses debt (out-of-whitelist → CROSS_TRACK_FINDINGS #2; lane's changed-headers-only filter keeps them from blocking unrelated diffs).
- ci.yml / platform-tests-optin.yml: `python3 yaml.safe_load` → OK (executed).

## G3 — #1699 time budgets

- `tests/sicnu_test_time_budget.h` behavior compiled & executed (standalone harness):
  - default: `scale=1.00 base2000->2001` (Release semantics unchanged)
  - `SICNU_TEST_TIME_SCALE=10`: `scale=10.00 … ->20001`
  - ASan build: `scale=4.00 … ->8001`
  - post-R1 bounded parse: `SICNU_TEST_TIME_SCALE=inf` → `scale=1.00`; `=garbage` → `scale=1.00` (hostile values ignored)
- Affected suites (Release build, QT_QPA_PLATFORM=offscreen, lock+serial):
  - `test_agent_ops_ui_smoke` (full): **12 assertions / 5 cases PASS**
  - `test_execution_governor_11` (full): **146 assertions / 10 cases PASS**
  - `test_data_manager` ("*remote raster*"): **5 assertions PASS**
  - `test_data_source_providers` ("Remote raster resolve on the application thread*"): **6 assertions PASS**
  - `test_asset_catalog_index` (full): **317 assertions / 8 cases PASS**
  - `test_data_foundation_scale` (full): **952 assertions / 1 case PASS**
  - `test_execution_plane` (full): **174 assertions / 18 cases PASS** (re-run after R1 comments: PASS again)
- Rename regression: `test_verifier_provenance_14` (full): **608 assertions / 19 cases PASS**.

## Shared-lock/resource policy (O6)

- Every build ran under `/tmp/exp-rs-r21-build.lock` with -j4 cap; memory recorded healthy (46+ GiB available throughout; no OOM/kill events in logs).
