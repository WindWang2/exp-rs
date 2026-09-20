# DECISIONS — ds41-plugin-lifecycle-13

Autonomous choices, alternatives weighed, defaults adopted. Newest last.

## D1 — Upgrade lives in `PluginRegistry::installOrUpgrade`, not a new Package path

Alternatives: (a) extend `PluginPackage::install` with lifecycle callbacks;
(b) new `PluginUpgrader` class; (c) registry orchestrates
(chosen). Rationale: the registry already owns drain/unload/load/state and
the reload seam; `PluginPackage` stays a pure filesystem component (its
existing staged-copy+atomic-swap is reused verbatim as the commit step).
No `Upgrading` state is added — Loading/Quiescing/Loaded/Failed/Unloaded
cover every observable point.

## D2 — Rollback source = fresh snapshot of the CURRENT install

Not the dev-mode `plugin-last-good-*` (may be stale or absent in production).
For an upgrade, the bytes at the install target ARE the last-known-good by
definition. Captured synchronously-but-bounded BEFORE drain (a refused
capture → upgrade refused, nothing touched). Deleted on commit; kept with a
diagnostic naming the path only when rollback itself fails.

## D3 — Snapshot machinery: staged copy + completion marker, single worker

Alternatives: content-addressed store, copy-on-write — both need a second
store format and GC of shared objects for packages that are ≤ a few hundred
MB; staged copy + marker is the smallest thing that is verifiable and
cancellable. Chosen:
- capture writes `<dest>.staging-<pid>/` then `snapshot.marker.json`
  (files, bytes, pluginId) LAST, then `rename` → dest (atomic publish);
- `verifyComplete` = marker parses + declared counts match a bounded walk;
- budgets: `SICNU_PLUGIN_SNAPSHOT_MAX_BYTES` (default 256 MiB) and
  `SICNU_PLUGIN_SNAPSHOT_MAX_FILES` (default 8192); over-budget →
  `QuotaExceeded` (existing typed code), staging removed;
- async capture for the dev-mode post-load refresh (load() never blocks);
  the worker is a `PluginSnapshotJob` owned by the registry — dtor and
  unloadAll cancel+join, so shutdown cannot leak a thread;
- sync-bounded capture for the upgrade-time snapshot (atomicity requires
  it before swap; install is an explicit action, not GUI startup).

## D4 — Snapshot root stays under `<tempDirectory>/sicnu-plugin-snapshots/`

Persistent user-dir roots considered and rejected: snapshots are process-
scoped recovery artifacts; temp gets OS hygiene for free and matches 12.0's
placement. Layout: `last-good-<id>` (dev hot reload), `upgrade-<id>-<pid>`
(in-flight upgrade), `*.staging-*` (in-flight capture).

## D5 — GC policy

- `configure()`: bounded sweep of the snapshot root — `*.staging-*` and
  `upgrade-*` are always crash residue (in-flight artifacts die with their
  process); `last-good-<id>` is removed only when the id is no longer
  discovered (abandoned dev tree / external uninstall). Root itself being a
  symlink → sweep skipped (fail closed).
- `PluginRegistry::uninstallPlugin(id)` — unload (drain-refusal aborts
  typed) → `PluginPackage::uninstall` → drop `last-good-<id>`. CLI
  `plugin uninstall` routes through it.
- Failed rollback keeps the snapshot (only copy of last-good) + names it.
- Crash mid-upgrade recovery: install dir holds whatever the rename
  committed; `PluginPackage`'s `.staging` sweep additionally restores a
  `<id>.old.*` backup when the target vanished (swap interrupted between
  the two renames) and removes stale backups when the target exists.

## D6 — Host race fix shape

`IpcChannel::close()` gets a dedicated `mJoinMutex` serializing
joinable-check+join, and `close()` becomes `noexcept` (join wrapped —
a teardown must never throw; the destructor calls it). This preserves the
existing contract "close() returns ⇒ reader reaped" for EVERY caller, not
just the transition winner. `ExecutionPool::drain()` gets the same
serialization (`mDrainMutex`). No new exception types — the fix makes the
throw unreachable instead of catching it at the boundary (belt: noexcept;
suspenders: typed outcomes already cover every request path).

## D7 — Upgrade result vocabulary

`PluginUpgradeResult{ status, pluginId, installedDir }` with
`status ∈ {Installed, Upgraded, Refused, RolledBack, Failed}`:
- Refused — nothing changed, old running (gates, drain, migration, budget)
- RolledBack — drain/swap attempted; old restored and running (was-loaded)
  or old bytes restored (was-not-loaded)
- Failed — rollback itself failed; snapshot kept, diagnostic names it
- Installed / Upgraded — commit succeeded
New diagnostics: `PluginUpgraded=4007` (Info, from→to),
`PluginUpgradeRolledBack=4008` (Warning), `PluginUpgradeFailed=4009` (Error).
Budget/snapshot-IO failures reuse `QuotaExceeded`/`ResourceMissing`.
Concurrent lifecycle op on the same id reuses the `mReloading` guard
(diagnostic reworded to cover upgrade too).

## D8 — Version gate posture

Downgrades/same-version reinstalls are ALLOWED (package-manager semantic —
rejecting them breaks legitimate rollback workflows). The direction is
recorded in the `PluginUpgraded` provenance diagnostic; a downgrade emits a
Warning. The hard version gate stays where it belongs: manifest validation
(api_version/min_host_api/abi) + Enforce-mode capability-permission check.

## D9 — Lifecycle ownership: mReloading becomes map<id, thread::id>

Problem found during implementation: reload() (and now installOrUpgrade)
guards against a second RELOAD/UPGRADE via mReloading, but a concurrent
raw `load()` or `unload()` from another thread could still enter the
transaction window — e.g. publish `Loaded` for the failing new bytes while
the rollback restores old ones. Fix: `mReloading` maps pluginId → owning
thread id; `load()`/`unload()` refuse calls whose owner is a DIFFERENT
thread (TrustRejected, typed). The owner's own re-entrant calls pass —
reload/upgrade call unload()+load() internally. RAII guard erases on every
exit path. loadAllValidated() candidates are unaffected (no owner → pass).

## D10 — Per-dest capture serialization inside capturePluginSnapshot

Superseding an in-flight async capture (newer bytes win by definition)
could race the older job's publish ladder: two renames → whichever lands
last wins → stale bytes published. Chosen fix: a static map<dest, mutex>
(held for the whole capture+publish, keyed by the deterministic dest path).
A superseded job's per-file cancel check exits it promptly, so the newer
capture's wait is bounded by one file's copy time. Alternative considered —
cancel-check inside the publish ladder: leaves a microsecond window where
a cancelled job still publishes stale bytes; rejected for explicitness.

## D11 — Sweep owns pid: same-pid residue is NEVER reclaimed

`*.staging-<pid>-<seq>` and `*.old-<pid>-<seq>` carry the owning pid; the
sweep skips same-pid staging entirely (it can only be a live capture of
THIS process — every capture exit path removes or renames its staging)
and skips same-pid `.old-` only while its dest is missing (live publish
window). A same-pid residue from a genuinely failed publish is therefore
kept until a different-pid sweep (crash+restart) — accepted: the only
producer of that residue is a fs-level failure already reported typed.
`upgrade-<id>-<pid>` dirs are swept when the suffix pid is NOT this
process (crash residue); this process's own is kept (a live upgrade or a
kept-for-inspection failed rollback — reclaimed on the NEXT process's
configure, documented trade-off vs keeping it forever).

## D12 — "Crash during swap" oracle coverage

Deterministic registry-level stand-in for the host crash leg: a v2
manifest declaring `runtime: host-process` + a native entrypoint file —
it VALIDATES (file exists) but load() fails E6006 since the test process
installs no host-process runtime → upgrade must roll back to v1 bytes +
reload the old generation. The full worker-crash-mid-session interleavings
stay in the test_plugin_host_process stress lane (WP4).

## D13 — uninstallPlugin cancels the plugin's in-flight capture first

Race found in review: removing last-good-<id> while its async capture is
in flight lets the job's publish land AFTER the remove → orphaned snapshot
for an uninstalled plugin. uninstallPlugin (and installOrUpgrade before
its swap) now extracts + cancels the plugin's mSnapshotJobs entry before
touching the tree.

## D14 — env knobs

`SICNU_PLUGIN_SNAPSHOT_MAX_BYTES` (256 MiB), `_MAX_FILE_BYTES` (64 MiB),
`_MAX_FILES` (8192) — floored so a hostile env cannot make the bound an
effective no-snapshot. `SICNU_PLUGIN_SNAPSHOT_WAIT_MS` (30 s, clamped
1 s–10 min) — how long reload() waits for an in-flight capture before
declaring "no usable snapshot".
