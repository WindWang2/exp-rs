# Plugin Platform 9.0 — Architecture decisions

All decisions follow one rule: extend the existing authorities, never add a
parallel one. Every design below states the root cause first (gap numbers
refer to CAPABILITY_MATRIX.md).

## D0 — Protocol 1.2: per-direction frame caps + feature negotiation (M0, M4)

Root cause G0.1/G4.1: the 1.1 frame cap is one atomic for both directions;
`quotas.maxResponseBytes` therefore also caps host→worker requests, and
future protocol surface cannot be negotiated — only versioned.

Design (additive minor bump, host and worker ship from the same SDK):
- `IpcChannel` gains **directional caps**: `setDirectionalFrameCaps(send, recv)`
  with the same monotonic-only guarantee; the legacy shared cap initializes
  both. Writer uses the send cap; reader uses the recv cap. `frameCap()`
  remains for compatibility (min of the two).
- `plugin.load` "limits" gains optional `maxRequestBytes` (host→worker
  ceiling for request frames) and keeps `maxFrameBytes` as the shared
  fallback for 1.1 peers. `maxResponseBytes` continues to bound
  worker→host frames — now WITHOUT side-capping requests (that side effect
  was the defect).
- `worker.hello` gains `"features": ["directionalFrameCaps", …]`. The host
  applies per-direction negotiation only when the feature is advertised; a
  1.1 worker (no feature) keeps exact 1.1 semantics. A 1.2 worker against a
  1.1 host is refused by the existing higher-minor rule (mismatched
  deployment guard, same-SDK shipping).
- Host-side application point: `PluginHostProcessRuntime::loadParamsFor`
  emits the limits; the worker applies them at plugin.load (unchanged seam).

## D1 — Capability enforcement moves to the boundaries that can hold it (M1)

Root cause G1.1–G1.3: declarations existed, but three enforceable seams
never consulted them. Enforcement points chosen where the enforcing process
already owns the decision:

1. **Model frameworks (enforced-host)**: the launcher-side runtime refuses
   `modelRuntime.load` for a framework not present in the plugin's parsed
   `access.modelProvider.frameworks` — typed E5005 before the request
   reaches the worker. When the plugin declares NO `access` object at all,
   the gate is inert (backward compatibility: deny-by-default only where
   8.0 already established it).
2. **Declarative UI (enforced-host)**: `describeUiSchema`/`invokeUi` refuse
   with E5005 when the parsed access marks `ui:false` while the manifest
   still declares UI contributions (inconsistent deployment) — a load-time
   validation, not a per-call cost.
3. **External tool operators (enforced-host)**: manifest-only
   `externalTool` operators refuse to spawn with typed E5005 when
   `access.externalProcess` is explicitly `false`. Explicit-false refuses;
   absent stays allowed (v1 manifests never had the field).
4. **Provider URI schemes (enforced-worker)**: the worker refuses
   `dataProvider.open/inspect` URIs whose scheme is not in the provider's
   declared `schemes` list (from the manifest the worker itself parsed) —
   typed E5005. Discover stays unfiltered (it is the enumeration surface).
   Providers that declare no schemes keep today's behavior.
5. **Declaration-vs-enforcement matrix as data (enforced-host, M1)**:
   `exprs::pluginCapabilityEnforcementMatrix()` returns the per-capability
   level table (enforced-host/enforced-worker/enforced-os/advisory/
   audit-only/refused-by-contract) with the same wording as the docs — one
   source of truth consumed by `plugin inspect`, doctor and the debug
   bundle. Docs quotes code, not the other way round.

Honesty rules unchanged: in-process runtime keeps declaration+audit only;
no network claim; no sandbox claim.

## D2 — Concurrency 2.0: observability + stress evidence, no new scheduler (M2)

Root cause G2.1/G2.2. The FIFO gate semantics are already exact; what is
missing is evidence and observability, not a different algorithm.
- `ConcurrencyGate` tracks `waiting()` and `peakActive()` (monotonic).
- Session tracks `inFlight()`, `peakInFlight()`, dropped events count and
  a **typed last failure** (code+message, set at every typed outcome).
- No queues are added; E6007 refusal stays the backpressure mechanism.
- New stress test: 3 rounds × 7 concurrent callers with colliding fates
  (2 slow past the deadline, 2 worker crashes, 3 echoes) on async futures
  with a hard 45 s wait per caller — asserts every caller terminates with
  a typed envelope, in-flight is 0 at quiesce, and unload stays clean.
  (Futures, not rendezvous barriers; the slot-accounting invariant is
  covered by the gate unit tests.)

## D3 — Process isolation: orphan detection surface (M3)

Root cause G3.1. Cleanup is already enforced (setpgid + group kill +
waitpid); what is missing is proof that the group is GONE.
- POSIX: `processGroupHasMembers(pgid)` probe via `kill(-pgid, 0)` —
  ESRCH ⇒ reaped; other ⇒ survivors. Surfaced in the runtime snapshot and
  the debug bundle; asserted by the orphan test after crash and after
  graceful shutdown. Best-effort: on EPERM the probe reports "unknown",
  never "clean" (fail honest).
- Windows/macOS: probe is documented not-run-on-this-host; job-object
  kill-on-close semantics unchanged from 8.0.

## D4 — Stable SDK surface: kitchen-sink fixture (M4, M5)

Root cause G4.2/G5.1. The stable plugin contract is the manifest + worker
protocol + entry points (the in-process C++ ABI stays gated by
api/abi checks; no new C ABI is invented because the host-process path —
the actual stability boundary — is JSON and needs no struct layout
pinning; inventing one would be surface without a consumer).
- AS SHIPPED, the existing `isolation_plugin` integration fixture was
  extended (instead of a new kitchen-sink fixture): it now exports the
  misbehavior operators PLUS one data provider (in-memory store with
  declared schemes), one model runtime (identity tensor backend with an
  exact known-answer), one agent tool (SpatialTool envelope echo) and the
  declarative UI provider — every contribution kind over the real worker
  path.
- Conformance kit additions are driven by optional manifest
  `conformance` declarations (`dataProviderTarget`, `agentToolTarget`,
  `modelFrameworkTarget`) so third parties opt in exactly like 8.0.

## D5 — Declarative UI 2.0: validate at the host edge (M6)

Root cause G6.1/G6.2. describe-side is already hard-capped; invoke-side
forwards plugin-controlled event JSON to plugin code unchecked.
- Host-side `validateUiEvent` (bounded contributionId/controlId lengths,
  eventType whitelist matching the host renderer's ACTUAL vocabulary —
  `clicked|changed|command` plus `submit|custom` headroom — and a JSON
  value size cap) refuses with the new typed **E6010** BEFORE the worker
  round-trip, without touching the channel. `custom` events keep a bounded
  payload; unknown types refuse.
- Schema gains optional, validated, capped control fields `description`
  and `accessibilityLabel` (strings within `maxStringLength`); normalized
  output preserves them. Purely additive; older schemas unaffected.
- Workbench placement remains Track 7's seam (unchanged boundary).

## D6 — Packaging 3.0: failure never touches previous-good (M7)

Root cause G7.1. 8.0 has staged install + rollback; the remaining risk is
the interrupted/foreign state and constraint checking. AS SHIPPED:
- Interrupted installs: the 8.0 sweep is retained and now VERIFIED by a
  test that plants a DEAD-process staging leftover (pid+1, 48 h old) and
  asserts the install removes it. (The marker-file pre-flight sketched
  here during planning was NOT built — the sweep + same-pid cleanup covers
  the same failure class.)
- Dependency constraints: probed at install time against the installed set
  with semver-ish ranges (^, ~, >=, =, exact, bare; npm 0.x semantics).
  Satisfied → info diagnostic; unsatisfied → typed E3003 WARNING and the
  install PROCEEDS. HONEST SCOPE: nothing enforces dependencies at load
  time (the loader validates spec syntax only) — docs and the diagnostic
  say "advisory" verbatim.
- Corrupt-package behavior (checksum mismatch refuses, previous-good
  survives) was already delivered and tested in 8.0; 9.0 adds the
  interrupted-staging and dependency tests alongside it.
- Disk-full cannot be forced portably: covered by the unwritable-target
  test on POSIX (not-run label on Windows) — explicitly recorded, never
  claimed green.

## D7 — Offline plugin index (M8)

Root cause G8.1. Discovery scans roots but nothing can answer "what is
available, compatible, and pinned" without loading every manifest afresh.
- `exprs::PluginIndex`: scans given directories for plugin packages
  (manifest-only, no dlopen), produces a JSON index artifact
  (`{generatedAt, host{api,abi,platform}, plugins:[{id,version,path,
  compatible,reason}]}`), with reproducible metadata (sorted keys, no
  timestamps beyond the declared one).
- Compatibility filter = manifest api/abi/platform vs host axes.
- Pinned versions: caller-supplied pin map `{id: version}` marks
  up-to-date/downgrade/upgrade — pure function, offline.
- CLI: `plugin index [dir…] [--json]`. No network, no service, no store:
  a plain artifact generator/query.

## D8 — Conformance kit 3.0 + diagnostics (M9, M10)

- Every check reports `status: "pass"|"fail"|"skipped"` (plus the legacy
  boolean) so automation can separate not-applicable from green (G9.1).
- New checks: `PT_PROVIDERS`, `PT_AGENTTOOL`, `PT_MODEL` (kitchen-sink
  round-trips over the worker path), `PT_PROCESS_CLEANUP` (worker pid gone
  + process group empty after unload), `PT_QUOTA` (E6007 overload refusal
  observed), `PT_PERMISSIONS` (out-of-root workDir refused E5005).
- `plugin doctor` quotes the enforcement matrix and, for a LIVE
  host-process session, the health snapshot. AS SHIPPED the snapshot
  carries workerPid, droppedEvents, generation, poisoned, effective
  concurrency, in-flight/peak/gate waiters, process-group state and the
  typed last failure; the retired-groups trail and redacted diagnostics
  live in `plugin debug-bundle` (the full support surface).
- `plugin debug-bundle <id>` writes a JSON bundle: manifest (as parsed),
  capabilities + enforcement matrix, quotas, health snapshot, diagnostics
  log, index entry — with **secret redaction**: any string value whose KEY
  matches `password|secret|token|api[-_]?key|credential|private[-_]?key`
  (case-insensitive) is replaced by `"[redacted]"` recursively, including
  inside manifest JSON. Redaction is a pure utility with its own unit test.

## Compatibility statement (whole track)

- Protocol: additive minor 1.1→1.2; 1.0/1.1 peers keep documented semantics.
- Manifest: no required-field changes; all new fields optional + validated;
  round-trip preserved (exhaustive test added).
- ABI: `EXPRS_*` entry points unchanged; new optional entry points are
  probed, never required.
- CLI: all new subcommands/fields additive; existing JSON keys never
  renamed or removed.
