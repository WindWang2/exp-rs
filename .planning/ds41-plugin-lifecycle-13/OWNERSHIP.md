# OWNERSHIP — ds41-plugin-lifecycle-13

## Core ownership (allowed to modify)

- `src/sdk/exprs/` — plugin lifecycle, package, snapshot, IPC channel.
- `src/plugins/` — host session/worker/runtime plumbing.
- `tests/test_exprs_plugin_loader.cpp`, `tests/test_exprs_plugin_system.cpp`,
  `tests/test_exprs_ipc.cpp`, `tests/test_plugin_host_process.cpp` — new
  oracles land in the existing targets; `tests/CMakeLists.txt` only if a new
  test target were needed (plan: none).
- `src/cli/cli_commands.cpp` — the `plugin install`/`plugin uninstall`
  handlers only (minimal: swap the filesystem-only call for the registry
  seam; no UX/surface changes beyond a `status` field in the JSON result).
- `src/sdk/CMakeLists.txt` — add the new `plugin_snapshot` source files.
- `.planning/ds41-plugin-lifecycle-13/`, `.goal-loop-ledger.md`,
  `.gitignore` (whitelist line for this planning dir).

## Forbidden without proven necessity

- `src/app/**`, `src/processing/**`, `src/operators/**`, `src/core/**`,
  `src/gui/**`, workflow/model-runtime code.
- Any second lifecycle state machine: `PluginState` enum stays untouched —
  upgrade reuses Loading/Quiescing/Loaded/Failed/Unloaded.

## Shared conflict hotspots (touch last, keep diffs minimal)

- `src/cli/cli_commands.cpp` — other tracks may touch the same dispatch
  chain; edits confined to the install/uninstall blocks (~40 lines).
- `src/sdk/exprs/plugin_diagnostics.h` — enum append-only (new codes at the
  end of the E4xxx/E6xxx families).
- `src/sdk/CMakeLists.txt` — single source-list line.

## Parallel tracks observed

- `geo-maintenance-13` (geospatial), `ds41-raster-merge-types`,
  `ds41-http-fetch-strict`, PR #1135 (temporal phenology): disjoint paths.
