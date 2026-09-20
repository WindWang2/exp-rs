# DEDUP — ds41-plugin-lifecycle-13

## What 12.0 (merged #1134) already provides — do NOT rebuild

- `PluginRegistry::reload()` dev-mode hot reload: manifest gate → last-good
  snapshot lookup → migrate seam → drain+unload → load → snapshot rollback.
- `snapshotPluginFiles`/`restorePluginFromSnapshot` statics in
  `plugin_registry.cpp` — symlink-refusing recursive copy, rollback restore.
- `PluginPackage::install()` — staged copy (containment + regular-files-only +
  checksum verify) then atomic rename swap with backup-restore on promote
  failure. It is filesystem-only: it never touches the registry/lifecycle.
- `PluginHostProcessSession` — generation-tracked sessions, kill ladder,
  poison-on-timeout, FIFO concurrency gate, typed IPC outcomes.
- `IpcChannel` — typed Outcome vocabulary (E6002–E6009), cancel frames,
  bounded event queue.

## Real gaps this track owns (code evidence)

1. **Atomic install-time upgrade** — `commandPlugin` calls
   `PluginPackage::install()` directly (`cli_commands.cpp:738`); the registry
   is never consulted. Installing over a LOADED plugin swaps bytes under
   running code and leaves the old generation running with no reload. No
   `installOrUpgrade`-style seam exists anywhere (grep-verified).
2. **Snapshot I/O** — `refreshLastGoodSnapshot()` (plugin_registry.cpp:491)
   does a synchronous whole-dir `remove_all`+copy on the load() publish path,
   once per plugin at eager load. No byte/file budget, no completeness
   marker (reload() checks only "plugin.json + ≥2 entries"), no cancel.
3. **Snapshot GC** — `plugin-last-good-<id>` in `<temp>` is only deleted by a
   successful `restorePluginFromSnapshot`. Abandoned dev trees, uninstalled
   plugins and crashed upgrade staging have no lifecycle. `PluginPackage`'s
   `.staging` root sweeps only >24 h leftovers and cannot restore a crashed
   swap (target missing + `<id>.old.<pid>` backup present → data loss).
4. **Host race** — `IpcChannel::close()` unguarded joinable/join pair
   (throw site located, see BASELINE). `ExecutionPool::drain()` same class.

## Overlap check vs open work

- PR #1135 (temporal phenology): no file overlap (verified via changed-files
  scan). No other open PRs. No open issues. No unmerged plugin-lifecycle
  branches.

## Pivot

None needed — all four WPs are live gaps with direct code evidence.

## Overlap scan #2 (mid-implementation)

- `origin/master` still `79adfe78` — no drift.
- New open PRs #1136 (offline-labs) and #1137 (geospatial): zero overlap with `src/sdk/**`, `src/plugins/**`, `tests/test_exprs_*`, or `src/cli/cli_commands.cpp`. #1136 touches only `src/cli/lab_batch_runner.*`.
- #1135 (temporal phenology) still open, still no sdk/plugins overlap.
