# ADR 0130: Plugin Lifecycle, Unload Safety & Path Containment

## Context

The exprs SDK/plugin framework (PR #743) shipped with lifecycle and security
defects confirmed as issues #747 (P1: dock/menu use-after-free after dlclose,
no execution quiescence, GUI-exit teardown ordering), #748 (P1: POSIX-only SDK
sources break the documented Windows build), #755 (P2: enable-after-disable
never re-loads; `py:` algorithms never unregistered), #756 (P2: manifest
`entrypoint` escapes the plugin root; validation→load TOCTOU), and #757 (P2:
external-tool operators escape `SICNU_MCP_WORKSPACE` through manifest
constants). Unload could unmap plugin code under a running worker thread, the
plugin manager reported state that was false, and the load path trusted
manifest-declared paths that the security docs promised were confined.

## Decision

1. **One unload seam, ordered.** All unload traffic flows through
   `exprs::PluginRegistry::unload/unloadAll`: (1) arm the execution-barrier
   drain (new dispatch refused with a typed failure); (2) bounded wait for
   in-flight executions — the registry lock is NOT held while waiting because
   draining executors re-enter the registry; (3) release UI contributions
   through the shell sink; (4) revoke registry contributions; (5)
   `shutdown()`; (6) delete the plugin instance; (7) `dlclose`/`FreeLibrary`
   last. A timeout refuses the unload with a stable `PluginInUse` (E4005)
   diagnostic and restores `Loaded` — code is never unmapped under an
   executing thread. `SICNU_PLUGIN_UNLOAD_TIMEOUT_MS` (default 30 s, cap
   10 min) bounds the wait. `unloadAll` (host shutdown) leaves a still-busy
   plugin mapped for process exit instead of forcing it out.
2. **Owner-scoped execution leases.** `PluginExecutionBarrier`
   (process-wide, plugins/framework) issues RAII leases per `pluginId`;
   every in-process path that executes plugin code holds one: operator
   adapters (acquired before `ensureLoaded`), agent-tool executors, model
   runtime factory + inference calls, AND the direct `RSOperatorRegistry`
   path (JobEngine/workflow) via `LeaseHoldingOperator` — factories registered
   there wrap their instance so the lease spans create → run → destroy.
   After unload completes the entry is closed with a generation bump; a fresh
   load reopens it (the `pluginLoaded` sink hook) while pre-unload handles stay
   invalid across reloads (model-runtime adapters compare generations before
   touching plugin memory, and deliberately leak rather than destruct into
   unmapped code). Plugin UI executions (menu actions, dock events, plugin
   timers) are outside the lease guarantee — they run on the GUI thread where
   unload also runs; the out-of-process host is the structural answer.
   Pure-manifest external-tool operators execute no plugin code and take no
   lease; data providers are listing-only today and must take leases before
   any in-process call surface ships. Concurrent unload attempts share one
   drain (drainer counting); a late cancel cannot reopen a closed entry.
3. **UI reverse ownership.** The shell installs a `UiShellSink`
   (`ExprsPluginShellUi`); plugin docks/menu actions/preferences pages are
   attached AND released through it, driven by `PluginUiHost`. Unload deletes
   plugin-created widgets/actions/pages (including detaching a page from an
   open Preferences dialog) while the binary is still mapped; the host never
   loses track of an attached widget. The plugin manager's enable/disable is
   a full round-trip: disable = unload (refused-while-busy, without flipping
   the persisted flag), enable = load + re-attach — no restart.
4. **Python contribution revocation.** The IPC bridge records every `py:`
   id registered through it; `PythonPluginAdapter::unload()` revokes them
   (provider `removeAlgorithm` → `AtomicAlgorithmRegistry::unregisterAdapter`)
   before the bridge dies. The worker daemon pops the plugin's
   `algo_executors` entries on `unload_plugin` (owner evidence attached at
   `classFactory`). Reload re-registers cleanly with no duplicates and no
   dead entries.
5. **Load-path containment.** `exprs::PathPolicy` is the single owner of
   path rules: manifest `entrypoint` must resolve (symlinks followed) to a
   regular file inside the canonical plugin root; absolute values and `..`
   components are rejected lexically. The validator enforces it at
   validation time AND the loader re-checks immediately before mapping
   (`EntrypointOutsideRoot` E3007), closing the validation→load TOCTOU
   window as far as the OS allows.
6. **Workspace effect policy.** `ExternalProcess::run` validates the
   resolved effects — working directory, every absolute/`..`-naming argv
   element except argv[0], and absolute env values from the manifest —
   against `SICNU_MCP_WORKSPACE` (+ the owning plugin's directory as an
   explicit extra root) whenever the variable is set, and refuses with
   `refusedByPolicy` (E5005) BEFORE spawning. Output publish targets are
   gated the same way at the operator layer. This is a path policy, NOT an
   OS sandbox: PATH-resolved executables are not confined and an allowed tool
   can still write inside the workspace.
7. **SDK portability by design.** The SDK is std::filesystem-based
   (discovery, package, validator, registry index, path policy); the loader
   maps libraries via `LoadLibraryW/GetProcAddress/FreeLibrary` on Windows
   and `dlopen/dlsym/dlclose` elsewhere. `ExternalProcess` keeps the POSIX
   fork/exec implementation and a typed "not supported on Windows" refusal —
   an honest documented limitation, not a silent gap. No runtime-validated
   Windows claim is made.
8. **Conformance kit + CLI parity.** The CLI `plugin enable/disable`
   implements the same round-trip contract as the GUI Plugin Manager
   (disable = unload with E4005 refusal while busy, persisted flag untouched;
   enable = load). `sicnu_geo_rs_cli plugin test <dir>` drives manifest
   (PT_MANIFEST), compatibility (PT_COMPAT), containment (PT_CONTAINMENT),
   load (PT_LOAD), registration (PT_REGISTER), unload revocation (PT_REVOKE)
   and the enable round-trip (PT_ROUNDTRIP) with structured JSON output.

Out-of-process native plugin hosting remains a designed follow-up (see
`docs/plugins/isolation.md`); the in-process lifecycle above is its
precondition, and no partial isolation layer is shipped.

## Consequences

- Unload is now safe by construction: disable during a long run refuses with
  a diagnostic instead of crashing the dock/menu/worker.
- The plugin manager tells the truth: enable/disable is a real round-trip;
  `py:` catalogs stay clean across reload cycles.
- A malicious manifest can no longer make the host map code outside the
  plugin root, or spawn an external tool rooted outside the workspace.
- Windows builds compile the whole SDK; external-tool execution requires a
  POSIX host today (typed refusal + docs).
- New tests: barrier unit suite, unload-refusal integration, containment
  (validator + loader TOCTOU), sandbox policy matrix, python round-trip.

## Evidence

- `src/plugins/framework/plugin_execution_barrier.*`, `plugin_registry.cpp`
  (unload/unloadAll), `plugin_ui_host.*` + `src/app/plugin_shell_ui.*`
  (reverse ownership), `src/sdk/exprs/path_policy.*` + `plugin_loader.cpp`
  (containment), `external_process.cpp` (effect policy),
  `src/python/isolated/app_interface_bridge.cpp` + `worker_daemon.py`
  (python revocation), `src/cli/cli_commands.cpp` (`plugin test`).
- Tests: `test_plugin_execution_barrier.cpp`, `test_plugins_runtime_host.cpp`
  (unload refusal, round-trip execution restore, direct-path lease guarding),
  `test_plugin_manifest.cpp` + `test_exprs_plugin_loader.cpp` (containment),
  `test_exprs_external_process.cpp` (policy matrix),
  `test_plugin_host.cpp` (python round-trip). Local full-suite evidence:
  2421/2421 ctest green on 2026-09-07 (one fixture-dependent test requires
  the gitignored `data/phr_xs.tif` sample).
