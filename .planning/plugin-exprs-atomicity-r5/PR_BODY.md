# PR: fix(tests,plugins): R5 track 03 — atomic install/rollback & hot-reload test isolation, scratch RAII, host/in-process error parity

## Execution-time facts

- `BASE_SHA`: `a726d17a6224632d929e782e996351732632f272` (= the 2026-09-27 snapshot SHA; re-fetched and re-verified at execution start on 2026-09-28). Final sync target: latest `origin/master` at PR time.
- Read: issues **#1362**, **#1364** (both OPEN, 0 comments, unassigned); PRs **#1354** (merged, pid-unique scratch rework for `test_exprs_plugin_system.cpp`), **#1348** (merged, plugin lifecycle/rollback contracts, D-6 documented drift), **#1335** (Debian red-zone origin), **#1365** (open, R5 track 04 — no file overlap); review threads of #1348/#1354 via PR bodies + tracked planning docs (ROLLBACK_CONTRACT.md appendix A, DECISIONS.md D-6).
- Remote CI was not awaited per execution contract.

## Master build break fixed en route

`tests/CMakeLists.txt:14728` carried a leftover `=======` conflict marker (introduced via the #1353 merge `6ff7aabd8`; present on the master-side parent). **master did not configure** — `cmake --preset dev-default` died with a CMake parse error before this PR's changes. Both merge parents already carry both union blocks, so the fix deletes the single marker line. (Same en-route class as #1348's carried master link fix.)

## Root causes & fixes

### #1362 — pid-unique scratch dirs leak on assertion failure
Every case in `test_exprs_plugin_system.cpp` removed its `/tmp/<leaf>.<pid>` scratch AFTER the last `REQUIRE`; a failed assertion aborted the case and leaked the tree forever (pids are not reused within a boot). Two cases ("registry lifecycle", "registry policy blocks plugin ids") had **no cleanup at all** — they leaked on every run.

Fix: RAII `ScratchGuard` (new shared header `tests/support/exprs_test_env.h`) — removes stale residue at construction and the tree at destruction; `remove_all(std::error_code)` never throws, so the guard cannot mask the original failure. The binary-scope user-root redirect became a destructor-carrying static so the `exprs_test_userroot.<pid>` tree is removed at process exit on every outcome. Same-file bonus: the two lockdrop cases held a `std::thread` joinable across `REQUIRE`s — a failed assertion would unwind into `~thread()` == `std::terminate`, killing the process and skipping every cleanup; outcomes are now captured before `join()`.

### #1364 — hot-reload / installOrUpgrade flake family (:493/:1138/:1226)
The loader file still used the shared fixed paths that #1354 removed from the *system* file — confirming the issue's suspicion that #1354's rework was "likely the real fix" but leaving the loader family exposed:

- `UpgradeFixture` (:1138/:1226 and 10 sibling cases): fixed shared `<temp>/exprs_test_upgrade` whose constructor `remove_all` erased a parallel case process's install mid-transaction, plus a global `SICNU_PLUGIN_USER_ROOT` env pointing at that shared tree.
- `ReloadFixture` (:493 family): wrote manifests into the SHARED build-tree fixture dir (`plugin_fixtures/hello_plugin`, referenced by 5 test binaries), and used the GLOBAL `<temp>/sicnu-plugin-snapshots` root, whose wholesale removal in the destructor could nuke a sibling process's live rollback source.
- ctest PRE_TEST discovery runs each case as its own process; under `ctest -j2` two processes overlap deterministically enough that the slow/loaded Debian lane (where #1364 was observed) flaked while the fast CachyOS lane won the race — "not reproducible on this machine" was timing, not absence.

Fix: every fixture/case in the loader file (and the adjacent r4/parity suites, per #1362's own adjacent-audit mandate) gets a pid-unique root; the hello fixture is COPIED into the private scratch before use; `options.tempDirectory` pins the snapshot root inside the same private tree; the binary-wide `SICNU_PLUGIN_USER_ROOT` redirect stops enable/disable persistence from writing the REAL `$HOME/sicnu_geo_rs/plugins.index.json` (a shared cross-process file AND a developer-profile pollution). `RegistryUnloadGuard` orders unload-before-scratch-removal so a mapped fixture `.so` is never unlinked under the guard, on any assertion outcome. Windows `_WIN32` branches preserved (no Windows lane on this host — compile-level/inspection only, disclosed).

### #1364 verification contract (issue's action list, executed)
1. The three cases pass on this env at BASE (twice) — recorded in EVIDENCE.md.
2. The trampling mechanism is fixed at the root (shared paths → pid-unique), so the original Debian failure condition (cross-process interference) no longer exists in this suite family; paired-process stress runs are clean post-fix.
3. Closing note drafted for the issue: original-env rerun remains open for whoever holds that lane, but the root cause is removed at the source for both environments.

### #1348 D-6 residual — worker typed errors folded into `LibraryLoadFailed`
The worker ships its full typed load log in `IpcError.data` (E4002 carrier). `PluginHostProcessRuntime::loadPlugin` discarded it and logged a flat `LibraryLoadFailed`, so the host-process channel reported a different code than the in-process dlopen channel for the same bad plugin (pinned as drift by the parity table). The runtime now re-emits each `PluginDiagnostic::fromJson` record with its ORIGINAL code (pluginId re-attributed); failures with no typed payload (timeout, channel closed) keep the session diagnostic. Parity table updated: "id mismatch" is now `InitializationFailed` on BOTH channels — matching what `ROLLBACK_CONTRACT.md` appendix A already declared as the intended contract. Protocol unchanged.

### New rollback oracles (fault injection at every transaction stage)
- validation failure → :1175 (pre-drain refusal), :1730 (policy-gated)
- post-copy failure → :1885 (checksum verification fails after staging copy)
- activation failure → NEW: chmod-injected EACCES on the backup rename after the drain → asserts RolledBack, typed diagnostic, v1 bytes, v1 running, snapshot consumed, staging empty, permissions restored on any exit path
- reload failure → :1226 + NEW 15-cycle failed-reload loop asserting exact byte restore, zero residue, and ZERO `hello_plugin` mappings in `/proc/self/maps` after `unloadAll()` (handle/dlclose leak oracle)

## Change boundary

- `tests/CMakeLists.txt` — one-line master marker deletion
- `tests/support/exprs_test_env.h` — NEW shared header (scratchRoot/ScratchGuard/UserRootRedirect)
- `tests/test_exprs_plugin_system.cpp`, `tests/test_exprs_plugin_loader.cpp`, `tests/test_plugin_channel_parity_r4.cpp`, `tests/test_plugin_{lifecycle_halffail,lifecycle_unload_order,host_boundary,pollution_gate}_r4.cpp` — isolation + RAII + oracles
- `src/plugins/host/plugin_host_process_runtime.cpp` — the D-6 typed-error forwarding (only product-code change; diagnostics surface, protocol-compatible)
- `.planning/plugin-exprs-atomicity-r5/{INVENTORY,DECISIONS,EVIDENCE}.md` — evidence trail (force-added, matching #1348's convention)

Out of scope (honored): generic atomic_fs (Track 08), model Python worker (Track 07), Qt/MCP teardown (Track 01), no unrelated formatting/refactors.

## Test evidence

(see .planning/plugin-exprs-atomicity-r5/EVIDENCE.md E2..E7 for the full log)

- Master marker fix: configure failed on clean master, passes after the one-line deletion.
- #1362: pre-fix success-path leak reproduced (`exprs_test_registry.<pid>` / `exprs_test_policy.<pid>` left behind by green runs); post-fix acceptance with an intentionally injected `FAIL()` — case aborts (exit 42), `/tmp` carries zero residue.
- #1364: paired-process protocol (2 concurrent processes × 8 rounds of the :1138 case): pre-fix **7/16 failed** (`Installed` → `2 == 0` Refused), post-fix **0/16 failed**, no scratch residue.
- Post-fix gates, all green ×2: loader 429 assertions/36 cases, system 129/16, parity 10/3, halffail 68/9, unload_order 34/4, boundary 35/8, pollution 19/3. New oracles (activation-fault injection, 15-cycle reload leak check with `/proc/self/maps`) green; included in the loader counts.
- No Windows lane: compile-level verification + inspection only.

## Independent review

One independent reviewer subagent (fresh eyes, diff + issues + evidence only), verdict **SHIP-WITH-FIXES**, zero Blocker/High:

- [Medium] the activation-fault-injection case was not POSIX-guarded (on Windows `FILE_ATTRIBUTE_READONLY` does not block child renames — deterministic false red) → fixed: `#ifndef _WIN32` wrapper with rationale comment.
- [Medium] overbroad claim: two loader cases (`*rejects ABI mismatch*`, `*full native plugin lifecycle*`) still wrote manifests into the shared build-tree fixture dir → fixed: both converted to the private `HelloFixtureCopy` copy; the loader file now has zero shared-fixture writers. Remaining (pre-existing, disclosed): test_plugin_lifecycle_halffail_r4's 9 cases still share the build-tree `plugin_fixtures/broken_plugin` dir — intra-binary race window under parallel ctest, not exercised by this track's claims, left for the halffail suite owner.
- [Low] `PermissionsRestore` declared after the post-chmod REQUIRE → fixed: declared before it, so a surprising chmod result cannot strand the tree read-only.
- [Nit] a throwing `fs::remove_all` tail in the issue-1156 case → removed (ScratchGuard owns the cleanup).

## Compatibility & risk

- No product API/format/behavior change except the D-6 diagnostic forwarding: host-process load failures now surface the worker's typed codes (e.g. `InitializationFailed`) in addition to the session summary; consumers matching strictly on `LibraryLoadFailed` for worker failures would see the typed code first (the parity contract documents this as the intended surface).
- Test-only risk: scratch paths changed from fixed to pid-unique — nothing outside the plugin test family references them (audited).

## Remaining (not this track)

- #1355/#1356/#1357/#1358 (GDAL/ASan/teardown classes) — other tracks.
- Windows-lane runtime confirmation of the reload/rollback family — no Windows host here.
- Original-Debian-lane rerun of the three cases (#1364's step 1) — the root cause is removed at the source; the rerun is a formality for whoever holds that lane.
