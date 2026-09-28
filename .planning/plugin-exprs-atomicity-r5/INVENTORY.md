# R5 Track 03 — plugin/exprs hot-reload, atomic install/rollback, temp resources, host/in-process parity

## Execution-time facts (refreshed 2026-09-28, NOT the 2026-09-27 planning snapshot)

- `origin/master` = `a726d17a6224632d929e782e996351732632f272` (identical to snapshot SHA; snapshot master had not moved).
- Open PRs: **#1365** (R5 track 04 persistence — touches `src/experiment`, `src/agent`; no file overlap with this track).
- Seed issues both still OPEN, 0 comments:
  - **#1362** — `tests/test_exprs_plugin_system.cpp` pid-unique scratch dirs leak on assertion failure (cleanup `rm -rf` placed AFTER the last REQUIRE; no scope guard).
  - **#1364** — `tests/test_exprs_plugin_loader.cpp` :493/:1138/:1226 (hot-reload swap / installOrUpgrade atomic upgrade / failed-upgrade rollback) failed on #1335's Debian env, not reproducible on #1354's CachyOS lane; PR #1354's pid-unique-scratch rework of `test_exprs_plugin_system.cpp` is the suspected real fix but did not touch the loader file.
- Related merged work: **#1348** (r4-plugin-exprs lifecycle/rollback contracts; D-6 documented drift: host-process worker typed `InitializationFailed` folded into `LibraryLoadFailed`), **#1354** (r4-ci-redzone: pid-unique scratch + self-sufficiency for `test_exprs_plugin_system.cpp`), **#1344** (atomic_fs audit — Track 08 owns the generic layer).
- Branches: 6 sibling R5 worktrees/branches active on this host (r5-ci-budgets, r5-core-platform, r5-model-runtime, r5-persistence, r5-rs-gdal, r5-workflow-durability). `hardening/r5-persistence-consistency-20260928-001519` is PR #1365's branch.

## Master build break found at configure time (in-scope en-route fix)

`tests/CMakeLists.txt:14728` carried a leftover `=======` conflict marker (introduced by the #1353 merge `6ff7aabd8`; the marker existed on the master-side parent already). **master did not configure** — `cmake --preset dev-default` failed with a CMake parse error. Fix: delete the single marker line (both union blocks were already present on both parents). Same class as the en-route master fix #1348 carried.

## Root-cause inventory

### #1362 (leak on assertion failure)
- Every case in `test_exprs_plugin_system.cpp` builds a pid-unique root `/tmp/<leaf>.<pid>`, `rm -rf`s it at case start AND after the last REQUIRE. A failed REQUIRE aborts the case → trailing cleanup never runs → residue forever (pids not reused within a boot).
- The binary-scope `kUserRootRedirected` static initializer cleans `<scratch>/exprs_test_userroot.<pid>` only at process START — nothing runs at ABNORMAL case end.

### #1364 (cross-process trampling → the 3 flaky failures)
- `UpgradeFixture` (cases :1138/:1175/:1199/:1226/:1263/:1284/:1730/:1783/:1840/:1885/:1928/:1983/:1989):
  - `root` = FIXED shared `<temp>/exprs_test_upgrade` — constructor `remove_all`s it while a parallel process of the same binary may be mid-transaction;
  - sets the GLOBAL `SICNU_PLUGIN_USER_ROOT` env var to a path inside that shared tree.
- `ReloadFixture` (cases :117/:440/:493/:570/:638/:2101/:2137/:2178):
  - fixture dir `SICNU_TEST_HELLO_PLUGIN_DIR` = FIXED shared `${CMAKE_BINARY_DIR}/plugin_fixtures/hello_plugin`, written in-place by every case **and by 4 other test binaries** (lifecycle_halffail, unload_order, channel_parity, pollution_gate r4 suites reference the same define) — one process writes `{ not json` while another validates;
  - never sets `options.tempDirectory`, so dev last-good snapshots land in the GLOBAL `<temp>/sicnu-plugin-snapshots`; the fixture destructor `remove_all`s that whole shared root (nukes sibling processes' live snapshots mid-test).
- ctest PRE_TEST discovery runs each Catch2 case as its own process; under `ctest -j2` (and across the 5 binaries sharing the fixture) two processes overlap → manifest/copy/snapshot trampling → the exact flaky family of #1364. Slow/loaded Debian CI widened the window; the fast CachyOS lane usually won the race — "not reproducible on this machine" was timing, not absence.
- Fixed shared paths also remain in: `/tmp/exprs_test_pkgmeta_win` (:160), `/tmp/exprs_test_loader_escape` (:719), `<temp>/exprs_test_lockdrop` (:801), `<temp>/exprs_test_lockdrop_revoke` (:905), `<temp>/exprs_test_snapshot` (:1326), `<temp>/exprs_test_sweep*` (:1447..1670).

### #1348 D-6 residual (host/in-process error-code parity)
- Worker `plugin.load` failure → worker sends `IpcError{code:"E4002", data: loadLog.toJson()}` (typed per-diagnostic array; `PluginDiagnostic::fromJson` exists).
- Host `PluginHostProcessRuntime::loadPlugin` (`plugin_host_process_runtime.cpp:164`) discards `error.data` and logs a flat `LibraryLoadFailed`. In-process, the same failure surfaces as the loader's own typed code (e.g. `InitializationFailed` for manifest/binary id mismatch). Parity table row "id mismatch" pins the drift.

## Build/test entry points (this host)

- Toolchain: `~/toolchain/cmake-dist/bin` (cmake), `~/toolchain/ninja`, SDK prefix `/home/kevin/pwb-sdks/root/usr` (Qt 6.11.2, GDAL 3.13.3), compiler `/usr/sbin/c++`.
- Configure: `cmake --preset dev-default -DCMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr` (Debug, ENABLE_TESTS=ON).
- Build: `cmake --build build-dev --parallel 2`. Test binaries run directly (`build-dev/tests/test_exprs_plugin_loader` etc.); ctest PRE_TEST selection by case name is unreliable for `-R "plugin|exprs"` (#1348 gate note).
- NOTE: user shell aliases `ninja` to `-j40` — always use `cmake --build --parallel 2`, never bare ninja.

## Not this track (boundaries)

- Generic atomic_fs layer → Track 08 (only consume/minimal interface patches).
- Model Python worker → Track 07.
- Qt/MCP global teardown → Track 01.
- #1355/#1356/#1357/#1358/#1359/#1360/#1361/#1363 belong to other tracks/owners.
