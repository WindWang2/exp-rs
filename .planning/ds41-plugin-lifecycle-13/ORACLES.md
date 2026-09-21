# ORACLES — ds41-plugin-lifecycle-13

Executable acceptance conditions. Each maps to tests or verifiable checks.

## O1 — Atomic install-time upgrade (WP1)

| # | Oracle | Evidence |
|---|--------|----------|
| O1.1 | Fresh install via `installOrUpgrade` behaves exactly like `PluginPackage::install` (staged copy + swap) and reports `Installed` | new test in test_exprs_plugin_loader or system |
| O1.2 | Upgrade of a LOADED plugin: new version ends up Loaded; old contributions revoked, new registered; result `Upgraded` with from→to provenance in diagnostics | test |
| O1.3 | Bad manifest in the new package → `Refused`, typed diagnostic, OLD version still Loaded and on disk | test |
| O1.4 | Policy/capability/API gate failure → `Refused` before drain; old running | test (enforce-mode capability or API-range manifest) |
| O1.5 | Migration failure → `Refused`, old running, nothing on disk changed | test |
| O1.6 | New binary fails to load → snapshot restored, old version Loaded again, result `RolledBack`, typed `PluginUpgradeRolledBack` | test |
| O1.7 | After rollback: on-disk manifest is the OLD version, recorded permissions/capabilities/provenance match pre-upgrade state | test assertions |
| O1.8 | Crash/failure inside `PluginPackage`'s own swap → old install intact on disk; result `RolledBack`, old version running | test via forced promote failure (chmod/lock) or construct review |
| O1.9 | Busy plugin (drain refused) → `Refused` with PluginInUse, old running, no residue | covered by unload barrier semantics + diagnostic |
| O1.10 | Upgrade never requires `SICNU_PLUGIN_DEV`; production (devMode=false) and dev both reach the same typed results | tests run with devMode=false fixture |
| O1.11 | Concurrent upgrade vs reload/upgrade on the same id → second refused typed | test |

## O2 — Snapshot I/O (WP2)

| # | Oracle | Evidence |
|---|--------|----------|
| O2.1 | Dev-mode load() no longer blocks on a whole-dir copy: the snapshot is captured on a bounded worker; load returns before the copy finishes | test asserts capture completes async (or structurally: no synchronous copy on publish path) |
| O2.2 | Byte budget: payload over `SICNU_PLUGIN_SNAPSHOT_MAX_BYTES` → typed refusal, no partial snapshot published | test with env-set tiny budget |
| O2.3 | File-count budget: over `SICNU_PLUGIN_SNAPSHOT_MAX_FILES` → typed refusal | test |
| O2.4 | Completeness: a snapshot without the completion marker is never used for rollback (existing entries<2 heuristic replaced by marker check) | test writes partial snapshot |
| O2.5 | Cancel/shutdown: registry teardown with an in-flight capture joins the worker (no leak, no hang, no use-after-free) | test destroys registry context / unloadAll mid-capture |
| O2.6 | Symlinked entry inside a plugin dir → capture fails closed, typed | test |

## O3 — Snapshot GC / temp hygiene (WP3)

| # | Oracle | Evidence |
|---|--------|----------|
| O3.1 | After a successful reload/upgrade cycle the snapshot root contains no `*.staging-*` or `upgrade-*` residue | test asserts dir contents |
| O3.2 | `uninstallPlugin` removes the plugin's last-good snapshot alongside the package | test |
| O3.3 | configure()/refresh sweeps `last-good-*` dirs whose id is no longer discovered (abandoned dev tree) | test deletes plugin dir, reconfigures, asserts snapshot gone |
| O3.4 | Crash residue: `.staging/<id>.old.*` backup + missing target → next sweep restores the backup; backup + existing target → stale backup removed | test in test_exprs_plugin_system (POSIX) |
| O3.5 | Cleanup only ever touches paths inside the owned roots; a symlinked snapshot-root entry fails closed (no target traversal) | test plants a symlink |
| O3.6 | Failed rollback keeps the snapshot for recovery + names it in diagnostics | construct review + diagnostic assertion |

## O4 — Host race hardening (WP4)

| # | Oracle | Evidence |
|---|--------|----------|
| O4.1 | Deterministic: N threads call `IpcChannel::close()` concurrently → exactly one joins, none throws | new test in test_exprs_ipc (catches the OLD bug: without the fix a second join throws system_error) |
| O4.2 | The existing `interleaved timeout, crash and cancel` stress runs multiple rounds; every outcome typed, no exception escapes | existing test re-run; and the O4.1 unit test is the injected-fault discriminator |
| O4.3 | unload/restart/recycle/reload interleavings: entry identity + mutex lifetime preserved (runtime's existing discipline unchanged) | existing test_plugin_host_process suite green |
| O4.4 | Worker-side `ExecutionPool::drain()` serialized — concurrent drainers cannot double-join | construct review (worker side has no direct harness); same pattern as O4.1 |

## Global gates

- targeted build (`sicnu_exprs`, `sicnu_plugins*`, affected test targets) OK
- `ctest -R 'exprs_plugin|exprs_ipc|plugin_host_process'` green **twice consecutively**
- ≥1 new test provably catches an injected regression (O4.1 without fix throws)
- `git diff --check` clean; independent review P0/P1 = 0; re-verify ×2
