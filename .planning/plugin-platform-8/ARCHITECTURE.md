# Plugin Platform 8.0 — Architecture & Decisions

All decisions below follow "smallest extension of an existing authoritative seam".

## D1. Host protocol evolves as v1.1 (minor bump), NOT 2.0
`EXP_RS_HOST_PROTOCOL_VERSION_MINOR` 0→1 and `Ipc::kProtocolVersionMinor` 0→1 (kept in lockstep).
Rationale: the existing wire contract (id-correlated request/response, progress, cancel, events,
typed errors, frame caps) already satisfies protocol-2.0's semantic requirements; everything this
track needs is ADDITIVE, which is exactly what the minor axis means (peer minor ≤ local minor).
Same-major/minor-additive keeps every v1.0 worker and host binary compatible. Renaming to "2.0"
would add no enforcement power and would break the versioning vocabulary.
Additive surface:
- `worker.hello` gains `maxConcurrentRequests` (v1.0 workers omit it → host treats as 1).
- `plugin.load` params gain `limits` { `maxFrameBytes` } (host cap negotiated down; worker clamps
  writes; worker's read cap unchanged — host never sends large frames).
- New methods `ui.describe` / `ui.invoke` (declarative UI, D5). v1.0 workers answer unknown
  methods with E6008 (existing fail-safe) — a v1.1 host treats E6008 on ui.describe as
  "plugin offers no UI".

## D2. Concurrency: worker dispatch pool + host-side FIFO gate (quotas become exact)
- Worker: lifecycle methods (`plugin.load`/`plugin.shutdown`) stay on the main dispatch thread;
  execution methods (`operator.execute`, `agentTool.execute`, `dataProvider.*`,
  `modelRuntime.load|infer`) run on a bounded pool of
  `min(quota.maxRequestConcurrency, worker ceiling)` threads pulling from the channel's request
  queue. Per-request `shared_ptr<atomic<bool>>` cancel flags replace the single `cancelled` atom
  (id-routed cancels become correct with >1 in-flight; broadcast still supported).
- Host: `PluginHostProcessSession` gains a FIFO-fair concurrency gate sized from
  `quota.maxRequestConcurrency` (the quota's documented "host-side session gate (exact)" becomes
  true). Bounded slot acquisition: wait up to the effective deadline, then typed E6007 overload
  refusal. Proxies stop holding `entry.mutex` across requests (they snapshot the session
  shared_ptr under the lock, then request unlocked) — sessions are refcounted so respawn races
  are UAF-free by construction.
- Timeout policy (kill ladder precision):
  - sole in-flight request → cancel(id) → 3 s grace → kill (unchanged v1 semantics);
  - concurrent in-flight → cancel(id) → grace → the timed-out request fails typed E6004, the
    session is marked POISONED, the worker keeps serving peers; when in-flight drains to 0 the
    poisoned worker is killed and the next request applies the existing restart policy.
    A hung execution thread leaks only until that kill — bounded by the slot count.
  Rationale: one plugin's hung operator must not kill unrelated in-flight calls of the SAME
  plugin now that concurrency exists; total resource exposure is still bounded (slots × deadline).

## D3. Progress/event boundedness
- Worker coalesces progress frames per request: at most one frame per 20 ms window, always
  forwarding the latest value/message; completion responses are never throttled.
- Host channel bounds the pending-event queue (`options.maxQueuedEvents`, default 1024); overflow
  increments a dropped-events counter surfaced in `protocolFailure()`-adjacent diagnostics
  (worker.log flood cannot exhaust host memory).

## D4. Cross-platform enforcement matrix (honest)
- POSIX spawn gains `setpgid(0,0)` in the child; kill ladder kills `-pid` (process group) then
  pid, mirroring external_process.cpp's proven setsid/kill(-pid) pattern. No privileges needed.
- POSIX pre-exec `setrlimit(RLIMIT_AS)` when `quota.workerMemoryBytes > 0` (coarse, best-effort,
  computed before fork to stay allocation-free in the child).
- Windows unchanged (job object: kill-on-close, ActiveProcessLimit, process memory, CPU rate cap).
- macOS follows the POSIX path; not locally exercised (documented as unverified, ci lane is
  configure+build only).
- HostProcess model runtime proxy constructor now snapshots the session under `entry.mutex`
  (fixes inconsistent locking found in baseline audit G11).

## D5. Declarative out-of-process UI (no QWidget serialization)
- Schema model in the Qt-free SDK (`exprs/plugin_ui_schema.h`): contributions
  { commands, menuItems, settingsPages, dockPanels, contextActions, helpTopics } built from
  bounded schema controls (text/number/checkbox/combo/slider/button/label/group/range). Hard caps:
  ≤64 controls per page, ≤32 combo options, ≤256-char strings, group depth ≤4; unknown control
  types / oversized schemas fail VALIDATION (fail closed), never render.
- Plugin surface: optional exported entry point `EXPRS_createUiSchemaProviderV1` (additive, no V1
  ABI change — same pattern as `EXPRS_createUiContributionV1`). The worker probes it after
  `plugin.load`; `ui.describe` returns the validated schema; `ui.invoke` carries
  {contributionId, controlId, eventType, value, context} and returns state updates (bounded JSON).
- Host side (`src/plugins/framework/plugin_ui_schema_host.*`): the host OWNS all widgets;
  commands register through the existing command seam, menus/context actions through the shell
  seams, settings pages through the existing preferences seam. Events flow host→worker with the
  plugin generation id; unload revokes schema contributions before the worker dies.
- Validation refuses `runtime: host-process` + `ui` NO LONGER: the manifest `ui` section becomes
  legal for host-process plugins when a schema provider is present; absence at load = typed
  diagnostic, not a crash.

## D6. Capability 2.0: enforcement where a boundary exists, declaration elsewhere
- Worker-side: the worker re-derives capability roots from the manifest it already received plus
  its materialized services; `operator.execute` validates `workDir` against fsWriteRoots ∪ temp
  (fail closed → typed E5001 policy refusal). Honest boundary: it gates the host-provided
  workDir seam, NOT arbitrary plugin I/O (documented).
- Validation: `access`-vs-`capabilities`/`permissions` consistency warnings (e.g. declared write
  roots without filesystem_write; network:true unused); default deny preserved and tested.

## D7. Packaging: integrity + staging, honestly scoped
- Manifest `package` object: `checksums` (file → sha256), `sbom` {path, format}, `signature`
  metadata (verified=no trust anchor → integrity only, no authenticity claim).
- install(): stage into `<root>/.staging/<id>-<pid>` → verify declared checksums during copy →
  atomic rename → rollback (remove staging) on any failure; existing install untouched on failure.

## D8. Conformance kit extension (additive PT_* checks)
PT_CANCEL, PT_CONCURRENCY, PT_UI_SCHEMA, PT_RESTART join the kit, driven by an optional
`conformance` manifest section declaring target operator ids; undeclared targets report
"skipped" — the kit never invents behavior a plugin does not declare.

## Cross-track seams
- `tests/CMakeLists.txt`: append-only additions; one baseline bugfix line (worker path suffix).
- `src/cli/cli_commands.cpp`: conformance additions scoped inside commandPlugin.
- Dataset/experiment 8.0 track: no shared files.
