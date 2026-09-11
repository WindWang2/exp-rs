# Plugin Platform 8.0 — Baseline Audit

Date: 2026-09-11 · Auditor: primary agent · Mode: read-only against `origin/master`

## 1. Repository state

- `origin/master` = `2d4f0daedd5ecf8ef33ce33ca062aabe113753ad` (identical to local master).
- Last planning-time master `322dfd3876` is an ancestor; two later CI-fix PRs merged since.
- Open PRs: **none**. Open issues: **none**.
- Remote branches: all `feat/*-6`, `feat/*-7` branches are merged (ahead=0, ancestor of master).
  Two branch tips (`feat/dataset-experiment-foundation-5` ahead=10, `feat/remote-sensing-io-interoperability-5`
  ahead=16) are merge-lineage residue only — their PRs (#770, #771) were merged; content is in master.
  No divergent work to preserve. `itk-upstream/*` branches are a vendored read-only mirror.
- Untracked in the main checkout: `exp-rs-dataset-experiment-mlops-8/` — a **different 8.0 track's**
  worktree (dataset/experiment/MLOps). Not ours; do not touch; its planned files (src/dataset,
  src/experiment) are cross-track seams.

## 2. Recent merged PRs → ownership map (last 30)

| PR | Track | Relation to plugin-platform-8 |
|---|---|---|
| #836 fix(ci) ipc_channel Options/MSVC jsoncpp | plugin IPC | direct predecessor; build fixes only |
| #835 fix(sdk) ExternalProcess namespace | plugin SDK | direct predecessor |
| #834 fix(geo) range_cache GDAL bridge | geospatial I/O | none |
| #833 fix(ci) master build post-832 | CI | none |
| #832 cartography-platform-7 | cartography | none |
| #831 verification-observability-7 | verification | neighbor; do not duplicate fuzz/stress harnesses |
| **#830 plugin-isolation-runtime-5** | **this track's base** | host-process worker, IPC v1, capabilities, quotas, crash recovery |
| #829 scientific-algorithms-7 | processing | none |
| #828 execution-plane-runtime-7 | execution plane | seam neighbor (Executor/JobEngine) |
| #827 pi-spatial-scientist-harness-7 | agent | seam neighbor (AgentToolCatalog) |
| #826 professional-workbench-7 | app/GUI | seam neighbor (shell, PluginUiHost consumer) |
| #825 model-runtime-multimodal-7 | models | seam neighbor (IModelRuntime) |
| #824 dataset-experiment-7 | data | seam neighbor (DatasetStore/ExperimentStore) |
| #823 cloud-geospatial-io-7 | I/O | none |
| #822 resolve-open-issues-773-817 | cross-cutting fixes | includes plugin fixes (#755/#756, ADR 0130) |
| #821/#820/#819/#818 | help/cartography/workbench/data 6.0 | none for this track |
| #772–#762 | 5.0/4.0 platform tracks | #765 plugin-sdk-ecosystem-4 = older base of this track |

Conclusion: **no other 8.0 branch is pushed or open**; the only concurrent 8.0 work is the local
untracked dataset/experiment worktree. Overlap risk is low; shared files to keep minimal edits in:
`src/cli/cli_commands.cpp` (plugin conformance kit), `src/plugins/**`, `tests/CMakeLists.txt`.

## 3. Current plugin implementation inventory (verified against code)

### In place (implemented + tested)
- **PluginV1 ABI + loader** (`src/sdk/exprs/plugin_loader.{h,cpp}`, `plugin_interface.h`): entry-point
  probe, API/ABI gate before dlopen, containment re-check before load (ADR 0130).
- **Registry lifecycle** (`plugin_registry.{h,cpp}`): single lifecycle owner; configure/refresh/load/
  unload/loadAllValidated/ensureLoaded; enable/disable persisted user index; unload = drain (E4005) →
  revoke → shutdown → dlclose (barrier-protected, `PluginExecutionBarrier`).
- **IPC transport stack** (`ipc_frame/ipc_stream/ipc_envelope/ipc_channel`): length-prefixed JSON
  frames, 32 MiB symmetric cap (E6003), versioned envelope (E6001 gate), request/response with id
  correlation, progress, cancel (point + broadcast), events, structured `IpcError` with stable codes.
  Channel-level **multiple in-flight requests already work** (reader thread + id map).
- **Host-process runtime** (`src/plugins/host/*`): one worker process per plugin
  (`exprs_plugin_host_worker`), inherited-handle transport (never stdio), handshake validating
  protocol+API+ABI axes before plugin code runs, proxy contributions into the ordinary sinks
  (RSOperatorRegistry / agent catalog / data providers / model runtimes unchanged), crash detection
  (EOF), deadline kill ladder (cancel frame → 3 s grace → TerminateJobObject/SIGKILL), bounded
  restart policy (3 per 60 s window, atomic arm), generation counter for stale handles.
- **Capabilities v1** (`plugin_capabilities.{h,cpp}`): manifest `access` object — fs read/write roots
  with ${plugin}/${workspace}/${temp} expansion + canonicalization (fail-closed), network,
  externalProcess, gpu hint, workspace/project mutation, model frameworks, ui, destructive.
- **Quotas v1** (`plugin_quotas.{h,cpp}`): maxRequestConcurrency, requestDeadlineMs,
  maxResponseBytes, workerMemoryBytes, workerCpuRatePercent, maxChildProcesses, gpuHint; env
  ceilings (SICNU_PLUGIN_QUOTA_*), manifest clamped to ceilings. Windows job-object enforcement
  (memory, active process, CPU rate); POSIX: RLIMIT-advisory, documented honestly.
- **ExternalProcess** (`external_process.{h,cpp}` ~1100 lines): bounded child process runner used by
  external-tool operators; default child env baseline (credential hygiene).
- **UI v1 (in-process only)** (`plugin_ui.h`, `plugin_ui_host.*`, `plugin_shell_ui.*`): Qt
  UiContributionV1 with reverse-ownership shell sink; host-process plugins declaring `ui` are
  **refused at validation**.
- **Conformance kit** (`plugin test` CLI): PT_MANIFEST/PT_COMPAT/PT_CONTAINMENT/PT_LOAD/PT_REGISTER/
  PT_HOST_LAUNCH/PT_EXECUTE/PT_REVOKE/PT_ROUNDTRIP.
- **Tests**: test_plugin_host_process (real worker + crash/hang/flood/lifecycle fixtures),
  test_exprs_ipc, test_plugin_capabilities, test_exprs_external_process(_win), manifest/loader/system
  suites, isolation_plugin fixture (echo/crash/hang/flood/slow operators).
- **Docs**: docs/plugins/{README,host-process,isolation,permissions,packaging,manifest-v1,
  external-process}.md; four version axes in version.h + docs/sdk/versioning.md.

### Verified gaps (this track's work)

| # | Gap | Evidence |
|---|---|---|
| G1 | **maxRequestConcurrency is advisory; all worker calls serialize** — proxies hold `entry.mutex`
across the whole request; worker dispatch loop is serial ("serial dispatch in v1"); docs admit
"effective 1" | plugin_host_proxies.cpp:147,215,258,348; plugin_host_worker_main.cpp:345;
docs/plugins/host-process.md quota table |
| G2 | **Worker cannot execute concurrently** — single dispatch thread; one `cancelled` flag +
single `executingRequestId` (cancel-by-id is broken with >1 in-flight) | plugin_host_worker_main.cpp:345-352 |
| G3 | **Timeout kill ladder kills the whole worker** on first timeout (cancelAll broadcast) —
correct for serial v1, too coarse once concurrency exists | plugin_host_session.cpp:421-435 |
| G4 | **No declarative out-of-process UI** — host-process + `ui` refused at validation; only
in-process Qt UiContributionV1 exists | docs/plugins/host-process.md; plugin_ui.h |
| G5 | **No UI schema validation anywhere** (needed before out-of-process UI schema can be trusted) | — |
| G6 | **Conformance kit lacks** hang-cancel (PT_CANCEL), quota refusal, protocol-negotiation,
malformed-frame, restart-exhaustion checks | cli_commands.cpp plugin test |
| G7 | **`SICNU_TEST_PLUGIN_HOST_WORKER` hardcodes `.exe`** — test_plugin_host_process cannot pass
on Linux/macOS as configured | tests/CMakeLists.txt:7033 |
| G8 | **docs/plugins/capabilities.md referenced but missing** (drift; permissions.md covers part) | plugin_capabilities.h:19, plugin_quotas.h:10 |
| G9 | **Worker has no per-request deadline enforcement** (host-side only) and no maxResponseBytes
per-quota application (global frame cap only) | plugin_quotas.h enforcement matrix |
| G10 | No progress-throttle/bounded-diagnostics on the channel (a plugin can spam events) | ipc_channel.cpp reader loop |
| G11 | `HostProcessModelRuntimeProxy` ctor issues `session->request` without `entry.mutex`
(inconsistent locking vs other proxies) | plugin_host_proxies.cpp:321 |
| G12 | Packaging (`plugin_package.{h,cpp}`) has checksums? — verify; SBOM metadata absent | plugin_package.cpp (304 lines, verify) |
| G13 | macOS lane: configure+build seam only; job-object/POSIX enforcement matrix untested there
(document, don't claim) | ci.yml tier 3 |

### Refused-by-contract / by honesty
- OS-level sandboxing beyond job objects/RLIMIT: refused (no privilege escalation; documented).
- Live QWidget transport across processes: refused by design (that is work package E's declarative
  contract instead).
- Network interception for plugin code: refused (documented as not enforced).

## 4. Cross-track seams (minimize edits)

- `src/cli/cli_commands.cpp` — conformance kit extension only (scoped to commandPlugin).
- `tests/CMakeLists.txt` — append-only test/fixture registrations.
- `src/sdk/exprs/*` — additive protocol/SDK surface (v1.1 minor bump), no renames.
- Dataset/experiment 8.0 track (local worktree): no shared files in our plan.

## 5. Environment evidence

- Linux 6.18.49-2-lts x64, 16 CPUs, 62 GB RAM; Ninja available; system Qt/GDAL previously
  configured (main checkout `build/` exists but its ninja file is corrupt → fresh worktree build).
- Build policy: targeted targets first (`sicnu_sdk`, `sicnu_plugins_hostprocess`,
  `exprs_plugin_host_worker`, fixtures, plugin tests), bounded parallelism.
