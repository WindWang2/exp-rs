# DECISIONS — R5 Track 03 plugin/exprs atomicity

## D-1: Master build break fixed en route (tests/CMakeLists.txt:14728)

`origin/master` = a726d17a6 did not configure: a leftover `=======` marker from the
#1353 merge survived in `tests/CMakeLists.txt`. Both merge parents already carried
both union blocks (parity map gate + track-16 verify-chain), so the marker line is
pure residue. Fix = delete the line. Same en-route class as #1348's carried master
link fix. No semantic change.

## D-2: #1362 closure = RAII guards, not "reorder the rm -rf"

Catch2 aborts a failing case by unwinding; locals' destructors run. Every
cleanup-after-last-REQUIRE in `test_exprs_plugin_system.cpp` is replaced by a
`ScratchGuard` (ctor removes stale residue, dtor removes the tree,
`remove_all(std::error_code)` — never throws, cannot mask the original failure).
The binary-scope `kUserRootRedirected` bool-lambda became a destructor-carrying
static so the pid-unique user-root scratch is removed at PROCESS EXIT even when a
case aborted. Destructors use error_code overloads only (a throwing destructor
during unwinding would terminate and defeat the guard).

Bonus findings folded in (same pattern class, same file):
- "registry lifecycle" and "registry policy blocks plugin ids" had NO cleanup at
  all — they leaked on EVERY run, not just failures.
- Two lockdrop cases held `std::thread loader` joinable across REQUIREs: a failed
  assertion unwound into `~thread()` on a joinable thread = `std::terminate` —
  the process dies and NO cleanup (guard or otherwise) can run. Outcomes are now
  captured into locals, `loader.join()` runs, then the asserts.

## D-3: #1364 closure = extend #1354's pid-unique-scratch rework to the loader file

The three flaky cases share two trampling surfaces:
- `UpgradeFixture` used a FIXED `<temp>/exprs_test_upgrade` for every case process
  (constructor `remove_all`s it) AND pointed the global `SICNU_PLUGIN_USER_ROOT`
  env at that shared tree. Two parallel case processes erase each other's install
  mid-transaction — exactly the :1138/:1226 family.
- `ReloadFixture` wrote manifests into the SHARED build-tree fixture dir
  (`${CMAKE_BINARY_DIR}/plugin_fixtures/hello_plugin`, referenced by 5 test
  binaries) and used the GLOBAL `<temp>/sicnu-plugin-snapshots` root whose
  wholesale removal in the destructor nukes sibling processes' live rollback
  sources — the :493 family.
Fix: every fixture/case gets a pid-unique root (`<leaf>.<snapshotOwnerPid()>`),
the hello fixture is COPIED into the private root before use, and
`options.tempDirectory` pins the snapshot root inside the same private tree. The
legacy-snapshot cases now address `fixture.snapshotRoot` instead of the global.
Mapped/unmapped safety: a `RegistryUnloadGuard` declared between sink and fixture
guarantees the registry unloads (sink alive) BEFORE the scratch tree holding the
mapped .so is removed, on every assertion outcome.

Windows note: the loader file's `_WIN32` branches are preserved; no Windows lane
exists on this host, so Windows verification is compile-level/inspection only
(disclosed in the PR).

## D-4: Fault-injection oracle for the post-drain ACTIVATION leg

Existing rollback tests covered: pre-drain validation refusal, checksum-failure
post-drain, unloadable-next-version, busy drain, migration abort. Missing: a
post-drain SWAP failure (the install() internal transaction failing between drain
and commit). New case injects it deterministically on POSIX by making the user
root read-only after v1 is installed+loaded (rename(target→backup) fails EACCES),
then asserts: status RolledBack, typed PluginUpgradeRolledBack, v1 bytes, v1
running, snapshot consumed, staging empty. A `PermissionsRestore` guard restores
writable mode on any exit path so the fixture teardown cannot silently strand a
read-only tree (which would leak the pid-unique root — the very bug class this
track closes).

## D-5: #1348 D-6 closure at the runtime consumption layer

The worker ships its full typed load log in `IpcError.data` (E4002 carrier);
`PluginHostProcessRuntime::loadPlugin` discarded it and logged a flat
`LibraryLoadFailed`. The fix re-emits each `PluginDiagnostic::fromJson` record
with its ORIGINAL code (pluginId re-attributed to the host-side id) before the
session-level summary. Failures with no typed payload (timeout, channel closed)
keep the old diagnostic. Chosen here rather than in `plugin_host_session.cpp`
(D-6's suggested home) because the session is generic request plumbing; the
runtime owns plugin-load semantics — smallest blast radius, protocol unchanged.
Parity table + header comment updated: "id mismatch" is now InitializationFailed
on BOTH channels, matching what ROLLBACK_CONTRACT.md appendix A already declared
as the intended contract (the table previously pinned the drift; the doc pinned
the target).

## D-6 (this track): repeated-reload leak oracle reads /proc/self/maps

15 bounded failed-reload cycles, each asserting exact byte restore + zero broken
payload residue; after `unloadAll()` the fixture library must have ZERO mappings
in /proc/self/maps (Linux-only oracle). This is the "重复 reload 有限循环
handle/resource leak" acceptance made executable: any dlopen/dlclose pair that
leaks its handle keeps the .so mapped and fails the count.

## Non-goals honored

No atomic_fs refactor (Track 08), no model Python worker (Track 07), no Qt/MCP
teardown (Track 01), no product behavior change beyond the D-5 typed-error
forwarding (a diagnostics-surface fix, protocol-compatible).
