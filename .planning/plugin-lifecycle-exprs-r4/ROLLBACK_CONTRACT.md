# ROLLBACK_CONTRACT — exprs plugin registry transactions (Track R4)

Status: DRAFT (state machine transcribed from `src/sdk/exprs/plugin_registry.cpp`
@ master `15e5c66b5`; the doc↔test cross-reference table is finalized in WP-G
after the full suite runs twice).

This document is the normative transaction contract the r4 test suite pins.
Every rule cites the implementation seam it is enforced by and (once WP-G
closes) at least one test case name.

## 1. installOrUpgrade state machine

Entry: `PluginRegistry::installOrUpgrade(sourceDir, options)` → `PluginUpgradeResult`
with `status ∈ { Installed, Upgraded, Refused, RolledBack, Failed }`.

| stage | entry condition | success exit | failure exit | failure mode |
|---|---|---|---|---|
| S1 stage-validate | source manifest parses; id valid; source validates | → S2 | `Refused` | ManifestInvalidJson / ManifestInvalidField / TrustRejected (bad id, validation errors) |
| S2 policy gate | probe record passes `applyPolicyGate` | → S3 | `Refused` | TrustRejected (blocked/incompatible new version) |
| S3 ownership fence | no other lifecycle op for the id (`mReloading`); no in-flight `Loading`; no shadowing root | → S4 | `Refused` | TrustRejected (concurrent op / load in flight / shadowed id) |
| S4 snapshot current install | `capturePluginSnapshot(target → upgrade-<id>-<pid>)` ok + marker | → S5 | `Refused` | QuotaExceeded / EntrypointOutsideRoot / ResourceMissing |
| S5 migrate state | `migrateState` (if set) returns true; a THROW counts as failure | → S6 | `Refused` (upgrade snapshot dropped) | InitializationFailed |
| S6 drain | `unload(id)` succeeds (barrier; busy ⇒ refuse) | → S7 | `Refused` (snapshot dropped) | PluginInUse |
| S7 atomic swap | `PluginPackage::install` stages, checksum-verifies, renames; landed bytes re-read == gated manifest | → S8 | rollback leg R1 | TrustRejected (TOCTOU), checksum/staging failures |
| S8 publish | rescan: publishable state; `load(id)` iff it was loaded | → C commit (snapshot GC'd; PluginUpgraded) | rollback leg R2 | RegistrationFailed / Failed states |
| R1 (swap failed) | previous bytes intact on disk — restore from snapshot only if not; `oldGenerationOk()` reloads v1 | `RolledBack` + PluginUpgradeRolledBack | `Failed` + PluginUpgradeFailed (snapshot kept + named) | — |
| R2 (publish failed) | `restorePluginSnapshot` restores verified v1 bytes; reload; evidence preserved across refreshes | `RolledBack` + PluginUpgradeRolledBack | `Failed` + PluginUpgradeFailed | — |

Invariants (all rollback legs):

- I1: after `RolledBack`, the on-disk bytes AND the running generation are
  exactly v1's (version, permissions, capabilities — compared as sets).
- I2: after `RolledBack`/`Upgraded`, the per-upgrade snapshot is consumed —
  the snapshot root holds no `upgrade-*` residue.
- I3: after `Refused` (pre-drain), v1 is still loaded AND untouched on disk.
- I4: failure evidence is never erased by intermediate `refresh()` calls —
  diagnostics captured after the failure are re-added across every refresh.
- I5: only one lifecycle operation per id at a time; refusals are typed.

## 2. Hot-reload (dev-mode) snapshot semantics

Entry: `PluginRegistry::reload(pluginId, options)`; `options.devMode` can only
LOWER authority (both caller AND host policy must allow dev mode).

| step | rule | enforcement |
|---|---|---|
| R0 dev gate | `options.devMode && policy.devMode`, else TrustRejected, old version stays loaded | plugin_registry.cpp:1018-1042 |
| R1 single-flight | second concurrent reload of the id refused (TrustRejected) | :1046-1057 |
| R2 stage-validate | broken new manifest refused BEFORE unload; typed with the parse/validation's own code; no snapshot, no unload, no dlopen | :1096-1131 |
| R3 last-known-good | rollback source is the pid-attributed last-good snapshot (legacy layout as fallback source only); verified (marker + counts + manifest identity); absent/unusable snapshot ⇒ honest typed warning, failed reload leaves the plugin UNLOADED instead of rolled back | :1145-1229 |
| R4 migration | failed OR throwing migrateState aborts with the old version still loaded | :1235-1242 |
| R5 drain | busy plugin refused (PluginInUse), old version stays loaded | :1247-1253 |
| R6 swap+load | load new bytes; success = new generation running | :1254-1266 |
| R7 rollback | restore verified snapshot over the plugin dir, reload; reload still reports false (never a silent downgrade); PluginReloadRolledBack | :1294-1333 |

## 3. Unload order (issue #747 sequence)

1. `beginPluginDrain` arms the barrier; record → Quiescing.
2. `waitPluginIdle` bounded (explicit `timeoutMs` or the registry budget);
   refusal ⇒ drain cancelled, state restored to Loaded, PluginInUse typed.
3. Record leaves the loaded set; state → Unloaded (short critical section).
4. `revokePlugin` with the registry mutex DROPPED (#1156) — contributions are
   revoked while the code is still mapped.
5. `shutdown()` → `delete instance` → `dlclose` LAST (loader contract).

Invariants: no unbounded join; no contribution outlives the mapping; a busy
plugin stays fully usable after a refused unload.

## 4. Half-initialized state contract (WP-B)

Any mid-load failure (dlopen, entrypoint nullptr/throw, id mismatch,
initialize false/throw) leaves: no loaded entry, no contributions, record
state exactly `Failed`, no last-good snapshot published, snapshot root empty,
retry idempotent, `unloadAll()` clean, registry reusable.

## 5. Appendix A — dual-channel diagnostic mapping (WP-E)

| bad-plugin sample | in-process channel | host-process channel | relation |
|---|---|---|---|
| garbage payload (not a library) | `LibraryLoadFailed` | `LibraryLoadFailed` | equal |
| entrypoint missing | `EntrypointMissing` | `EntrypointMissing` | equal (validation stage is channel-independent) |
| manifest/binary id mismatch | `InitializationFailed` | `InitializationFailed` | equal (same check inside the worker) |

Both channels: typed code (never `None`), non-empty message, attributed to the
declared plugin id. Pins: `tests/test_plugin_channel_parity_r4.cpp`.

## 6. Doc ↔ test cross-reference (WP-G final table)

To be completed after the double green run — every contract row above must
cite at least one green TEST_CASE name; dead links fail the WP-G gate.
