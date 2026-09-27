# EVIDENCE — R5 Track 03 plugin/exprs atomicity

Environment: CachyOS, GCC 16.2.1, Qt 6.11.2 / GDAL 3.13.3 (local SDK prefix
`/home/kevin/pwb-sdks/root/usr`), Debug build (`dev-default` preset, fresh
`build-dev/`), `cmake --build --parallel 2` hard cap, direct binary runs with
`LD_LIBRARY_PATH=<sdk>/lib:/usr/lib QT_QPA_PLATFORM=offscreen` (the same pins
ctest's SicnuTestEnv applier sets). Local verification only — online CI was not
awaited (execution contract).

## E1 — master configure break (found at execution time)

- `git rev-parse origin/master` = `a726d17a6...`; `cmake --preset dev-default`
  on a clean tree failed: `CMake Error at tests/CMakeLists.txt:14728: Parse
  error. Expected a command name, got unquoted argument with text "=======".`
- Root: leftover marker from the #1353 merge (`6ff7aabd8`); both parents carry
  both union blocks (`git show 6ff7aabd8^1/^2`), so the marker is pure residue.
- After deleting the single line: configure succeeds (32 s generate, no errors).

## E2 — #1362 leak mechanism: PRE-FIX leak, POST-FIX acceptance

Pre-fix (BASE code, success path — the two cases had NO cleanup at all):
- ran `test_exprs_plugin_system "*registry lifecycle*"` and
  `"*registry policy blocks*"` → both suites green, yet after the runs:
  `/tmp/exprs_test_registry.374596` and `/tmp/exprs_test_policy.374637`
  remained. Leak-on-success proven (and leak-on-abort follows from the same
  missing-scope-guard structure).

Post-fix acceptance (issue #1362's own criterion — inject an assertion failure):
- `FAIL("intentional #1362 acceptance injection...")` injected into the
  discovery case; run exits 42 with `1 failed` as expected; after the process
  exited: `ls /tmp/exprs_test_discovery.*` → **no residue** (the ScratchGuard
  destructor ran during unwinding). Injection reverted and target rebuilt green.
- Historical residue sweep: 30 `exprs_test_userroot.<pid>` dirs from pre-fix-era
  runs (this host's earlier full-ctest runs + the pre-fix experiment above) —
  post-fix binaries add ZERO residue (count 30 → 30 across post-fix runs; all
  30 cleaned manually afterwards).

## E3 — #1364 cross-process trampling: reproduced pre-fix, gone post-fix

Paired-process protocol (emulates ctest -j2 PRE_TEST: two processes of the
loader binary running the :1138 UpgradeFixture case simultaneously, 8 rounds,
outcomes counted from exit codes):

- PRE-FIX: **procA 2/8 failed, procB 5/8 failed** (7 of 16 runs). Failure
  signature (both logs): `tests/test_exprs_plugin_loader.cpp:1148: FAILED:
  REQUIRE( first.status == PluginRegistry::PluginUpgradeStatus::Installed )`
  with expansion `2 == 0` (Refused) — the peer's constructor `remove_all`
  erased the shared fixed `<temp>/exprs_test_upgrade` tree mid-install, and
  both processes also shared the global `SICNU_PLUGIN_USER_ROOT`.
- POST-FIX: same protocol, same machine load — **0/16 failures**, and no
  `exprs_test_upgrade.*` / `exprs_test_reload.*` / `exprs_test_userroot.*`
  residue after the loop.

This is the mechanistic reproduction #1364 asked for: the historical
Debian-lane flake family is the shared-path trampling, now removed at the
source (pid-unique roots + private fixture copies + private snapshot roots +
binary-wide user-root redirect).

## E4 — post-fix module gates (final deliverable state)

All run directly; "All tests passed" from Catch2, twice for the three suites
the track touched most:

| suite | pass 1 | pass 2 |
|---|---|---|
| test_exprs_plugin_loader | All tests passed (429 assertions in 36 cases) | All tests passed (429/36) |
| test_exprs_plugin_system | All tests passed (129 assertions in 16 cases) | All tests passed (129/16) |
| test_plugin_channel_parity_r4 | All tests passed (10 assertions in 3 cases) | All tests passed (10/3) |
| test_plugin_lifecycle_halffail_r4 | All tests passed (68 assertions in 9 cases) | — |
| test_plugin_lifecycle_unload_order_r4 | All tests passed (34 assertions in 4 cases) | — |
| test_plugin_host_boundary_r4 | All tests passed (35 assertions in 8 cases) | — |
| test_plugin_pollution_gate_r4 | All tests passed (19 assertions in 3 cases) | — |

Includes the three historical #1364 cases (`*hot reload swaps*`,
`*installOrUpgrade installs a fresh package*`,
`*installOrUpgrade rolls back when the new version cannot load*`) green in
every pass.

## E5 — new oracles

- `*rolls back when the activation swap fails*` (chmod-injected EACCES on the
  post-drain backup rename): green — status RolledBack, typed
  PluginUpgradeRolledBack, v1 bytes + running, snapshot consumed, staging
  empty.
- `*repeated failed hot reloads*` (15 cycles, `/proc/self/maps` must contain
  zero `hello_plugin` mappings after unloadAll): green — combined 5-case
  focused run: "All tests passed (187 assertions in 5 test cases)".

## E6 — Windows lane disclosure

No Windows host available in this track. Windows verification is limited to:
the `_WIN32` branches preserved and consistently compiled-out, the shared
header's `_putenv` branch, and static review of `CrossProcessInstallLock`'s
CreateFileW(exclusive-share) fail-fast path (semantics unchanged by this PR).

## E7 — D-6 chain evidence

Parity suite failed (1/10 assertions) with only the runtime-layer forwarding in
place; root cause traced to `IpcError::toJson` dropping non-object `data` (the
worker's load log is an ARRAY) — the `fromJson` side explicitly documents
array support. One-line serializer fix; parity then green ×2. The parity table
row "id mismatch" now asserts `InitializationFailed` on BOTH channels, matching
ROLLBACK_CONTRACT.md appendix A's declared contract.
